/*
 * ygui2 wire-cost contract test — headless, no GPU, no PTY.
 *
 * Pins the incremental emit model (strategy.md §4 / §12) at the envelope
 * level through a sink capture:
 *   - the FIRST emit ships one envelope (RESERVE + the whole tree +
 *     offsets);
 *   - a CLEAN frame ships ZERO bytes (the sink is not called);
 *   - one label text change ships exactly ONE envelope, and it is smaller
 *     than the first frame (a single addressed reopen, not a tree resend);
 *   - a second clean emit after that again ships nothing.
 */
#include <yetty/api/ygui2/framework.h>
#include <yetty/api/ygui2/widgets/label.h>
#include <yetty/api/ygui2/widgets/panel.h>
#include <yetty/api/ygui2/widget.h>
#include <yetty/ygui2/defs.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yetty/ysdf/funcs.gen.h>

struct sink_capture {
    uint32_t envelope_count;
    size_t last_size;
    size_t first_size;
};

static void capture_sink(const uint8_t *bytes, size_t byte_count, void *userdata)
{
    struct sink_capture *capture = userdata;
    (void)bytes;
    capture->envelope_count++;
    capture->last_size = byte_count;
    if (capture->first_size == 0) {
        capture->first_size = byte_count;
    }
}

static void test_wire_cost_model(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));

    /* panel root; a transparent column with two labels. */
    struct yetty_yclass_ptr_result panel_class_res = yetty_ygui2_panel_class_get();
    YTEST_REQUIRE_OK(test, panel_class_res);
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, panel_class_res.value);
    YTEST_REQUIRE_OK(test, root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_panel_set_bg(root_res.value, 0xFF141A1Fu));

    struct yetty_yclass_ptr_result widget_class_res = yetty_ygui2_widget_class_get();
    YTEST_REQUIRE_OK(test, widget_class_res);
    struct yetty_yclass_object_ptr_result column_res =
        yetty_ygui2_widget_add(root_res.value, widget_class_res.value);
    YTEST_REQUIRE_OK(test, column_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_transparent(column_res.value));
    struct yetty_ygui2_layout column_layout = {
        .grow = 1.0f,
        .direction = YETTY_YGUI2_DIRECTION_COLUMN,
        .gap = 4.0f,
        .pad_left = 8.0f,
        .pad_top = 8.0f,
    };
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(column_res.value, &column_layout));

    struct yetty_yclass_ptr_result label_class_res = yetty_ygui2_label_class_get();
    YTEST_REQUIRE_OK(test, label_class_res);
    struct yetty_yclass_object_ptr_result title_res =
        yetty_ygui2_widget_add(column_res.value, label_class_res.value);
    YTEST_REQUIRE_OK(test, title_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_label_set_text(title_res.value, "cpu 42%"));
    struct yetty_ygui2_layout label_layout = {.basis = 20.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(title_res.value, &label_layout));
    struct yetty_yclass_object_ptr_result body_res =
        yetty_ygui2_widget_add(column_res.value, label_class_res.value);
    YTEST_REQUIRE_OK(test, body_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_label_set_text(body_res.value, "mem 17%"));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(body_res.value, &label_layout));

    /* First frame: one insertion envelope. */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 1);
    YTEST_CHECK(test, capture.first_size > 0);

    /* Clean frame: ZERO bytes. */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 1);

    /* One text change: exactly one envelope, smaller than the first frame
     * (a single addressed reopen, not a tree resend). */
    YTEST_REQUIRE_OK(test, yetty_ygui2_label_set_text(title_res.value, "cpu 43%"));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2);
    YTEST_CHECK(test, capture.last_size > 0);
    YTEST_CHECK(test, capture.last_size < capture.first_size);

    /* Unchanged setter: no dirty, no bytes. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_label_set_text(title_res.value, "cpu 43%"));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/button.h>

static void click_counter(struct yetty_yclass_object *widget, void *userdata)
{
    (void)widget;
    (*(int *)userdata)++;
}

/* Interaction: press+release inside a button fires on_click exactly once;
 * a captured release outside does not. */
static void test_click_dispatch(struct ytest *test)
{
    struct sink_capture capture = {0};
    int clicks = 0;
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 200.0f, 100.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result button_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_button_class_get().value);
    YTEST_REQUIRE_OK(test, button_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(button_res.value, 20.0f, 20.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(button_res.value, 80.0f, 24.0f));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_button_on_click_set(button_res.value, click_counter, &clicks));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework)); /* layout runs */

    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, clicks, 1);

    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 150.0f, 90.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, clicks, 1);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/complex_host.h>
#include <yetty/ydraw-list/complex.h>

/* Streaming: a complex_host ships its creation record once inside the
 * insertion envelope; complex_host_stream then ships exactly ONE tiny
 * addressed-update envelope (CMD_PATH + UPDATE) — no repaint, no tree
 * dirt, and a clean emit afterwards still ships nothing. */
static void test_stream_update(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result host_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_complex_host_class_get().value);
    YTEST_REQUIRE_OK(test, host_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(host_res.value, 10.0f, 10.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(host_res.value, 200.0f, 100.0f));

    /* Minimal complex creation record: {type, payload_size} + 4 payload
     * words. The type must be a REAL complex-space id: COMPLEX_TYPE_BASE+1
     * is exactly CMD_DELETE (the documented command/complex namespace
     * collision) and must be REJECTED, not adopted as a "complex". */
    uint32_t command_typed[6] = {YETTY_YDRAW_COMPLEX_TYPE_BASE + 1u, 16u, 1u, 2u, 3u, 4u};
    enum { STREAM_CHILD_NODE_ID = 7 };
    struct yetty_ycore_void_result command_res =
        yetty_ygui2_complex_host_set_record(host_res.value, command_typed, 6, STREAM_CHILD_NODE_ID);
    YTEST_CHECK(test, YETTY_IS_ERR(command_res)); /* a command is not a complex */
    if (YETTY_IS_ERR(command_res)) {
        yetty_ycore_error_destroy(command_res.error);
    }
    uint32_t record_words[6] = {YETTY_YDRAW_COMPLEX_TYPE_BASE + 0x1000u, 16u, 1u, 2u, 3u, 4u};
    /* Malformed payload size must be rejected too. */
    uint32_t bad_size[6] = {YETTY_YDRAW_COMPLEX_TYPE_BASE + 0x1000u, 12u, 1u, 2u, 3u, 4u};
    struct yetty_ycore_void_result bad_size_res =
        yetty_ygui2_complex_host_set_record(host_res.value, bad_size, 6, STREAM_CHILD_NODE_ID);
    YTEST_CHECK(test, YETTY_IS_ERR(bad_size_res));
    if (YETTY_IS_ERR(bad_size_res)) {
        yetty_ycore_error_destroy(bad_size_res.error);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_set_record(host_res.value, record_words, 6,
                                                               STREAM_CHILD_NODE_ID));

    /* Streaming before the first insertion must fail, not misaddress. */
    uint32_t stream_payload[4] = {9u, 8u, 7u, 6u};
    struct yetty_ycore_void_result early_res =
        yetty_ygui2_complex_host_stream(host_res.value, stream_payload, sizeof(stream_payload));
    YTEST_CHECK(test, YETTY_IS_ERR(early_res));
    if (YETTY_IS_ERR(early_res)) {
        yetty_ycore_error_destroy(early_res.error);
    }
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 0);

    /* First frame: one insertion envelope carrying the record. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 1);
    YTEST_CHECK(test, capture.first_size > 0);

    /* Stream: exactly one more envelope, smaller than the insertion, and
     * the tree stays clean (no repaint scheduled). */
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_stream(host_res.value, stream_payload,
                                                           sizeof(stream_payload)));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2);
    YTEST_CHECK(test, capture.last_size > 0);
    YTEST_CHECK(test, capture.last_size < capture.first_size);
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);

    /* Clean emit after a stream: still zero bytes. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/popup_menu.h>
#include <yetty/api/ygui2/widgets/textinput.h>

static void popup_select_counter(struct yetty_yclass_object *popup, uint32_t index, void *userdata)
{
    (void)popup;
    (void)index;
    (*(int *)userdata)++;
}

/* Focus traversal + overlay lifecycle (phase 6): Tab cycles focus over
 * focusable widgets in tree order; typing lands in the focused textinput;
 * an overlay popup swallows a press inside, and a press outside a
 * dismiss-on-outside overlay hides it. */
