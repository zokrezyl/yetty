/*
 * yterminal ydraw-ingest ROUND TRIP — headless (#728), no GPU, no PTY.
 *
 * The acceptance test for non-destructive complex resize: the EXACT
 * serialized envelope bytes a producer ships (drawable-list serialize —
 * what ygui2's framework_ship encodes) are fed through the terminal's
 * real ingest pipeline (yetty_yterminal_mime_ingest_serialized →
 * terminal_ydraw_apply_body: record walk, scope folding, NODE_ID
 * binding, CMD_PATH'd update routing, journaling, extent refresh,
 * receiver-local chrome replacement) against an INSTRUMENTED complex
 * factory, and the resulting grid state is asserted:
 *
 *   - a geometry update reaches the SAME runtime instance — never a
 *     re-creation;
 *   - the retained scroll-retirement extent follows the runtime's new
 *     AABB;
 *   - the chrome group's content is replaced LOCALLY (no wire bytes)
 *     with fresh prims stamped at the figure's ORIGINAL paint-z, even
 *     when the update envelope carries no scope;
 *   - a failed chrome emission leaves the OLD chrome standing and the
 *     retryable chrome_dirty state set; the next update retries and
 *     converges.
 *
 * The yplot half of the contract (record rechrome, ranges rollback) is
 * covered headless in yplot_emit; this test proves the terminal-side
 * ORCHESTRATION those half-tests bypass.
 */

#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/api/ygui2/framework.h>
#include <yetty/api/ygui2/widget.h>
#include <yetty/api/ygui2/widgets/panel.h>
#include <yetty/api/yvterm/grid.h>
#include <yetty/api/yvterm/vterm.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/yui-core/view.h>
#include <yetty/yvterm/group-key.h>

#include "ytest.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A complex-tier type id no real factory owns. */
#define HARNESS_COMPLEX_TYPE 0x80cdef01u

/* Wire record: [type][payload_size][x][y][w][h][chrome_group_id][marker].
 * Bounds words sit at payload offsets 0..3 (the generic complex AABB
 * contract the ingest reads). */
enum { HARNESS_RECORD_WORDS = 8, HARNESS_PAYLOAD_BYTES = 6 * 4 };

/* Instrumentation shared by the factory and the test body. */
struct harness_state {
    int created;
    int destroyed;
    struct yetty_ydraw_complex *last_instance;
    uint32_t update_calls;
    uint32_t emit_chrome_calls;
    int fail_emit_chrome;
    uint32_t chrome_generation; /* stamped into each emitted chrome label */
};

struct harness_factory {
    struct yetty_ydraw_concrete_factory base;
    struct harness_state *state;
};

static void harness_instance_destroy(struct yetty_ydraw_complex *self)
{
    struct harness_state *state = self->instance_data;
    state->destroyed++;
    free(self->buffer_data);
    free(self);
}

/* Geometry-op semantics mirrored from yplot: sentinel in the
 * sample_offset slot patches the record's bounds words in place, updates
 * the AABB and flags the chrome — sample storage (the marker word here)
 * untouched. */
static struct yetty_ycore_void_result harness_instance_update(struct yetty_ydraw_complex *self,
                                                              uint32_t target_field,
                                                              const void *body, size_t body_size)
{
    struct harness_state *state = self->instance_data;
    (void)target_field;
    state->update_calls++;
    if (!body || body_size < 12u) {
        return YETTY_ERR(yetty_ycore_void, "harness update: body truncated");
    }
    const uint32_t *words = body;
    if (words[0] != 0xFFFFFFFEu) {
        return YETTY_ERR(yetty_ycore_void, "harness update: unknown op");
    }
    uint32_t *payload = (uint32_t *)self->buffer_data + 2;
    payload[2] = words[1]; /* new width bits */
    payload[3] = words[2]; /* new height bits */
    struct rectangle_result aabb_res = yetty_ydraw_complex_record_aabb(self->buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        self->bounds = aabb_res.value;
    } else {
        yetty_ycore_error_destroy(aabb_res.error);
    }
    self->dirty = 1;
    self->chrome_dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result harness_instance_emit_chrome(
    struct yetty_ydraw_complex *self, struct yetty_ydraw_drawable_list *list)
{
    struct harness_state *state = self->instance_data;
    state->emit_chrome_calls++;
    if (state->fail_emit_chrome) {
        return YETTY_ERR(yetty_ycore_void, "harness emit_chrome: injected failure");
    }
    char label[32];
    snprintf(label, sizeof(label), "CHROME%03u", state->chrome_generation);
    struct yetty_ycore_buffer view = {
        .data = (uint8_t *)(uintptr_t)label,
        .capacity = strlen(label),
        .size = strlen(label),
    };
    return yetty_ydraw_drawable_list_add_text(list, 4.0f, 12.0f, &view, 10.0f, 0xFFFFFFFFu, 0, -1,
                                              0.0f);
}

static const struct yetty_ydraw_complex_ops *harness_instance_ops(void)
{
    static const struct yetty_ydraw_complex_ops ops = {
        .destroy = harness_instance_destroy,
        .update = harness_instance_update,
        .emit_chrome = harness_instance_emit_chrome,
    };
    return &ops;
}

static struct yetty_ydraw_complex_ptr_result harness_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    struct harness_factory *factory = (struct harness_factory *)self;
    if (!buffer_data || size < HARNESS_RECORD_WORDS * sizeof(uint32_t)) {
        return YETTY_ERR(yetty_ydraw_complex_ptr, "harness create: record too small");
    }
    struct yetty_ydraw_complex *instance = calloc(1, sizeof(*instance));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_complex_ptr, "harness create: alloc");
    }
    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_complex_ptr, "harness create: record alloc");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = HARNESS_COMPLEX_TYPE;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->ops = harness_instance_ops();
    instance->instance_data = factory->state;
    instance->chrome_group_id = ((const uint32_t *)buffer_data)[6];
    struct rectangle_result aabb_res = yetty_ydraw_complex_record_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        instance->bounds = aabb_res.value;
    } else {
        yetty_ycore_error_destroy(aabb_res.error);
    }
    factory->state->created++;
    factory->state->last_instance = instance;
    return YETTY_OK(yetty_ydraw_complex_ptr, instance);
}

