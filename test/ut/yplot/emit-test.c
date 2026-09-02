/*
 * yplot emit_into contract test — pure, headless (GPU-less yetty_yplot_core).
 *
 * Locks down the new public emission primitive yetty_yplot_emit_into(): input
 * validation, emitting into an existing list (append, not replace), origin
 * handling, and the log-range failure/partial-list behavior. Records are
 * inspected via the list's serialized byte size (no prim-count API); the deeper
 * derived-flag and record-level assertions land with the shared plan builder in
 * the frontend-unification step.
 */

#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yplot/resolve.h>
#include <yetty/yplot/yplot.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A minimal, serializable plan: a curve-less plot (grid/axes only) at a known
 * size. Enough to exercise emit_into's contract without the yfsvm pipeline. */
static struct yetty_yplot_render_plan minimal_plan(void)
{
    struct yetty_yplot_render_plan plan = {0};
    plan.uniforms.bounds_w = 400.0f;
    plan.uniforms.bounds_h = 200.0f;
    plan.uniforms.x_min = -1.0f;
    plan.uniforms.x_max = 1.0f;
    plan.uniforms.y_min = -1.0f;
    plan.uniforms.y_max = 1.0f;
    plan.uniforms.flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES;
    plan.uniforms.function_count = 0;
    plan.buffers.bytecode = NULL;
    plan.buffers.bytecode_len = 0;
    plan.buffers.data = NULL;
    plan.buffers.data_count = 0;
    plan.legend_count = 0;
    return plan;
}

static int emit_fails(const struct yetty_yplot_render_plan *plan,
                      struct yetty_ydraw_drawable_list *dest, float origin_x, float origin_y)
{
    struct yetty_ycore_void_result res = yetty_yplot_emit_into(plan, dest, origin_x, origin_y);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return 1;
    }
    return 0;
}

/* emit_into APPENDS into an existing list: prior records survive and the plot
 * adds bytes. */