static void test_focus_and_overlay(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result first_input_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_textinput_class_get().value);
    YTEST_REQUIRE_OK(test, first_input_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_focusable(first_input_res.value, 1));
    struct yetty_yclass_object_ptr_result second_input_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_textinput_class_get().value);
    YTEST_REQUIRE_OK(test, second_input_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_focusable(second_input_res.value, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* No focus yet; Tab lands on the first focusable, Tab again advances,
     * a third Tab wraps. */
    static const uint8_t tab_byte[1] = {0x09};
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, tab_byte, 1));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(first_input_res.value).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, tab_byte, 1));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(second_input_res.value).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, tab_byte, 1));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(first_input_res.value).value, 1);

    /* Typed bytes land in the focused input. */
    static const uint8_t typed[2] = {'h', 'i'};
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, typed, 2));
    char text[16] = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_textinput_text_copy(first_input_res.value, text, sizeof(text)));
    YTEST_CHECK(test, strcmp(text, "hi") == 0);

    /* Overlay popup: a press on an item selects it and closes the popup; a
     * press outside a dismiss-on-outside overlay hides it without any
     * selection. */
    struct yetty_yclass_object_ptr_result popup_res =
        yetty_ygui2_framework_overlay_add(framework, yetty_ygui2_popup_menu_class_get().value);
    YTEST_REQUIRE_OK(test, popup_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_popup_menu_item_add(popup_res.value, "one"));
    int selections = 0;
    YTEST_REQUIRE_OK(test, yetty_ygui2_popup_menu_on_select_set(popup_res.value,
                                                                popup_select_counter, &selections));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_dismiss_on_outside(popup_res.value, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(popup_res.value, 50.0f, 50.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(popup_res.value, 100.0f, 60.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_is_visible(popup_res.value).value, 1);
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 60.0f, 60.0f, 0, 1, 0));
    YTEST_CHECK_EQ_INT(test, selections, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_is_visible(popup_res.value).value, 0);
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 60.0f, 60.0f, 0, 0, 0));

    /* Re-open, then a press outside dismisses without selecting. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_visible(popup_res.value, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 250.0f, 180.0f, 0, 1, 0));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_is_visible(popup_res.value).value, 0);
    YTEST_CHECK_EQ_INT(test, selections, 1);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/yterminal/client-input.h>
#include <yetty/ymgui/wire.h>

/* Sink that also records the shape of each delivery (for the rebuild
 * sequence pin: delete envelope, raw cursor-home bytes, insertion). */
struct sequence_capture {
    uint32_t call_count;
    size_t sizes[16];
    uint8_t heads[16][4];
};

static void sequence_sink(const uint8_t *bytes, size_t byte_count, void *userdata)
{
    struct sequence_capture *capture = userdata;
    if (capture->call_count < 16) {
        capture->sizes[capture->call_count] = byte_count;
        for (size_t index = 0; index < 4 && index < byte_count; ++index) {
            capture->heads[capture->call_count][index] = bytes[index];
        }
    }
    capture->call_count++;
}

static size_t encode_b64(const uint8_t *bytes, size_t byte_count, char *out)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_length = 0;
    for (size_t index = 0; index < byte_count; index += 3) {
        uint32_t chunk = (uint32_t)bytes[index] << 16;
        int have = 1;
        if (index + 1 < byte_count) {
            chunk |= (uint32_t)bytes[index + 1] << 8;
            have = 2;
        }
        if (index + 2 < byte_count) {
            chunk |= bytes[index + 2];
            have = 3;
        }
        out[out_length++] = alphabet[(chunk >> 18) & 63];
        out[out_length++] = alphabet[(chunk >> 12) & 63];
        if (have > 1) {
            out[out_length++] = alphabet[(chunk >> 6) & 63];
        }
        if (have > 2) {
            out[out_length++] = alphabet[chunk & 63];
        }
    }
    return out_length;
}

/* Fragmented input: the SAME mouse-press/release OSC envelopes that worked
 * as one buffer must work fed ONE BYTE AT A TIME (PTY reads chunk
 * arbitrarily), and a split CSI must not leak bytes into text. */
static void test_fragmented_input(struct ytest *test)
{
    struct sink_capture capture = {0};
    int clicks = 0;
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 200.0f, 100.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result button_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_button_class_get().value);
    YTEST_REQUIRE_OK(test, button_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(button_res.value, 20.0f, 20.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(button_res.value, 80.0f, 24.0f));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_button_on_click_set(button_res.value, click_counter, &clicks));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    for (int pressed = 1; pressed >= 0; --pressed) {
        struct yetty_client_input_mouse mouse = {
            .magic = YETTY_CLIENT_INPUT_MOUSE_MAGIC,
            .version = YMGUI_WIRE_VERSION,
            .kind = YETTY_YMGUI_INPUT_MOUSE_BUTTON,
            .button = 0,
            .pressed = pressed,
            .x = 30.0f,
            .y = 30.0f,
        };
        char envelope[256];
        int prefix_length = snprintf(envelope, sizeof(envelope), "\x1b]%u;;",
                                     (unsigned)YETTY_OSC_SC_CLIENT_INPUT_MOUSE);
        size_t length = (size_t)prefix_length;
        length += encode_b64((const uint8_t *)&mouse, sizeof(mouse), envelope + length);
        envelope[length++] = 0x07;
        for (size_t index = 0; index < length; ++index) {
            YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(
                                       framework, (const uint8_t *)envelope + index, 1));
        }
    }
    YTEST_CHECK_EQ_INT(test, clicks, 1);

    /* Split CSI: ESC, then '[', then 'Z' as separate reads — Shift-Tab
     * must fire focus traversal, and no byte may reach a text widget. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_focusable(button_res.value, 1));
    static const uint8_t escape_byte[1] = {0x1b};
    static const uint8_t bracket_byte[1] = {'['};
    static const uint8_t final_byte[1] = {'Z'};
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, escape_byte, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, bracket_byte, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, final_byte, 1));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(button_res.value).value, 1);

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Resize NEVER rebuilds: the first frame reserves the viewport-
 * independent budget, so grow, shrink, and width changes are all
 * relayout-only — one envelope of targeted repaints, no delete, no
 * cursor home, and the live insertion (with any complex runtimes)
 * survives. */
static void test_resize_keeps_scene(struct ytest *test)
{
    struct sequence_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, sequence_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_panel_set_bg(root_res.value, 0xFF141A1Fu));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.call_count, 1); /* first insertion */

    /* GROW: relayout only — one envelope, nothing destroyed. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 500.0f, 600.0f));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.call_count, 2);
    YTEST_CHECK(test, capture.sizes[1] != 3); /* no ESC[H home bytes */

    /* SHRINK: same. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 500.0f, 200.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.call_count, 3);
    YTEST_CHECK(test, capture.sizes[2] != 3);

    /* WIDTH-ONLY: same. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 640.0f, 200.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.call_count, 4);
    YTEST_CHECK(test, capture.sizes[3] != 3);

    /* Clean after every resize — nothing left half-applied. */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.call_count, 4);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Flex relayout that RESIZES a clean sibling must repaint it: growing one
 * label's basis shrinks the grow sibling, whose baked-in geometry must be
 * re-emitted even though only the other widget was touched. */
static void test_sibling_resize_repaints(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 100.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result row_res = yetty_ygui2_row_add(root_res.value);
    YTEST_REQUIRE_OK(test, row_res);
    struct yetty_ygui2_layout row_layout = {.grow = 1.0f, .direction = YETTY_YGUI2_DIRECTION_ROW};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(row_res.value, &row_layout));
    struct yetty_yclass_object_ptr_result fixed_res =
        yetty_ygui2_widget_add(row_res.value, yetty_ygui2_label_class_get().value);
    YTEST_REQUIRE_OK(test, fixed_res);
    struct yetty_ygui2_layout fixed_layout = {.basis = 100.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(fixed_res.value, &fixed_layout));
    struct yetty_yclass_object_ptr_result grow_res =
        yetty_ygui2_widget_add(row_res.value, yetty_ygui2_button_class_get().value);
    YTEST_REQUIRE_OK(test, grow_res);
    struct yetty_ygui2_layout grow_layout = {.grow = 1.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(grow_res.value, &grow_layout));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    uint32_t after_first = capture.envelope_count;

    /* Touch ONLY the fixed label's basis; the grow button resizes. */
    fixed_layout.basis = 180.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(fixed_res.value, &fixed_layout));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, (int)(capture.envelope_count - after_first), 1);
    /* The grow sibling's size-change dirt was consumed by that emit — the
     * next frame is clean (nothing left half-repainted). */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Hidden or removed widgets must lose focus and stop receiving keys. */
static void test_hidden_focus_and_remove(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result input_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_textinput_class_get().value);
    YTEST_REQUIRE_OK(test, input_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_focusable(input_res.value, 1));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    static const uint8_t tab_byte[1] = {0x09};
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, tab_byte, 1));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(input_res.value).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_visible(input_res.value, 0));
    static const uint8_t letter_byte[1] = {'x'};
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_input(framework, letter_byte, 1));
    char text[8] = {0};
    YTEST_REQUIRE_OK(test, yetty_ygui2_textinput_text_copy(input_res.value, text, sizeof(text)));
    YTEST_CHECK(test, text[0] == '\0'); /* hidden widget received nothing */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_widget_has_focus(input_res.value).value, 0);

    /* Remove: unlinks, dirties the parent, survives a follow-up emit. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_remove(input_res.value));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/ydraw_embed.h>

/* Guards: minted depth over the wire budget errors at add; an embed buffer
 * carrying command records is rejected; leaf records are accepted. */
