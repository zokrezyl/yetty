/*
 * ydraw-list command wire + golden contract test.
 *
 * Builds command streams with the ydraw-list builders, walks them back with
 * the real parser (yetty_ydraw_drawable_command_parse), and pins:
 *   - builder/parser roundtrips (CMD_ZERO, SDF box, CMD_DELETE)
 *   - group open/close with end_group payload_size back-patching
 *   - clear() resetting the buffer
 *   - a stable textual dump for golden regression review
 *   - graceful rejection of truncated / invalid-length records (no over-read)
 *
 * Deterministic and headless. The default drawable-list registry supplies the
 * cmd + SDF strides the parser needs.
 */

#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-iterator.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/drawable-list-registry.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ysdf/default-registry.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A registry with the default handlers (cmd + SDF + text) so the parser can
 * stride every record kind this test emits. */
static struct yetty_ydraw_drawable_list_registry *make_registry(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry_ptr_result reg_res =
        yetty_ydraw_drawable_list_registry_create_default();
    YTEST_REQUIRE_OK(test, reg_res);
    return reg_res.value;
}

static struct yetty_ysdf_box a_box(void)
{
    struct yetty_ysdf_box box = {.center_x = 10.0f,
                                 .center_y = 20.0f,
                                 .half_width = 5.0f,
                                 .half_height = 6.0f,
                                 .corner_radius = 0.0f};
    return box;
}

/* Append one box prim to the buffer. */
static void add_box(struct ytest *test, struct yetty_ydraw_drawable_list *buf)
{
    struct yetty_ysdf_box box = a_box();
    struct yetty_ycore_void_result res = yetty_ydraw_drawable_list_add_cmd_add_box(
        buf, /*id=*/0, /*z_order=*/0, /*fill=*/0xFF112233u, /*stroke=*/0, /*stroke_w=*/0.0f, &box);
    YTEST_REQUIRE_OK(test, res);
}

/*---------------------------------------------------------------------------
 * Roundtrip: CMD_ZERO, box, CMD_DELETE.
 *-------------------------------------------------------------------------*/
static void test_roundtrip(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_zero(buf));
    add_box(test, buf);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_delete(buf, 7u));

    const uint8_t *bytes = yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);

    /* Walk every record; assert the sequence and that the strides sum to the
     * exact buffer length (no bytes left over, none over-read). */
    size_t off = 0;
    int index = 0;
    while (off < len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result parsed =
            yetty_ydraw_drawable_command_parse(reg, bytes + off, (uint32_t)(len - off), &cmd);
        YTEST_REQUIRE_OK(test, parsed);
        YTEST_REQUIRE(test, parsed.value > 0);
        if (index == 0) {
            YTEST_CHECK_EQ_INT(test, cmd.kind, YETTY_YDRAW_COMMAND_ADD);
            YTEST_CHECK_EQ_SIZE(test, cmd.entry.data[0], YETTY_YDRAW_CMD_ZERO);
        } else if (index == 1) {
            YTEST_CHECK_EQ_INT(test, cmd.kind, YETTY_YDRAW_COMMAND_ADD);
            YTEST_CHECK_EQ_SIZE(test, cmd.entry.data[0], YETTY_YSDF_BOX);
        } else if (index == 2) {
            YTEST_CHECK_EQ_INT(test, cmd.kind, YETTY_YDRAW_COMMAND_DELETE);
            YTEST_CHECK_EQ_SIZE(test, cmd.id, 7u);
        }
        off += parsed.value;
        index++;
    }
    YTEST_CHECK_EQ_INT(test, index, 3);
    YTEST_CHECK_EQ_SIZE(test, off, len);

    yetty_ydraw_drawable_list_destroy(buf);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Group: begin/end back-patches payload_size; the body parses to the box.
 *-------------------------------------------------------------------------*/