static void test_emit_into_appends(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();
    struct yetty_ycore_void_result first = yetty_yplot_emit_into(&plan, dest, 0.0f, 0.0f);
    YTEST_REQUIRE(test, YETTY_IS_OK(first));
    size_t after_first = yetty_ydraw_drawable_list_size(dest);
    YTEST_CHECK(test, after_first > 0);

    /* A second emit into the same list must not shrink or replace the first. */
    struct yetty_ycore_void_result second = yetty_yplot_emit_into(&plan, dest, 0.0f, 0.0f);
    YTEST_REQUIRE(test, YETTY_IS_OK(second));
    size_t after_second = yetty_ydraw_drawable_list_size(dest);
    YTEST_CHECK(test, after_second > after_first);

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* Input validation rejects bad plans/args before mutating dest. */
static void test_emit_into_validation(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();

    YTEST_CHECK(test, emit_fails(NULL, dest, 0.0f, 0.0f));
    YTEST_CHECK(test, emit_fails(&plan, NULL, 0.0f, 0.0f));
    YTEST_CHECK(test, emit_fails(&plan, dest, INFINITY, 0.0f)); /* non-finite origin */
    YTEST_CHECK(test, emit_fails(&plan, dest, 0.0f, NAN));

    struct yetty_yplot_render_plan bad_dims = minimal_plan();
    bad_dims.uniforms.bounds_w = 0.0f; /* non-positive figure dimension */
    YTEST_CHECK(test, emit_fails(&bad_dims, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan bad_buffers = minimal_plan();
    bad_buffers.buffers.data = NULL;
    bad_buffers.buffers.data_count = 3; /* count > 0 with NULL data */
    YTEST_CHECK(test, emit_fails(&bad_buffers, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan bad_bytecode = minimal_plan();
    bad_bytecode.buffers.bytecode = NULL;
    bad_bytecode.buffers.bytecode_len = 4; /* len > 0 with NULL bytecode */
    YTEST_CHECK(test, emit_fails(&bad_bytecode, dest, 0.0f, 0.0f));

    struct yetty_yplot_data_buffer null_samples = {0};
    null_samples.samples = NULL;
    null_samples.count = 5; /* per-buffer count > 0 with NULL samples */
    struct yetty_yplot_render_plan bad_databuf = minimal_plan();
    bad_databuf.buffers.data = &null_samples;
    bad_databuf.buffers.data_count = 1;
    YTEST_CHECK(test, emit_fails(&bad_databuf, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan big_legend = minimal_plan();
    big_legend.legend_count = 10000u; /* over capacity -> reject, not clamp */
    YTEST_CHECK(test, emit_fails(&big_legend, dest, 0.0f, 0.0f));

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* A log axis over a non-positive range is rejected at the shared choke point. */
static void test_emit_into_log_range(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();
    plan.uniforms.flags |= YETTY_YPLOT_FLAG_XLOG; /* x range spans <= 0 */
    YTEST_CHECK(test, emit_fails(&plan, dest, 0.0f, 0.0f));

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* The shared expression path (yetty_yplot_emit_expression) is deterministic and
 * origin-consistent — the parity guarantee that yecho / yplot-yaml / the shell
 * render identically when they hand it the same source + config. */
static void test_emit_expression_shared_path(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 400.0f,
        .bounds_h = 200.0f,
        .x_min = -3.14159f,
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    const char *source = "f=sin(x); g=cos(x); @plot.title=\"t\"; @plot.legend=on";
    size_t source_len = strlen(source);

    struct yetty_ydraw_drawable_list_result la =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    struct yetty_ydraw_drawable_list_result lb =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, la);
    YTEST_REQUIRE_OK(test, lb);

    float figure_w = 0.0f, figure_h = 0.0f;
    YTEST_REQUIRE(
        test, YETTY_IS_OK(yetty_yplot_emit_expression(source, source_len, NULL, 0, &config,
                                                      la.value, 0.0f, 0.0f, &figure_w, &figure_h)));
    YTEST_REQUIRE(test,
                  YETTY_IS_OK(yetty_yplot_emit_expression(source, source_len, NULL, 0, &config,
                                                          lb.value, 0.0f, 0.0f, NULL, NULL)));

    size_t size_a = yetty_ydraw_drawable_list_size(la.value);
    size_t size_b = yetty_ydraw_drawable_list_size(lb.value);
    YTEST_CHECK(test, size_a > 0 && size_a == size_b);
    YTEST_CHECK(test, memcmp(yetty_ydraw_drawable_list_data(la.value),
                             yetty_ydraw_drawable_list_data(lb.value), size_a) == 0);
    YTEST_CHECK_NEAR(test, figure_w, 400.0f, 1e-3);
    YTEST_CHECK_NEAR(test, figure_h, 200.0f, 1e-3);

    /* Emitting at a non-zero origin shifts coordinates but not the byte size. */
    struct yetty_ydraw_drawable_list_result lc =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, lc);
    YTEST_REQUIRE(test,
                  YETTY_IS_OK(yetty_yplot_emit_expression(source, source_len, NULL, 0, &config,
                                                          lc.value, 100.0f, 50.0f, NULL, NULL)));
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(lc.value) == size_a);

    (void)yetty_ydraw_drawable_list_destroy(la.value);
    (void)yetty_ydraw_drawable_list_destroy(lb.value);
    (void)yetty_ydraw_drawable_list_destroy(lc.value);
}

/* A plot that declares only a data buffer and no function renders the buffer as
 * a curve (compiling a zero-function program is skipped, not treated as an
 * error). Regression pin for the buffer-language demo. */
static void test_emit_expression_buffer_only(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 400.0f,
        .bounds_h = 200.0f,
        .x_min = 0.0f,
        .x_max = 1.0f,
        .y_min = -1.0f,
        .y_max = 1.0f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    const char *source = "data=buffer; @data.size=8; @data.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2";

    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list);
    struct yetty_ycore_void_result emit_res = yetty_yplot_emit_expression(
        source, strlen(source), NULL, 0, &config, list.value, 0.0f, 0.0f, NULL, NULL);
    YTEST_CHECK(test, YETTY_IS_OK(emit_res));
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_error_destroy(emit_res.error);
    }
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(list.value) > 0);
    (void)yetty_ydraw_drawable_list_destroy(list.value);
}

/* Whether `word` occurs on any 4-byte boundary of `bytes`. */
static int bytes_contain_word(const uint8_t *bytes, size_t size, uint32_t word)
{
    for (size_t offset = 0; offset + sizeof(word) <= size; offset += sizeof(word)) {
        uint32_t probe;
        memcpy(&probe, bytes + offset, sizeof(probe));
        if (probe == word) {
            return 1;
        }
    }
    return 0;
}

/* Self-owned chrome contract — the receiver half of the resize story,
 * fully headless. A record emitted with a chrome_group_id carries the
 * retained chrome tail; yetty_yplot_record_rechrome re-plans it IN PLACE
 * at a new figure size (inset plot bounds + tick steps rewritten, sample
 * data untouched) and emits fresh chrome prims for the new layout. A
 * range patched into the uniform words drives the same re-plan. Legacy
 * records (no chrome group) reject. */
static void test_record_rechrome(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 300.0f,
        .bounds_h = 200.0f,
        .x_min = 0.0f,
        .x_max = 10.0f,
        .y_min = 0.0f,
        .y_max = 100.0f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
        .title = "cpu",
        .chrome_group_id = 77u,
    };
    float samples[4] = {11.0f, 22.0f, 33.0f, 44.0f};
    struct yetty_yplot_buffer_input buffer = {
        .samples = samples,
        .count = 4,
        .name = "live",
        .ring = true,
    };
    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list);
    YTEST_REQUIRE_OK(test, yetty_yplot_emit_expression("", 0, &buffer, 1, &config, list.value, 0.0f,
                                                       0.0f, NULL, NULL));

    /* The complex record is the first thing in the list's raw stream. */
    const uint8_t *stream = yetty_ydraw_drawable_list_data(list.value);
    size_t stream_size = yetty_ydraw_drawable_list_size(list.value);
    YTEST_REQUIRE(test, stream != NULL && stream_size > 8u);
    uint32_t record_header[2];
    memcpy(record_header, stream, sizeof(record_header));
    YTEST_REQUIRE(test, record_header[0] == 0x80000003u);
    size_t record_size = 2u * sizeof(uint32_t) + record_header[1];
    YTEST_REQUIRE(test, record_size <= stream_size);
    uint8_t *record = malloc(record_size);
    YTEST_REQUIRE(test, record != NULL);
    memcpy(record, stream, record_size);
    /* The chrome group and the tick-label text prims follow the record. */
    YTEST_CHECK(test, stream_size > record_size);
    (void)yetty_ydraw_drawable_list_destroy(list.value);

    YTEST_CHECK_EQ_INT(test, (int)yetty_yplot_record_chrome_group(record, record_size), 77);

    uint32_t *payload = (uint32_t *)record + 2;
    float inset_w_before;
    float inset_h_before;
    memcpy(&inset_w_before, &payload[2], sizeof(float));
    memcpy(&inset_h_before, &payload[3], sizeof(float));
    YTEST_CHECK(test, inset_w_before < 300.0f); /* label margins were inset */

    /* Re-plan at 600x400: the inset plot rect grows (still under the new
     * figure size) and the SAMPLE DATA WORDS are byte-identical. */
    YTEST_REQUIRE_OK(test, yetty_yplot_record_rechrome(record, record_size, 600.0f, 400.0f, NULL));
    float inset_w_after;
    float inset_h_after;
    memcpy(&inset_w_after, &payload[2], sizeof(float));
    memcpy(&inset_h_after, &payload[3], sizeof(float));
    YTEST_CHECK(test, inset_w_after > inset_w_before && inset_w_after < 600.0f);
    YTEST_CHECK(test, inset_h_after > inset_h_before && inset_h_after < 400.0f);
    union {
        float value;
        uint32_t bits;
    } sample_probe;
    sample_probe.value = 33.0f;
    YTEST_CHECK(test, bytes_contain_word(record, record_size, sample_probe.bits));

    /* Fresh chrome prims for the new layout, including the title text. */
    struct yetty_ydraw_drawable_list_result chrome_list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, chrome_list);
    YTEST_REQUIRE_OK(
        test, yetty_yplot_record_rechrome(record, record_size, 0.0f, 0.0f, chrome_list.value));
    const uint8_t *chrome_bytes = yetty_ydraw_drawable_list_data(chrome_list.value);
    size_t chrome_size = yetty_ydraw_drawable_list_size(chrome_list.value);
    YTEST_CHECK(test, chrome_size > 0);
    int title_found = 0;
    for (size_t offset = 0; !title_found && offset + 3u <= chrome_size; offset++) {
        title_found = memcmp(chrome_bytes + offset, "cpu", 3u) == 0;
    }
    YTEST_CHECK(test, title_found);
    (void)yetty_ydraw_drawable_list_destroy(chrome_list.value);

    /* A range patched into the uniform words re-plans the tick steps. */
    float step_before;
    memcpy(&step_before, &payload[9], sizeof(float)); /* y_step */
    float new_y_min = 0.0f;
    float new_y_max = 8.0f;
    memcpy(&payload[6], &new_y_min, sizeof(float));
    memcpy(&payload[7], &new_y_max, sizeof(float));
    YTEST_REQUIRE_OK(test, yetty_yplot_record_rechrome(record, record_size, 0.0f, 0.0f, NULL));
    float step_after;
    memcpy(&step_after, &payload[9], sizeof(float));
    YTEST_CHECK(test, step_after != step_before);
    free(record);

    /* Legacy record (no chrome group): the accessor answers 0 and a
     * rechrome is a clean error. */
    struct yetty_yplot_render_config legacy_config = config;
    legacy_config.chrome_group_id = 0u;
    struct yetty_ydraw_drawable_list_result legacy_list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, legacy_list);
    YTEST_REQUIRE_OK(test, yetty_yplot_emit_expression("", 0, &buffer, 1, &legacy_config,
                                                       legacy_list.value, 0.0f, 0.0f, NULL, NULL));
    const uint8_t *legacy_stream = yetty_ydraw_drawable_list_data(legacy_list.value);
    uint32_t legacy_header[2];
    memcpy(legacy_header, legacy_stream, sizeof(legacy_header));
    size_t legacy_size = 2u * sizeof(uint32_t) + legacy_header[1];
    uint8_t *legacy_record = malloc(legacy_size);
    YTEST_REQUIRE(test, legacy_record != NULL);
    memcpy(legacy_record, legacy_stream, legacy_size);
    (void)yetty_ydraw_drawable_list_destroy(legacy_list.value);
    YTEST_CHECK_EQ_INT(test, (int)yetty_yplot_record_chrome_group(legacy_record, legacy_size), 0);
    struct yetty_ycore_void_result legacy_res =
        yetty_yplot_record_rechrome(legacy_record, legacy_size, 500.0f, 300.0f, NULL);
    YTEST_CHECK(test, YETTY_IS_ERR(legacy_res));
    if (YETTY_IS_ERR(legacy_res)) {
        yetty_ycore_error_destroy(legacy_res.error);
    }
    free(legacy_record);
}