static void test_guards(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);

    /* Depth: root is minted level 1; panels nest to the CONTAINMENT
     * budget of 7 minted levels (each adds a skin subgroup below — level
     * 7 + skin = yvterm's full 8-deep ingest stack), and the add that
     * would need an 8th errors. Ingest of the max-depth frame is pinned
     * by test_depth_budget_emits. */
    struct yetty_yclass_object *parent = root_res.value;
    for (int level = 2; level <= 7; ++level) {
        struct yetty_yclass_object_ptr_result nested_res =
            yetty_ygui2_widget_add(parent, yetty_ygui2_panel_class_get().value);
        YTEST_REQUIRE_OK(test, nested_res);
        parent = nested_res.value;
    }
    struct yetty_yclass_object_ptr_result too_deep_res =
        yetty_ygui2_widget_add(parent, yetty_ygui2_panel_class_get().value);
    YTEST_CHECK(test, YETTY_IS_ERR(too_deep_res));
    if (YETTY_IS_ERR(too_deep_res)) {
        yetty_ycore_error_destroy(too_deep_res.error);
    }

    /* Embed containment: command records are rejected, leaves accepted. */
    struct yetty_yclass_object_ptr_result embed_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_ydraw_embed_class_get().value);
    YTEST_REQUIRE_OK(test, embed_res);
    struct yetty_ydraw_drawable_list_result bad_list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, bad_list_res);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_delete(bad_list_res.value, 7u));
    struct yetty_ycore_void_result bad_set_res =
        yetty_ygui2_ydraw_embed_set_buffer(embed_res.value, bad_list_res.value);
    YTEST_CHECK(test, YETTY_IS_ERR(bad_set_res));
    if (YETTY_IS_ERR(bad_set_res)) {
        yetty_ycore_error_destroy(bad_set_res.error);
    }
    yetty_ydraw_drawable_list_destroy(bad_list_res.value); /* rejected: still ours */

    struct yetty_ydraw_drawable_list_result good_list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, good_list_res);
    struct yetty_ysdf_box box = {
        .center_x = 10.0f, .center_y = 10.0f, .half_width = 10.0f, .half_height = 10.0f};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_box(good_list_res.value, 0, 0,
                                                                     0xFF92A86Bu, 0u, 0.0f, &box));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_ydraw_embed_set_buffer(embed_res.value, good_list_res.value));

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/dropdown.h>

struct dropdown_change_capture {
    int change_count;
    int remove_on_change;                 /* remove the dropdown from inside */
    struct yetty_yclass_object *dropdown; /* the in-callback removal target */
};

static void dropdown_change_counter(struct yetty_yclass_object *widget, uint32_t index,
                                    void *userdata)
{
    (void)widget;
    (void)index;
    struct dropdown_change_capture *capture = userdata;
    capture->change_count++;
    if (capture->remove_on_change && capture->dropdown) {
        struct yetty_yclass_object *dropdown = capture->dropdown;
        capture->dropdown = NULL;
        struct yetty_ycore_void_result remove_res = yetty_ygui2_widget_remove(dropdown);
        if (YETTY_IS_ERR(remove_res)) {
            yetty_ycore_error_destroy(remove_res.error);
        }
    }
}

/* Dropdown/popup lifetime: the popup lives under the OVERLAY root with the
 * dropdown as its callback userdata. Removing the dropdown must sever that
 * relationship — a press on the former popup coordinates must dispatch
 * NOTHING (no freed-memory access, no stale Python/user callback) and the
 * orphaned popup must be reclaimed at the next boundary. Also covers the
 * nastier ordering: the dropdown removed from INSIDE the popup's own
 * selection dispatch (the popup's stack frames are still executing — its
 * destruction must defer past the dispatch). */
static void test_dropdown_remove_lifetime(struct ytest *test)
{
    struct sink_capture capture = {0};
    struct dropdown_change_capture change = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, capture_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 220.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result dropdown_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_dropdown_class_get().value);
    YTEST_REQUIRE_OK(test, dropdown_res);
    struct yetty_yclass_object *dropdown = dropdown_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(dropdown, 20.0f, 20.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(dropdown, 120.0f, 24.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_dropdown_item_add(dropdown, "alpha"));
    YTEST_REQUIRE_OK(test, yetty_ygui2_dropdown_item_add(dropdown, "beta"));
    YTEST_REQUIRE_OK(
        test, yetty_ygui2_dropdown_on_change_set(dropdown, dropdown_change_counter, &change));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* Open the popup (press the field), select item 0: fixture sanity —
     * the popup sits at (20, 46), first row center ~8px below its top. */
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 0, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 58.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 58.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, change.change_count, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_dropdown_selected(dropdown).value, 0);

    /* Re-open, then remove the dropdown while its popup is VISIBLE. A
     * press on the former popup coordinates must dispatch nothing. */
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 30.0f, 0, 0, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_remove(dropdown));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 58.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 58.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, change.change_count, 1);              /* severed: no dispatch */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework)); /* reaped + projected */

    /* Fresh dropdown; this time the dropdown is removed from INSIDE the
     * popup's own selection callback — the popup must stay inert-alive
     * through its executing dispatch and be reclaimed afterwards. */
    struct yetty_yclass_object_ptr_result second_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_dropdown_class_get().value);
    YTEST_REQUIRE_OK(test, second_res);
    struct yetty_yclass_object *second = second_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(second, 20.0f, 60.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_size(second, 120.0f, 24.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_dropdown_item_add(second, "solo"));
    change.remove_on_change = 1;
    change.dropdown = second;
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_dropdown_on_change_set(second, dropdown_change_counter, &change));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 70.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 70.0f, 0, 0, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 94.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 94.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, change.change_count, 2); /* selection fired... */
    YTEST_CHECK(test, change.dropdown == NULL);       /* ...and removed it */
    /* The popup was orphaned mid-dispatch; the next boundary reaps it and
     * everything keeps working. */
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 94.0f, 0, 1, 0));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_feed_mouse_button(framework, 30.0f, 94.0f, 0, 0, 0));
    YTEST_CHECK_EQ_INT(test, change.change_count, 2);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/*===========================================================================
 * Envelope decoding — command-semantics assertions (not byte counting).
 * The sink stores the last envelope; decode_last_envelope() strips the
 * OSC framing and yface/LZ4F coding; walk_top_level() yields the actual
 * command records so tests can assert types, addressed paths, ids and
 * field payloads.
 *=========================================================================*/
#include <yetty/api/ygui2/widgets/scrollarea.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yplot/yplot.h>
#include <yetty/ysdf/types.gen.h>

#include <math.h>

/* Consume a Result expected to be an error: destroys the chain and
 * answers whether it WAS one. */
static int consumed_error(struct yetty_ycore_void_result res)
{
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return 1;
    }
    return 0;
}

struct decode_capture {
    uint32_t envelope_count;
    size_t last_size;
    uint8_t last_envelope[131072];
};

static void decode_sink(const uint8_t *bytes, size_t byte_count, void *userdata)
{
    struct decode_capture *capture = userdata;
    capture->envelope_count++;
    if (byte_count <= sizeof(capture->last_envelope)) {
        memcpy(capture->last_envelope, bytes, byte_count);
        capture->last_size = byte_count;
    } else {
        capture->last_size = 0;
    }
}

static int decode_last_envelope(const struct decode_capture *capture,
                                struct yetty_ycore_buffer *out_raw)
{
    const uint8_t *bytes = capture->last_envelope;
    size_t size = capture->last_size;
    size_t first_semi = 0;
    while (first_semi < size && bytes[first_semi] != ';') {
        first_semi++;
    }
    size_t second_semi = first_semi + 1;
    while (second_semi < size && bytes[second_semi] != ';') {
        second_semi++;
    }
    if (second_semi >= size) {
        return 0;
    }
    size_t payload_start = second_semi + 1;
    size_t payload_end = payload_start;
    while (payload_end < size && bytes[payload_end] != 0x1b) {
        payload_end++;
    }
    if (payload_end <= payload_start) {
        return 0;
    }
    struct yetty_ycore_void_result decode_res =
        yetty_yface_decode((const char *)bytes + payload_start, payload_end - payload_start,
                           /*compressed=*/1, out_raw);
    if (YETTY_IS_ERR(decode_res)) {
        yetty_ycore_error_destroy(decode_res.error);
        return 0;
    }
    return 1;
}

enum { WIRE_EVENTS_MAX = 512 };

struct wire_event {
    uint32_t type;
    uint32_t id;    /* GROUP / UPDATE / DELETE target id */
    uint32_t field; /* UPDATE payload's leading field word */
    int addressed;  /* preceded by CMD_PATH */
    uint32_t path_count;
    uint32_t path_ids[YETTY_YDRAW_CMD_PATH_MAX_IDS];
};

struct wire_events {
    struct wire_event items[WIRE_EVENTS_MAX];
    uint32_t count;
};

/* Walk the TOP-LEVEL records of a serialized list (group payloads are
 * opaque here — a top-level GROUP is exactly "a group (re)insertion"). */