static void test_group_backpatch(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    struct yetty_ydraw_id_result marker = yetty_ydraw_drawable_list_begin_group(buf, 10u);
    YTEST_REQUIRE_OK(test, marker);
    add_box(test, buf);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(buf, marker.value));

    const uint8_t *bytes = yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);

    /* Top-level: one record — the whole group (kind ADD, type CMD_GROUP). */
    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, bytes, (uint32_t)len, &cmd);
    YTEST_REQUIRE_OK(test, parsed);
    YTEST_CHECK_EQ_INT(test, cmd.kind, YETTY_YDRAW_COMMAND_ADD);
    YTEST_CHECK_EQ_SIZE(test, cmd.entry.data[0], YETTY_YDRAW_CMD_GROUP);
    YTEST_CHECK_EQ_SIZE(test, cmd.entry.data[1], 10u); /* group id */
    uint32_t payload_size = cmd.entry.data[2];
    YTEST_CHECK(test, payload_size > 0); /* end_group patched it */
    YTEST_CHECK_EQ_SIZE(test, parsed.value, 12u + payload_size);
    YTEST_CHECK_EQ_SIZE(test, parsed.value, len); /* group is the whole buffer */

    /* The group body (after the 12-byte header) parses to the box. */
    struct yetty_ydraw_command inner;
    struct yetty_ycore_size_result inner_parsed =
        yetty_ydraw_drawable_command_parse(reg, bytes + 12, payload_size, &inner);
    YTEST_REQUIRE_OK(test, inner_parsed);
    YTEST_CHECK_EQ_INT(test, inner.kind, YETTY_YDRAW_COMMAND_ADD);
    YTEST_CHECK_EQ_SIZE(test, inner.entry.data[0], YETTY_YSDF_BOX);
    YTEST_CHECK_EQ_SIZE(test, inner_parsed.value, payload_size);

    yetty_ydraw_drawable_list_destroy(buf);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * clear() resets the buffer to empty.
 *-------------------------------------------------------------------------*/
static void test_clear_resets(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    add_box(test, buf);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(buf) > 0);
    yetty_ydraw_drawable_list_clear(buf);
    YTEST_CHECK_EQ_SIZE(test, yetty_ydraw_drawable_list_size(buf), 0);

    /* Reusable after clear. */
    add_box(test, buf);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(buf) > 0);

    yetty_ydraw_drawable_list_destroy(buf);
}

/*---------------------------------------------------------------------------
 * Golden: a stable textual dump of the command structure (types + ids +
 * kinds — no field sizes, so it survives prim-layout tweaks but catches any
 * command-sequence or type-word regression).
 *-------------------------------------------------------------------------*/
static size_t dump_commands(struct yetty_ydraw_drawable_list_registry *reg, const uint8_t *bytes,
                            size_t len, char *out, size_t out_cap)
{
    size_t off = 0, written = 0;
    while (off < len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result parsed =
            yetty_ydraw_drawable_command_parse(reg, bytes + off, (uint32_t)(len - off), &cmd);
        if (YETTY_IS_ERR(parsed) || parsed.value == 0) {
            written += (size_t)snprintf(out + written, out_cap - written, "PARSE-ERROR\n");
            break;
        }
        if (cmd.kind == YETTY_YDRAW_COMMAND_DELETE) {
            written += (size_t)snprintf(out + written, out_cap - written, "DELETE id=%u\n", cmd.id);
        } else {
            uint32_t type = cmd.entry.data[0];
            if (type == YETTY_YDRAW_CMD_GROUP) {
                written += (size_t)snprintf(out + written, out_cap - written,
                                            "CMD type=0x%08x id=%u\n", type, cmd.entry.data[1]);
            } else {
                written +=
                    (size_t)snprintf(out + written, out_cap - written, "ADD type=0x%08x\n", type);
            }
        }
        off += parsed.value;
    }
    return written;
}

static void test_golden_dump(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_zero(buf));
    add_box(test, buf);
    struct yetty_ydraw_id_result marker = yetty_ydraw_drawable_list_begin_group(buf, 10u);
    YTEST_REQUIRE_OK(test, marker);
    add_box(test, buf);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(buf, marker.value));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_delete(buf, 7u));

    char dump[512];
    dump_commands(reg, yetty_ydraw_drawable_list_data(buf), yetty_ydraw_drawable_list_size(buf),
                  dump, sizeof(dump));

    const char *golden = "ADD type=0x00000000\n"
                         "ADD type=0x7ffffffe\n"
                         "CMD type=0x80000002 id=10\n"
                         "DELETE id=7\n";
    YTEST_CHECK_STR_EQ(test, dump, golden);

    yetty_ydraw_drawable_list_destroy(buf);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Truncated / invalid records: the parser rejects, never over-reads.
 *-------------------------------------------------------------------------*/