/* Extract the leading complex record of an emitted plot list into a
 * fresh mutable buffer. */
static uint8_t *extract_record(struct ytest *test, const struct yetty_yplot_render_config *config,
                               size_t *out_size)
{
    float samples[4] = {11.0f, 22.0f, 33.0f, 44.0f};
    struct yetty_yplot_buffer_input buffer = {
        .samples = samples,
        .count = 4,
        .name = "live",
        .ring = true,
    };
    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list);
    YTEST_REQUIRE_OK(test, yetty_yplot_emit_expression("", 0, &buffer, 1, config, list.value, 0.0f,
                                                       0.0f, NULL, NULL));
    const uint8_t *stream = yetty_ydraw_drawable_list_data(list.value);
    uint32_t record_header[2];
    memcpy(record_header, stream, sizeof(record_header));
    YTEST_REQUIRE(test, record_header[0] == 0x80000003u);
    size_t record_size = 2u * sizeof(uint32_t) + record_header[1];
    uint8_t *record = malloc(record_size);
    YTEST_REQUIRE(test, record != NULL);
    memcpy(record, stream, record_size);
    (void)yetty_ydraw_drawable_list_destroy(list.value);
    *out_size = record_size;
    return record;
}

/* Ranges patch — ATOMIC: a successful patch rewrites the range words and
 * re-plans the tick steps; every rejected patch (bad range, bad axis, a
 * legacy record without the chrome tail) leaves the record byte-identical
 * to before the call. The RANGES update op is a thin wrapper over this. */
