/*
 * yplatform/yworkpool/default.c - POSIX lazy worker pool.
 *
 * Generic offload primitive. See include/yetty/yplatform/yworkpool.h.
 *
 * Implementation outline:
 *   - Single FIFO queue protected by a mutex + condvar.
 *   - Workers (yplatform_ythread) loop: wait → pop → run() → post done().
 *   - Spawned lazily on submit: an existing idle worker is preferred; if
 *     all live workers are busy and live_count < max_threads, a new worker
 *     is spawned to handle the new job; otherwise the job sits in the
 *     queue and the next worker that finishes its current job picks it up.
 *   - Workers stay alive for the lifetime of the pool. No idle timeout in
 *     v1 — pools are long-lived and pool sizes are small (e.g. 4 for GPU).
 *
 * job.done is dispatched to the loop thread via event_loop->ops->post_to_loop
 * so the caller never has to think about thread hand-off.
 */

#include <yetty/yplatform/yworkpool.h>

#include <yetty/yplatform/thread.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

/* Linked-list node holding a single submitted job. Allocated by submit,
 * freed by the worker after run() returns and done() has been posted. */
struct yworkpool_node {
    struct yetty_yplatform_yworkpool_job job;
    struct yworkpool_node *next;
};

/* Bookkeeping for one worker thread. We keep the thread handles around so
 * destroy() can join them cleanly. */
struct yworkpool_worker {
    struct yetty_yplatform_ythread *thread;
    struct yetty_yplatform_yworkpool *pool;
};

struct yetty_yplatform_yworkpool {
    struct yetty_yevent_event_loop *loop;
    char *name;
    uint32_t max_threads;

    /* All of these are protected by `lock`. */
    struct yetty_yplatform_ymutex *lock;
    struct yetty_yplatform_ycond *cond;

    struct yworkpool_node *head; /* FIFO: head is the next job to run */
    struct yworkpool_node *tail;
    uint32_t queue_len;

    uint32_t live_count; /* total threads that have been spawned */
    uint32_t idle_count; /* threads currently waiting on cond */
    int shutdown;        /* destroy() flag; workers exit when seen */

    struct yworkpool_worker *workers; /* size = max_threads */
};

/* Pop the head job. Caller must hold `lock` and ensure the queue is
 * non-empty. */
static struct yworkpool_node *queue_pop(struct yetty_yplatform_yworkpool *pool)
{
    struct yworkpool_node *node = pool->head;
    pool->head = node->next;
    if (!pool->head) {
        pool->tail = NULL;
    }
    pool->queue_len--;
    node->next = NULL;
    return node;
}

/* Push to tail. Caller must hold `lock`. */
static void queue_push(struct yetty_yplatform_yworkpool *pool, struct yworkpool_node *node)
{
    node->next = NULL;
    if (pool->tail) {
        pool->tail->next = node;
    } else {
        pool->head = node;
    }
    pool->tail = node;
    pool->queue_len++;
}

/* Worker entry. `arg` is the per-worker struct, kept alive by the pool
 * for the lifetime of the thread. */
static int worker_main(void *arg)
{
    struct yworkpool_worker *w = arg;
    struct yetty_yplatform_yworkpool *pool = w->pool;

    ydebug("yworkpool[%s]: worker started", pool->name);

    for (;;) {
        yetty_yplatform_ymutex_lock(pool->lock);

        while (!pool->head && !pool->shutdown) {
            pool->idle_count++;
            yetty_yplatform_ycond_wait(pool->cond, pool->lock);
            pool->idle_count--;
        }

        if (pool->shutdown && !pool->head) {
            yetty_yplatform_ymutex_unlock(pool->lock);
            ydebug("yworkpool[%s]: worker exiting", pool->name);
            return 0;
        }

        struct yworkpool_node *node = queue_pop(pool);
        yetty_yplatform_ymutex_unlock(pool->lock);

        /* Run the blocking work outside the lock. */
        node->job.run(node->job.ctx);

        if (node->job.done) {
            pool->loop->ops->post_to_loop(pool->loop, node->job.done, node->job.ctx);
        }
        free(node);
    }
}