static void test_truncated_type_only(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    /* Just a box type word, exact-sized alloc so an over-read trips ASAN. */
    uint32_t *bytes = malloc(sizeof(uint32_t));
    YTEST_REQUIRE_NOT_NULL(test, bytes);
    bytes[0] = YETTY_YSDF_BOX;
    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)bytes, 4u, &cmd);
    YTEST_CHECK_ERR(test, parsed);
    free(bytes);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

static void test_truncated_delete(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    /* DELETE needs 12 bytes (type|id|payload_size); give only 8. */
    uint32_t *bytes = malloc(2 * sizeof(uint32_t));
    YTEST_REQUIRE_NOT_NULL(test, bytes);
    bytes[0] = YETTY_YDRAW_CMD_DELETE;
    bytes[1] = 7u;
    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)bytes, 8u, &cmd);
    YTEST_CHECK_ERR(test, parsed);
    free(bytes);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

static void test_invalid_group_size(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    /* A GROUP header claiming a huge payload the buffer can't back. */
    uint32_t *bytes = malloc(3 * sizeof(uint32_t));
    YTEST_REQUIRE_NOT_NULL(test, bytes);
    bytes[0] = YETTY_YDRAW_CMD_GROUP;
    bytes[1] = 1u;
    bytes[2] = 0x00FFFFFFu; /* payload_size far beyond the 12 bytes supplied */
    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)bytes, 12u, &cmd);
    YTEST_CHECK_ERR(test, parsed);
    free(bytes);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * paint_z metadata accessor: SDF with and without the id word (signed
 * reinterpretation of the wire z), text `layer`, complex default zero,
 * and NULL for record families that never render (FONT, cmds).
 *-------------------------------------------------------------------------*/
