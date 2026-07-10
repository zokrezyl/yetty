/*
 * test_pthread_create_join.c — pthread_create + pthread_join via
 *                              runtime_owned routing.
 *
 * WHAT this verifies:
 *   The wasm guest declares pthread_t / pthread_create / pthread_join
 *   as env imports (yos_imports.h provides the import-attribute
 *   redeclarations). yos's `yos_pthread_host_link()` binds them under
 *   the bare names (env.pthread_create, env.pthread_join). When the
 *   guest calls pthread_create, yos spawns a host pthread running a
 *   sibling wasm3 runtime that re-enters the guest at the supplied
 *   start_routine. Joining the thread blocks the calling thread on a
 *   futex/cond keyed by CLONE_CHILD_CLEARTID until the child exits.
 *
 * WHY this matters:
 *   pthread is the cornerstone of every multi-threaded C program we
 *   plan to run (libuv, lua's coroutine emulation, nvim's async
 *   tasks). The L1 host-pthread implementation in
 *   src/yos/impl/pthread.c is taken from yos-private; this test
 *   pins that we can drive it from a guest that imports `pthread_*`
 *   by their bare libc names — i.e. the user's "no name-mangling"
 *   contract holds for the pthread surface.
 *
 * Expected: exit 0, stdout contains "thread started", stdout contains "thread joined".
 */

#include "yos_imports.h"

/* yos_imports.h doesn't yet emit a thread function-pointer signature
 * we can use directly, so declare the imports inline against the
 * canonical wasm32 ABI: pthread_t = u32, attr/arg = u32, return i32. */

typedef unsigned int pthread_t;
typedef unsigned int pthread_attr_t;

__attribute__((import_module("env"), import_name("pthread_create")))
int pthread_create(pthread_t *thr,
                   const pthread_attr_t *attr,
                   void *(*start)(void *),
                   void *arg);

__attribute__((import_module("env"), import_name("pthread_join")))
int pthread_join(pthread_t thr, void **retval);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static void *worker(void *arg) {
    (void)arg;
    say("thread started\n");
    return (void *)0;
}

void _start(void) {
    pthread_t tid;
    int r = pthread_create(&tid, 0, worker, 0);
    if (r != 0) {
        say("pthread_create failed\n");
        _exit(1);
    }
    void *rv = (void *)1;
    r = pthread_join(tid, &rv);
    if (r != 0) {
        say("pthread_join failed\n");
        _exit(2);
    }
    say("thread joined\n");
    _exit(0);
}
