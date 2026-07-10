//
//  Smoke test for the wasm3 threads-proposal hack.
//
//  Builds a tiny wasm module inline (no external toolchain), spawns N threads,
//  each gets a sibling runtime sharing one linear memory, and each thread runs
//  ITER atomic-rmw-add increments on memory[0]. After all threads join, we
//  expect memory[0] == N * ITER.
//
//  Build:
//    cc -I../../source -I../.. threads_smoke.c \
//       ../../build-threads/source/libm3.a -lpthread -o threads_smoke
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>

#include "wasm3.h"

#define N_THREADS 8
#define N_ITERS   100000

// Hand-crafted wasm module:
//   (module
//     (memory (export "memory") 1 1 shared)
//     (func (export "inc")
//       i32.const 0          ;; address
//       i32.const 1          ;; addend
//       i32.atomic.rmw.add   ;; mem[0] += 1; pushes old value
//       drop)
//     (func (export "load") (result i32)
//       i32.const 0
//       i32.atomic.load))
//
// Each section is: id, ULEB128 length, body.
// Memory flags: 0x03 = bit0 (has_max) | bit1 (shared, threads proposal).
// Atomic memarg: alignHint then offset, both u32 ULEB. We use align=2 (4 bytes)
// and offset=0.
static const uint8_t s_wasm_module[] = {
    // \0asm
    0x00, 0x61, 0x73, 0x6d,
    // version 1
    0x01, 0x00, 0x00, 0x00,

    // ---- Type section (id 1) ----  body = 8 bytes
    0x01, 0x08,
    0x02,                       // 2 types
        0x60, 0x00, 0x00,                     // type 0: () -> ()
        0x60, 0x00, 0x01, 0x7f,               // type 1: () -> (i32)

    // ---- Function section (id 3) ----  body = 3 bytes
    0x03, 0x03,
    0x02,
    0x00,
    0x01,

    // ---- Memory section (id 5) ----  body = 4 bytes
    0x05, 0x04,
    0x01,
    0x03, 0x01, 0x01,                          // flags=3 has_max+shared, min=1, max=1

    // ---- Export section (id 7) ----  body = 23 bytes
    0x07, 0x17,
    0x03,
        0x03, 'i','n','c',                0x00, 0x00,   // func 0      (6 bytes)
        0x04, 'l','o','a','d',            0x00, 0x01,   // func 1      (7 bytes)
        0x06, 'm','e','m','o','r','y',    0x02, 0x00,   // memory 0    (9 bytes)

    // ---- Code section (id 10) ----  body = 22 bytes
    0x0a, 0x16,
    0x02,                       // 2 funcs

        // func 0: inc           — body = 11 bytes
        0x0b,
        0x00,                   // 0 local groups
        0x41, 0x00,             // i32.const 0
        0x41, 0x01,             // i32.const 1
        0xfe, 0x1e, 0x02, 0x00, // i32.atomic.rmw.add  align=2 offset=0
        0x1a,                   // drop
        0x0b,                   // end

        // func 1: load          — body = 8 bytes
        0x08,
        0x00,                   // 0 local groups
        0x41, 0x00,             // i32.const 0
        0xfe, 0x10, 0x02, 0x00, // i32.atomic.load
        0x0b,                   // end
};

struct worker_args {
    IM3Environment env;
    IM3Runtime     parent;
    int            iters;
    int            ok;
    char           err[128];
};

static void *
worker (void * arg)
{
    struct worker_args * w = (struct worker_args *) arg;

    IM3Runtime rt = m3_NewSiblingRuntime (w->parent, 64 * 1024, NULL);
    if (!rt) { snprintf(w->err, sizeof w->err, "NewSiblingRuntime failed"); return NULL; }

    IM3Module mod;
    M3Result r = m3_ParseModule (w->env, &mod, s_wasm_module, sizeof s_wasm_module);
    if (r) { snprintf(w->err, sizeof w->err, "parse: %s", r); m3_FreeRuntime(rt); return NULL; }

    r = m3_LoadModule (rt, mod);
    if (r) { snprintf(w->err, sizeof w->err, "load: %s", r); m3_FreeRuntime(rt); return NULL; }

    IM3Function inc;
    r = m3_FindFunction (&inc, rt, "inc");
    if (r) { snprintf(w->err, sizeof w->err, "find inc: %s", r); m3_FreeRuntime(rt); return NULL; }

    for (int i = 0; i < w->iters; ++i) {
        r = m3_Call (inc, 0, NULL);
        if (r) { snprintf(w->err, sizeof w->err, "call inc: %s", r); m3_FreeRuntime(rt); return NULL; }
    }

    w->ok = 1;
    m3_FreeRuntime (rt);
    return NULL;
}

int
main (void)
{
    IM3Environment env = m3_NewEnvironment ();
    if (!env) { fprintf(stderr, "NewEnvironment failed\n"); return 1; }

    IM3Runtime master = m3_NewRuntime (env, 64 * 1024, NULL);
    if (!master) { fprintf(stderr, "NewRuntime failed\n"); return 1; }

    IM3Module masterMod;
    M3Result r = m3_ParseModule (env, &masterMod, s_wasm_module, sizeof s_wasm_module);
    if (r) { fprintf(stderr, "master parse: %s\n", r); return 1; }

    r = m3_LoadModule (master, masterMod);
    if (r) { fprintf(stderr, "master load: %s\n", r); return 1; }

    IM3Function masterLoad;
    r = m3_FindFunction (&masterLoad, master, "load");
    if (r) { fprintf(stderr, "master find load: %s\n", r); return 1; }

    pthread_t        threads [N_THREADS];
    struct worker_args wa [N_THREADS];

    for (int i = 0; i < N_THREADS; ++i) {
        wa[i].env    = env;
        wa[i].parent = master;
        wa[i].iters  = N_ITERS;
        wa[i].ok     = 0;
        wa[i].err[0] = 0;
        if (pthread_create (&threads[i], NULL, worker, &wa[i]) != 0) {
            fprintf(stderr, "pthread_create %d failed\n", i);
            return 1;
        }
    }
    for (int i = 0; i < N_THREADS; ++i)
        pthread_join (threads[i], NULL);

    int all_ok = 1;
    for (int i = 0; i < N_THREADS; ++i) {
        if (!wa[i].ok) {
            fprintf(stderr, "thread %d failed: %s\n", i, wa[i].err);
            all_ok = 0;
        }
    }
    if (!all_ok) return 1;

    // Read counter back via the master (uses the shared memory)
    r = m3_Call (masterLoad, 0, NULL);
    if (r) { fprintf(stderr, "load call: %s\n", r); return 1; }

    uint32_t counter = 0;
    const void * results[1];
    results[0] = &counter;
    r = m3_GetResults (masterLoad, 1, results);
    if (r) { fprintf(stderr, "get results: %s\n", r); return 1; }

    uint32_t expected = (uint32_t)(N_THREADS * N_ITERS);
    printf ("counter=%u expected=%u  threads=%d iters=%d\n",
            counter, expected, N_THREADS, N_ITERS);

    return (counter == expected) ? 0 : 2;
}
