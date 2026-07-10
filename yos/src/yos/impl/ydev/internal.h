/*
 * Internal ydev primitives shared between the platform-agnostic core and
 * the per-platform backends. Not part of the public ABI; do not include
 * from outside src/yos/impl/ydev/ or src/yos/platform/.
 *
 * Central concept: ydev_vfd — a fixed-record ring buffer paired with a
 * self-pipe. Producers (framework callbacks) push records; consumers
 * (client threads) pop. The pipe's read end is exposed to the client as
 * the device's fd; one byte appears whenever the ring goes empty -> non-
 * empty, and is drained when the ring goes non-empty -> empty. This is
 * what makes poll() / select() / kqueue work on every backend, including
 * the ones that have no native fd of their own.
 */

#ifndef YOS_YDEV_INTERNAL_H
#define YOS_YDEV_INTERNAL_H

#include <yos/ydev/ydev.h>
#include <yos/ydev/perm.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── thread-local last-error scratch ─────────────────────────────────── */

void ydev_set_error(const char *fmt, ...);
void ydev_clear_error(void);

/* ── the vfd primitive ───────────────────────────────────────────────── */

typedef void (*ydev_vfd_dropper_t)(void *record);

struct ydev_vfd {
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;

    int             pipe_r;
    int             pipe_w;

    size_t          record_size;
    size_t          record_cap;
    size_t          head;        /* next slot to write into          */
    size_t          tail;        /* next slot to read out of         */
    size_t          used;
    uint8_t        *ring;

    /* Called for any record that is overwritten when the producer
     * lands on a full ring, or for any record left behind at
     * destroy. Lets the camera backend CFRelease the CVPixelBuffer
     * that came in with the dropped slot. May be NULL. */
    ydev_vfd_dropper_t on_drop;

    bool            closed;
    uint64_t        seq_next;    /* monotonic counter for producer    */
};

int  ydev_vfd_init(struct ydev_vfd *v, size_t record_size, size_t record_cap,
                   ydev_vfd_dropper_t on_drop);
void ydev_vfd_destroy(struct ydev_vfd *v);
int  ydev_vfd_fd(const struct ydev_vfd *v);

/* Push from the framework callback thread. Always succeeds: if the ring
 * is full, the oldest record is dropped via on_drop. Returns the seq
 * number assigned to this push. */
uint64_t ydev_vfd_push(struct ydev_vfd *v, const void *record);

/* Pop the oldest record. timeout_ms: -1 blocks, 0 non-blocking, >0 waits
 * up to N ms. Returns YDEV_OK / YDEV_AGAIN / YDEV_IO. */
ydev_result_t ydev_vfd_pop(struct ydev_vfd *v, void *record_out, int timeout_ms);

/* Wake any blocked consumer so it can observe v->closed. Used by close
 * paths in the backends. */
void ydev_vfd_close(struct ydev_vfd *v);

/* Monotonic time helper used by every timestamp in the public API. */
uint64_t ydev_now_ns(void);

/* ── per-process state ──────────────────────────────────────────────── */

struct ydev_state {
    bool  initialised;
    void *jvm;            /* Android only */
};

extern struct ydev_state g_ydev;

/* ── permission state (cross-platform half) ─────────────────────────── */

struct ydev_perm_state {
    pthread_mutex_t        lock;
    int                    pipe_r;
    int                    pipe_w;
    ydev_perm_status_t     status[5];   /* indexed by ydev_capability_t  */
};

extern struct ydev_perm_state g_ydev_perm;

void ydev_perm_init_once(void);
void ydev_perm_set(ydev_capability_t cap, ydev_perm_status_t st);

/* ── per-platform backend hooks for permissions (implemented in
 *    src/yos/platform/<platform>/perm_*.{c,m}). ────────────────────── */

ydev_perm_status_t ydev_perm_query_platform(ydev_capability_t cap);
ydev_result_t      ydev_perm_request_platform(ydev_capability_t cap);

#ifdef __cplusplus
}
#endif

#endif
