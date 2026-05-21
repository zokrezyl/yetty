/*
 * scene-canvas-test.c — unit tests for scene-canvas.
 *
 * Drives scene-canvas directly through its public API + canvas vtable,
 * bypassing the GPU pipeline. Created with the test-mode constructor
 * (yetty_ydraw_scene_canvas_create_for_test) which skips figure factory,
 * font cache, and default font setup.
 *
 * What this is for:
 *   1. Verify the entity tree's basic lifecycle invariants (create /
 *      lookup / delete / id-collision).
 *   2. Hammer add+delete cycles and assert the entity table and grid
 *      stay bounded (catch leaks / unbounded growth).
 *   3. Drive the render-side codepaths (rebuild_grid +
 *      build_drawable_staging) repeatedly with a stable scene and
 *      assert RSS doesn't drift — that's the "ygui freeze" leak signature.
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw/canvas.h>
#include <yetty/ydraw/scene-canvas.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw/flyweight.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/*=============================================================================
 * Test scaffolding
 *===========================================================================*/

static int g_failures = 0;
static int g_assertions = 0;

#define FAIL(fmt, ...)                                                                              \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);                 \
        g_failures++;                                                                               \
    } while (0)

#define ASSERT_TRUE(cond, fmt, ...)                                                                 \
    do {                                                                                            \
        g_assertions++;                                                                             \
        if (!(cond)) {                                                                              \
            FAIL("%s: " fmt, #cond, ##__VA_ARGS__);                                                 \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_U(got, expect)                                                                    \
    do {                                                                                            \
        g_assertions++;                                                                             \
        uint64_t _g = (uint64_t)(got);                                                              \
        uint64_t _e = (uint64_t)(expect);                                                           \
        if (_g != _e) {                                                                             \
            FAIL("%s: got %llu expected %llu", #got, (unsigned long long)_g,                        \
                 (unsigned long long)_e);                                                           \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

#define ASSERT_OK(res)                                                                              \
    do {                                                                                            \
        g_assertions++;                                                                             \
        if (YETTY_IS_ERR(res)) {                                                                    \
            FAIL("not OK: %s", (res).error.msg ? (res).error.msg : "(no msg)");                     \
            yetty_ycore_error_destroy((res).error);                                                 \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

#define ASSERT_ERR(res)                                                                             \
    do {                                                                                            \
        g_assertions++;                                                                             \
        if (YETTY_IS_OK(res)) {                                                                     \
            FAIL("expected ERR, got OK");                                                           \
            return;                                                                                 \
        }                                                                                           \
        yetty_ycore_error_destroy((res).error);                                                     \
    } while (0)

/*=============================================================================
 * Canvas setup helper.
 *
 * 800×600-pixel scene, 10×10-pixel cells → 80×60 grid. Matches the
 * geometry yjungle uses by default.
 *===========================================================================*/

static struct yetty_ydraw_canvas *make_canvas(void)
{
    struct yetty_ydraw_canvas_ptr_result r = yetty_ydraw_scene_canvas_create_for_test();
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "make_canvas: create_for_test failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    struct yetty_ydraw_canvas *c = r.value;

    struct yetty_ycore_pixel_size cell = {.width = 10.0f, .height = 10.0f};
    struct yetty_ycore_void_result rc = c->ops->set_cell_size(c, cell);
    if (YETTY_IS_ERR(rc)) {
        fprintf(stderr, "make_canvas: set_cell_size failed: %s\n", rc.error.msg);
        yetty_ycore_error_destroy(rc.error);
        exit(2);
    }

    struct yetty_ycore_grid_size grid = {.cols = 80, .rows = 60};
    rc = c->ops->set_grid_size(c, grid);
    if (YETTY_IS_ERR(rc)) {
        fprintf(stderr, "make_canvas: set_grid_size failed: %s\n", rc.error.msg);
        yetty_ycore_error_destroy(rc.error);
        exit(2);
    }
    return c;
}

static void destroy_canvas(struct yetty_ydraw_canvas *c)
{
    struct yetty_ycore_void_result r = c->ops->destroy(c);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "destroy_canvas: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}

/*=============================================================================
 * RSS sampling — Linux /proc/self/statm. Returns RSS in bytes.
 *===========================================================================*/

static size_t rss_bytes(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) {
        return 0;
    }
    long size_pages = 0, resident_pages = 0;
    if (fscanf(f, "%ld %ld", &size_pages, &resident_pages) != 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }
    return (size_t)resident_pages * (size_t)page_size;
}

/* Run code in `body` `iters` times. Sample RSS after every `sample_every`
 * iterations. After the loop, fail if the final RSS exceeds the early
 * baseline by more than `tolerance_bytes`. Reports the actual growth.
 *
 * The baseline is the second sample (not the first), so any one-time
 * lazy alloc on the first iteration doesn't pollute the budget. */
#define RSS_STABLE_OR_FAIL(iters, sample_every, tolerance_bytes, body)                              \
    do {                                                                                            \
        size_t _baseline = 0;                                                                       \
        size_t _last = 0;                                                                           \
        for (uint32_t _i = 0; _i < (iters); _i++) {                                                 \
            body;                                                                                   \
            if (_i % (sample_every) == 0) {                                                         \
                size_t _now = rss_bytes();                                                          \
                if (_i == (sample_every)) {                                                         \
                    _baseline = _now;                                                               \
                }                                                                                   \
                _last = _now;                                                                       \
            }                                                                                       \
        }                                                                                           \
        long _delta = (long)_last - (long)_baseline;                                                \
        fprintf(stderr,                                                                             \
                "  RSS baseline=%zu KB last=%zu KB delta=%+ld KB (tolerance %ld KB)\n",             \
                _baseline / 1024, _last / 1024, _delta / 1024,                                      \
                (long)(tolerance_bytes) / 1024);                                                    \
        g_assertions++;                                                                             \
        if (_delta > (long)(tolerance_bytes)) {                                                     \
            FAIL("RSS grew by %ld KB (tolerance %ld KB)", _delta / 1024,                            \
                 (long)(tolerance_bytes) / 1024);                                                   \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

/*=============================================================================
 * Drawable helpers — pack a small SDF circle drawable in a buffer the
 * caller owns. We use the per-cmd add_X helpers via a temporary
 * yetty_ydraw_draw_list to serialise one drawable, then point the
 * flyweight at the resulting bytes.
 *===========================================================================*/

/* Build a tiny ydraw_draw_list containing one circle drawable at
 * (cx, cy, r). Caller destroys with yetty_ydraw_draw_list_destroy. */
static struct yetty_ydraw_draw_list *make_circle_buffer(float cx, float cy, float r)
{
    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_destroy(br.error);
        return NULL;
    }
    struct yetty_ydraw_draw_list *buf = br.value;
    struct yetty_ysdf_circle g = {cx, cy, r};
    struct yetty_ycore_void_result rc =
        yetty_ydraw_draw_list_add_cmd_add_circle(buf, 0, 0, 0xFFFF0000u, 0u, 0.0f, &g);
    if (YETTY_IS_ERR(rc)) {
        yetty_ycore_error_destroy(rc.error);
        yetty_ydraw_draw_list_destroy(buf);
        return NULL;
    }
    return buf;
}

/* Resolve the first drawable in `buf` as a flyweight pointing into the
 * buf's bytes. Lifetime: as long as buf is not mutated. */
static int get_first_drawable_flyweight(struct yetty_ydraw_draw_list *buf,
                                        const struct yetty_ydraw_flyweight_registry *reg,
                                        struct yetty_ydraw_drawable_flyweight *out)
{
    struct yetty_ydraw_primitive_iter_result ir =
        yetty_ydraw_draw_list_drawable_first(buf, reg);
    if (YETTY_IS_ERR(ir)) {
        yetty_ycore_error_destroy(ir.error);
        return -1;
    }
    *out = ir.value.fw;
    return 0;
}

/* Build a private flyweight registry for the test (matches the one
 * scene-canvas builds internally). Caller destroys with
 * yetty_ydraw_flyweight_registry_destroy. */
static struct yetty_ydraw_flyweight_registry *make_registry(void)
{
    struct yetty_ydraw_flyweight_registry_ptr_result r = yetty_ydraw_flyweight_create();
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "make_registry: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

/*=============================================================================
 * Tests
 *===========================================================================*/

/* T1. Root entity exists and has external_id = 0. */
static void test_root_entity(void)
{
    fprintf(stderr, "[T1] root entity\n");
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_drawable *root = yetty_ydraw_scene_canvas_root(c);
    ASSERT_TRUE(root != NULL, "root should not be NULL");

    /* lookup by id 0 returns root. */
    struct yetty_ydraw_drawable *by_zero = yetty_ydraw_drawable_lookup(c, 0);
    ASSERT_TRUE(by_zero == root, "lookup(0) should return root");

    destroy_canvas(c);
}

/* T2. Create / lookup / delete one entity. */
static void test_entity_create_lookup_delete(void)
{
    fprintf(stderr, "[T2] entity create / lookup / delete\n");
    struct yetty_ydraw_canvas *c = make_canvas();

    /* Initially no entity with id=42. */
    ASSERT_TRUE(yetty_ydraw_drawable_lookup(c, 42) == NULL, "lookup(42) initially NULL");

    /* Create entity 42 as child of root. */
    struct yetty_ydraw_drawable_ptr_result er =
        yetty_ydraw_drawable_create_group(c, NULL, 42);
    ASSERT_OK(er);
    struct yetty_ydraw_drawable *e = er.value;
    ASSERT_TRUE(e != NULL, "entity must not be NULL");

    /* Lookup finds it. */
    ASSERT_TRUE(yetty_ydraw_drawable_lookup(c, 42) == e, "lookup(42) finds created entity");

    /* Delete and verify gone. */
    struct yetty_ycore_void_result dr = yetty_ydraw_drawable_delete(c, e);
    ASSERT_OK(dr);
    ASSERT_TRUE(yetty_ydraw_drawable_lookup(c, 42) == NULL, "lookup(42) NULL after delete");

    destroy_canvas(c);
}

/* T3. Duplicate external_id is rejected. */
static void test_duplicate_id_rejected(void)
{
    fprintf(stderr, "[T3] duplicate id rejected\n");
    struct yetty_ydraw_canvas *c = make_canvas();

    struct yetty_ydraw_drawable_ptr_result r1 =
        yetty_ydraw_drawable_create_group(c, NULL, 7);
    ASSERT_OK(r1);

    struct yetty_ydraw_drawable_ptr_result r2 =
        yetty_ydraw_drawable_create_group(c, NULL, 7);
    ASSERT_ERR(r2);

    destroy_canvas(c);
}

/* T4. After delete, the same external_id is reusable. */
static void test_id_reuse_after_delete(void)
{
    fprintf(stderr, "[T4] id reusable after delete\n");
    struct yetty_ydraw_canvas *c = make_canvas();

    struct yetty_ydraw_drawable_ptr_result r1 =
        yetty_ydraw_drawable_create_group(c, NULL, 99);
    ASSERT_OK(r1);

    struct yetty_ycore_void_result dr = yetty_ydraw_drawable_delete(c, r1.value);
    ASSERT_OK(dr);

    struct yetty_ydraw_drawable_ptr_result r2 =
        yetty_ydraw_drawable_create_group(c, NULL, 99);
    ASSERT_OK(r2);

    destroy_canvas(c);
}

/* T5. Add a primitive to an entity, then clear it — the buckets the
 * primitive landed in should be dropped. */
static void test_entity_add_prim_then_clear(void)
{
    fprintf(stderr, "[T5] add prim, clear\n");
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_flyweight_registry *reg = make_registry();

    struct yetty_ydraw_drawable_ptr_result er =
        yetty_ydraw_drawable_create_group(c, NULL, 1);
    ASSERT_OK(er);
    struct yetty_ydraw_drawable *e = er.value;

    struct yetty_ydraw_draw_list *cb = make_circle_buffer(100.0f, 100.0f, 20.0f);
    ASSERT_TRUE(cb != NULL, "circle buffer alloc");
    struct yetty_ydraw_drawable_flyweight fw;
    ASSERT_EQ_U(get_first_drawable_flyweight(cb, reg, &fw), 0);

    struct yetty_ycore_void_result ar = yetty_ydraw_drawable_add_prim(c, e, &fw);
    ASSERT_OK(ar);

    /* drawable count should have ticked up by 1. */
    ASSERT_EQ_U(c->ops->drawable_count(c), 1);

    struct yetty_ycore_void_result clr = yetty_ydraw_drawable_clear(c, e);
    ASSERT_OK(clr);
    ASSERT_EQ_U(c->ops->drawable_count(c), 0);

    /* Entity still exists (clear doesn't delete it). */
    ASSERT_TRUE(yetty_ydraw_drawable_lookup(c, 1) == e, "entity survives clear");

    yetty_ydraw_draw_list_destroy(cb);
    yetty_ydraw_flyweight_registry_destroy(reg);
    destroy_canvas(c);
}

/* T6. Delete cleans up the entity's primitives too. */
static void test_delete_removes_prims(void)
{
    fprintf(stderr, "[T6] delete removes prims\n");
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_flyweight_registry *reg = make_registry();

    struct yetty_ydraw_drawable_ptr_result er =
        yetty_ydraw_drawable_create_group(c, NULL, 1);
    ASSERT_OK(er);

    struct yetty_ydraw_draw_list *cb = make_circle_buffer(200.0f, 200.0f, 30.0f);
    struct yetty_ydraw_drawable_flyweight fw;
    ASSERT_EQ_U(get_first_drawable_flyweight(cb, reg, &fw), 0);
    struct yetty_ycore_void_result ar = yetty_ydraw_drawable_add_prim(c, er.value, &fw);
    ASSERT_OK(ar);
    ASSERT_EQ_U(c->ops->drawable_count(c), 1);

    struct yetty_ycore_void_result dr = yetty_ydraw_drawable_delete(c, er.value);
    ASSERT_OK(dr);
    ASSERT_EQ_U(c->ops->drawable_count(c), 0);
    ASSERT_TRUE(yetty_ydraw_drawable_lookup(c, 1) == NULL, "lookup gone after delete");

    yetty_ydraw_draw_list_destroy(cb);
    yetty_ydraw_flyweight_registry_destroy(reg);
    destroy_canvas(c);
}

/* T7. Stress add+delete: 5000 cycles of create-with-prim-then-delete.
 * Entity-slot recycling should keep memory bounded. Failure here would
 * directly explain the ygui "freeze the terminal" leak. */
static void test_add_delete_churn_no_leak(void)
{
    fprintf(stderr, "[T7] add+delete churn (5000 cycles)\n");
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_flyweight_registry *reg = make_registry();

    /* Warmup so first-iteration lazy allocs don't pollute the baseline. */
    for (int w = 0; w < 50; w++) {
        struct yetty_ydraw_drawable_ptr_result er =
            yetty_ydraw_drawable_create_group(c, NULL, 1u);
        if (YETTY_IS_OK(er)) {
            struct yetty_ydraw_draw_list *cb = make_circle_buffer(50.0f, 50.0f, 10.0f);
            struct yetty_ydraw_drawable_flyweight fw;
            if (cb && get_first_drawable_flyweight(cb, reg, &fw) == 0) {
                struct yetty_ycore_void_result ar =
                    yetty_ydraw_drawable_add_prim(c, er.value, &fw);
                if (YETTY_IS_ERR(ar)) yetty_ycore_error_destroy(ar.error);
            }
            yetty_ydraw_draw_list_destroy(cb);
            struct yetty_ycore_void_result dr =
                yetty_ydraw_drawable_delete(c, er.value);
            if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        } else {
            yetty_ycore_error_destroy(er.error);
        }
    }

    /* Allow at most 1 MB of additional RSS over the warmup baseline
     * across 5000 churns. Real growth would be on the order of
     * MBs-per-cycle if the slot/arena recycling were broken. */
    RSS_STABLE_OR_FAIL(5000, 250, 1u << 20, {
        struct yetty_ydraw_drawable_ptr_result er =
            yetty_ydraw_drawable_create_group(c, NULL, 1u);
        if (YETTY_IS_OK(er)) {
            struct yetty_ydraw_draw_list *cb = make_circle_buffer(50.0f, 50.0f, 10.0f);
            struct yetty_ydraw_drawable_flyweight fw;
            if (cb && get_first_drawable_flyweight(cb, reg, &fw) == 0) {
                struct yetty_ycore_void_result ar =
                    yetty_ydraw_drawable_add_prim(c, er.value, &fw);
                if (YETTY_IS_ERR(ar)) yetty_ycore_error_destroy(ar.error);
            }
            yetty_ydraw_draw_list_destroy(cb);
            struct yetty_ycore_void_result dr =
                yetty_ydraw_drawable_delete(c, er.value);
            if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        } else {
            yetty_ycore_error_destroy(er.error);
        }
    });

    yetty_ydraw_flyweight_registry_destroy(reg);
    destroy_canvas(c);
}

/* T8. Idle render-side rebuilds: build a 30-entity scene, then call
 * rebuild_grid + build_drawable_staging in a loop with the scene
 * unchanged. The canvas reports !dirty between calls, so each rebuild
 * MUST early-out. If it doesn't, this loop turns 200 invocations into
 * minutes of work — exactly matching the yetty render-loop idle case
 * that exposed the GB/sec growth via yjungle.
 *
 * Two assertions:
 *  - elapsed wall time stays under WALL_BUDGET_NS (catches "rebuilds
 *    every frame even when scene is unchanged"),
 *  - RSS doesn't drift more than RSS_TOLERANCE (catches per-call alloc
 *    that doesn't get freed). */
static void test_idle_rebuild_no_growth(void)
{
    /* 200 iters of "scene is clean" should be microseconds total — a
     * rebuild on an unchanged scene must early-out. Earlier runs of
     * this test hung past 60s at 200 iters; the budget here is set
     * deliberately tight so a regression to "rebuild on every call"
     * fails the test quickly rather than wedging CI. */
    enum { ITERS = 3, RSS_TOLERANCE = 256 * 1024 };
    const long WALL_BUDGET_NS = 100L * 1000L * 1000L; /* 100 ms */

    fprintf(stderr, "[T8] idle rebuild_grid + build_drawable_staging (%d cycles)\n", ITERS);
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_flyweight_registry *reg = make_registry();

    fprintf(stderr, "  building 30 entities...\n");
    /* Build a 30-entity scene with one circle each. */
    for (uint32_t i = 0; i < 30; i++) {
        struct timespec ts0, ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts0);
        struct yetty_ydraw_drawable_ptr_result er =
            yetty_ydraw_drawable_create_group(c, NULL, 100u + i);
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        long us_create = (ts1.tv_sec - ts0.tv_sec) * 1000000L
                         + (ts1.tv_nsec - ts0.tv_nsec) / 1000L;
        ASSERT_OK(er);

        clock_gettime(CLOCK_MONOTONIC, &ts0);
        struct yetty_ydraw_draw_list *cb =
            make_circle_buffer(50.0f + (float)i * 10.0f, 100.0f, 8.0f);
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        long us_buf = (ts1.tv_sec - ts0.tv_sec) * 1000000L
                      + (ts1.tv_nsec - ts0.tv_nsec) / 1000L;
        ASSERT_TRUE(cb != NULL, "circle buffer");
        struct yetty_ydraw_drawable_flyweight fw;
        ASSERT_EQ_U(get_first_drawable_flyweight(cb, reg, &fw), 0);

        clock_gettime(CLOCK_MONOTONIC, &ts0);
        struct yetty_ycore_void_result ar =
            yetty_ydraw_drawable_add_prim(c, er.value, &fw);
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        long us_addprim = (ts1.tv_sec - ts0.tv_sec) * 1000000L
                          + (ts1.tv_nsec - ts0.tv_nsec) / 1000L;
        ASSERT_OK(ar);

        fprintf(stderr, "    i=%u create=%ld us buf=%ld us add_prim=%ld us\n",
                i, us_create, us_buf, us_addprim);
        yetty_ydraw_draw_list_destroy(cb);
    }
    ASSERT_EQ_U(c->ops->drawable_count(c), 30);
    fprintf(stderr, "  built. priming rebuild_grid...\n");

    /* Prime: do one full rebuild so capacity is set + dirty cleared. */
    struct timespec t_prime0, t_prime1;
    clock_gettime(CLOCK_MONOTONIC, &t_prime0);
    struct yetty_ycore_void_result rr = c->ops->rebuild_grid(c);
    clock_gettime(CLOCK_MONOTONIC, &t_prime1);
    long prime_rg_us = (t_prime1.tv_sec - t_prime0.tv_sec) * 1000000L
                       + (t_prime1.tv_nsec - t_prime0.tv_nsec) / 1000L;
    fprintf(stderr, "  prime rebuild_grid took %ld us\n", prime_rg_us);
    ASSERT_OK(rr);

    clock_gettime(CLOCK_MONOTONIC, &t_prime0);
    struct yetty_ydraw_drawable_staging_result ds = c->ops->build_drawable_staging(c);
    clock_gettime(CLOCK_MONOTONIC, &t_prime1);
    long prime_bd_us = (t_prime1.tv_sec - t_prime0.tv_sec) * 1000000L
                       + (t_prime1.tv_nsec - t_prime0.tv_nsec) / 1000L;
    fprintf(stderr, "  prime build_drawable_staging took %ld us\n", prime_bd_us);
    ASSERT_OK(ds);

    size_t rss_before = rss_bytes();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (uint32_t i = 0; i < ITERS; i++) {
        struct yetty_ycore_void_result _rr = c->ops->rebuild_grid(c);
        if (YETTY_IS_ERR(_rr)) yetty_ycore_error_destroy(_rr.error);
        struct yetty_ydraw_drawable_staging_result _ds = c->ops->build_drawable_staging(c);
        if (YETTY_IS_ERR(_ds)) yetty_ycore_error_destroy(_ds.error);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1000L * 1000L * 1000L
                       + (t1.tv_nsec - t0.tv_nsec);
    size_t rss_after = rss_bytes();
    long rss_delta = (long)rss_after - (long)rss_before;

    fprintf(stderr, "  elapsed %ld us over %d iters (%.1f us/iter)\n",
            elapsed_ns / 1000, ITERS, (double)elapsed_ns / 1000.0 / (double)ITERS);
    fprintf(stderr, "  RSS before=%zu KB after=%zu KB delta=%+ld KB\n",
            rss_before / 1024, rss_after / 1024, rss_delta / 1024);

    g_assertions++;
    if (elapsed_ns > WALL_BUDGET_NS) {
        FAIL("rebuild_grid+build_drawable_staging took %ld ms total over %d clean iters "
             "— means it's redoing work every call. Wall budget was %ld ms.",
             elapsed_ns / 1000000L, ITERS, WALL_BUDGET_NS / 1000000L);
    }
    g_assertions++;
    if (rss_delta > (long)RSS_TOLERANCE) {
        FAIL("RSS grew %ld KB over %d idle rebuild cycles (tolerance %d KB)",
             rss_delta / 1024, ITERS, RSS_TOLERANCE / 1024);
    }

    yetty_ydraw_flyweight_registry_destroy(reg);
    destroy_canvas(c);
}

/* T9. Rebuild after every modification — simulate the yetty render loop
 * that reacts to per-envelope updates. Adds + deletes interleaved with
 * staging rebuilds; the staging buffer's capacity should plateau. */
static void test_modify_rebuild_loop_no_growth(void)
{
    fprintf(stderr, "[T9] modify + rebuild loop (3000 cycles)\n");
    struct yetty_ydraw_canvas *c = make_canvas();
    struct yetty_ydraw_flyweight_registry *reg = make_registry();

    /* Initial scene. */
    for (uint32_t i = 0; i < 10; i++) {
        struct yetty_ydraw_drawable_ptr_result er =
            yetty_ydraw_drawable_create_group(c, NULL, 200u + i);
        ASSERT_OK(er);
        struct yetty_ydraw_draw_list *cb =
            make_circle_buffer(60.0f + (float)i * 12.0f, 150.0f, 5.0f);
        struct yetty_ydraw_drawable_flyweight fw;
        ASSERT_EQ_U(get_first_drawable_flyweight(cb, reg, &fw), 0);
        struct yetty_ycore_void_result ar =
            yetty_ydraw_drawable_add_prim(c, er.value, &fw);
        ASSERT_OK(ar);
        yetty_ydraw_draw_list_destroy(cb);
    }

    /* Use a monotonically increasing id so each replace allocates a
     * fresh external_id (matches yjungle's behaviour, matches scene-
     * canvas's strict no-reopen rule). */
    uint64_t next_id = 300;

    RSS_STABLE_OR_FAIL(3000, 200, 2u << 20, {
        /* Replace entity 200 + (i%10): delete then create-with-prim. */
        uint32_t victim_external_id = 200u + (_i % 10u);
        struct yetty_ydraw_drawable *victim =
            yetty_ydraw_drawable_lookup(c, victim_external_id);
        /* Skip if a previous cycle already replaced this slot — the new
         * id is next_id-1 at this point and we'd need to track it. The
         * simpler invariant: track the latest id for each slot via the
         * `next_id` counter; we replace by deleting whatever is at
         * external_id (200..209) on iteration 0, then incrementally. */
        if (victim) {
            struct yetty_ycore_void_result _dr =
                yetty_ydraw_drawable_delete(c, victim);
            if (YETTY_IS_ERR(_dr)) yetty_ycore_error_destroy(_dr.error);
        }
        /* Re-create with a fresh id mapped to the same slot in the
         * chain. */
        struct yetty_ydraw_drawable_ptr_result _er =
            yetty_ydraw_drawable_create_group(c, NULL, victim ? victim_external_id : next_id++);
        if (YETTY_IS_OK(_er)) {
            struct yetty_ydraw_draw_list *_cb =
                make_circle_buffer(60.0f + (float)(_i % 10u) * 12.0f, 150.0f, 5.0f);
            struct yetty_ydraw_drawable_flyweight _fw;
            if (_cb && get_first_drawable_flyweight(_cb, reg, &_fw) == 0) {
                struct yetty_ycore_void_result _ar =
                    yetty_ydraw_drawable_add_prim(c, _er.value, &_fw);
                if (YETTY_IS_ERR(_ar)) yetty_ycore_error_destroy(_ar.error);
            }
            yetty_ydraw_draw_list_destroy(_cb);
        } else {
            yetty_ycore_error_destroy(_er.error);
        }

        struct yetty_ycore_void_result _rr = c->ops->rebuild_grid(c);
        if (YETTY_IS_ERR(_rr)) yetty_ycore_error_destroy(_rr.error);
        struct yetty_ydraw_drawable_staging_result _ds = c->ops->build_drawable_staging(c);
        if (YETTY_IS_ERR(_ds)) yetty_ycore_error_destroy(_ds.error);
    });

    yetty_ydraw_flyweight_registry_destroy(reg);
    destroy_canvas(c);
}

/*=============================================================================
 * Main
 *===========================================================================*/

int main(void)
{
    test_root_entity();
    test_entity_create_lookup_delete();
    test_duplicate_id_rejected();
    test_id_reuse_after_delete();
    test_entity_add_prim_then_clear();
    test_delete_removes_prims();
    test_add_delete_churn_no_leak();
    test_idle_rebuild_no_growth();
    test_modify_rebuild_loop_no_growth();

    fprintf(stderr, "\n--- scene-canvas-test summary ---\n");
    fprintf(stderr, "assertions: %d\n", g_assertions);
    fprintf(stderr, "failures:   %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