static void harness_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                     struct yetty_ydraw_complex *instance)
{
    (void)self;
    harness_instance_destroy(instance);
}

static void harness_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    free(self);
}

static struct harness_factory *harness_factory_create(struct harness_state *state)
{
    struct harness_factory *factory = calloc(1, sizeof(*factory));
    if (!factory) {
        return NULL;
    }
    factory->base.type_id = HARNESS_COMPLEX_TYPE;
    factory->base.destroy = harness_factory_destroy;
    factory->base.create_instance = harness_create_instance;
    factory->base.destroy_instance = harness_destroy_instance;
    factory->state = state;
    return factory;
}

/*===========================================================================
 * Envelope builders — the producer half, byte-identical to what a live
 * app's framework_ship() hands the DCS channel.
 *=========================================================================*/

static void harness_record_write(uint32_t *words, float x, float y, float w, float h,
                                 uint32_t chrome_group_id, uint32_t marker)
{
    words[0] = HARNESS_COMPLEX_TYPE;
    words[1] = HARNESS_PAYLOAD_BYTES;
    memcpy(&words[2], &x, sizeof(float));
    memcpy(&words[3], &y, sizeof(float));
    memcpy(&words[4], &w, sizeof(float));
    memcpy(&words[5], &h, sizeof(float));
    words[6] = chrome_group_id;
    words[7] = marker;
}

/* Feed one drawable list's serialized bytes through the terminal ingest. */
static void ingest_list(struct ytest *test, struct yetty_yterminal_terminal *terminal,
                        struct yetty_ydraw_drawable_list *list)
{
    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(list, &raw);
    YTEST_REQUIRE(test, raw != NULL && raw_size > 0u);
    YTEST_REQUIRE_OK(test, yetty_yterminal_mime_ingest_serialized(terminal, raw, raw_size));
}

/* Count live chrome labels carrying `generation` (and, when
 * `out_other_generations` is set, any OTHER generation still alive) on
 * the block, and report the paint-z of the matched label. */
static void scan_chrome_labels(struct ytest *test, struct yetty_yclass_object *grid_obj,
                               uint32_t generation, int *out_found, int *out_other_generations,
                               int32_t *out_paint_z)
{
    *out_found = 0;
    if (out_other_generations) {
        *out_other_generations = 0;
    }
    char expected[32];
    snprintf(expected, sizeof(expected), "CHROME%03u", generation);
    struct yetty_ycore_void_result view_res = yetty_yvterm_grid_set_view(grid_obj, 0, 0);
    YTEST_REQUIRE_OK(test, view_res);
    uint32_t resolved = 0;
    struct yetty_ycore_const_uint32_ptr_result window_res =
        yetty_yvterm_grid_view_window(grid_obj, 24, &resolved);
    YTEST_REQUIRE_OK(test, window_res);
    YTEST_REQUIRE(test, window_res.value != NULL);
    for (uint32_t row = 0; row < resolved; ++row) {
        uint32_t slot = window_res.value[row];
        struct yetty_ycore_uint32_result block_count_res =
            yetty_yvterm_grid_slot_rich_block_count(grid_obj, slot);
        YTEST_REQUIRE_OK(test, block_count_res);
        for (uint32_t block_index = 0; block_index < block_count_res.value; ++block_index) {
            uint32_t record_count = 0;
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block(grid_obj, slot, block_index,
                                                                     NULL, &record_count));
            for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
                uint32_t word_count = 0;
                struct yetty_ycore_const_uint32_ptr_result words_res =
                    yetty_yvterm_grid_slot_rich_block_record(grid_obj, slot, block_index,
                                                             record_index, &word_count, NULL);
                YTEST_REQUIRE_OK(test, words_res);
                if (!words_res.value || word_count < 3u) {
                    continue;
                }
                const uint8_t *record_bytes = (const uint8_t *)words_res.value;
                size_t record_size = (size_t)word_count * sizeof(uint32_t);
                int is_label = 0;
                for (size_t offset = 0; offset + 6u <= record_size; ++offset) {
                    if (memcmp(record_bytes + offset, "CHROME", 6u) == 0) {
                        is_label = 1;
                        if (offset + strlen(expected) <= record_size &&
                            memcmp(record_bytes + offset, expected, strlen(expected)) == 0) {
                            *out_found += 1;
                            if (out_paint_z) {
                                YTEST_REQUIRE_OK(test,
                                                 yetty_yvterm_grid_slot_rich_block_record_paint_key(
                                                     grid_obj, slot, block_index, record_index,
                                                     out_paint_z, NULL, NULL));
                            }
                        } else if (out_other_generations) {
                            *out_other_generations += 1;
                        }
                        break;
                    }
                }
                (void)is_label;
            }
        }
    }
}