static void test_record_patch_ranges(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 300.0f,
        .bounds_h = 200.0f,
        .x_min = 0.0f,
        .x_max = 10.0f,
        .y_min = 0.0f,
        .y_max = 100.0f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
        .title = "cpu",
        .chrome_group_id = 88u,
    };
    size_t record_size = 0;
    uint8_t *record = extract_record(test, &config, &record_size);
    uint32_t *payload = (uint32_t *)record + 2;

    /* Successful y patch: range words move, tick step re-plans. */
    float step_before;
    memcpy(&step_before, &payload[9], sizeof(float));
    YTEST_REQUIRE_OK(test, yetty_yplot_record_patch_ranges(record, record_size, 1u, 0.0f, 8.0f));
    float patched_min;
    float patched_max;
    float step_after;
    memcpy(&patched_min, &payload[6], sizeof(float));
    memcpy(&patched_max, &payload[7], sizeof(float));
    memcpy(&step_after, &payload[9], sizeof(float));
    YTEST_CHECK(test, patched_min == 0.0f && patched_max == 8.0f);
    YTEST_CHECK(test, step_after != step_before);

    /* Every rejection is a byte-identical no-op. */
    uint8_t *snapshot = malloc(record_size);
    YTEST_REQUIRE(test, snapshot != NULL);
    memcpy(snapshot, record, record_size);
    struct yetty_ycore_void_result bad_range_res =
        yetty_yplot_record_patch_ranges(record, record_size, 1u, 5.0f, 5.0f);
    YTEST_CHECK(test, YETTY_IS_ERR(bad_range_res));
    if (YETTY_IS_ERR(bad_range_res)) {
        yetty_ycore_error_destroy(bad_range_res.error);
    }
    struct yetty_ycore_void_result bad_axis_res =
        yetty_yplot_record_patch_ranges(record, record_size, 2u, 0.0f, 1.0f);
    YTEST_CHECK(test, YETTY_IS_ERR(bad_axis_res));
    if (YETTY_IS_ERR(bad_axis_res)) {
        yetty_ycore_error_destroy(bad_axis_res.error);
    }
    YTEST_CHECK(test, memcmp(record, snapshot, record_size) == 0);
    free(snapshot);
    free(record);

    /* Legacy record (no chrome tail): the patch reports failure AND the
     * record — range words included — is byte-identical afterwards. */
    struct yetty_yplot_render_config legacy_config = config;
    legacy_config.chrome_group_id = 0u;
    size_t legacy_size = 0;
    uint8_t *legacy_record = extract_record(test, &legacy_config, &legacy_size);
    uint8_t *legacy_snapshot = malloc(legacy_size);
    YTEST_REQUIRE(test, legacy_snapshot != NULL);
    memcpy(legacy_snapshot, legacy_record, legacy_size);
    struct yetty_ycore_void_result legacy_res =
        yetty_yplot_record_patch_ranges(legacy_record, legacy_size, 1u, 0.0f, 8.0f);
    YTEST_CHECK(test, YETTY_IS_ERR(legacy_res));
    if (YETTY_IS_ERR(legacy_res)) {
        yetty_ycore_error_destroy(legacy_res.error);
    }
    YTEST_CHECK(test, memcmp(legacy_record, legacy_snapshot, legacy_size) == 0);
    free(legacy_snapshot);
    free(legacy_record);
}

