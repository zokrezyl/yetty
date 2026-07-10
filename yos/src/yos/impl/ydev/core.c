/*
 * ydev core — vfd ring + self-pipe primitive, init/shutdown, time.
 *
 * Every backend builds on ydev_vfd. The ring holds opaque fixed-size
 * records (camera slot, sensor sample, location fix, audio chunk); the
 * pipe carries one byte per empty -> non-empty transition so that
 * poll/select/kqueue on the consumer side wake at the right moment.
 */

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

struct ydev_state      g_ydev;
struct ydev_perm_state g_ydev_perm;

ydev_result_t ydev_init(const ydev_init_t *init)
{
    if (g_ydev.initialised) {
        if (init && init->jvm) g_ydev.jvm = init->jvm;
        return YDEV_OK;
    }
    g_ydev.jvm = init ? init->jvm : NULL;
    g_ydev.initialised = true;
    ydev_perm_init_once();
    return YDEV_OK;
}

void ydev_shutdown(void)
{
    g_ydev.initialised = false;
}

uint64_t ydev_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ─────────────────────────────────────────────────────────────────────
 * ydev_vfd
 * ──────────────────────────────────────────────────────────────────── */

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int ydev_vfd_init(struct ydev_vfd *v, size_t record_size, size_t record_cap,
                  ydev_vfd_dropper_t on_drop)
{
    if (!v || record_size == 0 || record_cap == 0) return -1;
    memset(v, 0, sizeof(*v));

    int p[2];
    if (pipe(p) != 0) return -1;
    /* Both ends non-blocking: the producer never blocks waiting for the
     * consumer to read the wakeup byte, and the consumer can drain the
     * byte with a single non-blocking read. */
    if (set_nonblock(p[0]) != 0 || set_nonblock(p[1]) != 0) {
        close(p[0]); close(p[1]);
        return -1;
    }
    v->pipe_r = p[0];
    v->pipe_w = p[1];

    v->ring = calloc(record_cap, record_size);
    if (!v->ring) {
        close(v->pipe_r); close(v->pipe_w);
        return -1;
    }
    v->record_size = record_size;
    v->record_cap  = record_cap;
    v->on_drop     = on_drop;

    if (pthread_mutex_init(&v->lock, NULL) != 0) goto fail_ring;
    if (pthread_cond_init(&v->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&v->lock);
        goto fail_ring;
    }
    return 0;

fail_ring:
    free(v->ring);
    close(v->pipe_r); close(v->pipe_w);
    return -1;
}

void ydev_vfd_destroy(struct ydev_vfd *v)
{
    if (!v) return;
    /* Drain whatever remains so backends can release platform refs. */
    if (v->on_drop) {
        while (v->used > 0) {
            v->on_drop(v->ring + v->tail * v->record_size);
            v->tail = (v->tail + 1) % v->record_cap;
            v->used--;
        }
    }
    pthread_cond_destroy(&v->not_empty);
    pthread_mutex_destroy(&v->lock);
    if (v->pipe_r >= 0) close(v->pipe_r);
    if (v->pipe_w >= 0) close(v->pipe_w);
    free(v->ring);
    memset(v, 0, sizeof(*v));
    v->pipe_r = v->pipe_w = -1;
}

int ydev_vfd_fd(const struct ydev_vfd *v) { return v ? v->pipe_r : -1; }

uint64_t ydev_vfd_push(struct ydev_vfd *v, const void *record)
{
    pthread_mutex_lock(&v->lock);

    bool was_empty = (v->used == 0);

    if (v->used == v->record_cap) {
        /* Full: drop the oldest slot. Hand it to on_drop first so the
         * camera backend can CFRelease the platform buffer. */
        if (v->on_drop)
            v->on_drop(v->ring + v->tail * v->record_size);
        v->tail = (v->tail + 1) % v->record_cap;
        v->used--;
    }

    memcpy(v->ring + v->head * v->record_size, record, v->record_size);
    v->head = (v->head + 1) % v->record_cap;
    v->used++;
    uint64_t seq = ++v->seq_next;

    pthread_cond_signal(&v->not_empty);

    if (was_empty) {
        /* Edge-trigger the wakeup pipe. Non-blocking, EAGAIN means a
         * previous byte hasn't been drained yet — fine, the consumer
         * will see it on the next poll. */
        char x = 1;
        ssize_t w = write(v->pipe_w, &x, 1);
        (void)w;
    }

    pthread_mutex_unlock(&v->lock);
    return seq;
}

static int wait_until(pthread_cond_t *c, pthread_mutex_t *m,
                      const struct timespec *deadline)
{
    if (!deadline) {
        return pthread_cond_wait(c, m);
    }
    return pthread_cond_timedwait(c, m, deadline);
}

ydev_result_t ydev_vfd_pop(struct ydev_vfd *v, void *record_out, int timeout_ms)
{
    pthread_mutex_lock(&v->lock);

    struct timespec deadline;
    struct timespec *deadline_p = NULL;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        deadline_p = &deadline;
    }

    while (v->used == 0 && !v->closed) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&v->lock);
            return YDEV_AGAIN;
        }
        int r = wait_until(&v->not_empty, &v->lock, deadline_p);
        if (r == ETIMEDOUT) {
            pthread_mutex_unlock(&v->lock);
            return YDEV_AGAIN;
        }
    }

    if (v->closed && v->used == 0) {
        pthread_mutex_unlock(&v->lock);
        return YDEV_IO;
    }

    memcpy(record_out, v->ring + v->tail * v->record_size, v->record_size);
    v->tail = (v->tail + 1) % v->record_cap;
    v->used--;

    if (v->used == 0) {
        /* Drain the wakeup byte so the next poll() blocks again. */
        char buf[8];
        while (read(v->pipe_r, buf, sizeof buf) > 0) {}
    }

    pthread_mutex_unlock(&v->lock);
    return YDEV_OK;
}

void ydev_vfd_close(struct ydev_vfd *v)
{
    pthread_mutex_lock(&v->lock);
    v->closed = true;
    pthread_cond_broadcast(&v->not_empty);
    pthread_mutex_unlock(&v->lock);
}