static void walk_top_level(const uint8_t *raw, size_t raw_size, struct wire_events *events)
{
    events->count = 0;
    if (!raw || raw_size <= YETTY_YDRAW_SERIAL_HEADER_BYTES) {
        return;
    }
    const uint8_t *bytes = raw + YETTY_YDRAW_SERIAL_HEADER_BYTES;
    size_t size = raw_size - YETTY_YDRAW_SERIAL_HEADER_BYTES;
    size_t offset = 0;
    uint32_t pending_path[YETTY_YDRAW_CMD_PATH_MAX_IDS] = {0};
    uint32_t pending_path_count = 0;
    int pending_valid = 0;
    while (offset + 8 <= size && events->count < WIRE_EVENTS_MAX) {
        uint32_t type = 0;
        uint32_t word1 = 0;
        memcpy(&type, bytes + offset, sizeof(type));
        memcpy(&word1, bytes + offset + 4, sizeof(word1));
        size_t advance = 0;
        struct wire_event *event = NULL;
        if (type == YETTY_YDRAW_CMD_PATH) {
            pending_path_count =
                word1 <= YETTY_YDRAW_CMD_PATH_MAX_IDS ? word1 : YETTY_YDRAW_CMD_PATH_MAX_IDS;
            memcpy(pending_path, bytes + offset + 8, pending_path_count * sizeof(uint32_t));
            pending_valid = 1;
            advance = 8 + (size_t)word1 * sizeof(uint32_t);
        } else if (type == YETTY_YDRAW_CMD_GROUP || type == YETTY_YDRAW_CMD_UPDATE ||
                   type == YETTY_YDRAW_CMD_DELETE) {
            uint32_t payload_size = 0;
            memcpy(&payload_size, bytes + offset + 8, sizeof(payload_size));
            event = &events->items[events->count++];
            event->type = type;
            event->id = word1;
            event->field = 0;
            if (type == YETTY_YDRAW_CMD_UPDATE && payload_size >= sizeof(uint32_t)) {
                memcpy(&event->field, bytes + offset + 12, sizeof(uint32_t));
            }
            advance = 12 + payload_size;
        } else if (type == YETTY_YDRAW_CMD_PAINT_Z || type == YETTY_YDRAW_CMD_PAINT_Z_END ||
                   type == YETTY_YDRAW_CMD_NODE_ID || type == YETTY_YDRAW_CMD_RESERVE ||
                   type == YETTY_YDRAW_CMD_GROUP_REF) {
            advance = 8;
        } else {
            size_t sdf_size = yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG);
            if (sdf_size > 0) {
                advance = sdf_size + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
            } else {
                advance = 8 + word1; /* [type][payload_size] family */
            }
        }
        if (event) {
            event->addressed = pending_valid;
            event->path_count = pending_valid ? pending_path_count : 0;
            memcpy(event->path_ids, pending_path, sizeof(pending_path));
            pending_valid = 0;
        } else if (type != YETTY_YDRAW_CMD_PATH) {
            pending_valid = 0;
        }
        if (advance == 0 || offset + advance > size) {
            break;
        }
        offset += advance;
    }
}

static int raw_contains_word(const uint8_t *raw, size_t raw_size, uint32_t value)
{
    if (!raw || raw_size < sizeof(uint32_t)) {
        return 0;
    }
    for (size_t offset = 0; offset + sizeof(uint32_t) <= raw_size; offset += sizeof(uint32_t)) {
        uint32_t word = 0;
        memcpy(&word, raw + offset, sizeof(word));
        if (word == value) {
            return 1;
        }
    }
    return 0;
}

static uint32_t count_events(const struct wire_events *events, uint32_t type, uint32_t field)
{
    uint32_t total = 0;
    for (uint32_t index = 0; index < events->count; index++) {
        if (events->items[index].type != type) {
            continue;
        }
        if (type == YETTY_YDRAW_CMD_UPDATE && events->items[index].field != field) {
            continue;
        }
        total++;
    }
    return total;
}

static int has_group_with_id(const struct wire_events *events, uint32_t id)
{
    for (uint32_t index = 0; index < events->count; index++) {
        if (events->items[index].type == YETTY_YDRAW_CMD_GROUP && events->items[index].id == id) {
            return 1;
        }
    }
    return 0;
}

static int all_groups_addressed(const struct wire_events *events)
{
    for (uint32_t index = 0; index < events->count; index++) {
        if (events->items[index].type == YETTY_YDRAW_CMD_GROUP && !events->items[index].addressed) {
            return 0;
        }
    }
    return 1;
}

/* Decode + walk the last captured envelope into `events`; 0 on failure. */
static int decode_and_walk(struct ytest *test, const struct decode_capture *capture,
                           struct yetty_ycore_buffer *out_raw, struct wire_events *events)
{
    memset(out_raw, 0, sizeof(*out_raw));
    if (!decode_last_envelope(capture, out_raw)) {
        YTEST_CHECK(test, 0);
        return 0;
    }
    walk_top_level(out_raw->data, out_raw->size, events);
    return 1;
}

/* Viewport resize must be NON-DESTRUCTIVE: no replacement of the root
 * containment groups (1/2), every reopened group addressed at its live
 * path, and a hosted complex record never re-sent — its runtime (and
 * accumulated streamed state) survives grow, shrink and width-only
 * resizes. */
static void test_resize_addressed_only(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result label_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_label_class_get().value);
    YTEST_REQUIRE_OK(test, label_res);
    struct yetty_yclass_object_ptr_result host_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_complex_host_class_get().value);
    YTEST_REQUIRE_OK(test, host_res);
    /* FIXED geometry: the complex host must never repaint on resize. */
    struct yetty_ygui2_layout host_spec = {.basis = 40.0f, .cross_size = 60.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(host_res.value, &host_spec));
    uint32_t record[6] = {0x80001000u, 16u, 1u, 2u, 3u, 4u};
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_set_record(host_res.value, record, 6u, 7u));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 1);

    /* Every accepted height is inside the first-frame reservation — the
     * LARGEST accepted resize (32768) included: the hosted runtime (and
     * its receiver-only streamed state) must survive them all. */
    const float resize_steps[5][2] = {{500.0f, 400.0f},
                                      {300.0f, 200.0f},
                                      {500.0f, 9000.0f},
                                      {500.0f, 32768.0f},
                                      {500.0f, 300.0f}};
    for (uint32_t step = 0; step < 5; step++) {
        YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, resize_steps[step][0],
                                                                  resize_steps[step][1]));
        YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
        struct yetty_ycore_buffer raw;
        struct wire_events events;
        if (!decode_and_walk(test, &capture, &raw, &events)) {
            continue;
        }
        YTEST_CHECK(test, !has_group_with_id(&events, 1u)); /* GROUP(1) stays live */
        YTEST_CHECK(test, !has_group_with_id(&events, 2u)); /* GROUP(2) stays live */
        YTEST_CHECK(test, all_groups_addressed(&events));
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_DELETE, 0), 0);
        /* The hosted complex is NOT re-sent: its runtime survives. */
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Layout-property changes are RELAYOUT, not structure: a container gap
 * change moves children with offsets (no container replacement, hosted
 * complex untouched); an identical spec is a no-op that emits nothing. */
static void test_layout_change_granularity(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result panel_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, panel_res);
    struct yetty_ygui2_layout panel_spec = {.basis = 200.0f, .cross_size = 300.0f, .gap = 4.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(panel_res.value, &panel_spec));
    struct yetty_ygui2_layout fixed_spec = {.basis = 20.0f, .cross_size = 80.0f};
    struct yetty_yclass_object_ptr_result first_res =
        yetty_ygui2_widget_add(panel_res.value, yetty_ygui2_label_class_get().value);
    YTEST_REQUIRE_OK(test, first_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(first_res.value, &fixed_spec));
    struct yetty_yclass_object_ptr_result host_res =
        yetty_ygui2_widget_add(panel_res.value, yetty_ygui2_complex_host_class_get().value);
    YTEST_REQUIRE_OK(test, host_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(host_res.value, &fixed_spec));
    uint32_t record[6] = {0x80001000u, 16u, 1u, 2u, 3u, 4u};
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_set_record(host_res.value, record, 6u, 9u));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* Gap change: offsets only — the panel is NOT replaced. */
    panel_spec.gap = 14.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(panel_res.value, &panel_spec));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        YTEST_CHECK(test, count_events(&events, YETTY_YDRAW_CMD_UPDATE,
                                       YETTY_YDRAW_GROUP_FIELD_OFFSET) >= 1u);
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Identical spec: NO dirt, NO envelope. */
    uint32_t envelopes_before = capture.envelope_count;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(panel_res.value, &panel_spec));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, envelopes_before);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Switching a flex child to absolute placement changes the parent flow
 * even when the stored coordinates already match — the framework must
 * become dirty and re-lay the siblings (offsets/repaints, no parent
 * subtree replacement). */
static void test_absolute_transition_dirt(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_ygui2_layout fixed_spec = {.basis = 30.0f, .cross_size = 100.0f};
    struct yetty_yclass_object_ptr_result first_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_label_class_get().value);
    YTEST_REQUIRE_OK(test, first_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(first_res.value, &fixed_spec));
    struct yetty_yclass_object_ptr_result second_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_label_class_get().value);
    YTEST_REQUIRE_OK(test, second_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(second_res.value, &fixed_spec));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);

    /* abs_x/abs_y are zero-initialized: the VALUES match, only the MODE
     * changes — the framework must still become dirty and relayout. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_set_position(first_res.value, 0.0f, 0.0f));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, !has_group_with_id(&events, 1u));
        YTEST_CHECK(test, all_groups_addressed(&events));
        YTEST_CHECK(test, count_events(&events, YETTY_YDRAW_CMD_UPDATE,
                                       YETTY_YDRAW_GROUP_FIELD_OFFSET) >= 1u);
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* The scrollarea's owned content group: one wheel tick ships exactly ONE
 * offset update — independent of how many minted children the content
 * holds — and no group is re-sent. */