/* Number of records in a drawable list's raw stream (SDF fixed strides,
 * [type][payload] family otherwise) — mirrors the receiver's walk. */
static uint32_t count_prim_records(const struct yetty_ydraw_drawable_list *list)
{
    const uint8_t *bytes = yetty_ydraw_drawable_list_data(list);
    size_t remaining = yetty_ydraw_drawable_list_size(list);
    uint32_t count = 0;
    while (bytes && remaining >= 2u * sizeof(uint32_t)) {
        uint32_t type;
        uint32_t word1;
        memcpy(&type, bytes, sizeof(type));
        memcpy(&word1, bytes + sizeof(uint32_t), sizeof(word1));
        size_t stride = yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG);
        if (stride > 0) {
            stride += (type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0u;
        } else {
            stride = 2u * sizeof(uint32_t) + word1;
        }
        if (stride == 0 || stride > remaining) {
            break;
        }
        count++;
        bytes += stride;
        remaining -= stride;
    }
    return count;
}

/* Yplot chrome can legally exceed 64 records — log ticks on BOTH axes,
 * the title/axis strips, and a forced legend over 8 named expression
 * curves plus 16 named buffers (a swatch AND a label per entry). A
 * receiver-side fixed staging cap would strand such figures with stale
 * chrome forever; this pins that the emission really is that large and
 * that a local re-plan regenerates ALL of it. */