struct yetty_yplatform_yworkpool_ptr_result yetty_yplatform_yworkpool_create(
    struct yetty_yevent_event_loop *loop, const char *name, uint32_t max_threads)
{
    if (!loop || !loop->ops || !loop->ops->post_to_loop) {
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr,
                         "yworkpool_create: loop or post_to_loop is NULL");
    }
    if (max_threads == 0) {
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr,
                         "yworkpool_create: max_threads must be >= 1");
    }

    struct yetty_yplatform_yworkpool *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr, "yworkpool_create: pool calloc failed");
    }

    pool->loop = loop;
    pool->max_threads = max_threads;
    pool->name = name ? strdup(name) : strdup("");
    if (!pool->name) {
        free(pool);
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr, "yworkpool_create: name strdup failed");
    }

    pool->workers = calloc(max_threads, sizeof(*pool->workers));
    if (!pool->workers) {
        free(pool->name);
        free(pool);
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr, "yworkpool_create: workers calloc failed");
    }

    pool->lock = yetty_yplatform_ymutex_create();
    pool->cond = yetty_yplatform_ycond_create();
    if (!pool->lock || !pool->cond) {
        if (pool->lock) {
            yetty_yplatform_ymutex_destroy(pool->lock);
        }
        if (pool->cond) {
            yetty_yplatform_ycond_destroy(pool->cond);
        }
        free(pool->workers);
        free(pool->name);
        free(pool);
        return YETTY_ERR(yetty_yplatform_yworkpool_ptr, "yworkpool_create: sync primitive failed");
    }

    yinfo("yworkpool[%s]: created (max_threads=%u)", pool->name, max_threads);
    return YETTY_OK(yetty_yplatform_yworkpool_ptr, pool);
}

struct yetty_ycore_void_result yetty_yplatform_yworkpool_submit(
    struct yetty_yplatform_yworkpool *pool, struct yetty_yplatform_yworkpool_job job)
{
    if (!pool) {
        return YETTY_ERR(yetty_ycore_void, "yworkpool_submit: pool is NULL");
    }
    if (!job.run) {
        return YETTY_ERR(yetty_ycore_void, "yworkpool_submit: job.run is NULL");
    }

    struct yworkpool_node *node = malloc(sizeof(*node));
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "yworkpool_submit: node alloc failed");
    }
    node->job = job;
    node->next = NULL;

    yetty_yplatform_ymutex_lock(pool->lock);
    queue_push(pool, node);

    /* Three cases: (1) an idle worker can pick this up — signal it.
     * (2) no idle worker but we can spawn another — spawn it; the new
     * worker will dequeue our job. (3) at max and all busy — wait, the
     * next worker to finish its current job will dequeue. */
    if (pool->idle_count > 0) {
        yetty_yplatform_ycond_signal(pool->cond);
        yetty_yplatform_ymutex_unlock(pool->lock);
        return YETTY_OK_VOID();
    }

    if (pool->live_count < pool->max_threads) {
        uint32_t slot = pool->live_count;
        pool->workers[slot].pool = pool;
        pool->live_count++;
        yetty_yplatform_ymutex_unlock(pool->lock);

        pool->workers[slot].thread =
            yetty_yplatform_ythread_create(worker_main, &pool->workers[slot]);
        if (!pool->workers[slot].thread) {
            /* Roll back the live count. The node we pushed stays in the
             * queue — if any other worker exists they'll handle it; if
             * not, it'll sit until destroy() drains it (silently). */
            yetty_yplatform_ymutex_lock(pool->lock);
            pool->live_count--;
            yetty_yplatform_ymutex_unlock(pool->lock);
            ywarn("yworkpool[%s]: ythread_create failed for slot %u", pool->name, slot);
            return YETTY_ERR(yetty_ycore_void, "yworkpool_submit: ythread_create failed");
        }
        ydebug("yworkpool[%s]: spawned worker %u (live=%u)", pool->name, slot, slot + 1);
        return YETTY_OK_VOID();
    }

    /* At max, all busy: queued. A worker will pick it up after its
     * current job. */
    yetty_yplatform_ymutex_unlock(pool->lock);
    return YETTY_OK_VOID();
}

void yetty_yplatform_yworkpool_destroy(struct yetty_yplatform_yworkpool *pool)
{
    if (!pool) {
        return;
    }
    yinfo("yworkpool[%s]: destroying (live=%u, queued=%u)", pool->name, pool->live_count,
          pool->queue_len);

    yetty_yplatform_ymutex_lock(pool->lock);
    pool->shutdown = 1;
    yetty_yplatform_ycond_broadcast(pool->cond);
    yetty_yplatform_ymutex_unlock(pool->lock);

    for (uint32_t i = 0; i < pool->live_count; i++) {
        if (pool->workers[i].thread) {
            yetty_yplatform_ythread_join(pool->workers[i].thread);
            pool->workers[i].thread = NULL;
        }
    }

    /* Drain any jobs that never ran. Their done() is NOT invoked — the
     * caller's ctx may need its own teardown but that's outside our
     * contract. */
    struct yworkpool_node *node = pool->head;
    while (node) {
        struct yworkpool_node *next = node->next;
        free(node);
        node = next;
    }

    yetty_yplatform_ymutex_destroy(pool->lock);
    yetty_yplatform_ycond_destroy(pool->cond);
    free(pool->workers);
    free(pool->name);
    free(pool);
}