/*===========================================================================
 * The round trip.
 *=========================================================================*/
static void test_geometry_roundtrip(struct ytest *test)
{
    enum {
        WIDGET_GROUP_ID = 10,
        CHROME_GROUP_ID = 5,
        COMPLEX_NODE_ID = 7,
        FIGURE_PAINT_Z = 500,
    };
    struct harness_state state = {0};
    struct yetty_yterminal_terminal_result terminal_res =
        yetty_yterminal_ingest_harness_open(80, 24);
    YTEST_REQUIRE_OK(test, terminal_res);
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    struct harness_factory *factory = harness_factory_create(&state);
    YTEST_REQUIRE_NOT_NULL(test, factory);
    YTEST_REQUIRE_OK(test, yetty_ydraw_complex_factory_register(
                               yetty_yterminal_ingest_harness_factory(terminal), &factory->base));
    struct yetty_yclass_object *vterm_obj = yetty_yterminal_ingest_harness_grid(terminal);
    YTEST_REQUIRE_NOT_NULL(test, vterm_obj);
    struct yetty_yclass_object_ptr_result grid_obj_res = yetty_yvterm_vterm_grid_object(vterm_obj);
    YTEST_REQUIRE_OK(test, grid_obj_res);
    struct yetty_yclass_object *grid_obj = grid_obj_res.value;

    uint64_t widget_key = yetty_yvterm_group_key_fold(YETTY_YVTERM_GROUP_KEY_ROOT, WIDGET_GROUP_ID);
    uint64_t complex_key = yetty_yvterm_group_key_fold(widget_key, COMPLEX_NODE_ID);

    /* CREATION envelope, the ygui2 shape, inside an ambient paint band:
     * PAINT_Z(500){ GROUP(widget){ NODE_ID + record + GROUP(chrome){label} } }. */
    state.chrome_generation = 1;
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_paint_z(list, FIGURE_PAINT_Z));
        struct yetty_ydraw_id_result widget_res =
            yetty_ydraw_drawable_list_begin_group(list, WIDGET_GROUP_ID);
        YTEST_REQUIRE_OK(test, widget_res);
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_node_id(list, COMPLEX_NODE_ID));
        uint32_t record[HARNESS_RECORD_WORDS];
        harness_record_write(record, 0.0f, 0.0f, 100.0f, 50.0f, CHROME_GROUP_ID, 0xAB12CD34u);
        YTEST_REQUIRE_OK(
            test, YETTY_IS_OK(yetty_ydraw_drawable_list_add_prim(list, record, sizeof(record)))
                      ? YETTY_OK_VOID()
                      : YETTY_ERR(yetty_ycore_void, "creation record add"));
        struct yetty_ydraw_id_result chrome_res =
            yetty_ydraw_drawable_list_begin_group(list, CHROME_GROUP_ID);
        YTEST_REQUIRE_OK(test, chrome_res);
        struct harness_state initial_chrome = state;
        (void)initial_chrome;
        {
            char label[] = "CHROME001";
            struct yetty_ycore_buffer view = {
                .data = (uint8_t *)label,
                .capacity = sizeof(label) - 1u,
                .size = sizeof(label) - 1u,
            };
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_text(
                                       list, 4.0f, 12.0f, &view, 10.0f, 0xFFFFFFFFu, 0, -1, 0.0f));
        }
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, chrome_res.value));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, widget_res.value));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_paint_z_end(list));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    YTEST_CHECK_EQ_INT(test, state.created, 1);
    struct yetty_ydraw_complex *bound = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_vterm_rich_update_target(vterm_obj, complex_key, &bound));
    YTEST_CHECK(test, bound == state.last_instance);
    YTEST_CHECK_EQ_INT(test, (int)bound->chrome_group_id, CHROME_GROUP_ID);
    float extent_top = 0.0f;
    float extent_bottom = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(grid_obj, complex_key, &extent_top,
                                                                &extent_bottom));
    YTEST_CHECK(test, extent_top == 0.0f && extent_bottom == 50.0f);

    /* GEOMETRY envelope — the SCOPE-LESS addressed update a live range
     * change ships: CMD_PATH([widget]) + UPDATE(node, GEOMETRY, w, h).
     * No record, no samples, no chrome bytes. */
    state.chrome_generation = 2;
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        uint32_t path_ids[1] = {WIDGET_GROUP_ID};
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, 1u));
        float new_w = 200.0f;
        float new_h = 120.0f;
        uint32_t payload[4] = {0u, 0xFFFFFFFEu, 0u, 0u};
        memcpy(&payload[2], &new_w, sizeof(float));
        memcpy(&payload[3], &new_h, sizeof(float));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_update(list, COMPLEX_NODE_ID,
                                                                        payload, sizeof(payload)));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    /* RUNTIME IDENTITY: the op reached the live instance — no re-create,
     * no destroy. */
    YTEST_CHECK_EQ_INT(test, state.created, 1);
    YTEST_CHECK_EQ_INT(test, state.destroyed, 0);
    YTEST_CHECK_EQ_INT(test, (int)state.update_calls, 1);
    struct yetty_ydraw_complex *after = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_vterm_rich_update_target(vterm_obj, complex_key, &after));
    YTEST_CHECK(test, after == bound);
    /* Retained extent follows the runtime's new AABB. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(grid_obj, complex_key, &extent_top,
                                                                &extent_bottom));
    YTEST_CHECK(test, extent_top == 0.0f && extent_bottom == 120.0f);
    /* LOCAL chrome replacement, at the figure's ORIGINAL z. */
    YTEST_CHECK_EQ_INT(test, (int)state.emit_chrome_calls, 1);
    YTEST_CHECK_EQ_INT(test, (int)after->chrome_dirty, 0);
    int found = 0;
    int stale = 0;
    int32_t chrome_z = 0;
    scan_chrome_labels(test, grid_obj, 2u, &found, &stale, &chrome_z);
    YTEST_CHECK_EQ_INT(test, found, 1);
    YTEST_CHECK_EQ_INT(test, stale, 0); /* generation-1 label replaced */
    YTEST_CHECK_EQ_INT(test, chrome_z, FIGURE_PAINT_Z);

    /* FAILURE: injected chrome-emission error — the update itself lands,
     * the OLD chrome stays standing, chrome_dirty stays retryable. */
    state.fail_emit_chrome = 1;
    state.chrome_generation = 3;
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        uint32_t path_ids[1] = {WIDGET_GROUP_ID};
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, 1u));
        float new_w = 300.0f;
        float new_h = 40.0f;
        uint32_t payload[4] = {0u, 0xFFFFFFFEu, 0u, 0u};
        memcpy(&payload[2], &new_w, sizeof(float));
        memcpy(&payload[3], &new_h, sizeof(float));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_update(list, COMPLEX_NODE_ID,
                                                                        payload, sizeof(payload)));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    YTEST_CHECK_EQ_INT(test, (int)state.update_calls, 2);
    YTEST_CHECK_EQ_INT(test, (int)after->chrome_dirty, 1); /* retry state kept */
    scan_chrome_labels(test, grid_obj, 2u, &found, &stale, NULL);
    YTEST_CHECK_EQ_INT(test, found, 1); /* generation-2 chrome still standing */
    YTEST_CHECK_EQ_INT(test, stale, 0);
    /* Extent refresh still happened for the shrink (independent of the
     * chrome failure). */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(grid_obj, complex_key, &extent_top,
                                                                &extent_bottom));
    YTEST_CHECK(test, extent_bottom == 40.0f);

    /* RETRY converges WITHOUT the figure receiving anything: the
     * envelope-close sweep replaces the chrome on the next UNRELATED
     * envelope (a lone prim append here) — a quiet dashboard panel is
     * never stuck with stale chrome. No new update call reaches the
     * runtime; the sweep alone converges it. */
    state.fail_emit_chrome = 0;
    state.chrome_generation = 4;
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        char label[] = "UNRELATED";
        struct yetty_ycore_buffer view = {
            .data = (uint8_t *)label,
            .capacity = sizeof(label) - 1u,
            .size = sizeof(label) - 1u,
        };
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_text(list, 2.0f, 8.0f, &view, 10.0f,
                                                                  0xFFFFFFFFu, 0, -1, 0.0f));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    YTEST_CHECK_EQ_INT(test, (int)state.update_calls, 2); /* no update needed */
    YTEST_CHECK_EQ_INT(test, (int)after->chrome_dirty, 0);
    scan_chrome_labels(test, grid_obj, 4u, &found, &stale, &chrome_z);
    YTEST_CHECK_EQ_INT(test, found, 1);
    YTEST_CHECK_EQ_INT(test, stale, 0);
    YTEST_CHECK_EQ_INT(test, chrome_z, FIGURE_PAINT_Z);
    YTEST_CHECK_EQ_INT(test, state.created, 1); /* never re-created, ever */
    YTEST_CHECK_EQ_INT(test, state.destroyed, 0);

    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_close(terminal));
    YTEST_CHECK_EQ_INT(test, state.destroyed, 1); /* teardown reaps the runtime */
}