static void test_big_chrome_has_no_record_cap(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 800.0f,
        .bounds_h = 900.0f,
        .x_min = 1.0f,
        .x_max = 1.0e6f,
        .y_min = 1.0f,
        .y_max = 1.0e6f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS |
                 YETTY_YPLOT_FLAG_XLOG | YETTY_YPLOT_FLAG_YLOG,
        .title = "many curves",
        .x_label = "time",
        .y_label = "value",
        .legend_mode = YETTY_YPLOT_LEGEND_ON,
        .chrome_group_id = 99u,
    };
    const char *source = "curve01=sin(x); curve02=cos(x); curve03=sin(2*x); curve04=cos(2*x); "
                         "curve05=sin(3*x); curve06=cos(3*x); curve07=sin(4*x); curve08=cos(4*x)";
    float samples[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    struct yetty_yplot_buffer_input buffers[16];
    char buffer_names[16][16];
    memset(buffers, 0, sizeof(buffers));
    for (uint32_t index = 0; index < 16u; index++) {
        snprintf(buffer_names[index], sizeof(buffer_names[index]), "series%02u", index);
        buffers[index].samples = samples;
        buffers[index].count = 4;
        buffers[index].name = buffer_names[index];
    }
    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list);
    YTEST_REQUIRE_OK(test,
                     yetty_yplot_emit_expression(source, strlen(source), buffers, 16u, &config,
                                                 list.value, 0.0f, 0.0f, NULL, NULL));
    const uint8_t *stream = yetty_ydraw_drawable_list_data(list.value);
    uint32_t record_header[2];
    memcpy(record_header, stream, sizeof(record_header));
    YTEST_REQUIRE(test, record_header[0] == 0x80000003u);
    size_t record_size = 2u * sizeof(uint32_t) + record_header[1];
    uint8_t *record = malloc(record_size);
    YTEST_REQUIRE(test, record != NULL);
    memcpy(record, stream, record_size);
    (void)yetty_ydraw_drawable_list_destroy(list.value);

    struct yetty_ydraw_drawable_list_result chrome_list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, chrome_list);
    YTEST_REQUIRE_OK(
        test, yetty_yplot_record_rechrome(record, record_size, 0.0f, 0.0f, chrome_list.value));
    uint32_t chrome_records = count_prim_records(chrome_list.value);
    YTEST_CHECK(test, chrome_records > 64u);
    (void)yetty_ydraw_drawable_list_destroy(chrome_list.value);
    free(record);
}

int main(void)
{
    struct ytest test = ytest_begin("yplot_emit");
    YTEST_RUN(&test, test_emit_into_appends);
    YTEST_RUN(&test, test_emit_into_validation);
    YTEST_RUN(&test, test_emit_into_log_range);
    YTEST_RUN(&test, test_emit_expression_shared_path);
    YTEST_RUN(&test, test_emit_expression_buffer_only);
    YTEST_RUN(&test, test_record_rechrome);
    YTEST_RUN(&test, test_record_patch_ranges);
    YTEST_RUN(&test, test_big_chrome_has_no_record_cap);
    return ytest_end(&test);
}