static void test_paint_z_accessor(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    /* Two boxes: anonymous at z = -4 (wire word is the two's-complement
     * bit pattern), id'd at z = 9 (id word shifts the z word by one). */
    struct yetty_ysdf_box box = a_box();
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_box(buf, /*id=*/0,
                                                                     /*z_order=*/(uint32_t)-4,
                                                                     0xFF112233u, 0, 0.0f, &box));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_box(buf, /*id=*/5, /*z_order=*/9u,
                                                                     0xFF112233u, 0, 0.0f, &box));

    const uint8_t *bytes = yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);

    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, bytes, (uint32_t)len, &cmd);
    YTEST_REQUIRE_OK(test, parsed);
    YTEST_REQUIRE_NOT_NULL(test, cmd.entry.ops);
    YTEST_REQUIRE_NOT_NULL(test, cmd.entry.ops->paint_z);
    YTEST_CHECK_EQ_INT(test, cmd.entry.ops->paint_z(cmd.entry.data), -4);

    struct yetty_ydraw_command id_cmd;
    struct yetty_ycore_size_result id_parsed = yetty_ydraw_drawable_command_parse(
        reg, bytes + parsed.value, (uint32_t)(len - parsed.value), &id_cmd);
    YTEST_REQUIRE_OK(test, id_parsed);
    YTEST_REQUIRE_NOT_NULL(test, id_cmd.entry.ops);
    YTEST_REQUIRE_NOT_NULL(test, id_cmd.entry.ops->paint_z);
    YTEST_CHECK_EQ_INT(test, id_cmd.entry.ops->paint_z(id_cmd.entry.data), 9);

    /* Text run: `layer` is the z source, same signed reinterpretation.
     * Wire shape (see text-drawable-list.h): 8-byte header, then the
     * 40-byte fixed payload prefix, then the UTF-8 bytes padded to 4. */
    uint32_t text_record[13] = {0};
    text_record[0] = YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST;
    text_record[1] = 44u; /* payload: 40 fixed + 2 text bytes, 4-aligned */
    float coord = 1.0f;
    memcpy(&text_record[2], &coord, 4); /* x */
    memcpy(&text_record[3], &coord, 4); /* y */
    float font_size = 16.0f;
    memcpy(&text_record[4], &font_size, 4);
    /* [5] rotation = 0 */
    text_record[6] = 0xFFFFFFFFu;  /* color */
    text_record[7] = (uint32_t)-3; /* layer — the z word */
    text_record[8] = (uint32_t)-1; /* font_id: default */
    text_record[9] = 2u;           /* text_len */
    memcpy(&text_record[12], "yo", 2u);
    struct yetty_ydraw_drawable_list_entry_ops_ptr_result text_ops_res =
        yetty_ydraw_text_drawable_list_handler(YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST);
    YTEST_REQUIRE_OK(test, text_ops_res);
    YTEST_REQUIRE_NOT_NULL(test, text_ops_res.value->paint_z);
    YTEST_CHECK_EQ_INT(test, text_ops_res.value->paint_z(text_record), -3);

    /* Complex creation records have no z field yet: helper and ops both
     * report the z-0 plane. */
    uint32_t complex_record[6] = {0x80000003u, 16u, 0u, 0u, 0u, 0u};
    YTEST_CHECK_EQ_INT(test, yetty_ydraw_complex_record_paint_z(complex_record), 0);
    struct yetty_ydraw_drawable_list_entry_ops_ptr_result complex_ops_res =
        yetty_ydraw_complex_record_handler(0x80000003u);
    YTEST_REQUIRE_OK(test, complex_ops_res);
    YTEST_REQUIRE_NOT_NULL(test, complex_ops_res.value->paint_z);
    YTEST_CHECK_EQ_INT(test, complex_ops_res.value->paint_z(complex_record), 0);

    /* Non-paint records never become render leaves: their ops leave the
     * accessor NULL and callers default to z 0. */
    struct yetty_ydraw_drawable_list_entry_ops_ptr_result cmd_ops_res =
        yetty_ydraw_cmd_handler(YETTY_YDRAW_CMD_ZERO);
    YTEST_REQUIRE_OK(test, cmd_ops_res);
    YTEST_CHECK(test, cmd_ops_res.value->paint_z == NULL);

    yetty_ydraw_drawable_list_destroy(buf);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Structural markers NODE_ID / PATH / RESERVE: builder -> parser roundtrip
 * through a MIXED stream, exact strides, data words intact — and every
 * surfaced ADD carries non-NULL ops (the walk's dispatch guard: an
 * ops-less marker is silently dropped by receivers, a regression that
 * already happened once with CMD_PATH).
 *-------------------------------------------------------------------------*/