/* HiDPI unit rule end-to-end: the producer authors every spatial value in
 * LOGICAL pixels; the receiver multiplies by its committed content scale
 * before its framebuffer-pixel row model. Pins, at scale 2 and 3:
 *
 *   - a declared RESERVE(90) reserves ceil(90 x scale / cell_h) rows —
 *     unscaled consumption reserved 1/scale of the needed rows;
 *   - the retained extent read-back stays PRODUCER-LOGICAL (unscaled),
 *     before and after a geometry op — the store never bakes the density
 *     into producer values. */
static void test_hidpi_scale_units(struct ytest *test)
{
    enum { SCALED_WIDGET_ID = 21, SCALED_NODE_ID = 3 };
    const float scales[] = {1.0f, 2.0f, 3.0f};
    for (size_t scale_index = 0; scale_index < sizeof(scales) / sizeof(scales[0]); ++scale_index) {
        float scale = scales[scale_index];
        struct harness_state state = {0};
        struct yetty_yterminal_terminal_result terminal_res =
            yetty_yterminal_ingest_harness_open(80, 24);
        YTEST_REQUIRE_OK(test, terminal_res);
        struct yetty_yterminal_terminal *terminal = terminal_res.value;
        YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_set_scale(terminal, scale));
        struct harness_factory *factory = harness_factory_create(&state);
        YTEST_REQUIRE_NOT_NULL(test, factory);
        YTEST_REQUIRE_OK(test,
                         yetty_ydraw_complex_factory_register(
                             yetty_yterminal_ingest_harness_factory(terminal), &factory->base));
        struct yetty_yclass_object *vterm_obj = yetty_yterminal_ingest_harness_grid(terminal);
        YTEST_REQUIRE_NOT_NULL(test, vterm_obj);
        struct yetty_yclass_object_ptr_result grid_obj_res =
            yetty_yvterm_vterm_grid_object(vterm_obj);
        YTEST_REQUIRE_OK(test, grid_obj_res);
        uint64_t widget_key =
            yetty_yvterm_group_key_fold(YETTY_YVTERM_GROUP_KEY_ROOT, SCALED_WIDGET_ID);
        uint64_t complex_key = yetty_yvterm_group_key_fold(widget_key, SCALED_NODE_ID);

        /* Creation with an explicit logical RESERVE(90). The harness cell
         * is 18 framebuffer px: scale 1 -> 5 rows, 2 -> 10, 3 -> 15. */
        state.chrome_generation = 1;
        {
            struct yetty_ydraw_drawable_list_result list_res =
                yetty_ydraw_drawable_list_config_buffer_create(NULL);
            YTEST_REQUIRE_OK(test, list_res);
            struct yetty_ydraw_drawable_list *list = list_res.value;
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_reserve(list, 90u));
            struct yetty_ydraw_id_result widget_res =
                yetty_ydraw_drawable_list_begin_group(list, SCALED_WIDGET_ID);
            YTEST_REQUIRE_OK(test, widget_res);
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_node_id(list, SCALED_NODE_ID));
            uint32_t record[HARNESS_RECORD_WORDS];
            harness_record_write(record, 0.0f, 0.0f, 100.0f, 50.0f, 0u, 0x5CA1E000u);
            YTEST_REQUIRE_OK(
                test, YETTY_IS_OK(yetty_ydraw_drawable_list_add_prim(list, record, sizeof(record)))
                          ? YETTY_OK_VOID()
                          : YETTY_ERR(yetty_ycore_void, "scaled creation record add"));
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, widget_res.value));
            ingest_list(test, terminal, list);
            (void)yetty_ydraw_drawable_list_destroy(list);
        }
        YTEST_CHECK_EQ_INT(test, state.created, 1);
        uint32_t live_span = 0;
        struct yetty_ycore_uint32_result span_res =
            yetty_yvterm_vterm_rich_group_query(vterm_obj, widget_key, &live_span);
        YTEST_REQUIRE_OK(test, span_res);
        YTEST_CHECK(test, span_res.value != 0);
        uint32_t expected_rows = (uint32_t)ceilf(90.0f * scale / 18.0f);
        YTEST_CHECK_EQ_INT(test, (int)live_span, (int)expected_rows);

        /* Extent read-back stays producer-logical at every scale. */
        float extent_top = 0.0f;
        float extent_bottom = 0.0f;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(grid_obj_res.value, complex_key,
                                                                    &extent_top, &extent_bottom));
        YTEST_CHECK(test, extent_top == 0.0f && extent_bottom == 50.0f);

        /* Geometry op: the refreshed extent is the runtime's LOGICAL
         * height, unscaled — the scale lives only at consumption. */
        {
            struct yetty_ydraw_drawable_list_result list_res =
                yetty_ydraw_drawable_list_config_buffer_create(NULL);
            YTEST_REQUIRE_OK(test, list_res);
            struct yetty_ydraw_drawable_list *list = list_res.value;
            uint32_t path_ids[1] = {SCALED_WIDGET_ID};
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, 1u));
            float new_w = 120.0f;
            float new_h = 60.0f;
            uint32_t payload[4] = {0u, 0xFFFFFFFEu, 0u, 0u};
            memcpy(&payload[2], &new_w, sizeof(float));
            memcpy(&payload[3], &new_h, sizeof(float));
            YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_update(
                                       list, SCALED_NODE_ID, payload, sizeof(payload)));
            ingest_list(test, terminal, list);
            (void)yetty_ydraw_drawable_list_destroy(list);
        }
        YTEST_CHECK_EQ_INT(test, (int)state.update_calls, 1);
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(grid_obj_res.value, complex_key,
                                                                    &extent_top, &extent_bottom));
        YTEST_CHECK(test, extent_bottom == 60.0f);

        YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_close(terminal));
    }
}

