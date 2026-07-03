/*
 * ydraw-core command wire + golden contract test.
 *
 * Builds command streams with the ydraw-core builders, walks them back with
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

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ydraw/drawable-list-registry.h>
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
    struct yetty_ysdf_box box = {
        .center_x = 10.0f, .center_y = 20.0f, .half_width = 5.0f, .half_height = 6.0f,
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
            written +=
                (size_t)snprintf(out + written, out_cap - written, "DELETE id=%u\n", cmd.id);
        } else {
            uint32_t type = cmd.entry.data[0];
            if (type == YETTY_YDRAW_CMD_GROUP) {
                written += (size_t)snprintf(out + written, out_cap - written,
                                            "CMD type=0x%08x id=%u\n", type, cmd.entry.data[1]);
            } else {
                written += (size_t)snprintf(out + written, out_cap - written, "ADD type=0x%08x\n",
                                            type);
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
    dump_commands(reg, yetty_ydraw_drawable_list_data(buf),
                  yetty_ydraw_drawable_list_size(buf), dump, sizeof(dump));

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

int main(void)
{
    struct ytest test = ytest_begin("ydraw_core_wire");
    YTEST_RUN(&test, test_roundtrip);
    YTEST_RUN(&test, test_group_backpatch);
    YTEST_RUN(&test, test_clear_resets);
    YTEST_RUN(&test, test_golden_dump);
    YTEST_RUN(&test, test_truncated_type_only);
    YTEST_RUN(&test, test_truncated_delete);
    YTEST_RUN(&test, test_invalid_group_size);
    return ytest_end(&test);
}