static void test_marker_roundtrip(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_node_id(buf, 77u));
    add_box(test, buf);
    uint32_t path_ids[3] = {7u, 42u, 9u};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_path(buf, path_ids, 3u));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_update(buf, 10000u, NULL, 0u));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_reserve(buf, 280u));
    uint32_t one_id[1] = {5u};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_path(buf, one_id, 1u));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_delete(buf, 6u));

    const uint8_t *bytes = NULL;
    size_t total = yetty_ydraw_drawable_list_serialize(buf, &bytes);
    YTEST_REQUIRE(test, total > YETTY_YDRAW_SERIAL_HEADER_BYTES);
    const uint8_t *cursor = bytes + YETTY_YDRAW_SERIAL_HEADER_BYTES;
    size_t remaining = total - YETTY_YDRAW_SERIAL_HEADER_BYTES;

    /* 1: NODE_ID — 8 bytes, ops present, id intact. */
    struct yetty_ydraw_command cmd;
    struct yetty_ycore_size_result step =
        yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)step.value, 8);
    YTEST_CHECK_EQ_INT(test, (int)cmd.kind, (int)YETTY_YDRAW_COMMAND_ADD);
    YTEST_REQUIRE_NOT_NULL(test, cmd.entry.data);
    YTEST_REQUIRE_NOT_NULL(test, (void *)cmd.entry.ops);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[0], (int)YETTY_YDRAW_CMD_NODE_ID);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[1], 77);
    cursor += step.value;
    remaining -= step.value;

    /* 2: the box — a normal SDF record strides past. */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)cmd.kind, (int)YETTY_YDRAW_COMMAND_ADD);
    cursor += step.value;
    remaining -= step.value;

    /* 3: PATH depth 3 — 8 + 12 bytes, ids intact, ops present. */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)step.value, 20);
    YTEST_REQUIRE_NOT_NULL(test, (void *)cmd.entry.ops);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[0], (int)YETTY_YDRAW_CMD_PATH);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[1], 3);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[2], 7);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[3], 42);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[4], 9);
    cursor += step.value;
    remaining -= step.value;

    /* 4: UPDATE right after the path — id intact (the latch consumer). */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)cmd.kind, (int)YETTY_YDRAW_COMMAND_UPDATE);
    YTEST_CHECK_EQ_INT(test, (int)cmd.update.id, 10000);
    cursor += step.value;
    remaining -= step.value;

    /* 5: RESERVE — 8 bytes, height intact, ops present. */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)step.value, 8);
    YTEST_REQUIRE_NOT_NULL(test, (void *)cmd.entry.ops);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[0], (int)YETTY_YDRAW_CMD_RESERVE);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[1], 280);
    cursor += step.value;
    remaining -= step.value;

    /* 6: PATH depth 1 — 12 bytes. */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)step.value, 12);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[1], 1);
    YTEST_CHECK_EQ_INT(test, (int)cmd.entry.data[2], 5);
    cursor += step.value;
    remaining -= step.value;

    /* 7: DELETE closes the stream exactly. */
    step = yetty_ydraw_drawable_command_parse(reg, cursor, (uint32_t)remaining, &cmd);
    YTEST_REQUIRE_OK(test, step);
    YTEST_CHECK_EQ_INT(test, (int)cmd.kind, (int)YETTY_YDRAW_COMMAND_DELETE);
    YTEST_CHECK_EQ_INT(test, (int)cmd.id, 6);
    YTEST_CHECK_EQ_INT(test, (int)(remaining - step.value), 0);

    yetty_ydraw_drawable_list_destroy(buf);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * CMD_PATH malformed inputs: zero count, count over the cap, and a count
 * that promises more ids than the buffer holds — every case is a clean
 * parse error, never a mis-stride or over-read.
 *-------------------------------------------------------------------------*/
static void test_path_malformed(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_registry *reg = make_registry(test);
    struct yetty_ydraw_command cmd;
    uint32_t words[4] = {YETTY_YDRAW_CMD_PATH, 0u, 0u, 0u};

    /* count = 0 */
    struct yetty_ycore_size_result parsed =
        yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)words, 16u, &cmd);
    YTEST_CHECK_ERR(test, parsed);

    /* count over the cap */
    words[1] = YETTY_YDRAW_CMD_PATH_MAX_IDS + 1u;
    parsed = yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)words, 16u, &cmd);
    YTEST_CHECK_ERR(test, parsed);

    /* count promises 4 ids, buffer holds 2 words total */
    words[1] = 4u;
    parsed = yetty_ydraw_drawable_command_parse(reg, (const uint8_t *)words, 8u, &cmd);
    YTEST_CHECK_ERR(test, parsed);

    /* builder-side guards mirror the parser */
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    uint32_t ids[1] = {1u};
    YTEST_CHECK_ERR(test, yetty_ydraw_drawable_list_add_cmd_path(buf_res.value, ids, 0u));
    YTEST_CHECK_ERR(test, yetty_ydraw_drawable_list_add_cmd_path(
                              buf_res.value, ids, YETTY_YDRAW_CMD_PATH_MAX_IDS + 1u));
    YTEST_CHECK_ERR(test, yetty_ydraw_drawable_list_add_cmd_path(buf_res.value, NULL, 1u));
    yetty_ydraw_drawable_list_destroy(buf_res.value);
    yetty_ydraw_drawable_list_registry_destroy(reg);
}

int main(void)
{
    struct ytest test = ytest_begin("ydraw_list_wire");
    YTEST_RUN(&test, test_roundtrip);
    YTEST_RUN(&test, test_group_backpatch);
    YTEST_RUN(&test, test_clear_resets);
    YTEST_RUN(&test, test_golden_dump);
    YTEST_RUN(&test, test_truncated_type_only);
    YTEST_RUN(&test, test_truncated_delete);
    YTEST_RUN(&test, test_invalid_group_size);
    YTEST_RUN(&test, test_paint_z_accessor);
    YTEST_RUN(&test, test_marker_roundtrip);
    YTEST_RUN(&test, test_path_malformed);
    return ytest_end(&test);
}