/* A LIVE density transition with a block already retained: the old
 * insertion's immutable span, its bound runtime and its logical extents
 * all survive unchanged, while a NEW insertion after the transition
 * reserves with the new committed density. (The harness world keeps its
 * cell metrics fixed, so the density product moves alone — reservation
 * math must follow it, retained state must not be disturbed.) */
static void test_density_transition_retained_reservation(struct ytest *test)
{
    enum { OLD_WIDGET_ID = 31, OLD_NODE_ID = 3, NEW_WIDGET_ID = 32, NEW_NODE_ID = 4 };
    struct harness_state state = {0};
    struct yetty_yterminal_terminal_result terminal_res =
        yetty_yterminal_ingest_harness_open(80, 24);
    YTEST_REQUIRE_OK(test, terminal_res);
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    struct harness_factory *factory = harness_factory_create(&state);
    YTEST_REQUIRE_NOT_NULL(test, factory);
    YTEST_REQUIRE_OK(test,
                     yetty_ydraw_complex_factory_register(
                         yetty_yterminal_ingest_harness_factory(terminal), &factory->base));
    struct yetty_yclass_object *vterm_obj = yetty_yterminal_ingest_harness_grid(terminal);
    YTEST_REQUIRE_NOT_NULL(test, vterm_obj);
    struct yetty_yclass_object_ptr_result grid_obj_res = yetty_yvterm_vterm_grid_object(vterm_obj);
    YTEST_REQUIRE_OK(test, grid_obj_res);
    uint64_t old_widget_key =
        yetty_yvterm_group_key_fold(YETTY_YVTERM_GROUP_KEY_ROOT, OLD_WIDGET_ID);
    uint64_t old_complex_key = yetty_yvterm_group_key_fold(old_widget_key, OLD_NODE_ID);

    /* Insert at density 1: RESERVE(90) on 18px cells = 5 rows. */
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_reserve(list, 90u));
        struct yetty_ydraw_id_result widget_res =
            yetty_ydraw_drawable_list_begin_group(list, OLD_WIDGET_ID);
        YTEST_REQUIRE_OK(test, widget_res);
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_node_id(list, OLD_NODE_ID));
        uint32_t record[HARNESS_RECORD_WORDS];
        harness_record_write(record, 0.0f, 0.0f, 100.0f, 50.0f, 0u, 0xD1D1D1D1u);
        YTEST_REQUIRE_OK(
            test, YETTY_IS_OK(yetty_ydraw_drawable_list_add_prim(list, record, sizeof(record)))
                      ? YETTY_OK_VOID()
                      : YETTY_ERR(yetty_ycore_void, "old creation record add"));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, widget_res.value));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    uint32_t old_span = 0;
    struct yetty_ycore_uint32_result old_span_res =
        yetty_yvterm_vterm_rich_group_query(vterm_obj, old_widget_key, &old_span);
    YTEST_REQUIRE_OK(test, old_span_res);
    YTEST_CHECK(test, old_span_res.value != 0);
    YTEST_CHECK_EQ_INT(test, (int)old_span, 5);

    /* THE TRANSITION: committed density 1 -> 2. */
    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_set_scale(terminal, 2.0f));

    /* Retained state undisturbed: span immutable, runtime bound, extents
     * still producer-logical. */
    uint32_t retained_span = 0;
    struct yetty_ycore_uint32_result retained_res =
        yetty_yvterm_vterm_rich_group_query(vterm_obj, old_widget_key, &retained_span);
    YTEST_REQUIRE_OK(test, retained_res);
    YTEST_CHECK(test, retained_res.value != 0);
    YTEST_CHECK_EQ_INT(test, (int)retained_span, 5);
    struct yetty_ydraw_complex *bound = NULL;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_vterm_rich_update_target(vterm_obj, old_complex_key, &bound));
    YTEST_CHECK(test, bound == state.last_instance);
    float extent_top = 0.0f;
    float extent_bottom = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_extent(
                               grid_obj_res.value, old_complex_key, &extent_top, &extent_bottom));
    YTEST_CHECK(test, extent_top == 0.0f && extent_bottom == 50.0f);

    /* A NEW insertion reserves with the NEW density: RESERVE(90) x2 on
     * 18px cells = 10 rows. */
    {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YTEST_REQUIRE_OK(test, list_res);
        struct yetty_ydraw_drawable_list *list = list_res.value;
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_reserve(list, 90u));
        struct yetty_ydraw_id_result widget_res =
            yetty_ydraw_drawable_list_begin_group(list, NEW_WIDGET_ID);
        YTEST_REQUIRE_OK(test, widget_res);
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_node_id(list, NEW_NODE_ID));
        uint32_t record[HARNESS_RECORD_WORDS];
        harness_record_write(record, 0.0f, 0.0f, 100.0f, 50.0f, 0u, 0xD2D2D2D2u);
        YTEST_REQUIRE_OK(
            test, YETTY_IS_OK(yetty_ydraw_drawable_list_add_prim(list, record, sizeof(record)))
                      ? YETTY_OK_VOID()
                      : YETTY_ERR(yetty_ycore_void, "new creation record add"));
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, widget_res.value));
        ingest_list(test, terminal, list);
        (void)yetty_ydraw_drawable_list_destroy(list);
    }
    uint64_t new_widget_key =
        yetty_yvterm_group_key_fold(YETTY_YVTERM_GROUP_KEY_ROOT, NEW_WIDGET_ID);
    uint32_t new_span = 0;
    struct yetty_ycore_uint32_result new_span_res =
        yetty_yvterm_vterm_rich_group_query(vterm_obj, new_widget_key, &new_span);
    YTEST_REQUIRE_OK(test, new_span_res);
    YTEST_CHECK(test, new_span_res.value != 0);
    YTEST_CHECK_EQ_INT(test, (int)new_span, 10);

    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_close(terminal));
}