static void test_scroll_single_content_offset(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result scroll_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_scrollarea_class_get().value);
    YTEST_REQUIRE_OK(test, scroll_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_scrollarea_configure(scroll_res.value, 24.0f, 0.0f));
    struct yetty_ygui2_layout viewport_spec = {.basis = 100.0f, .cross_size = 120.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &viewport_spec));
    struct yetty_ygui2_layout item_spec = {.basis = 30.0f, .cross_size = 100.0f};
    for (uint32_t index = 0; index < 5; index++) {
        struct yetty_yclass_object_ptr_result item_res =
            yetty_ygui2_widget_add(scroll_res.value, yetty_ygui2_label_class_get().value);
        YTEST_REQUIRE_OK(test, item_res);
        YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(item_res.value, &item_spec));
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_mouse_scroll(framework, 10.0f, 50.0f, -1.0f));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        YTEST_CHECK_EQ_INT(
            test, count_events(&events, YETTY_YDRAW_CMD_UPDATE, YETTY_YDRAW_GROUP_FIELD_OFFSET), 1);
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Layout changes on the VIEWPORT keep working after children exist:
     * the content group reads the scrollarea's flow spec live, so a gap
     * or pad change re-lays the children (offsets, no reopen). */
    struct yetty_ygui2_layout relayout_spec = viewport_spec;
    relayout_spec.gap = 18.0f;
    relayout_spec.pad_top = 12.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &relayout_spec));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        /* Children moved: offsets shipped; the only permissible group
         * operation is an ADDRESSED size-implied skin reopen (the content
         * group grew) — never an unaddressed fresh insertion. */
        YTEST_CHECK(test, all_groups_addressed(&events));
        YTEST_CHECK(test, count_events(&events, YETTY_YDRAW_CMD_UPDATE,
                                       YETTY_YDRAW_GROUP_FIELD_OFFSET) >= 1u);
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Direction flip re-lays the children as a ROW (their rects change —
     * offsets/skin reopens dictated by geometry, no content re-send)... */
    relayout_spec.direction = YETTY_YGUI2_DIRECTION_ROW;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &relayout_spec));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    /* ...and back in COLUMN flow, a wheel tick is STILL exactly one
     * content-group offset update. */
    relayout_spec.direction = YETTY_YGUI2_DIRECTION_COLUMN;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &relayout_spec));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_mouse_scroll(framework, 10.0f, 50.0f, -1.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        YTEST_CHECK_EQ_INT(
            test, count_events(&events, YETTY_YDRAW_CMD_UPDATE, YETTY_YDRAW_GROUP_FIELD_OFFSET), 1);
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

#include <yetty/api/ygui2/widgets/plot.h>

/* The plot widget: creation ships the yplot complex record once, bound
 * DIRECTLY in the widget's containment group (retained content — never
 * in the skin subgroup); streaming ships tiny addressed updates and
 * dirties nothing. */
static void test_plot_widget_stream(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 500.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result plot_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_plot_class_get().value);
    YTEST_REQUIRE_OK(test, plot_res);
    struct yetty_ygui2_layout plot_layout = {.basis = 180.0f, .cross_size = 420.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(plot_res.value, &plot_layout));
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_set_title(plot_res.value, "wire pin"));
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_set_y_range(plot_res.value, -1.0f, 1.0f));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_plot_add_stream_buffer(plot_res.value, "live", 32u, "#6BA892"));
    /* Streaming before the first paint is rejected (no live figure). */
    float samples[32] = {0};
    YTEST_CHECK(test, YETTY_IS_ERR(yetty_ygui2_plot_stream_samples(plot_res.value, samples, 32u)));

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 1);
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        /* The creation envelope carries the yplot complex record. */
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, 0x80000003u));
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Over-capacity chunks are rejected before touching the wire. */
    YTEST_CHECK(test, YETTY_IS_ERR(yetty_ygui2_plot_stream_samples(plot_res.value, samples, 33u)));

    for (uint32_t index = 0; index < 32u; index++) {
        samples[index] = (float)index / 32.0f;
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_stream_samples(plot_res.value, samples, 32u));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2); /* ONE update envelope */
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        /* Bulk overwrite = the sample chunk + the linear ring-head op. */
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_UPDATE, 0), 2);
        yetty_ycore_buffer_destroy(&raw);
    }
    /* Streaming dirtied nothing: the next emit ships zero bytes. */
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 2);

    /* APPEND: one sample = ONE tiny envelope holding exactly two
     * addressed updates — the chunk at the ring cursor and the ring-head
     * op. Nothing else; the window is never re-sent. */
    float appended = 0.5f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_append_samples(plot_res.value, &appended, 1u));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 3);
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        YTEST_CHECK_EQ_INT(test, (int)events.count, 2);
        for (uint32_t index = 0; index < events.count; index++) {
            YTEST_CHECK(test, events.items[index].type == YETTY_YDRAW_CMD_UPDATE);
            YTEST_CHECK(test, events.items[index].addressed);
        }
        /* The whole envelope stays TINY — this is the bandwidth story. */
        YTEST_CHECK(test, raw.size < 160);
        yetty_ycore_buffer_destroy(&raw);
    }
    /* A second append advances the cursor (chunk offset 1 now) and still
     * ships one small envelope. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_append_samples(plot_res.value, &appended, 1u));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, 4);
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/*===========================================================================
 * Nested inspection — depth-first walk INTO group bodies: proves where a
 * record is actually bound (its enclosing group path + latched node id)
 * and the maximum group nesting an envelope asks the ingest stack for.
 *=========================================================================*/
enum { NESTED_HITS_MAX = 16, NESTED_PATH_MAX = 10 };

struct nested_hit {
    uint32_t type;
    uint32_t node_id; /* latched CMD_NODE_ID for this record; 0 = none */
    uint32_t group_path[NESTED_PATH_MAX];
    uint32_t group_depth;
};

struct nested_scan {
    struct nested_hit hits[NESTED_HITS_MAX];
    uint32_t hit_count;
    uint32_t max_group_depth;
};

static void walk_nested_records(const uint8_t *bytes, size_t size, uint32_t *group_path,
                                uint32_t group_depth, struct nested_scan *scan)
{
    size_t offset = 0;
    uint32_t latched_node_id = 0;
    if (group_depth > scan->max_group_depth) {
        scan->max_group_depth = group_depth;
    }
    while (offset + 8 <= size) {
        uint32_t type = 0;
        uint32_t word1 = 0;
        memcpy(&type, bytes + offset, sizeof(type));
        memcpy(&word1, bytes + offset + 4, sizeof(word1));
        size_t advance = 0;
        if (type == YETTY_YDRAW_CMD_PATH) {
            advance = 8 + (size_t)word1 * sizeof(uint32_t);
            latched_node_id = 0;
        } else if (type == YETTY_YDRAW_CMD_GROUP) {
            uint32_t payload_size = 0;
            memcpy(&payload_size, bytes + offset + 8, sizeof(payload_size));
            if (group_depth < NESTED_PATH_MAX && offset + 12 + payload_size <= size) {
                group_path[group_depth] = word1;
                walk_nested_records(bytes + offset + 12, payload_size, group_path, group_depth + 1u,
                                    scan);
            }
            advance = 12 + payload_size;
            latched_node_id = 0;
        } else if (type == YETTY_YDRAW_CMD_UPDATE || type == YETTY_YDRAW_CMD_DELETE) {
            uint32_t payload_size = 0;
            memcpy(&payload_size, bytes + offset + 8, sizeof(payload_size));
            advance = 12 + payload_size;
            latched_node_id = 0;
        } else if (type == YETTY_YDRAW_CMD_NODE_ID) {
            latched_node_id = word1;
            advance = 8;
        } else if (type == YETTY_YDRAW_CMD_PAINT_Z || type == YETTY_YDRAW_CMD_PAINT_Z_END ||
                   type == YETTY_YDRAW_CMD_RESERVE || type == YETTY_YDRAW_CMD_GROUP_REF) {
            advance = 8;
        } else {
            size_t sdf_size = yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG);
            if (sdf_size > 0) {
                advance = sdf_size + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
            } else {
                /* [type][payload_size] family — text/font/complex. Record
                 * complex-space types AND text prims with their binding
                 * context (text placement proves where chrome landed). */
                advance = 8 + word1;
                if (((type & 0x80000000u) || type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) &&
                    scan->hit_count < NESTED_HITS_MAX) {
                    struct nested_hit *hit = &scan->hits[scan->hit_count++];
                    hit->type = type;
                    hit->node_id = latched_node_id;
                    hit->group_depth = group_depth;
                    memcpy(hit->group_path, group_path, sizeof(hit->group_path));
                }
                latched_node_id = 0;
            }
        }
        if (advance == 0 || offset + advance > size) {
            break;
        }
        offset += advance;
    }
}

static void scan_envelope(struct ytest *test, const struct decode_capture *capture,
                          struct nested_scan *scan)
{
    memset(scan, 0, sizeof(*scan));
    struct yetty_ycore_buffer raw = {0};
    if (!decode_last_envelope(capture, &raw)) {
        YTEST_CHECK(test, 0);
        return;
    }
    uint32_t group_path[NESTED_PATH_MAX] = {0};
    if (raw.size > YETTY_YDRAW_SERIAL_HEADER_BYTES) {
        walk_nested_records(raw.data + YETTY_YDRAW_SERIAL_HEADER_BYTES,
                            raw.size - YETTY_YDRAW_SERIAL_HEADER_BYTES, group_path, 0u, scan);
    }
    yetty_ycore_buffer_destroy(&raw);
}

static const struct nested_hit *scan_find_type(const struct nested_scan *scan, uint32_t type)
{
    for (uint32_t index = 0; index < scan->hit_count; index++) {
        if (scan->hits[index].type == type) {
            return &scan->hits[index];
        }
    }
    return NULL;
}

/* The T5 separation contract, end to end at the wire level:
 *   1. the hosted complex binds DIRECTLY in the widget containment group
 *      ([root, host] + latched node id) — NOT under the skin subgroup;
 *   2. the stream envelope's CMD_PATH targets exactly that binding;
 *   3. a root skin repaint and a viewport resize do NOT re-send the
 *      creation record, and streaming afterwards still addresses the
 *      same live binding. */
static void test_retained_binding_and_stream(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result host_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_complex_host_class_get().value);
    YTEST_REQUIRE_OK(test, host_res);
    struct yetty_ygui2_layout host_spec = {.grow = 1.0f}; /* stretches BOTH axes */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(host_res.value, &host_spec));
    uint32_t record_words[6] = {0x80001000u, 16u, 1u, 2u, 3u, 4u};
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_complex_host_set_record(host_res.value, record_words, 6u, 7u));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    uint32_t host_id = yetty_ygui2_widget_node_id(host_res.value).value;
    struct nested_scan scan;
    scan_envelope(test, &capture, &scan);
    const struct nested_hit *binding = scan_find_type(&scan, 0x80001000u);
    YTEST_CHECK(test, binding != NULL);
    if (binding) {
        /* Bound at [GROUP(1) -> GROUP(host)] with node id 7 — exactly the
         * containment path, one level, NO skin subgroup in between. */
        YTEST_CHECK_EQ_INT(test, (int)binding->group_depth, 2);
        YTEST_CHECK_EQ_INT(test, (int)binding->group_path[0], 1);
        YTEST_CHECK_EQ_INT(test, (int)binding->group_path[1], (int)host_id);
        YTEST_CHECK_EQ_INT(test, (int)binding->node_id, 7);
    }

    /* Stream: the envelope must target exactly that binding. */
    uint32_t stream_payload[4] = {0u, 0u, 2u, 0x3F800000u};
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_stream(host_res.value, stream_payload,
                                                           sizeof(stream_payload)));
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, (int)events.count, 1);
        if (events.count == 1) {
            const struct wire_event *update = &events.items[0];
            YTEST_CHECK_EQ_INT(test, (int)update->type, (int)YETTY_YDRAW_CMD_UPDATE);
            YTEST_CHECK_EQ_INT(test, (int)update->id, 7);
            YTEST_CHECK(test, update->addressed);
            YTEST_CHECK_EQ_INT(test, (int)update->path_count, 2);
            YTEST_CHECK_EQ_INT(test, (int)update->path_ids[0], 1);
            YTEST_CHECK_EQ_INT(test, (int)update->path_ids[1], (int)host_id);
        }
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Root skin repaint: the creation record must NOT be re-sent... */
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_mark_skin_dirty(root_res.value));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    /* ...and the SAME binding still receives streams afterwards. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_stream(host_res.value, stream_payload,
                                                           sizeof(stream_payload)));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, (int)events.count, 1);
        YTEST_CHECK(test, events.count == 1 && events.items[0].path_count == 2 &&
                              events.items[0].path_ids[1] == host_id);
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Viewport resize with a GROWING host (both axes change): still no
     * record re-send, streaming still lands at the same binding. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 520.0f, 360.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    /* Theme restyle: every widget repaints its skin — the record must
     * STILL not be re-sent. */
    struct yetty_ygui2_theme theme;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_theme_copy(framework, &theme));
    theme.accent = 0xFF7788AAu;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_theme(framework, &theme));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_complex_host_stream(host_res.value, stream_payload,
                                                           sizeof(stream_payload)));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, events.count == 1 && events.items[0].id == 7u);
        yetty_ycore_buffer_destroy(&raw);
    }

    /* An INTENTIONAL record replacement DOES re-send (structural). */
    uint32_t replacement[6] = {0x80001000u, 16u, 9u, 9u, 9u, 9u};
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_complex_host_set_record(host_res.value, replacement, 6u, 7u));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, 0x80001000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Depth budget: the max-depth accepted tree (containment 7) EMITS with a
 * maximum nesting of exactly 8 groups — the deepest widget's skin
 * subgroup fills, but never exceeds, yvterm's ingest stack. */
static void test_depth_budget_emits(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object *parent = root_res.value;
    for (int level = 2; level <= 7; ++level) {
        struct yetty_yclass_object_ptr_result nested_res =
            yetty_ygui2_widget_add(parent, yetty_ygui2_panel_class_get().value);
        YTEST_REQUIRE_OK(test, nested_res);
        parent = nested_res.value;
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct nested_scan scan;
    scan_envelope(test, &capture, &scan);
    YTEST_CHECK_EQ_INT(test, (int)scan.max_group_depth, 8);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Scrollarea geometry: pads apply EXACTLY ONCE (exact child rects, all
 * four pads + cross size), and a ROW viewport scrolls horizontally with
 * exact coordinate deltas and one content-group offset update. */
static void test_scrollarea_geometry(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 300.0f, 200.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result scroll_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_scrollarea_class_get().value);
    YTEST_REQUIRE_OK(test, scroll_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_scrollarea_configure(scroll_res.value, 24.0f, 0.0f));
    struct yetty_ygui2_layout viewport_spec = {.basis = 100.0f,
                                               .cross_size = 200.0f,
                                               .gap = 6.0f,
                                               .pad_left = 10.0f,
                                               .pad_top = 12.0f,
                                               .pad_right = 14.0f,
                                               .pad_bottom = 16.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &viewport_spec));
    struct yetty_ygui2_layout item_spec = {.basis = 30.0f};
    struct yetty_yclass_object *items[3] = {0};
    for (uint32_t index = 0; index < 3; index++) {
        struct yetty_yclass_object_ptr_result item_res =
            yetty_ygui2_widget_add(scroll_res.value, yetty_ygui2_label_class_get().value);
        YTEST_REQUIRE_OK(test, item_res);
        YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(item_res.value, &item_spec));
        items[index] = item_res.value;
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* The scrollarea sits at the root origin (0,0), 200 wide, 100 tall.
     * Pads apply ONCE: first child at (pad_left, pad_top), width =
     * viewport width - pad_left - pad_right, siblings step basis+gap. */
    float item_x = 0.0f;
    float item_y = 0.0f;
    float item_w = 0.0f;
    float item_h = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[0], &item_x, &item_y, &item_w, &item_h));
    YTEST_CHECK(test, item_x == 10.0f);
    YTEST_CHECK(test, item_y == 12.0f);
    YTEST_CHECK(test, item_w == 200.0f - 10.0f - 14.0f);
    YTEST_CHECK(test, item_h == 30.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[1], NULL, &item_y, NULL, NULL));
    YTEST_CHECK(test, item_y == 12.0f + 30.0f + 6.0f);

    /* COLUMN wheel: children move UP by exactly wheel_step. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_mouse_scroll(framework, 50.0f, 50.0f, -1.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[0], NULL, &item_y, NULL, NULL));
    YTEST_CHECK(test, item_y == 12.0f - 24.0f);

    /* ROW viewport: same widget, horizontal flow — the wheel must move
     * the MAIN axis (x). The wheel is clamped to the MEASURED content
     * overhang, so first make the row overflow the 200px viewport
     * (10 + 3x80 + 2x6 + 14 = 276): a wheel shifts children LEFT by
     * wheel_step, the wire carries exactly one content-group offset
     * update, and the offset pins at the 76px limit. */
    struct yetty_ygui2_layout wide_item_spec = {.basis = 80.0f};
    for (uint32_t index = 0; index < 3; index++) {
        YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(items[index], &wide_item_spec));
    }
    struct yetty_ygui2_layout row_spec = viewport_spec;
    row_spec.direction = YETTY_YGUI2_DIRECTION_ROW;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(scroll_res.value, &row_spec));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    float row_x_before = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[0], &row_x_before, &item_y, NULL, NULL));
    YTEST_CHECK(test, item_y == 12.0f); /* fresh row flow: pads once again */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_mouse_scroll(framework, 50.0f, 50.0f, -1.0f));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    float row_x_after = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[0], &row_x_after, NULL, NULL, NULL));
    YTEST_CHECK(test, row_x_after == row_x_before - 24.0f);
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK_EQ_INT(test, count_events(&events, YETTY_YDRAW_CMD_GROUP, 0), 0);
        YTEST_CHECK_EQ_INT(
            test, count_events(&events, YETTY_YDRAW_CMD_UPDATE, YETTY_YDRAW_GROUP_FIELD_OFFSET), 1);
        yetty_ycore_buffer_destroy(&raw);
    }

    /* Wheel past the measured end: the offset pins at extent - viewport
     * (276 - 200 = 76) instead of scrolling into blank space, and a
     * wheel AT the limit is a complete no-op — no dirt, no envelope. */
    for (int wheel = 0; wheel < 5; wheel++) {
        YTEST_REQUIRE_OK(test,
                         yetty_ygui2_framework_feed_mouse_scroll(framework, 50.0f, 50.0f, -1.0f));
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(items[0], &row_x_after, NULL, NULL, NULL));
    YTEST_CHECK(test, row_x_after == 10.0f - 76.0f);
    uint32_t envelopes_at_limit = capture.envelope_count;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_feed_mouse_scroll(framework, 50.0f, 50.0f, -1.0f));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, envelopes_at_limit);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* The px value of the first CMD_RESERVE in a decoded envelope, or 0. */