/* A borrowed capture PTY: host→client bytes (the OSC resize envelope)
 * land in `bytes`; resize ops are counted. */
struct capture_pty {
    struct yetty_platform_pty base;
    uint8_t bytes[65536];
    size_t size;
    uint32_t resize_calls;
};

static struct yetty_ycore_size_result capture_pty_write(struct yetty_platform_pty *self,
                                                        const char *data, size_t len)
{
    struct capture_pty *pty = (struct capture_pty *)self;
    size_t room = sizeof(pty->bytes) - pty->size;
    size_t take = len < room ? len : room;
    memcpy(pty->bytes + pty->size, data, take);
    pty->size += take;
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result capture_pty_resize(struct yetty_platform_pty *self,
                                                         uint32_t cols, uint32_t rows,
                                                         uint32_t pixel_w, uint32_t pixel_h)
{
    struct capture_pty *pty = (struct capture_pty *)self;
    (void)cols;
    (void)rows;
    (void)pixel_w;
    (void)pixel_h;
    pty->resize_calls++;
    return YETTY_OK_VOID();
}

static void capture_pty_init(struct capture_pty *pty)
{
    static const struct yetty_platform_pty_ops ops = {
        .write = capture_pty_write,
        .resize = capture_pty_resize,
    };
    memset(pty, 0, sizeof(*pty));
    pty->base.ops = &ops;
}

static void resize_sink_noop(const uint8_t *bytes, size_t byte_count, void *userdata)
{
    (void)bytes;
    (void)byte_count;
    (void)userdata;
}

/* The zoom→envelope→client CONTRACT, end to end on the production path:
 * a pane-subscribed client, a real YETTY_YCORE_ZOOM_CELL_SIZE view event,
 * the OSC envelope captured off the (harness) PTY, and the EXACT bytes
 * fed to a ygui2 framework. Pins the whole chain the reviews demanded:
 *
 *   - the zoom handler EMITS the pane resize (it used to update the grid
 *     and render state and tell the client nothing);
 *   - the published scale is yvterm's COMMITTED SNAPPED product, not the
 *     nominal density x zoom (base cell 18 at nominal 1.1 commits a 20px
 *     cell -> 20/18 = 1.111..., which differs from 1.1);
 *   - ygui2 commits that scale and the shrunken logical viewport in the
 *     SAME transition (the committed scale IS its input divisor). */
static void test_zoom_resize_contract(struct ytest *test)
{
    struct yetty_yterminal_terminal_result terminal_res =
        yetty_yterminal_ingest_harness_open(80, 24);
    YTEST_REQUIRE_OK(test, terminal_res);
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    struct capture_pty pty;
    capture_pty_init(&pty);
    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_set_pty(terminal, &pty.base));
    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_subscribe_pane(terminal));
    struct yetty_yclass_object *vterm_obj = yetty_yterminal_ingest_harness_grid(terminal);
    YTEST_REQUIRE_NOT_NULL(test, vterm_obj);

    struct yetty_yui_view *view = yetty_yterminal_terminal_as_view(terminal);
    YTEST_REQUIRE_NOT_NULL(test, view);
    view->bounds = (struct yetty_yui_rect){.x = 0.0f, .y = 0.0f, .w = 720.0f, .h = 432.0f};

    /* Establish the unzoomed baseline the way the layout-driven first
     * resize does in production (cell 9x18 — the vterm default): the
     * structural-zoom ratio is measured against this. */
    YTEST_REQUIRE_OK(test, yetty_yterminal_terminal_resize_grid(
                               terminal, (struct yetty_ycore_grid_size){.cols = 80, .rows = 24},
                               (struct yetty_ycore_pixel_size){.width = 9.0f, .height = 18.0f}));

    /* One structural zoom tick, delta 0.1: nominal zoom 1.1; the default
     * harness cell is 9x18, so the SNAPPED committed cell is
     * round(18 x 1.1) = 20 and the committed product 20/18 = 1.111... */
    uint32_t resize_calls_before = pty.resize_calls;
    struct yetty_yui_event zoom_event = {.type = YETTY_YCORE_ZOOM_CELL_SIZE};
    zoom_event.zoom_cell_size.delta = 0.1f;
    struct yetty_ycore_int_result handled_res = view->ops->on_event(view, &zoom_event);
    YTEST_REQUIRE_OK(test, handled_res);
    YTEST_CHECK_EQ_INT(test, handled_res.value, 1);

    float committed = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_yvterm_vterm_rich_density(vterm_obj, &committed));
    YTEST_CHECK_NEAR(test, committed, 20.0f / 18.0f, 1e-4);
    YTEST_CHECK(test, fabsf(committed - 1.1f) > 5e-3); /* snapped != nominal */

    /* The envelope was EMITTED (the missing call was the bug) and the
     * zoom performed EXACTLY ONE PTY resize — terminal_resize_grid owns
     * that operation; the handler's old second explicit call meant
     * duplicate SIGWINCH/NAWS traffic AND a failure point between the
     * committed rich scale and the client envelope. */
    YTEST_CHECK(test, pty.size > 0);
    YTEST_CHECK_EQ_INT(test, (int)(pty.resize_calls - resize_calls_before), 1);

    /* Feed the EXACT captured bytes to a ygui2 framework: it must commit
     * the published scale (its input divisor) AND the shrunken logical
     * viewport in one transition. */
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, resize_sink_noop, NULL));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, pty.bytes, pty.size));
    float client_scale = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_content_scale(framework, &client_scale));
    YTEST_CHECK_NEAR(test, client_scale, committed, 1e-4);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    float root_w = 0.0f;
    float root_h = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(root_res.value, NULL, NULL, &root_w, &root_h));
    YTEST_CHECK_NEAR(test, root_w, 720.0f / committed, 1e-2);
    YTEST_CHECK_NEAR(test, root_h, 432.0f / committed, 1e-2);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_set_pty(terminal, NULL));
    YTEST_REQUIRE_OK(test, yetty_yterminal_ingest_harness_close(terminal));
}

int main(void)
{
    /* The host→client emit path wraps envelopes in tmux passthrough when
     * TERM_PROGRAM says the PROCESS sits inside tmux — true for a dev
     * shell running ctest under tmux, never for the production yetty
     * host. Pin the production environment so the captured envelope is
     * the bare OSC framing a client actually receives. */
    unsetenv("TERM_PROGRAM");
    struct ytest test = ytest_begin("yterminal_ingest_roundtrip");
    YTEST_RUN(&test, test_geometry_roundtrip);
    YTEST_RUN(&test, test_hidpi_scale_units);
    YTEST_RUN(&test, test_density_transition_retained_reservation);
    YTEST_RUN(&test, test_zoom_resize_contract);
    return ytest_end(&test);
}