static uint32_t find_reserve_px(const uint8_t *raw, size_t raw_size)
{
    if (raw_size < YETTY_YDRAW_SERIAL_HEADER_BYTES) {
        return 0;
    }
    const uint8_t *bytes = raw + YETTY_YDRAW_SERIAL_HEADER_BYTES;
    size_t remaining = raw_size - YETTY_YDRAW_SERIAL_HEADER_BYTES;
    for (size_t offset = 0; offset + 2u * sizeof(uint32_t) <= remaining;
         offset += sizeof(uint32_t)) {
        uint32_t word;
        memcpy(&word, bytes + offset, sizeof(word));
        if (word == (uint32_t)YETTY_YDRAW_CMD_RESERVE) {
            uint32_t px;
            memcpy(&px, bytes + offset + sizeof(uint32_t), sizeof(px));
            return px;
        }
    }
    return 0;
}

/* Reservation modes (strategy.md §5): fullscreen reserves the FULL
 * supported range (every accepted resize in-budget); inline reserves the
 * declared viewport height only and REJECTS growth past it — the
 * scrollback-flow contract. The mode is immutable once inserted. */
static void test_reservation_modes(struct ytest *test)
{
    /* Fullscreen (default). */
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct yetty_ycore_buffer raw = {0};
    if (decode_last_envelope(&capture, &raw)) {
        YTEST_CHECK_EQ_INT(test, (int)find_reserve_px(raw.data, raw.size), 32768);
        yetty_ycore_buffer_destroy(&raw);
    } else {
        YTEST_CHECK(test, 0);
    }
    /* The reservation is immutable while inserted. */
    YTEST_CHECK(test, consumed_error(yetty_ygui2_framework_set_fullscreen(framework, 0)));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));

    /* Inline: reserve = the declared viewport height, growth rejected. */
    struct decode_capture inline_capture = {0};
    struct yetty_yclass_object_ptr_result inline_framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, inline_framework_res);
    struct yetty_yclass_object *inline_framework = inline_framework_res.value;
    YTEST_REQUIRE_OK(
        test, yetty_ygui2_framework_set_sink(inline_framework, decode_sink, &inline_capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_fullscreen(inline_framework, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(inline_framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result inline_root_res =
        yetty_ygui2_framework_root_create(inline_framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, inline_root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(inline_framework));
    if (decode_last_envelope(&inline_capture, &raw)) {
        YTEST_CHECK_EQ_INT(test, (int)find_reserve_px(raw.data, raw.size), 300);
        yetty_ycore_buffer_destroy(&raw);
    } else {
        YTEST_CHECK(test, 0);
    }
    /* Shrink and same-height stay relayout-only; growth is the explicit
     * documented rejection with the committed viewport untouched. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(inline_framework, 500.0f, 250.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(inline_framework));
    YTEST_CHECK(
        test, consumed_error(yetty_ygui2_framework_set_viewport(inline_framework, 500.0f, 350.0f)));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(inline_framework));
    float inline_w = 0.0f;
    float inline_h = 0.0f;
    YTEST_REQUIRE_OK(
        test, yetty_ygui2_widget_rect(inline_root_res.value, NULL, NULL, &inline_w, &inline_h));
    YTEST_CHECK(test, inline_w == 500.0f && inline_h == 250.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(inline_framework));
}

/* Output-level scan: the cursor home is written as its own 3-byte CSI H
 * chunk, distinct from the DCS envelopes. */
struct home_scan_capture {
    uint32_t home_writes;
    uint32_t other_writes;
};

static void home_scan_sink(const uint8_t *bytes, size_t byte_count, void *userdata)
{
    struct home_scan_capture *capture = userdata;
    if (byte_count == 3 && memcmp(bytes, "\x1b[H", 3) == 0) {
        capture->home_writes++;
    } else {
        capture->other_writes++;
    }
}

/* An inline clear() + re-emit must NOT home the cursor: the fresh
 * insertion is a new transcript block at the CURRENT cursor — a bare
 * CSI H would stamp it over visible row 1 of unrelated shell output
 * (strategy.md §5). Fullscreen clear() DOES home: after a fullscreen
 * insertion the cursor sits at the bottom of the old reservation. */
static void test_inline_clear_no_home(struct ytest *test)
{
    /* Inline: emit -> clear -> emit ships envelopes but never CSI H. */
    struct home_scan_capture inline_scan = {0};
    struct yetty_yclass_object_ptr_result inline_framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, inline_framework_res);
    struct yetty_yclass_object *inline_framework = inline_framework_res.value;
    YTEST_REQUIRE_OK(
        test, yetty_ygui2_framework_set_sink(inline_framework, home_scan_sink, &inline_scan));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_fullscreen(inline_framework, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(inline_framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result inline_root_res =
        yetty_ygui2_framework_root_create(inline_framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, inline_root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(inline_framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_clear(inline_framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(inline_framework));
    YTEST_CHECK_EQ_INT(test, (int)inline_scan.home_writes, 0);
    YTEST_CHECK(test, inline_scan.other_writes >= 3); /* insert + delete + reinsert */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(inline_framework));

    /* Fullscreen control: the same sequence homes exactly once (before
     * the re-insertion). */
    struct home_scan_capture fullscreen_scan = {0};
    struct yetty_yclass_object_ptr_result fullscreen_framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, fullscreen_framework_res);
    struct yetty_yclass_object *fullscreen_framework = fullscreen_framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(fullscreen_framework, home_scan_sink,
                                                          &fullscreen_scan));
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_framework_set_viewport(fullscreen_framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result fullscreen_root_res = yetty_ygui2_framework_root_create(
        fullscreen_framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, fullscreen_root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(fullscreen_framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_clear(fullscreen_framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(fullscreen_framework));
    YTEST_CHECK_EQ_INT(test, (int)fullscreen_scan.home_writes, 1);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(fullscreen_framework));
}

/* Encode one pane RESIZE envelope (OSC + base64 struct) and feed it whole
 * — the exact path a live host resize takes. */
static void send_resize_envelope(struct ytest *test, struct yetty_yclass_object *framework,
                                 float width, float height, float scale)
{
    struct yetty_client_input_resize resize = {
        .magic = YETTY_CLIENT_INPUT_RESIZE_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = 0,
        .content_scale = scale,
        .width = width,
        .height = height,
    };
    char envelope[256];
    int prefix_length = snprintf(envelope, sizeof(envelope), "\x1b]%u;;",
                                 (unsigned)YETTY_OSC_SC_CLIENT_INPUT_RESIZE);
    size_t length = (size_t)prefix_length;
    length += encode_b64((const uint8_t *)&resize, sizeof(resize), envelope + length);
    envelope[length++] = 0x07;
    YTEST_REQUIRE_OK(
        test, yetty_ygui2_framework_feed_input(framework, (const uint8_t *)envelope, length));
}

/* Viewport contract: dimensions must be finite and inside the supported
 * range (rejected outright otherwise); the first frame reserves the FULL
 * supported range, so EVERY accepted resize — up to 32768 px — is
 * relayout-only and no accepted transition can ever destroy retained
 * runtimes. The pane-envelope path is transactional with the input
 * scale. */
static void test_viewport_bounds(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));

    /* Non-finite and beyond-range sizes are rejected up front. */
    YTEST_CHECK(test,
                consumed_error(yetty_ygui2_framework_set_viewport(framework, INFINITY, 300.0f)));
    YTEST_CHECK(test,
                consumed_error(yetty_ygui2_framework_set_viewport(framework, 400.0f, 1.0e9f)));
    YTEST_CHECK(test,
                consumed_error(yetty_ygui2_framework_set_viewport(framework, 400.0f, 32769.0f)));

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* Any accepted height is inside the reservation: growth far past the
     * old 8192 budget is ordinary relayout — ONE envelope, no rebuild. */
    uint32_t envelopes_before_growth = capture.envelope_count;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 500.0f, 9000.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, envelopes_before_growth + 1u);
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 500.0f, 400.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);

    /* The PANE-DRIVEN path is TRANSACTIONAL: content_scale commits only
     * after the viewport transition succeeded, so input mapping can never
     * diverge from the drawn projection. */
    send_resize_envelope(test, framework, 800.0f, 600.0f, 2.0f);
    float committed_scale = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_content_scale(framework, &committed_scale));
    YTEST_CHECK(test, committed_scale == 2.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    float root_w = 0.0f;
    float root_h = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(root_res.value, NULL, NULL, &root_w, &root_h));
    YTEST_CHECK(test, root_w == 400.0f && root_h == 300.0f); /* 800x600 / scale 2 */

    /* Garbage scale rejects the WHOLE envelope — neither scale nor
     * viewport moves. */
    send_resize_envelope(test, framework, 800.0f, 600.0f, INFINITY);
    send_resize_envelope(test, framework, 800.0f, 600.0f, -1.0f);
    /* A valid scale with an absurd height fails the viewport transition —
     * the scale must NOT commit either. */
    send_resize_envelope(test, framework, 800.0f, 1.0e8f, 4.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_content_scale(framework, &committed_scale));
    YTEST_CHECK(test, committed_scale == 2.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(root_res.value, NULL, NULL, &root_w, &root_h));
    YTEST_CHECK(test, root_w == 400.0f && root_h == 300.0f);

    /* NO client-side ceiling: the host legally commits products beyond
     * any fixed cap (density 2.625 x structural zoom 8 = 21) — the
     * envelope must be ACCEPTED and both halves must transition. */
    send_resize_envelope(test, framework, 8400.0f, 6300.0f, 21.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_content_scale(framework, &committed_scale));
    YTEST_CHECK(test, committed_scale == 21.0f);
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_rect(root_res.value, NULL, NULL, &root_w, &root_h));
    YTEST_CHECK(test, root_w == 400.0f && root_h == 300.0f); /* 8400x6300 / 21 */

    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

/* Plot state contract: exactly one stream buffer with a bounded
 * capacity; appends of ANY count up to capacity (no hidden packet cap);
 * the record binds at the containment path with its chrome bracketed in
 * a sibling subgroup; RESIZE is one addressed geometry op — the record
 * and its samples are NEVER re-sent; a live range change is one
 * addressed ranges op; a short bulk overwrite ships the full window (no
 * stale tail); and a truly STRUCTURAL change (expression) replaces
 * the record with the cached window replayed inside the same envelope. */
static void test_plot_replay_and_binding(struct ytest *test)
{
    struct decode_capture capture = {0};
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    YTEST_REQUIRE_OK(test, framework_res);
    struct yetty_yclass_object *framework = framework_res.value;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_sink(framework, decode_sink, &capture));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_set_viewport(framework, 400.0f, 300.0f));
    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value);
    YTEST_REQUIRE_OK(test, root_res);
    struct yetty_yclass_object_ptr_result plot_res =
        yetty_ygui2_widget_add(root_res.value, yetty_ygui2_plot_class_get().value);
    YTEST_REQUIRE_OK(test, plot_res);
    struct yetty_ygui2_layout plot_spec = {.basis = 150.0f, .cross_size = 300.0f};
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(plot_res.value, &plot_spec));

    /* Capacity is validated up front: the degenerate and the wrapping
     * extremes are rejected BEFORE any allocation or figure mutation. */
    struct yetty_ycore_void_result too_small_res =
        yetty_ygui2_plot_add_stream_buffer(plot_res.value, "live", 1u, "#6BA892");
    YTEST_CHECK(test, YETTY_IS_ERR(too_small_res));
    if (YETTY_IS_ERR(too_small_res)) {
        yetty_ycore_error_destroy(too_small_res.error);
    }
    struct yetty_ycore_void_result too_large_res =
        yetty_ygui2_plot_add_stream_buffer(plot_res.value, "live", 65537u, "#6BA892");
    YTEST_CHECK(test, YETTY_IS_ERR(too_large_res));
    if (YETTY_IS_ERR(too_large_res)) {
        yetty_ycore_error_destroy(too_large_res.error);
    }
    YTEST_REQUIRE_OK(test,
                     yetty_ygui2_plot_add_stream_buffer(plot_res.value, "live", 128u, "#6BA892"));
    /* EXACTLY one stream buffer: the second declaration is rejected and
     * the first stays fully usable. */
    struct yetty_ycore_void_result second_res =
        yetty_ygui2_plot_add_stream_buffer(plot_res.value, "extra", 16u, "#74C5A5");
    YTEST_CHECK(test, YETTY_IS_ERR(second_res));
    if (YETTY_IS_ERR(second_res)) {
        yetty_ycore_error_destroy(second_res.error);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));

    /* The yplot record binds DIRECTLY under [root, plot] — retained —
     * and its chrome (tick labels) is bracketed in a SIBLING subgroup
     * inside the widget group (depth 3), which is what the receiver
     * replaces locally on a geometry/range op. */
    uint32_t plot_widget_id = yetty_ygui2_widget_node_id(plot_res.value).value;
    struct nested_scan scan;
    scan_envelope(test, &capture, &scan);
    const struct nested_hit *binding = scan_find_type(&scan, 0x80000003u);
    YTEST_CHECK(test, binding != NULL);
    if (binding) {
        YTEST_CHECK_EQ_INT(test, (int)binding->group_depth, 2);
        YTEST_CHECK_EQ_INT(test, (int)binding->group_path[0], 1);
        YTEST_CHECK_EQ_INT(test, (int)binding->group_path[1], (int)plot_widget_id);
        YTEST_CHECK(test, binding->node_id != 0);
    }
    const struct nested_hit *chrome_text =
        scan_find_type(&scan, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST);
    YTEST_CHECK(test, chrome_text != NULL);
    if (chrome_text) {
        YTEST_CHECK_EQ_INT(test, (int)chrome_text->group_depth, 3);
        YTEST_CHECK_EQ_INT(test, (int)chrome_text->group_path[0], 1);
        YTEST_CHECK_EQ_INT(test, (int)chrome_text->group_path[1], (int)plot_widget_id);
        YTEST_CHECK(test, chrome_text->group_path[2] != 0);
    }

    /* Capacity is the ONLY bound: a 65-sample steady-state append (over
     * the old hidden packet cap) succeeds; 129 > 128 is rejected. */
    float bulk_samples[129];
    for (uint32_t index = 0; index < 129u; index++) {
        bulk_samples[index] = 42.0f; /* 0x42280000 — distinctive */
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_append_samples(plot_res.value, bulk_samples, 65u));
    YTEST_CHECK(test,
                YETTY_IS_ERR(yetty_ygui2_plot_append_samples(plot_res.value, bulk_samples, 129u)));

    /* RESIZE + an append before the emit: the append lands on the LIVE
     * runtime (which survives — there is no swap), then the emit ships
     * ONE addressed geometry op. */
    plot_spec.basis = 180.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui2_widget_layout_set(plot_res.value, &plot_spec));
    float race_sample = 43.0f; /* 0x422C0000 — distinctive */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_append_samples(plot_res.value, &race_sample, 1u));

    /* THE RESIZE CONTRACT: the frame envelope carries NO yplot record,
     * NO sample bytes and NO chrome text — just the addressed geometry
     * op (plus offsets/skin bookkeeping). A 10 GB drawable resizes for
     * the same handful of bytes. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    struct yetty_ycore_buffer raw;
    struct wire_events events;
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80000003u)); /* no record */
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x42280000u)); /* no window */
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x422C0000u)); /* no race */
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, YETTY_YPLOT_UPDATE_OP_GEOMETRY));
        YTEST_CHECK(test, raw.size < 512u);
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);
    uint32_t envelopes_after_resize = capture.envelope_count;
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    YTEST_CHECK_EQ_INT(test, capture.envelope_count, envelopes_after_resize); /* clean */

    /* A LIVE range change is one addressed ranges op — same locality. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_set_y_range(plot_res.value, -2.0f, 2.0f));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, YETTY_YPLOT_UPDATE_OP_RANGES));
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x80000003u));
        YTEST_CHECK(test, raw.size < 256u);
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_CHECK_EQ_INT(test, yetty_ygui2_framework_is_dirty(framework).value, 0);

    /* SHORT bulk overwrite ships the FULL window (5 new + zeroed tail):
     * cache and runtime stay identical — no stale tail anywhere. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_stream_samples(plot_res.value, bulk_samples, 5u));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, raw.size > 128u * sizeof(float)); /* full window shipped */
        yetty_ycore_buffer_destroy(&raw);
    }
    /* A truly STRUCTURAL change (new expression = new drawable)
     * replaces the record and replays exactly the current cached state
     * inside the SAME insertion envelope (five 42.0 samples — the old
     * appended 43.0 was overwritten by the bulk load and must NOT
     * reappear). */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_set_expression(plot_res.value, "sin(2*x) * 0.5"));
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_emit(framework));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, 0x80000003u)); /* record */
        YTEST_CHECK(test, raw_contains_word(raw.data, raw.size, 0x42280000u)); /* window */
        YTEST_CHECK(test, !raw_contains_word(raw.data, raw.size, 0x422C0000u));
        yetty_ycore_buffer_destroy(&raw);
    }
    /* Steady-state appends resume tiny. */
    YTEST_REQUIRE_OK(test, yetty_ygui2_plot_append_samples(plot_res.value, &race_sample, 1u));
    if (decode_and_walk(test, &capture, &raw, &events)) {
        YTEST_CHECK(test, raw.size < 160);
        yetty_ycore_buffer_destroy(&raw);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui2_framework_dispose(framework));
}

int main(void)
{
    struct ytest test = ytest_begin("ygui2_wire");
    YTEST_RUN(&test, test_wire_cost_model);
    YTEST_RUN(&test, test_click_dispatch);
    YTEST_RUN(&test, test_stream_update);
    YTEST_RUN(&test, test_focus_and_overlay);
    YTEST_RUN(&test, test_fragmented_input);
    YTEST_RUN(&test, test_resize_keeps_scene);
    YTEST_RUN(&test, test_sibling_resize_repaints);
    YTEST_RUN(&test, test_hidden_focus_and_remove);
    YTEST_RUN(&test, test_guards);
    YTEST_RUN(&test, test_dropdown_remove_lifetime);
    YTEST_RUN(&test, test_resize_addressed_only);
    YTEST_RUN(&test, test_layout_change_granularity);
    YTEST_RUN(&test, test_absolute_transition_dirt);
    YTEST_RUN(&test, test_scroll_single_content_offset);
    YTEST_RUN(&test, test_plot_widget_stream);
    YTEST_RUN(&test, test_retained_binding_and_stream);
    YTEST_RUN(&test, test_depth_budget_emits);
    YTEST_RUN(&test, test_scrollarea_geometry);
    YTEST_RUN(&test, test_viewport_bounds);
    YTEST_RUN(&test, test_reservation_modes);
    YTEST_RUN(&test, test_inline_clear_no_home);
    YTEST_RUN(&test, test_plot_replay_and_binding);
    return ytest_end(&test);
}
