/*
 * ymux daemon⇄client contract test (#695 phase 5) — headless, no GPU, no
 * yvterm, REAL unix socket, in-process pump. The full tmux loop:
 *
 *   client ATTACH → daemon spawns a pane PTY (memory pair; the test holds
 *   the application end) → WELCOME + PAINT FULL → application output →
 *   PAINT frames → client surface parity → client input → bytes arrive at
 *   the application end → DETACH with continued output → reconnect with
 *   the same token resumes control and sees the missed content.
 */

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/client.h>
#include <yetty/api/ymux/daemon.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/rich.h>
#include <yetty/api/ymux/session.h>
#include <yetty/yface/yface.h>

#include <lz4frame.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

/* Module-private wire enum (YMUX_TERM_CAP_*) for the attach capability arg. */
#include "../../../src/yetty/ymux/proto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Plain hand-written client methods for the figure-surface proxy (not generated;
 * forward-declared exactly as the ymux tool does). */
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay(struct yetty_yclass_object *obj,
                                                           uint32_t channel_id,
                                                           const uint8_t *bytes, size_t len);
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay_close(struct yetty_yclass_object *obj,
                                                                 uint32_t channel_id);
struct yetty_ycore_void_result yetty_ymux_client_figure_input(struct yetty_yclass_object *obj,
                                                              uint32_t wire_code,
                                                              const uint8_t *bytes, size_t len);

/* OSC codes for figure input (from yetty/yterminal/client-input.h). */
enum { TEST_FIGURE_MOUSE = 700000, TEST_FIGURE_KEY = 700003 };

enum { TEST_MAX_PANES = 4 };

/* The spawn seam: hand the daemon one end of a memory pair, keep the
 * other as the "application" (what a shell's stdio would be). */
struct spawn_rig {
    struct yetty_platform_pty *application_end[TEST_MAX_PANES];
    uint32_t spawned;
};

static struct yetty_yplatform_pty_ptr_result rig_spawn(uint32_t rows, uint32_t cols, void *userdata)
{
    (void)rows;
    (void)cols;
    struct spawn_rig *rig = userdata;
    if (rig->spawned >= TEST_MAX_PANES) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "spawn rig: table full");
    }
    struct yetty_yplatform_memory_pty_pair_result pair_res =
        yetty_yplatform_memory_pty_pair_create(1u << 20);
    YETTY_RETURN_IF_ERR(yetty_yplatform_pty_ptr, pair_res, "spawn rig: pair");
    rig->application_end[rig->spawned] = pair_res.value.b;
    ++rig->spawned;
    return YETTY_OK(yetty_yplatform_pty_ptr, pair_res.value.a);
}

static void rig_dispose(struct spawn_rig *rig)
{
    for (uint32_t index = 0; index < rig->spawned; ++index) {
        if (rig->application_end[index]) {
            struct yetty_ycore_void_result destroy_res =
                rig->application_end[index]->ops->destroy(rig->application_end[index]);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            rig->application_end[index] = NULL;
        }
    }
}

/* Pump daemon + up to two clients until both go quiescent. */
static void pump(struct ytest *test, struct yetty_yclass_object *daemon,
                 struct yetty_yclass_object *client_a, struct yetty_yclass_object *client_b)
{
    for (int round = 0; round < 200; ++round) {
        int events = 0;
        struct yetty_ycore_int_result daemon_res = yetty_ymux_daemon_step(daemon);
        YTEST_REQUIRE_OK(test, daemon_res);
        events += daemon_res.value;
        if (client_a) {
            struct yetty_ycore_int_result client_res = yetty_ymux_client_step(client_a);
            YTEST_REQUIRE_OK(test, client_res);
            events += client_res.value;
        }
        if (client_b) {
            struct yetty_ycore_int_result client_res = yetty_ymux_client_step(client_b);
            YTEST_REQUIRE_OK(test, client_res);
            events += client_res.value;
        }
        if (events == 0 && round > 2) {
            return;
        }
    }
}

static void application_print(struct ytest *test, struct spawn_rig *rig, uint32_t pane_index,
                              const char *text)
{
    struct yetty_platform_pty *application = rig->application_end[pane_index];
    YTEST_REQUIRE_NOT_NULL(test, application);
    struct yetty_ycore_size_result write_res =
        application->ops->write(application, text, strlen(text));
    YTEST_REQUIRE_OK(test, write_res);
    YTEST_CHECK_EQ_SIZE(test, write_res.value, strlen(text));
}

static void socket_path_for(char *out, size_t cap, const char *tag)
{
    snprintf(out, cap, "ymux-test-%s-%d.sock", tag, (int)getpid());
}

/*---------------------------------------------------------------------------
 * Terminal-byte capture helpers for the vtsink lane assertions.
 *-------------------------------------------------------------------------*/

struct vt_capture {
    uint8_t bytes[1u << 16];
    size_t len;
};

static void vt_capture_sink(const uint8_t *bytes, size_t len, void *userdata)
{
    struct vt_capture *capture = userdata;
    if (!bytes || len == 0 || capture->len + len > sizeof(capture->bytes)) {
        return;
    }
    memcpy(capture->bytes + capture->len, bytes, len);
    capture->len += len;
}

static int vt_capture_contains(const struct vt_capture *capture, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || capture->len < needle_len) {
        return 0;
    }
    for (size_t offset = 0; offset + needle_len <= capture->len; ++offset) {
        if (memcmp(capture->bytes + offset, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * vtsink lane wire (#699.2): the SOLE terminal-byte path. The client hosts the
 * sink and the daemon delivers typed ordered feed() calls tunnelled through
 * VTSINK_RPC frames — civis establishing redraw, small deltas, no 2J, a live
 * generation per feed. The daemon projects NOTHING for a VT_TEXT client until
 * its lane is up (project_vt consumes the delta), so the lane stream is
 * self-contained from its first feed. End to end over the real socket
 * dispatch.
 *-------------------------------------------------------------------------*/

/* Hand-written vtsink seams (outside the generated headers). */
int yetty_ymux_client_route_overlay_input(struct yetty_yclass_object *obj, uint32_t input_class,
                                          uint32_t figure_id, uint32_t overlay_figure_id);
void yetty_ymux_client_set_overlay_input_active(struct yetty_yclass_object *obj, int active);
void yetty_ymux_client_set_overlay_input_handler(struct yetty_yclass_object *obj,
                                                 void (*handler)(uint32_t input_class,
                                                                 const uint8_t *bytes, size_t len,
                                                                 void *userdata),
                                                 void *userdata);
void yetty_ymux_client_overlay_input_deliver(struct yetty_yclass_object *obj, uint32_t input_class,
                                             const uint8_t *bytes, size_t len);
uint64_t yetty_ymux_client_overlay_consumed_count(struct yetty_yclass_object *obj,
                                                  uint32_t input_class);
void yetty_ymux_client_enable_vtsink(struct yetty_yclass_object *obj,
                                     void (*emit)(uint64_t generation, const uint8_t *bytes,
                                                  size_t len, void *userdata),
                                     void *userdata);
struct yetty_yclass_object *yetty_ymux_client_vtsink_object(struct yetty_yclass_object *obj);
void yetty_ymux_client_vtsink_defer_ack(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_vtsink_ack(struct yetty_yclass_object *obj,
                                                            uint64_t generation);

struct vtsink_emit_capture {
    struct vt_capture bytes;
    uint64_t last_generation;
    int calls;
};

static void vtsink_emit_sink(uint64_t generation, const uint8_t *bytes, size_t len, void *userdata)
{
    struct vtsink_emit_capture *capture = userdata;
    capture->last_generation = generation;
    capture->calls += 1;
    vt_capture_sink(bytes, len, &capture->bytes);
}

/*---------------------------------------------------------------------------
 * Attach → output → paint parity → input round-trip.
 *-------------------------------------------------------------------------*/
static void test_attach_paint_input(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "paint");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "main", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "main", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-paint"));
    pump(test, daemon, client, NULL);

    /* Attached, controller (first eligible), pane spawned. */
    YTEST_REQUIRE_EQ_INT(test, yetty_ymux_client_attached(client).value, 1);
    YTEST_CHECK_EQ_INT(test, rig.spawned, 1);
    uint32_t permissions = yetty_ymux_client_permissions(client).value;
    YTEST_CHECK(test, (permissions & YETTY_YMUX_PERMISSION_RESIZE) != 0);

    /* Application output arrives as vtsink feed() bytes (#699.3: text is
     * lane-only — the semantic paint surface is retired). */
    application_print(test, &rig, 0, "hello-from-pty\r\n");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "hello-from-pty"));

    /* Client input reaches the application end of the PTY. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_input_char(client, 'z', 0));
    pump(test, daemon, client, NULL);
    char input_buf[64] = {0};
    struct yetty_platform_pty *application = rig.application_end[0];
    struct yetty_ycore_size_result read_res =
        application->ops->read(application, input_buf, sizeof(input_buf));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_SIZE(test, read_res.value, 1);
    YTEST_CHECK_EQ_INT(test, input_buf[0], 'z');

    /* Special-key input routes through the permission-gated session path too
     * (not straight to the engine): ENTER reaches the application as CR. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_input_key(client, YETTY_YMUX_KEY_ENTER, 0));
    pump(test, daemon, client, NULL);
    char key_buf[64] = {0};
    struct yetty_ycore_size_result key_read =
        application->ops->read(application, key_buf, sizeof(key_buf));
    YTEST_REQUIRE_OK(test, key_read);
    YTEST_CHECK_EQ_SIZE(test, key_read.value, 1);
    YTEST_CHECK_EQ_INT(test, (unsigned char)key_buf[0], '\r');

    /* Mouse: the application enables SGR mouse mode, then a click routes through
     * the permission-gated session path and reaches the app as an SGR mouse
     * report (\e[<...M). */
    application_print(test, &rig, 0, "\x1b[?1006h\x1b[?1000h");
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_input_mouse_move(client, 2, 3, 0));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_input_mouse_button(client, 1, 1, 0)); /* left press */
    pump(test, daemon, client, NULL);
    char mouse_buf[64] = {0};
    struct yetty_ycore_size_result mouse_read =
        application->ops->read(application, mouse_buf, sizeof(mouse_buf));
    YTEST_REQUIRE_OK(test, mouse_read);
    YTEST_CHECK(test, mouse_read.value >= 3 && mouse_buf[0] == '\x1b' && mouse_buf[1] == '[' &&
                          mouse_buf[2] == '<'); /* SGR mouse report */

    /* Paste: the pasted text reaches the application (unwrapped — no bracketed
     * paste mode enabled here). */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_input_paste(client, "PX", 2));
    pump(test, daemon, client, NULL);
    char paste_buf[64] = {0};
    struct yetty_ycore_size_result paste_read =
        application->ops->read(application, paste_buf, sizeof(paste_buf));
    YTEST_REQUIRE_OK(test, paste_read);
    YTEST_CHECK(test, paste_read.value >= 2 && memcmp(paste_buf, "PX", 2) == 0);

    /* Rich half over the socket: the application emits a vendor DCS
     * envelope; the client receives it inside a transaction frame. */
    uint32_t prim[10] = {YETTY_YSDF_BOX, 0, 0xFF00FF00u, 0, 0};
    float geometry[5] = {4.0f, 9.0f, 4.0f, 9.0f, 0.0f};
    memcpy(&prim[5], geometry, sizeof(geometry));
    struct yetty_ycore_buffer_result b64_res = yetty_ycore_base64_encode(prim, sizeof(prim));
    YTEST_REQUIRE_OK(test, b64_res);
    char envelope[512];
    int envelope_len = snprintf(envelope, sizeof(envelope), "\x1bP600001y%.*s\x1b\\",
                                (int)b64_res.value.size, (const char *)b64_res.value.data);
    yetty_ycore_buffer_destroy(&b64_res.value);
    YTEST_REQUIRE(test, envelope_len > 0);
    struct yetty_ycore_size_result envelope_write_res =
        application->ops->write(application, envelope, (size_t)envelope_len);
    YTEST_REQUIRE_OK(test, envelope_write_res);
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, yetty_ymux_client_rich_generation(client).value >= 1);
    uint32_t rich_words = 0;
    struct yetty_ycore_const_uint32_ptr_result rich_body_res =
        yetty_ymux_client_rich_body(client, &rich_words);
    YTEST_REQUIRE_OK(test, rich_body_res);
    YTEST_REQUIRE_NOT_NULL(test, rich_body_res.value);
    YTEST_CHECK(test, rich_body_res.value[0] == 0x594D5052u); /* rich magic */
    YTEST_CHECK_EQ_INT(test, rich_body_res.value[2], 1);      /* one record */

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * ycat figure intake (#695): a YPB1 drawable-list DCS envelope reserves
 * ceil(scene_max_y / cell_height) rows — the daemon feeds that many newlines
 * so the next prompt lands BELOW the figure — and the client receives the
 * rich record anchored at the emission row carrying that span.
 *-------------------------------------------------------------------------*/
static void test_ycat_figure_reserves_rows(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "ycatrows");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "ycatrows", 12, 60));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "ycatrows", 0, 12, 60, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-ycat"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_EQ_INT(test, yetty_ymux_client_attached(client).value, 1);

    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "ycatrows").value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t pane_id = yetty_ymux_client_pane_id(client).value;
    struct yetty_yclass_object *pane = yetty_ymux_session_pane(session, pane_id).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    YTEST_REQUIRE_NOT_NULL(test, engine);

    application_print(test, &rig, 0, "before-figure\r\n");
    pump(test, daemon, client, NULL);
    uint32_t row_before = 0, col_before = 0;
    int visible = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_cursor(engine, &row_before, &col_before, &visible));

    /* ycat wire shape (src/yetty/ycat/dcs.c):
     *   ESC P 600001 y b64(bin_meta) ; b64(payload) ESC \
     * Uncompressed variant; the payload is the YPB1 drawable-list container
     * whose scene_max_y (offset 16) is the figure's pixel height. */
    uint32_t container[7] = {0x31425059u /* 'YPB1' */};
    float bounds[4] = {0.0f, 0.0f, 120.0f, 48.0f}; /* 48 px tall -> 3 rows @ 16 */
    memcpy(&container[1], bounds, sizeof(bounds));
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_NONE,
        .raw_size = sizeof(container),
    };
    struct yetty_ycore_buffer_result meta_b64 = yetty_ycore_base64_encode(&meta, sizeof(meta));
    YTEST_REQUIRE_OK(test, meta_b64);
    struct yetty_ycore_buffer_result body_b64 =
        yetty_ycore_base64_encode(container, sizeof(container));
    YTEST_REQUIRE_OK(test, body_b64);
    char ycat_envelope[512];
    int ycat_envelope_len =
        snprintf(ycat_envelope, sizeof(ycat_envelope), "\x1bP600001y%.*s;%.*s\x1b\\",
                 (int)meta_b64.value.size, (const char *)meta_b64.value.data,
                 (int)body_b64.value.size, (const char *)body_b64.value.data);
    yetty_ycore_buffer_destroy(&meta_b64.value);
    yetty_ycore_buffer_destroy(&body_b64.value);
    YTEST_REQUIRE(test, ycat_envelope_len > 0);
    struct yetty_platform_pty *application = rig.application_end[0];
    struct yetty_ycore_size_result ycat_write_res =
        application->ops->write(application, ycat_envelope, (size_t)ycat_envelope_len);
    YTEST_REQUIRE_OK(test, ycat_write_res);
    pump(test, daemon, client, NULL);

    /* Reservation: the cursor advanced exactly ceil(48/16) = 3 rows. */
    uint32_t row_after = 0, col_after = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_cursor(engine, &row_after, &col_after, &visible));
    YTEST_CHECK_EQ_INT(test, (int)row_after, (int)row_before + 3);

    /* The daemon store minted the record with the 3-row span. */
    struct yetty_yclass_object *store = yetty_ymux_pane_rich_store(pane).value;
    YTEST_REQUIRE_NOT_NULL(test, store);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_rich_count(store).value, 1);
    uint64_t rich_id = yetty_ymux_rich_id_at(store, 0).value;
    int anchor_kind = 0;
    uint64_t anchor_a = 0;
    uint32_t anchor_b = 0, span_rows = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_anchor(store, rich_id, &anchor_kind, &anchor_a,
                                                  &anchor_b, &span_rows));
    YTEST_CHECK_EQ_INT(test, (int)span_rows, 3);

    /* The client received the rich body: one record anchored at the
     * pre-reservation cursor row. */
    YTEST_CHECK(test, yetty_ymux_client_rich_generation(client).value >= 1);
    uint32_t rich_words = 0;
    struct yetty_ycore_const_uint32_ptr_result rich_body_res =
        yetty_ymux_client_rich_body(client, &rich_words);
    YTEST_REQUIRE_OK(test, rich_body_res);
    YTEST_REQUIRE_NOT_NULL(test, rich_body_res.value);
    YTEST_CHECK(test, rich_body_res.value[0] == 0x594D5052u);                   /* rich magic */
    YTEST_CHECK_EQ_INT(test, (int)rich_body_res.value[2], 1);                   /* one record */
    YTEST_CHECK_EQ_INT(test, (int)rich_body_res.value[3 + 3], (int)row_before); /* anchor_row */

    /* The next prompt lands BELOW the figure and reaches the client. */
    application_print(test, &rig, 0, "after-figure\r\n");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "after-figure"));

    /* Second figure, LZ4F-compressed — byte-identical to what ycat really
     * emits (dcs.c always compresses). 640 px -> 40 rows @ 16: TALLER than
     * the 12-row screen, so the reservation burst coalesces into one scroll
     * exceeding the region height (the shape that crashed the daemon in
     * vt_shadow_scroll_region before the magnitude clamp). */
    uint32_t tall_container[7] = {0x31425059u /* 'YPB1' */};
    float tall_bounds[4] = {0.0f, 0.0f, 120.0f, 640.0f};
    memcpy(&tall_container[1], tall_bounds, sizeof(tall_bounds));
    uint8_t compressed[512];
    size_t compressed_len = LZ4F_compressFrame(compressed, sizeof(compressed), tall_container,
                                               sizeof(tall_container), NULL);
    YTEST_REQUIRE(test, !LZ4F_isError(compressed_len));
    struct yetty_yface_bin_meta tall_meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .raw_size = sizeof(tall_container),
    };
    struct yetty_ycore_buffer_result tall_meta_b64 =
        yetty_ycore_base64_encode(&tall_meta, sizeof(tall_meta));
    YTEST_REQUIRE_OK(test, tall_meta_b64);
    struct yetty_ycore_buffer_result tall_body_b64 =
        yetty_ycore_base64_encode(compressed, compressed_len);
    YTEST_REQUIRE_OK(test, tall_body_b64);
    char tall_envelope[1024];
    int tall_envelope_len =
        snprintf(tall_envelope, sizeof(tall_envelope), "\x1bP600001y%.*s;%.*s\x1b\\",
                 (int)tall_meta_b64.value.size, (const char *)tall_meta_b64.value.data,
                 (int)tall_body_b64.value.size, (const char *)tall_body_b64.value.data);
    yetty_ycore_buffer_destroy(&tall_meta_b64.value);
    yetty_ycore_buffer_destroy(&tall_body_b64.value);
    YTEST_REQUIRE(test, tall_envelope_len > 0);
    struct yetty_ycore_size_result tall_write_res =
        application->ops->write(application, tall_envelope, (size_t)tall_envelope_len);
    YTEST_REQUIRE_OK(test, tall_write_res);
    pump(test, daemon, client, NULL);
    /* The reservation exceeds the screen: the cursor pins at the bottom row
     * and the daemon SURVIVES the oversized coalesced scroll. */
    uint32_t row_tall = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_cursor(engine, &row_tall, &col_after, &visible));
    YTEST_CHECK_EQ_INT(test, (int)row_tall, 11);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_rich_count(store).value, 2);
    uint64_t tall_id = yetty_ymux_rich_id_at(store, 1).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_anchor(store, tall_id, &anchor_kind, &anchor_a,
                                                  &anchor_b, &span_rows));
    YTEST_CHECK_EQ_INT(test, (int)span_rows, 40);

    /* Output after the burst still projects — the session is alive. */
    application_print(test, &rig, 0, "post-tall-alive\r\n");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "post-tall-alive"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Sustained text flood (`find /`, `seq`): the daemon must keep projecting
 * WHILE the flood runs — the attached screen scrolls live, it does not
 * freeze until quiescence. Feed ~2 MB of numbered lines in bounded chunks,
 * pumping between writes, and require vtsink feeds to arrive THROUGHOUT
 * the flood, not just once at the end.
 *-------------------------------------------------------------------------*/
static void test_flood_projects_continuously(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "flood");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "flood", 30, 100));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "flood", 0, 30, 100, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-flood"));
    pump(test, daemon, client, NULL);
    int calls_at_start = capture.calls;

    /* ~2 MB of numbered lines, written in 32 KB slabs with ONE daemon/client
     * step pair between slabs — the PTY stays hot the whole time, exactly
     * like a flooding application. Track how many slabs saw at least one new
     * vtsink feed within the following few steps. */
    struct yetty_platform_pty *application = rig.application_end[0];
    YTEST_REQUIRE_NOT_NULL(test, application);
    char line[64];
    char slab[32768];
    uint32_t line_number = 0;
    int slabs = 0;
    int slabs_with_feed = 0;
    int stall_run = 0, worst_stall_run = 0;
    for (int slab_index = 0; slab_index < 64; ++slab_index) {
        size_t used = 0;
        while (used + 32 < sizeof(slab)) {
            int line_len = snprintf(line, sizeof(line), "%07u flood-line\r\n", ++line_number);
            memcpy(slab + used, line, (size_t)line_len);
            used += (size_t)line_len;
        }
        size_t written = 0;
        while (written < used) {
            struct yetty_ycore_size_result write_res =
                application->ops->write(application, slab + written, used - written);
            if (YETTY_IS_ERR(write_res)) {
                yetty_ycore_error_destroy(write_res.error);
                break;
            }
            if (write_res.value == 0) {
                /* Memory-pty full: let the daemon drain a bit. */
                struct yetty_ycore_int_result drain_res = yetty_ymux_daemon_step(daemon);
                if (YETTY_IS_ERR(drain_res)) {
                    yetty_ycore_error_destroy(drain_res.error);
                    break;
                }
                struct yetty_ycore_int_result client_res = yetty_ymux_client_step(client);
                if (YETTY_IS_ERR(client_res)) {
                    yetty_ycore_error_destroy(client_res.error);
                    break;
                }
                continue;
            }
            written += write_res.value;
        }
        int calls_before = capture.calls;
        for (int step = 0; step < 6; ++step) {
            struct yetty_ycore_int_result daemon_res = yetty_ymux_daemon_step(daemon);
            YTEST_REQUIRE_OK(test, daemon_res);
            struct yetty_ycore_int_result client_res = yetty_ymux_client_step(client);
            YTEST_REQUIRE_OK(test, client_res);
        }
        ++slabs;
        if (capture.calls > calls_before) {
            ++slabs_with_feed;
            stall_run = 0;
        } else {
            ++stall_run;
            if (stall_run > worst_stall_run) {
                worst_stall_run = stall_run;
            }
        }
    }
    pump(test, daemon, client, NULL);

    fprintf(stderr, "flood: slabs=%d with_feed=%d worst_stall=%d feeds_total=%d\n", slabs,
            slabs_with_feed, worst_stall_run, capture.calls - calls_at_start);
    /* Live scrolling: most slabs must be followed by a visible frame, and the
     * screen must never freeze for more than a handful of slabs in a row. */
    YTEST_CHECK(test, slabs_with_feed >= slabs / 2);
    YTEST_CHECK(test, worst_stall_run <= 4);
    /* And the final content arrived. */
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "flood-line"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Detach (socket death) with continued output; token reconnect resumes and
 * sees the missed content. The pane must survive the client.
 *-------------------------------------------------------------------------*/
static void test_detach_reconnect_over_socket(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "reconnect");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "live", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "live", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-live"));
    pump(test, daemon, client, NULL);
    uint32_t pane_id = yetty_ymux_client_pane_id(client).value;
    YTEST_REQUIRE(test, pane_id != 0);
    application_print(test, &rig, 0, "before\r\n");
    pump(test, daemon, client, NULL);

    /* Hard client death (dispose closes the socket — no DETACH verb). */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    pump(test, daemon, NULL, NULL);

    /* The pane survives; output continues while nobody watches. */
    application_print(test, &rig, 0, "MISSED_WHILE_AWAY\r\n");
    pump(test, daemon, NULL, NULL);

    /* Reconnect with the same token onto the same pane. */
    struct yetty_yclass_object *resumed = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, resumed);
    struct vtsink_emit_capture resumed_capture = {0};
    yetty_ymux_client_enable_vtsink(resumed, vtsink_emit_sink, &resumed_capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(resumed, "live", pane_id, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-live"));
    pump(test, daemon, resumed, NULL);
    YTEST_REQUIRE_EQ_INT(test, yetty_ymux_client_attached(resumed).value, 1);
    /* Controller again (token resume) and no second PTY was spawned. */
    uint32_t permissions = yetty_ymux_client_permissions(resumed).value;
    YTEST_CHECK(test, (permissions & YETTY_YMUX_PERMISSION_RESIZE) != 0);
    YTEST_CHECK_EQ_INT(test, rig.spawned, 1);
    YTEST_CHECK(test, vt_capture_contains(&resumed_capture.bytes, "MISSED_WHILE_AWAY"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(resumed));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Two clients on one pane: the second does not resize the canonical pane;
 * both see the same content; takeover moves control + canonical geometry.
 *-------------------------------------------------------------------------*/
static void test_two_clients_controller_policy(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "policy");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *first = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, first);
    struct vtsink_emit_capture first_capture = {0};
    yetty_ymux_client_enable_vtsink(first, vtsink_emit_sink, &first_capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(first, "policy", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(first, "policy", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-one"));
    pump(test, daemon, first, NULL);
    uint32_t pane_id = yetty_ymux_client_pane_id(first).value;

    /* The named session exists now (tmux: new-session created it). */
    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "policy").value;
    YTEST_REQUIRE_NOT_NULL(test, session);

    struct yetty_yclass_object *second = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, second);
    struct vtsink_emit_capture second_capture = {0};
    yetty_ymux_client_enable_vtsink(second, vtsink_emit_sink, &second_capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(second, "policy", pane_id, 10, 60, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-two"));
    pump(test, daemon, first, second);

    /* The second attach did NOT resize the canonical pane. */
    struct yetty_yclass_object *pane = yetty_ymux_session_pane(session, pane_id).value;
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    uint32_t rows = 0, cols = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 6);
    YTEST_CHECK_EQ_INT(test, cols, 40);
    uint32_t second_permissions = yetty_ymux_client_permissions(second).value;
    YTEST_CHECK(test, (second_permissions & YETTY_YMUX_PERMISSION_RESIZE) == 0);

    /* Both watch the same application. */
    application_print(test, &rig, 0, "shared-content\r\n");
    pump(test, daemon, first, second);
    YTEST_CHECK(test, vt_capture_contains(&first_capture.bytes, "shared-content"));
    YTEST_CHECK(test, vt_capture_contains(&second_capture.bytes, "shared-content"));

    /* Non-controller resize verb: canonical unchanged. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_resize(second, 12, 70));
    pump(test, daemon, first, second);
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, cols, 40);

    /* Presentation effects: bell + title fan out to BOTH clients;
     * clipboard (OSC 52) reaches the CONTROLLER (first) only. */
    application_print(test, &rig, 0, "\a\x1b]0;effects-title\x07");
    /* OSC 52: base64("copied") = Y29waWVk, clipboard target 'c'. */
    application_print(test, &rig, 0, "\x1b]52;c;Y29waWVk\x07");
    pump(test, daemon, first, second);
    YTEST_CHECK(test, yetty_ymux_client_bell_count(first).value >= 1);
    YTEST_CHECK(test, yetty_ymux_client_bell_count(second).value >= 1);
    char first_title[64] = {0};
    char second_title[64] = {0};
    YTEST_CHECK(test, yetty_ymux_client_title(first, first_title, sizeof(first_title)).value >= 1);
    YTEST_CHECK(test,
                yetty_ymux_client_title(second, second_title, sizeof(second_title)).value >= 1);
    YTEST_CHECK_STR_EQ(test, first_title, "effects-title");
    YTEST_CHECK_STR_EQ(test, second_title, "effects-title");
    const char *clip_text = NULL;
    size_t clip_len = 0;
    int clip_target = -1;
    uint64_t first_clip_generation =
        yetty_ymux_client_clipboard(first, &clip_text, &clip_len, &clip_target).value;
    YTEST_CHECK(test, first_clip_generation >= 1);
    YTEST_REQUIRE_NOT_NULL(test, clip_text);
    YTEST_CHECK_EQ_SIZE(test, clip_len, 6);
    YTEST_CHECK(test, memcmp(clip_text, "copied", 6) == 0);
    YTEST_CHECK_EQ_INT(test, clip_target, 1);
    uint64_t second_clip_generation = yetty_ymux_client_clipboard(second, NULL, NULL, NULL).value;
    YTEST_CHECK_EQ_INT(test, (int)second_clip_generation, 0);

    /* Takeover: second becomes controller; ITS view becomes canonical. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_takeover(second));
    pump(test, daemon, first, second);
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 12);
    YTEST_CHECK_EQ_INT(test, cols, 70);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(first));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(second));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Shutdown verb sets the daemon's exit flag.
 *-------------------------------------------------------------------------*/
static void test_shutdown_verb(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "shutdown");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_daemon_shutdown_requested(daemon).value, 0);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_shutdown_server(client));
    pump(test, daemon, client, NULL);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_daemon_shutdown_requested(daemon).value, 1);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * The live-bridge shape: session created 24x80 (CLI default), shell
 * prints, then the controller attaches at the REAL pane size (66x279 —
 * canonical resize with content), and output keeps flowing.
 *-------------------------------------------------------------------------*/
static void test_attach_bridge_geometry(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "bridgegeo");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "geo", 24, 80));
    pump(test, daemon, client, NULL);
    application_print(test, &rig, 0, "prompt$ ");
    pump(test, daemon, client, NULL);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "geo", 0, 66, 279, 23,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "big"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_EQ_INT(test, yetty_ymux_client_attached(client).value, 1);

    application_print(test, &rig, 0, "after-resize-output\r\nline2\r\nline3\r\n");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "after-resize-output"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

static void test_vtsink_lane_wire(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "vtsinkwire");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "vtsink", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "vtsink", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-vs"));
    pump(test, daemon, client, NULL);

    /* WELCOME hosted + published the sink. */
    struct yetty_yclass_object *sink = yetty_ymux_client_vtsink_object(client);
    YTEST_REQUIRE_NOT_NULL(test, sink);

    /* Establishing output arrives through feed() — a fresh COMPLETE redraw
     * (civis): nothing was projected before the lane came up. */
    application_print(test, &rig, 0, "hi");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, capture.calls > 0);
    YTEST_CHECK(test, capture.last_generation > 0);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "\x1b[?25l"));
    YTEST_CHECK(test, !vt_capture_contains(&capture.bytes, "\x1b[2J"));
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "hi"));

    /* A delta stays a delta on the lane. */
    capture.bytes.len = 0;
    uint64_t established_generation = capture.last_generation;
    application_print(test, &rig, 0, "X");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, capture.bytes.len > 0);
    YTEST_CHECK(test, !vt_capture_contains(&capture.bytes, "\x1b[2J"));
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "X"));
    YTEST_CHECK(test, capture.bytes.len < 32);
    YTEST_CHECK(test, capture.last_generation > established_generation);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #11 P0: overlay-first input routing — the consumed/unconsumed
 * decision for all three input classes, both branches each. POINTER follows
 * the hit test (a hit ON the overlay figure = consumed chrome, anything else
 * falls through); KEY and PASTE follow the overlay's input focus. Consumed
 * deliveries land in the handler seat and the per-class accounting. */
struct overlay_input_capture {
    uint32_t last_class;
    size_t last_len;
    int calls;
};

static void overlay_input_capture_handler(uint32_t input_class, const uint8_t *bytes, size_t len,
                                          void *userdata)
{
    struct overlay_input_capture *capture = userdata;
    (void)bytes;
    capture->last_class = input_class;
    capture->last_len = len;
    ++capture->calls;
}

/* Review #16: the chrome seat is LOSSLESS end-to-end and the daemon-role
 * chrome CONSUMER is real — a >512-byte paste crosses the socket complete,
 * and consumed arrow keys move the attachment's scrollback view. */
static void test_chrome_consumer_lossless_and_scroll(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "chromeuse");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture scroll_capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &scroll_capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "chromeu", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "chromeu", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-cu"));
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *
                                                                     intake_obj);
    /* LOSSLESS: a 2000-byte paste (well past every former 512/64 boundary)
     * is ACCEPTED complete — the intake counts it with its class. */
    uint8_t big_paste[2000];
    for (size_t fill = 0; fill < sizeof(big_paste); ++fill) {
        big_paste[fill] = (uint8_t)('A' + (fill % 26));
    }
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_PASTE,
                                                                big_paste, sizeof(big_paste)));
    struct yetty_ycore_int_result paste_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, paste_step);
    struct yetty_ycore_uint64_result intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, YETTY_IS_OK(intake_res) && (intake_res.value >> 8) == 1 &&
                          (intake_res.value & 0xFF) == YMUX_INPUT_CLASS_PASTE);
    /* PAYLOAD IDENTITY at the consumer (review #17): the seat retained the
     * COMPLETE consumed event — byte-for-byte. */
    struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_last_event(
        struct yetty_yclass_object * last_obj, uint8_t *out_bytes, uint32_t out_capacity);
    uint8_t seat_copy[2048];
    struct yetty_ycore_uint64_result last_res =
        yetty_ymux_daemon_chrome_last_event(daemon, seat_copy, sizeof(seat_copy));
    YTEST_REQUIRE_OK(test, last_res);
    YTEST_CHECK(test, (uint32_t)(last_res.value >> 32) == YMUX_INPUT_CLASS_PASTE);
    YTEST_CHECK(test, (uint32_t)last_res.value == sizeof(big_paste));
    YTEST_CHECK(test, memcmp(seat_copy, big_paste, sizeof(big_paste)) == 0);
    /* ACCEPTANCE ACK (review #17): the daemon acked the sequence; the
     * sender's commit point is observable. */
    struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_acked(
        struct yetty_yclass_object * acked_obj);
    pump(test, daemon, client, NULL);
    struct yetty_ycore_uint32_result acked_res = yetty_ymux_client_overlay_input_acked(client);
    YTEST_CHECK(test, YETTY_IS_OK(acked_res) && acked_res.value == 1);

    /* The REAL consumer: scrollback content + a consumed ARROW-UP key event
     * anchor the attachment one row into scrollback (follow -> anchored). */
    for (int line = 0; line < 30; ++line) {
        char text[32];
        snprintf(text, sizeof(text), "scroll-%02d", line);
        application_print(test, &rig, 0, text);
        struct yetty_ycore_int_result print_step = yetty_ymux_daemon_step(daemon);
        YTEST_REQUIRE_OK(test, print_step);
    }
    pump(test, daemon, client, NULL);
    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "chromeu").value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    struct yetty_yclass_object *attachment = yetty_ymux_session_attachment(session, 1).value;
    YTEST_REQUIRE_NOT_NULL(test, attachment);
    YTEST_CHECK(test, yetty_ymux_attachment_is_following(attachment).value == 1);
    scroll_capture.bytes.len = 0; /* isolate the SCROLLED-VIEW projection */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 2, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[A", 3));
    struct yetty_ycore_int_result key_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, key_step);
    YTEST_CHECK(test, yetty_ymux_attachment_is_following(attachment).value == 0);
    /* The ANCHORED view PROJECTS (review #17): one row into scrollback, the
     * complete redraw of the moved view must reach the client — pinned on
     * the projected bytes, not just the anchor flag. Row scroll-24 is the
     * top row of the anchored 6-row view (30 lines + prompt, 1 back). */
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&scroll_capture.bytes, "scroll-2"));
    /* COPY-MODE semantics (review #17): arrow DOWN first walks the copy
     * cursor to the bottom edge (5 moves in a 6-row view), then the edge
     * press scrolls forward — back to the live edge (follow). */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(
                               client, 3, YMUX_INPUT_CLASS_KEY,
                               (const uint8_t *)"\x1b[B\x1b[B\x1b[B\x1b[B\x1b[B\x1b[B", 18));
    struct yetty_ycore_int_result down_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, down_step);
    YTEST_CHECK(test, yetty_ymux_attachment_is_following(attachment).value == 1);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #17: the chrome consumer is a REAL copy-mode — cursor movement,
 * space anchor, enter copy (byte-exact into the daemon paste buffer), q
 * release (CHROME_RELEASE observed at the client, input claim dropped),
 * and the paste verb delivering the buffer to the pane application. */
static void test_copy_mode_chrome(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "copymode");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "copym", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "copym", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-cm"));
    pump(test, daemon, client, NULL);
    application_print(test, &rig, 0, "row0\r\nABCDEFGH");
    struct yetty_ycore_int_result feed_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, feed_step);
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint32_result yetty_ymux_daemon_paste_buffer(
        struct yetty_yclass_object * buffer_obj, uint8_t *out_bytes, uint32_t out_capacity);
    struct yetty_ycore_uint32_result yetty_ymux_client_chrome_release_count(
        struct yetty_yclass_object * release_obj);
    struct yetty_ycore_void_result yetty_ymux_client_request_paste(struct yetty_yclass_object *
                                                                   paste_obj);

    /* The embedder claimed chrome focus (scene chrome hit). */
    yetty_ymux_client_set_overlay_input_active(client, 1);
    YTEST_CHECK(test,
                yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_KEY, 0, 1) == 1);

    /* down (cursor 0,0 -> 1,0), space (anchor), right x4 (-> 1,4), enter:
     * the span (1,0)..(1,4) of "ABCDEFGH" = "ABCDE". */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(
                               client, 1, YMUX_INPUT_CLASS_KEY,
                               (const uint8_t *)"\x1b[B \x1b[C\x1b[C\x1b[C\x1b[C\r", 17));
    struct yetty_ycore_int_result copy_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, copy_step);
    uint8_t buffer_copy[64] = {0};
    struct yetty_ycore_uint32_result buffer_res =
        yetty_ymux_daemon_paste_buffer(daemon, buffer_copy, sizeof(buffer_copy));
    YTEST_REQUIRE_OK(test, buffer_res);
    YTEST_CHECK(test, buffer_res.value == 5);
    YTEST_CHECK(test, memcmp(buffer_copy, "ABCDE", 5) == 0);

    /* Reversed selection (anchor AFTER cursor) copies the same span: space
     * at (1,4), left x4 to (1,0), enter. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(
                               client, 2, YMUX_INPUT_CLASS_KEY,
                               (const uint8_t *)" \x1b[D\x1b[D\x1b[D\x1b[D\r", 14));
    struct yetty_ycore_int_result reverse_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, reverse_step);
    memset(buffer_copy, 0, sizeof(buffer_copy));
    buffer_res = yetty_ymux_daemon_paste_buffer(daemon, buffer_copy, sizeof(buffer_copy));
    YTEST_REQUIRE_OK(test, buffer_res);
    YTEST_CHECK(test, buffer_res.value == 5);
    YTEST_CHECK(test, memcmp(buffer_copy, "ABCDE", 5) == 0);

    /* q exits copy-mode: CHROME_RELEASE reaches the client and drops the
     * input claim — keys route to the application again. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 3, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"q", 1));
    struct yetty_ycore_int_result quit_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, quit_step);
    pump(test, daemon, client, NULL);
    struct yetty_ycore_uint32_result release_res = yetty_ymux_client_chrome_release_count(client);
    YTEST_REQUIRE_OK(test, release_res);
    YTEST_CHECK(test, release_res.value == 1);
    YTEST_CHECK(test,
                yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_KEY, 0, 1) == 0);

    /* The paste verb delivers the buffer to the pane APPLICATION. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_request_paste(client));
    struct yetty_ycore_int_result paste_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, paste_step);
    pump(test, daemon, client, NULL);
    char pasted[64] = {0};
    struct yetty_platform_pty *application = rig.application_end[0];
    struct yetty_ycore_size_result pasted_read =
        application->ops->read(application, pasted, sizeof(pasted));
    YTEST_REQUIRE_OK(test, pasted_read);
    YTEST_CHECK_EQ_SIZE(test, pasted_read.value, 5);
    YTEST_CHECK(test, memcmp(pasted, "ABCDE", 5) == 0);

    /* ---- Review #19 copy-mode legs ---- */
    uint8_t buffer_again[128] = {0};

    /* SPLIT sequences + SS3 + modified arrows (streaming decoder): \e[ and
     * B in SEPARATE overlay frames still move down once; \eOC moves right;
     * \e[1;5D (ctrl-left) moves left. Net: cursor (1,0) -> space, right x2,
     * enter -> "ABC". */
    yetty_ymux_client_set_overlay_input_active(client, 1);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 10, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[", 2));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 11, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"B", 1));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 12, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1bOC", 3));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 13, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[1;5D", 6));
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_send_overlay_input(client, 14, YMUX_INPUT_CLASS_KEY,
                                                          (const uint8_t *)" \x1b[C\x1b[C\r", 8));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    buffer_res = yetty_ymux_daemon_paste_buffer(daemon, buffer_again, sizeof(buffer_again));
    YTEST_REQUIRE_OK(test, buffer_res);
    YTEST_CHECK_EQ_INT(test, (int)buffer_res.value, 3);
    YTEST_CHECK(test, memcmp(buffer_again, "ABC", 3) == 0);

    /* TRAILING-BLANK normalization: selecting "row0" plus ten trailing
     * blanks copies exactly "row0". (q resets the cursor to (0,0).) */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 15, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"q", 1));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    yetty_ymux_client_set_overlay_input_active(client, 1);
    YTEST_REQUIRE_OK(
        test,
        yetty_ymux_client_send_overlay_input(
            client, 16, YMUX_INPUT_CLASS_KEY,
            (const uint8_t *)" \x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\r", 29));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    memset(buffer_again, 0, sizeof(buffer_again));
    buffer_res = yetty_ymux_daemon_paste_buffer(daemon, buffer_again, sizeof(buffer_again));
    YTEST_REQUIRE_OK(test, buffer_res);
    YTEST_CHECK_EQ_INT(test, (int)buffer_res.value, 4);
    YTEST_CHECK(test, memcmp(buffer_again, "row0", 4) == 0);

    /* WIDE + COMBINING clusters: "W宽X" then e+U+0301 — the wide
     * continuation cell injects NO space and the combining mark rides its
     * base. Print on a fresh row, select it, compare the exact bytes. */
    application_print(test, &rig, 0, "\r\nW\xE5\xAE\xBDXe\xCC\x81");
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 17, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"q", 1));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    yetty_ymux_client_set_overlay_input_active(client, 1);
    /* Cursor (0,0) -> down x2 to the fresh row, select cols 0..4. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(
                               client, 18, YMUX_INPUT_CLASS_KEY,
                               (const uint8_t *)"\x1b[B\x1b[B \x1b[C\x1b[C\x1b[C\x1b[C\r", 20));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    memset(buffer_again, 0, sizeof(buffer_again));
    buffer_res = yetty_ymux_daemon_paste_buffer(daemon, buffer_again, sizeof(buffer_again));
    YTEST_REQUIRE_OK(test, buffer_res);
    YTEST_CHECK_EQ_INT(test, (int)buffer_res.value, 8);
    YTEST_CHECK(test, memcmp(buffer_again, "W\xE5\xAE\xBDXe\xCC\x81", 8) == 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #19: a selection past the retired 8 KiB fixed buffer copies
 * COMPLETE — the paste buffer grows (16 MiB cap) instead of silently
 * stopping mid-selection. 50x200 screen of 'X' = 10049 bytes with the
 * newlines. */
static void test_copy_mode_large_selection(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "copybig");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "copybig", 50, 200));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "copybig", 0, 50, 200, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-big"));
    pump(test, daemon, client, NULL);

    char full_line[204];
    memset(full_line, 'X', 200);
    full_line[200] = '\r';
    full_line[201] = '\n';
    full_line[202] = 0;
    for (int line_index = 0; line_index < 49; ++line_index) {
        application_print(test, &rig, 0, full_line);
        if ((line_index % 8) == 7) {
            pump(test, daemon, client, NULL);
        }
    }
    full_line[200] = 0; /* last row without the newline — no scroll past it */
    application_print(test, &rig, 0, full_line);
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint32_result yetty_ymux_daemon_paste_buffer(
        struct yetty_yclass_object * buffer_obj, uint8_t *out_bytes, uint32_t out_capacity);
    yetty_ymux_client_set_overlay_input_active(client, 1);

    /* Anchor at (0,0), march to (49,199), enter. */
    uint8_t moves[49 * 3 + 199 * 3 + 8];
    size_t move_len = 0;
    moves[move_len++] = ' ';
    for (int down = 0; down < 49; ++down) {
        moves[move_len++] = 0x1b;
        moves[move_len++] = '[';
        moves[move_len++] = 'B';
    }
    for (int right = 0; right < 199; ++right) {
        moves[move_len++] = 0x1b;
        moves[move_len++] = '[';
        moves[move_len++] = 'C';
    }
    moves[move_len++] = '\r';
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_KEY,
                                                                moves, (uint32_t)move_len));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));

    uint8_t *copied = malloc(16384);
    YTEST_REQUIRE_NOT_NULL(test, copied);
    struct yetty_ycore_uint32_result copied_res =
        yetty_ymux_daemon_paste_buffer(daemon, copied, 16384);
    YTEST_REQUIRE_OK(test, copied_res);
    fprintf(stderr, "copy-large: %u bytes\n", copied_res.value);
    YTEST_CHECK_EQ_INT(test, (int)copied_res.value, 50 * 200 + 49);
    YTEST_CHECK(test, copied[0] == 'X');
    YTEST_CHECK(test, copied[copied_res.value - 1] == 'X');
    YTEST_CHECK(test, copied[200] == '\n'); /* first row boundary intact */
    free(copied);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #21: a copy that hit the paste-buffer cap keeps only a prefix. The
 * PASTE verb must REFUSE it with an explicit truncation report — a partial
 * paste reported as "pasted N byte(s)" success is data loss the user cannot
 * see. Uses the force-truncated seam so the leg does not need a >16 MiB
 * selection. */
static void test_paste_truncation_refused(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "copytrunc");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "copytrunc", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "copytrunc", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-tr"));
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_daemon_force_paste_truncated(
        struct yetty_yclass_object * seam_obj);
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_force_paste_truncated(daemon));

    const char *reply_text = NULL;
    uint32_t reply_status = 0;
    uint64_t reply_before =
        yetty_ymux_client_session_reply(client, &reply_text, &reply_status).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_client_request_paste(client));
    for (int spin = 0; spin < 50; ++spin) {
        pump(test, daemon, client, NULL);
        uint64_t reply_now =
            yetty_ymux_client_session_reply(client, &reply_text, &reply_status).value;
        if (reply_now != reply_before) {
            break;
        }
    }
    YTEST_CHECK_EQ_INT(test, (int)reply_status, 1); /* refused, not success */
    YTEST_REQUIRE_NOT_NULL(test, (void *)reply_text);
    fprintf(stderr, "paste-trunc reply: %s\n", reply_text);
    YTEST_CHECK(test, strstr(reply_text, "truncated") != NULL);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Cycle-22 P0: a reconnect is a FRESH PROCESS (attach_takeover exits on
 * disconnect — there is no in-process resume), so its overlay events are NEW,
 * not replays of the dead connection's. The daemon dedup is PER-CONNECTION,
 * keyed on the live connection, NOT a session/USER-token watermark — so a
 * brand-new connection's seq 1 must APPLY, never be discarded as a duplicate
 * of some earlier connection's seq 1. (The earlier cycle-21 session-token
 * watermark aliased all same-user clients and dropped legitimate events.) */
static void test_overlay_fresh_connection_applies(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "reconnseq");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "reconnseq", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "reconnseq", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-rc"));
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *
                                                                     intake_obj);
    struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_acked(
        struct yetty_yclass_object * acked_obj);

    /* Apply seq 1 on the FIRST connection, then drop it before the ACK. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"pasted", 6));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    struct yetty_ycore_uint64_result intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 1);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));

    /* A brand-new process reconnects (same USER token) and sends seq 1 — a
     * NEW event from a NEW attachment. It must APPLY (intake rises), not be
     * silently swallowed as a below-watermark duplicate. */
    struct yetty_yclass_object *reclient = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, reclient);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(reclient, "reconnseq", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-rc"));
    pump(test, daemon, reclient, NULL);
    uint64_t before = yetty_ymux_daemon_chrome_intake(daemon).value >> 8;
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(reclient, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"fresh!", 6));
    pump(test, daemon, reclient, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(reclient).value, 1);
    uint64_t after = yetty_ymux_daemon_chrome_intake(daemon).value >> 8;
    YTEST_CHECK(test, after == before + 1); /* APPLIED, not discarded */

    /* Same-connection lost-ACK resend of seq 1 is still deduped (per-connection
     * watermark): a resend does NOT double-apply. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(reclient, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"fresh!", 6));
    pump(test, daemon, reclient, NULL);
    uint64_t after_resend = yetty_ymux_daemon_chrome_intake(daemon).value >> 8;
    YTEST_CHECK(test, after_resend == after); /* deduped, not re-applied */

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(reclient));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Cycle-22 P0: two CONCURRENT clients from the same OS user (same attach
 * token) each send overlay seq 1 on their OWN connection. Both must apply —
 * the per-connection dedup must NOT alias them into one sequence namespace
 * (the cycle-21 session-token watermark dropped the second as a duplicate). */
static void test_overlay_concurrent_same_user_no_alias(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "concurseq");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *
                                                                     intake_obj);

    struct yetty_yclass_object *client_a = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client_a);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client_a, "concurseq", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client_a, "concurseq", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "same-user"));
    pump(test, daemon, client_a, NULL);
    /* Second client, SAME token, attaching to the same session. */
    struct yetty_yclass_object *client_b = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client_b);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client_b, "concurseq", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "same-user"));
    pump(test, daemon, client_b, NULL);

    uint64_t before = yetty_ymux_daemon_chrome_intake(daemon).value >> 8;
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client_a, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"fromA!", 6));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client_b, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"fromB!", 6));
    pump(test, daemon, client_a, NULL);
    pump(test, daemon, client_b, NULL);
    uint64_t after = yetty_ymux_daemon_chrome_intake(daemon).value >> 8;
    /* BOTH seq-1 events applied — no cross-client aliasing. */
    YTEST_CHECK(test, after == before + 2);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client_a));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client_b));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #19: copy-mode copies the DISPLAYED timeline view. A row that
 * scrolled into history is selected after anchoring the view back to it —
 * the buffer must carry the HISTORY row, not the same-numbered live row. */
static void test_copy_mode_history_view(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "copyhist");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "copyhist", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "copyhist", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-ch"));
    pump(test, daemon, client, NULL);
    /* HISTMARK, then enough rows to push it off the 6-row screen. The live
     * screen's row 0 is "f2" (or later) — copying displayed row 0 without
     * view resolution would copy that, not HISTMARK. */
    application_print(test, &rig, 0, "HISTMARK\r\nf1\r\nf2\r\nf3\r\nf4\r\nf5\r\nf6\r\nf7");
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint32_result yetty_ymux_daemon_paste_buffer(
        struct yetty_yclass_object * buffer_obj, uint8_t *out_bytes, uint32_t out_capacity);
    yetty_ymux_client_set_overlay_input_active(client, 1);

    /* At the top edge every Up scrolls the VIEW back one timeline row.
     * 8 rows printed on a 6-row screen -> view_top(live) = 2; two Ups
     * anchor the view at row 0 = HISTMARK. Select cols 0..7 and copy. */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_KEY,
                                                          (const uint8_t *)"\x1b[A\x1b[A", 6));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_send_overlay_input(
                         client, 2, YMUX_INPUT_CLASS_KEY,
                         (const uint8_t *)" \x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\x1b[C\r", 23));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_step(daemon));
    uint8_t history_copy[64] = {0};
    struct yetty_ycore_uint32_result history_res =
        yetty_ymux_daemon_paste_buffer(daemon, history_copy, sizeof(history_copy));
    YTEST_REQUIRE_OK(test, history_res);
    fprintf(stderr, "copy-history: got %u bytes '%.*s'\n", history_res.value,
            (int)history_res.value, history_copy);
    YTEST_CHECK_EQ_INT(test, (int)history_res.value, 8);
    YTEST_CHECK(test, memcmp(history_copy, "HISTMARK", 8) == 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #17 item 8: the ATTACH terminal-strings tail resolves the
 * capability profile through the tmux terminfo/features state model —
 * TERM=screen (no ECH, no 256) with no features yields a profile whose
 * capability mirror drops ECH and TRUECOLOR; TERM=xterm-256color with
 * "256,RGB,hyperlinks,usstyle" turns the exotic bits ON. */
static void test_attach_terminal_state_model(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "termmodel");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct yetty_ycore_void_result yetty_ymux_client_set_terminal(
        struct yetty_yclass_object * term_obj, const char *term_name, const char *features);
    struct yetty_ycore_uint32_result yetty_ymux_projector_capabilities(struct yetty_yclass_object *
                                                                       caps_obj);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "tmodel", 6, 40));

    /* screen: base terminfo has no ECH and no 256 colours. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_set_terminal(client, "screen", ""));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(
                               client, "tmodel", 0, 6, 40, 16,
                               YMUX_TERM_CAPS_XTERM_256COLOR | YMUX_TERM_CAP_VT_TEXT, "tok-tm1"));
    pump(test, daemon, client, NULL);
    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "tmodel").value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    struct yetty_yclass_object *projector = yetty_ymux_session_projector(session, 1).value;
    YTEST_REQUIRE_NOT_NULL(test, projector);
    struct yetty_ycore_uint32_result caps_res = yetty_ymux_projector_capabilities(projector);
    YTEST_REQUIRE_OK(test, caps_res);
    YTEST_CHECK(test, (caps_res.value & YMUX_TERM_CAP_ECH) == 0);
    YTEST_CHECK(test, (caps_res.value & YMUX_TERM_CAP_TRUECOLOR) == 0);
    YTEST_CHECK(test, (caps_res.value & YMUX_TERM_CAP_DECSTBM) != 0);
    YTEST_CHECK(test, (caps_res.value & YMUX_TERM_CAP_VT_TEXT) != 0);

    /* Re-attach as a modern xterm with the exotic feature set. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_set_terminal(client, "xterm-256color",
                                                          "256,RGB,hyperlinks,usstyle"));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(
                               client, "tmodel", 0, 6, 40, 16,
                               YMUX_TERM_CAPS_XTERM_256COLOR | YMUX_TERM_CAP_VT_TEXT, "tok-tm2"));
    pump(test, daemon, client, NULL);
    struct yetty_yclass_object *projector2 = yetty_ymux_session_projector(session, 2).value;
    YTEST_REQUIRE_NOT_NULL(test, projector2);
    struct yetty_ycore_uint32_result caps2_res = yetty_ymux_projector_capabilities(projector2);
    YTEST_REQUIRE_OK(test, caps2_res);
    YTEST_CHECK(test, (caps2_res.value & YMUX_TERM_CAP_TRUECOLOR) != 0);
    YTEST_CHECK(test, (caps2_res.value & YMUX_TERM_CAP_ECH) != 0);
    YTEST_CHECK(test, (caps2_res.value & YMUX_TERM_CAP_HYPERLINK) != 0);
    YTEST_CHECK(test, (caps2_res.value & YMUX_TERM_CAP_EXTENDED_UNDERLINE) != 0);
    YTEST_CHECK(test, (caps2_res.value & YMUX_TERM_CAP_UNDERLINE_COLOUR) != 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #16: RAW terminal responses from a renderer are consumed by THAT
 * attachment's response parser — never by the pane application, and never
 * cross-attachment. Two clients attach; each sends its own CPR/DA; each
 * projector records its own; the application PTY receives ZERO bytes. */
static void test_tty_response_per_attachment(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "ttyresp");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *first = yetty_ymux_client_make(path).value;
    struct yetty_yclass_object *second = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, first);
    YTEST_REQUIRE_NOT_NULL(test, second);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(first, "resp", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(first, "resp", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-r1"));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(second, "resp", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-r2"));
    pump(test, daemon, first, second);

    struct yetty_ycore_void_result yetty_ymux_client_send_tty_response(
        struct yetty_yclass_object * send_obj, const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint64_result yetty_ymux_projector_response_state(
        struct yetty_yclass_object * state_obj);
    /* Client 1: a CPR split across two frames + a primary DA. */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_send_tty_response(first, (const uint8_t *)"\x1b[7;", 4));
    YTEST_REQUIRE_OK(
        test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)"12R\x1b[?64;1;9c", 13));
    /* Client 2: a different CPR. */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_send_tty_response(second, (const uint8_t *)"\x1b[3;4R", 6));
    pump(test, daemon, first, second);

    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "resp").value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    struct yetty_yclass_object *projector_one = yetty_ymux_session_projector(session, 1).value;
    struct yetty_yclass_object *projector_two = yetty_ymux_session_projector(session, 2).value;
    YTEST_REQUIRE_NOT_NULL(test, projector_one);
    YTEST_REQUIRE_NOT_NULL(test, projector_two);
    struct yetty_ycore_uint64_result state_one = yetty_ymux_projector_response_state(projector_one);
    struct yetty_ycore_uint64_result state_two = yetty_ymux_projector_response_state(projector_two);
    YTEST_REQUIRE_OK(test, state_one);
    YTEST_REQUIRE_OK(test, state_two);
    /* Attachment 1: 1 DA, 1 CPR at 7;12 (split across frames). */
    YTEST_CHECK(test, (state_one.value >> 48) == 1);
    YTEST_CHECK(test, ((state_one.value >> 32) & 0xFFFF) == 1);
    YTEST_CHECK(test, ((state_one.value >> 16) & 0xFFFF) == 7);
    YTEST_CHECK(test, (state_one.value & 0xFFFF) == 12);
    /* STREAMING state machine (review #17): a large fragmented XTGETTCAP
     * DCS reply (hex Smulx=\\E[4::%p1%dm), a DA2, and an OSC-11 color
     * reply — split at adversarial boundaries — all classify. */
    struct yetty_ycore_uint32_result yetty_ymux_projector_response_cap(
        struct yetty_yclass_object * cap_obj, uint32_t cap_index, char *out_text,
        uint32_t out_capacity);
    {
        /* XTGETTCAP reply: DCS 1 + r 536d756c78=5c45... ST — name "Smulx". */
        const char *dcs_head = "\x1bP1+r536d756c78=";
        YTEST_REQUIRE_OK(test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)dcs_head,
                                                                   (uint32_t)strlen(dcs_head)));
        /* value hex for "\\E[4::m" = 5c 45 5b 34 3a 3a 6d — split MID-BYTE-PAIR. */
        YTEST_REQUIRE_OK(
            test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)"5c455b34", 8));
        YTEST_REQUIRE_OK(
            test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)"3a3a6d\x1b", 7));
        YTEST_REQUIRE_OK(test,
                         yetty_ymux_client_send_tty_response(first, (const uint8_t *)"\\", 1));
        /* DA2 + OSC 11 color reply (BEL-terminated), one frame. */
        const char *tail = "\x1b[>41;354;0c\x1b]11;rgb:0b0b/1010/1414\x07";
        YTEST_REQUIRE_OK(test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)tail,
                                                                   (uint32_t)strlen(tail)));
        pump(test, daemon, first, second);
        char cap_text[80] = {0};
        struct yetty_ycore_uint32_result caps_res =
            yetty_ymux_projector_response_cap(projector_one, 0, cap_text, sizeof(cap_text));
        YTEST_REQUIRE_OK(test, caps_res);
        YTEST_CHECK(test, caps_res.value == 1);
        YTEST_CHECK(test, strcmp(cap_text, "Smulx=\\E[4::m") == 0);
    }

    /* THEME report (review #19): the ?996n query's ?997;1n answer — split
     * across frames — records scheme=dark on the projector as
     * response-DRIVEN state. */
    {
        int yetty_ymux_projector_theme_scheme(struct yetty_yclass_object * theme_obj);
        YTEST_CHECK_EQ_INT(test, yetty_ymux_projector_theme_scheme(projector_one), 0);
        YTEST_REQUIRE_OK(
            test, yetty_ymux_client_send_tty_response(first, (const uint8_t *)"\x1b[?99", 5));
        YTEST_REQUIRE_OK(test,
                         yetty_ymux_client_send_tty_response(first, (const uint8_t *)"7;1n", 4));
        pump(test, daemon, first, second);
        YTEST_CHECK_EQ_INT(test, yetty_ymux_projector_theme_scheme(projector_one), 1);
        YTEST_CHECK_EQ_INT(test, yetty_ymux_projector_theme_scheme(projector_two), 0);
    }

    /* SYNC cap transition (review #19): a DECRPM ?2026 report DRIVES the
     * attachment's capability profile — recognized turns the bit on, an
     * unsupported report turns it off. */
    {
        struct yetty_ycore_uint32_result yetty_ymux_projector_capabilities(
            struct yetty_yclass_object * caps_obj);
        uint32_t caps_before = yetty_ymux_projector_capabilities(projector_one).value;
        YTEST_CHECK(test, (caps_before & YMUX_TERM_CAP_SYNC) == 0);
        YTEST_REQUIRE_OK(test, yetty_ymux_client_send_tty_response(
                                   first, (const uint8_t *)"\x1b[?2026;1$y", 11));
        pump(test, daemon, first, second);
        uint32_t caps_on = yetty_ymux_projector_capabilities(projector_one).value;
        YTEST_CHECK(test, (caps_on & YMUX_TERM_CAP_SYNC) != 0);
        YTEST_REQUIRE_OK(test, yetty_ymux_client_send_tty_response(
                                   first, (const uint8_t *)"\x1b[?2026;0$y", 11));
        pump(test, daemon, first, second);
        uint32_t caps_off = yetty_ymux_projector_capabilities(projector_one).value;
        YTEST_CHECK(test, (caps_off & YMUX_TERM_CAP_SYNC) == 0);
    }

    /* LOSSLESS XTGETTCAP (review #19): a 60-char value — past the retired
     * 47-byte fixed slot — stores and reads back COMPLETE. */
    {
        char long_value[61];
        memset(long_value, 'V', 60);
        long_value[60] = 0;
        char hex_frame[256];
        size_t hex_offset = 0;
        hex_offset += (size_t)snprintf(hex_frame + hex_offset, sizeof(hex_frame) - hex_offset,
                                       "\x1bP1+r53796e63="); /* "Sync"= */
        for (int nibble_index = 0; nibble_index < 60; ++nibble_index) {
            hex_offset += (size_t)snprintf(hex_frame + hex_offset, sizeof(hex_frame) - hex_offset,
                                           "%02x", 'V');
        }
        hex_offset +=
            (size_t)snprintf(hex_frame + hex_offset, sizeof(hex_frame) - hex_offset, "\x1b\\");
        YTEST_REQUIRE_OK(test, yetty_ymux_client_send_tty_response(
                                   first, (const uint8_t *)hex_frame, (uint32_t)hex_offset));
        pump(test, daemon, first, second);
        char cap_read[128] = {0};
        struct yetty_ycore_uint32_result cap_count_res =
            yetty_ymux_projector_response_cap(projector_one, 1, cap_read, sizeof(cap_read));
        YTEST_REQUIRE_OK(test, cap_count_res);
        YTEST_CHECK(test, cap_count_res.value == 2);
        char expected_cap[80];
        snprintf(expected_cap, sizeof(expected_cap), "Sync=%s", long_value);
        YTEST_CHECK(test, strcmp(cap_read, expected_cap) == 0);
    }

    /* Attachment 2: its OWN CPR only — no cross-talk. */
    YTEST_CHECK(test, (state_two.value >> 48) == 0);
    YTEST_CHECK(test, ((state_two.value >> 32) & 0xFFFF) == 1);
    YTEST_CHECK(test, ((state_two.value >> 16) & 0xFFFF) == 3);
    YTEST_CHECK(test, (state_two.value & 0xFFFF) == 4);
    /* The pane APPLICATION received nothing from any response. */
    char leak_buf[64] = {0};
    struct yetty_platform_pty *application = rig.application_end[0];
    struct yetty_ycore_size_result leak_read =
        application->ops->read(application, leak_buf, sizeof(leak_buf));
    YTEST_CHECK(test, YETTY_IS_OK(leak_read) && leak_read.value == 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(second));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(first));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #15: the overlay CONSUMER route — a consumed overlay event the
 * bridge drained from the scene queue crosses the wire (OVERLAY_INPUT) and
 * lands at the daemon's chrome seat, where the chrome owner (the daemon)
 * consumes. */
static void test_overlay_input_reaches_chrome_seat(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "chromeseat");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "seat", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "seat", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-cs"));
    pump(test, daemon, client, NULL);

    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *
                                                                     intake_obj);
    struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_acked(
        struct yetty_yclass_object * acked_obj);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"pasted", 6));
    struct yetty_ycore_int_result step_res = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, step_res);
    struct yetty_ycore_uint64_result intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, YETTY_IS_OK(intake_res));
    YTEST_CHECK(test, (intake_res.value >> 8) == 1);
    YTEST_CHECK(test, (intake_res.value & 0xFF) == YMUX_INPUT_CLASS_PASTE);
    pump(test, daemon, client, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 1);

    /* DUPLICATE replay (review #19, the lost-ACK retry): the SAME sequence
     * re-ACKs but must NOT re-apply — the intake count stays at 1. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_PASTE,
                                                                (const uint8_t *)"pasted", 6));
    pump(test, daemon, client, NULL);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 1);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 1);

    /* STALE sequence (0-or-lower replay after progress): apply seq 2, then
     * replay seq 1 — seq 2 stays the applied high-water, intake stays 2. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 2, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[A", 3));
    pump(test, daemon, client, NULL);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 2);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 2);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 1, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[A", 3));
    pump(test, daemon, client, NULL);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 2); /* not re-applied */
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 2);

    /* Ordering across a replay: the next FRESH sequence still applies. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 3, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[B", 3));
    pump(test, daemon, client, NULL);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 3);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 3);

    /* REFUSAL (review #19 negative path): the seam NACKs the next event —
     * the client records the refused sequence with its reason, NOTHING is
     * applied, and the SAME sequence resent afterwards applies cleanly. */
    struct yetty_ycore_void_result yetty_ymux_daemon_refuse_next_overlay(
        struct yetty_yclass_object * refuse_obj, uint32_t refuse_count);
    struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_nacked(
        struct yetty_yclass_object * nacked_obj);
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_refuse_next_overlay(daemon, 1));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 4, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[A", 3));
    pump(test, daemon, client, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_nacked(client).value, 4);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 3); /* refused event NOT applied */
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 3);
    /* The retained event resends with the SAME sequence and now applies. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_send_overlay_input(client, 4, YMUX_INPUT_CLASS_KEY,
                                                                (const uint8_t *)"\x1b[A", 3));
    pump(test, daemon, client, NULL);
    intake_res = yetty_ymux_daemon_chrome_intake(daemon);
    YTEST_CHECK(test, (intake_res.value >> 8) == 4);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_overlay_input_acked(client).value, 4);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Cross-chunk bracketed-paste classification (review #14): the client's
 * classifier persists paste state AND a partial delimiter across raw
 * flushes — a \e[200~/\e[201~ split anywhere must still classify the span
 * as PASTE and everything else as KEY, in order. */
struct classify_capture {
    struct {
        uint32_t input_class;
        uint8_t bytes[64];
        size_t len;
    } runs[16];
    uint32_t count;
};

static void classify_capture_emit(uint32_t input_class, const uint8_t *bytes, size_t len,
                                  void *userdata)
{
    struct classify_capture *capture = userdata;
    if (capture->count >= 16) {
        return;
    }
    capture->runs[capture->count].input_class = input_class;
    capture->runs[capture->count].len = len;
    memcpy(capture->runs[capture->count].bytes, bytes, len < 64 ? len : 64);
    ++capture->count;
}

static void test_input_classifier_cross_chunk(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "classify");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    void yetty_ymux_client_overlay_classify_input(
        struct yetty_yclass_object * classify_obj, const uint8_t *classify_bytes,
        size_t classify_len,
        void (*classify_emit)(uint32_t input_class, const uint8_t *bytes, size_t len,
                              void *userdata),
        void *classify_userdata);

    struct classify_capture capture = {0};
    /* Open delimiter split after 3 bytes: "ab" + "\x1b[2" || "00~p1" ... */
    yetty_ymux_client_overlay_classify_input(client, (const uint8_t *)"ab\x1b[2", 5,
                                             classify_capture_emit, &capture);
    YTEST_CHECK(test, capture.count == 1);
    YTEST_CHECK(test, capture.runs[0].input_class == YMUX_INPUT_CLASS_KEY);
    YTEST_CHECK(test, capture.runs[0].len == 2 && capture.runs[0].bytes[0] == 'a');
    /* The completed delimiter + paste body; close delimiter split at its
     * LAST byte. */
    yetty_ymux_client_overlay_classify_input(client, (const uint8_t *)"00~pq\x1b[201", 10,
                                             classify_capture_emit, &capture);
    /* Emitted: the reconstructed open delimiter (PASTE), then the body run
     * (PASTE) up to the held-back close prefix. */
    YTEST_CHECK(test, capture.count == 3);
    YTEST_CHECK(test, capture.runs[1].input_class == YMUX_INPUT_CLASS_PASTE);
    YTEST_CHECK(test, capture.runs[1].len == 6);
    YTEST_CHECK(test, capture.runs[2].input_class == YMUX_INPUT_CLASS_PASTE);
    YTEST_CHECK(test, capture.runs[2].len == 2 && capture.runs[2].bytes[0] == 'p');
    /* The final byte closes the span; trailing keys classify as KEY again. */
    yetty_ymux_client_overlay_classify_input(client, (const uint8_t *)"~xy", 3,
                                             classify_capture_emit, &capture);
    YTEST_CHECK(test, capture.count == 5);
    YTEST_CHECK(test, capture.runs[3].input_class == YMUX_INPUT_CLASS_PASTE);
    YTEST_CHECK(test, capture.runs[3].len == 6);
    YTEST_CHECK(test, capture.runs[4].input_class == YMUX_INPUT_CLASS_KEY);
    YTEST_CHECK(test, capture.runs[4].len == 2 && capture.runs[4].bytes[0] == 'x');
    /* A held-back candidate that turns out NOT to be a delimiter flushes as
     * ordinary keys, in order. */
    yetty_ymux_client_overlay_classify_input(client, (const uint8_t *)"\x1b[20", 4,
                                             classify_capture_emit, &capture);
    YTEST_CHECK(test, capture.count == 5); /* held back — nothing emitted yet */
    yetty_ymux_client_overlay_classify_input(client, (const uint8_t *)"Xtail", 5,
                                             classify_capture_emit, &capture);
    YTEST_CHECK(test, capture.count == 7);
    YTEST_CHECK(test, capture.runs[5].input_class == YMUX_INPUT_CLASS_KEY);
    YTEST_CHECK(test, capture.runs[5].len == 4); /* the flushed \e[20 carry */
    YTEST_CHECK(test, capture.runs[6].input_class == YMUX_INPUT_CLASS_KEY);
    YTEST_CHECK(test, capture.runs[6].len == 5);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
}

/* Exact retention footprint of one run — mirrors the bridge's
 * attach_overlay_record_bytes (data + 8-byte header per <=48 KiB piece). */
static size_t overlay_record_bytes_probe(size_t len)
{
    enum { PIECE = 48u * 1024u };
    size_t records = len ? (len + PIECE - 1) / PIECE : 1;
    return len + 8 * records;
}

struct overlay_measure_probe {
    size_t total;
    uint32_t count;
};

static void overlay_measure_probe_emit(uint32_t input_class, const uint8_t *bytes, size_t len,
                                       void *userdata)
{
    (void)input_class;
    (void)bytes;
    struct overlay_measure_probe *probe = userdata;
    probe->total += overlay_record_bytes_probe(len);
    ++probe->count;
}

/* Cycle-25 P0: the bridge reserves retention capacity from a MEASURE pass over
 * the classifier's runs, then commits the source and retains. This proves the
 * two invariants that make that safe: (1) an adversarial chunk of many
 * alternating KEY/PASTE runs produces O(input) records — FAR more than any fixed
 * run-count estimate — and (2) the classifier save/restore makes the measure
 * pass state-neutral, so the retain pass reproduces byte-identical runs and the
 * measured footprint EQUALS the retained footprint exactly. */
static void test_overlay_measure_matches_retain(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "measure");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    void yetty_ymux_client_overlay_classify_input(
        struct yetty_yclass_object * classify_obj, const uint8_t *classify_bytes,
        size_t classify_len,
        void (*classify_emit)(uint32_t input_class, const uint8_t *bytes, size_t len,
                              void *userdata),
        void *classify_userdata);
    void yetty_ymux_client_overlay_classify_save(struct yetty_yclass_object * save_obj,
                                                 uint8_t *carry_out, uint32_t *carry_len_out,
                                                 int *paste_open_out);
    void yetty_ymux_client_overlay_classify_restore(struct yetty_yclass_object * restore_obj,
                                                    const uint8_t *carry, uint32_t carry_len,
                                                    int paste_open);

    /* Adversarial chunk: 64 "key + paste-open + key + paste-close" groups. Each
     * bracketed-paste toggle forces a run boundary — O(input) runs, well past
     * the former 32-run estimate. */
    uint8_t chunk[64 * 16];
    size_t chunk_len = 0;
    for (int group = 0; group < 64; ++group) {
        chunk[chunk_len++] = 'k';
        memcpy(chunk + chunk_len, "\x1b[200~", 6);
        chunk_len += 6;
        chunk[chunk_len++] = 'p';
        memcpy(chunk + chunk_len, "\x1b[201~", 6);
        chunk_len += 6;
    }

    /* Snapshot the classifier, run the MEASURE pass, restore. */
    uint8_t saved_carry[8];
    uint32_t saved_carry_len = 0;
    int saved_paste_open = 0;
    yetty_ymux_client_overlay_classify_save(client, saved_carry, &saved_carry_len,
                                            &saved_paste_open);
    struct overlay_measure_probe measure = {0};
    yetty_ymux_client_overlay_classify_input(client, chunk, chunk_len, overlay_measure_probe_emit,
                                             &measure);
    yetty_ymux_client_overlay_classify_restore(client, saved_carry, saved_carry_len,
                                               saved_paste_open);

    /* State is restored: a second snapshot equals the first. */
    uint8_t saved_carry2[8];
    uint32_t saved_carry_len2 = 0;
    int saved_paste_open2 = 0;
    yetty_ymux_client_overlay_classify_save(client, saved_carry2, &saved_carry_len2,
                                            &saved_paste_open2);
    YTEST_CHECK(test, saved_carry_len2 == saved_carry_len);
    YTEST_CHECK(test, saved_paste_open2 == saved_paste_open);

    /* The RETAIN pass reproduces the identical run set from the restored state. */
    struct overlay_measure_probe retain = {0};
    yetty_ymux_client_overlay_classify_input(client, chunk, chunk_len, overlay_measure_probe_emit,
                                             &retain);

    YTEST_CHECK(test, measure.count > 32);            /* O(input) runs, not ~32 */
    YTEST_CHECK(test, measure.count == retain.count); /* reproducible */
    YTEST_CHECK(test, measure.total == retain.total); /* EXACT reservation */

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
}

static void test_overlay_input_routing(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "ovroute");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    enum { OVERLAY_ID = 7002, CONTENT_ID = 7001 };

    /* POINTER: consumed exactly on the overlay figure, both branches. */
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_POINTER,
                                                            OVERLAY_ID, OVERLAY_ID) == 1);
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_POINTER,
                                                            CONTENT_ID, OVERLAY_ID) == 0);

    /* KEY + PASTE: follow overlay input focus, both branches. */
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_KEY,
                                                            OVERLAY_ID, OVERLAY_ID) == 0);
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_PASTE, 0,
                                                            OVERLAY_ID) == 0);
    yetty_ymux_client_set_overlay_input_active(client, 1);
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_KEY,
                                                            OVERLAY_ID, OVERLAY_ID) == 1);
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_PASTE, 0,
                                                            OVERLAY_ID) == 1);
    yetty_ymux_client_set_overlay_input_active(client, 0);
    YTEST_CHECK(test, yetty_ymux_client_route_overlay_input(client, YMUX_INPUT_CLASS_KEY,
                                                            OVERLAY_ID, OVERLAY_ID) == 0);

    /* The ORDERED production input queue (review #16): raw chunks and
     * pointer events pop in WIRE ARRIVAL order — a click and a key from
     * one pump can never observe each other's stale focus state. Both
     * interleavings pinned. */
    int yetty_ymux_client_ordered_push_raw(struct yetty_yclass_object * queue_obj,
                                           const uint8_t *queue_bytes, uint32_t queue_len);
    int yetty_ymux_client_ordered_push_pointer(
        struct yetty_yclass_object * queue_obj, float queue_x, float queue_y, uint32_t queue_kind,
        uint32_t queue_button, uint32_t queue_mods, uint32_t queue_pressed);
    int yetty_ymux_client_ordered_head_kind(struct yetty_yclass_object * queue_obj);
    int yetty_ymux_client_ordered_pop_raw(struct yetty_yclass_object * queue_obj,
                                          uint8_t *out_bytes, uint32_t out_capacity,
                                          uint32_t *out_len);
    int yetty_ymux_client_ordered_pop_pointer(
        struct yetty_yclass_object * queue_obj, float *out_x, float *out_y, uint32_t *out_kind,
        uint32_t *out_button, uint32_t *out_mods, uint32_t *out_pressed);
    /* Interleaving 1: click then key. */
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_pointer(client, 10.0f, 20.0f, 1, 0, 0, 1));
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_raw(client, (const uint8_t *)"k", 1));
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 2);
    float pop_x = 0, pop_y = 0;
    uint32_t pop_kind = 0, pop_button = 0, pop_mods = 0, pop_pressed = 0;
    YTEST_CHECK(test,
                yetty_ymux_client_ordered_pop_pointer(client, &pop_x, &pop_y, &pop_kind,
                                                      &pop_button, &pop_mods, &pop_pressed) == 1);
    YTEST_CHECK(test, pop_x == 10.0f && pop_kind == 1 && pop_pressed == 1);
    uint8_t raw_buf[8];
    uint32_t raw_len = 0;
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 1);
    YTEST_CHECK(test,
                yetty_ymux_client_ordered_pop_raw(client, raw_buf, sizeof(raw_buf), &raw_len));
    YTEST_CHECK(test, raw_len == 1 && raw_buf[0] == 'k');
    /* Interleaving 2: key then click — the raw chunk pops FIRST. */
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_raw(client, (const uint8_t *)"j", 1));
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_pointer(client, 30.0f, 40.0f, 1, 0, 4, 0));
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 1);
    YTEST_CHECK(test,
                yetty_ymux_client_ordered_pop_raw(client, raw_buf, sizeof(raw_buf), &raw_len));
    YTEST_CHECK(test, raw_len == 1 && raw_buf[0] == 'j');
    YTEST_CHECK(test,
                yetty_ymux_client_ordered_pop_pointer(client, &pop_x, &pop_y, &pop_kind,
                                                      &pop_button, &pop_mods, &pop_pressed) == 1);
    YTEST_CHECK(test, pop_y == 40.0f && pop_mods == 4 && pop_pressed == 0);
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 0);
    /* SATURATION (review #17): the queue is dynamic — hundreds of mixed
     * events (past the former 64-entry ring) all queue, in order, with a
     * focus-releasing press at former-capacity position included. */
    for (int flood = 0; flood < 200; ++flood) {
        if (flood % 3 == 2) {
            YTEST_CHECK(test, yetty_ymux_client_ordered_push_pointer(client, (float)flood, 1.0f, 1,
                                                                     0, 0, flood == 64));
        } else {
            uint8_t flood_byte = (uint8_t)('a' + (flood % 26));
            YTEST_CHECK(test, yetty_ymux_client_ordered_push_raw(client, &flood_byte, 1));
        }
    }
    int flood_ok = 1;
    for (int flood = 0; flood < 200 && flood_ok; ++flood) {
        if (flood % 3 == 2) {
            flood_ok =
                yetty_ymux_client_ordered_pop_pointer(client, &pop_x, &pop_y, &pop_kind,
                                                      &pop_button, &pop_mods, &pop_pressed) == 1 &&
                pop_x == (float)flood && pop_pressed == (uint32_t)(flood == 64);
        } else {
            flood_ok = yetty_ymux_client_ordered_pop_raw(client, raw_buf, sizeof(raw_buf),
                                                         &raw_len) == 1 &&
                       raw_len == 1 && raw_buf[0] == (uint8_t)('a' + (flood % 26));
        }
    }
    YTEST_CHECK(test, flood_ok);
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 0);

    /* PEEK/COMMIT ownership protocol (cycle-24 P0): the bridge READS a raw head
     * without consuming it, guarantees retention capacity, and only COMMITS
     * (drops the head) once retention is assured — so a reservation OOM leaves
     * the exact event at the head (backpressure), never consumed-then-dropped. */
    int yetty_ymux_client_ordered_peek_raw(struct yetty_yclass_object * queue_obj,
                                           uint8_t *out_bytes, uint32_t out_capacity,
                                           uint32_t *out_len);
    int yetty_ymux_client_ordered_drop_head(struct yetty_yclass_object * queue_obj);
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_raw(client, (const uint8_t *)"AB", 2));
    YTEST_CHECK(test, yetty_ymux_client_ordered_push_raw(client, (const uint8_t *)"CD", 2));
    uint8_t peek_buf[8];
    uint32_t peek_len = 0;
    /* Two peeks see the SAME head — peek does not consume. */
    YTEST_CHECK(test, yetty_ymux_client_ordered_peek_raw(client, peek_buf, sizeof(peek_buf),
                                                         &peek_len) == 1);
    YTEST_CHECK(test, peek_len == 2 && peek_buf[0] == 'A' && peek_buf[1] == 'B');
    peek_buf[0] = 0;
    YTEST_CHECK(test, yetty_ymux_client_ordered_peek_raw(client, peek_buf, sizeof(peek_buf),
                                                         &peek_len) == 1);
    YTEST_CHECK(test, peek_buf[0] == 'A'); /* still the same head after a second peek */
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 1);
    /* A too-small buffer returns 0 but reports the size (grow-and-retry). */
    YTEST_CHECK(test, yetty_ymux_client_ordered_peek_raw(client, peek_buf, 1, &peek_len) == 0);
    YTEST_CHECK(test, peek_len == 2);
    /* Commit: drop the head; 'CD' becomes the head, in wire order. */
    YTEST_CHECK(test, yetty_ymux_client_ordered_drop_head(client) == 1);
    YTEST_CHECK(test, yetty_ymux_client_ordered_peek_raw(client, peek_buf, sizeof(peek_buf),
                                                         &peek_len) == 1);
    YTEST_CHECK(test, peek_len == 2 && peek_buf[0] == 'C' && peek_buf[1] == 'D');
    YTEST_CHECK(test, yetty_ymux_client_ordered_drop_head(client) == 1);
    YTEST_CHECK(test, yetty_ymux_client_ordered_head_kind(client) == 0);
    YTEST_CHECK(test,
                yetty_ymux_client_ordered_drop_head(client) == 0); /* empty: nothing to drop */

    /* Consumed deliveries: per-class accounting + the handler seat. */
    struct overlay_input_capture capture = {0};
    yetty_ymux_client_overlay_input_deliver(client, YMUX_INPUT_CLASS_POINTER, (const uint8_t *)"p",
                                            1);
    YTEST_CHECK(test,
                yetty_ymux_client_overlay_consumed_count(client, YMUX_INPUT_CLASS_POINTER) == 1);
    YTEST_CHECK(test, yetty_ymux_client_overlay_consumed_count(client, YMUX_INPUT_CLASS_KEY) == 0);
    yetty_ymux_client_set_overlay_input_handler(client, overlay_input_capture_handler, &capture);
    yetty_ymux_client_overlay_input_deliver(client, YMUX_INPUT_CLASS_KEY, (const uint8_t *)"k", 1);
    yetty_ymux_client_overlay_input_deliver(client, YMUX_INPUT_CLASS_PASTE,
                                            (const uint8_t *)"\x1b[200~xy\x1b[201~", 14);
    YTEST_CHECK(test, capture.calls == 2);
    YTEST_CHECK(test, capture.last_class == YMUX_INPUT_CLASS_PASTE);
    YTEST_CHECK(test, capture.last_len == 14);
    YTEST_CHECK(test, yetty_ymux_client_overlay_consumed_count(client, YMUX_INPUT_CLASS_KEY) == 1);
    YTEST_CHECK(test,
                yetty_ymux_client_overlay_consumed_count(client, YMUX_INPUT_CLASS_PASTE) == 1);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #12: slow-client EPOCH recovery must DISCARD the queued VTSINK_RPC
 * terminal deltas — not preserve the backlog. A stalled client accumulates
 * scrolling output until the daemon recovers (can_fit defer → epoch reset);
 * after draining, content that scrolled OFF the 6-row pane must be ABSENT
 * from the wire (its deltas were dropped with the dead epoch) while the
 * final screen arrives via the fresh epoch's complete redraw. */
/* Selection ownership (review #13): the daemon pushes PANE_MODES on change —
 * the app subscribing mouse flips bit0, unsubscribing clears it. */
static void test_pane_modes_push(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "panemodes");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    uint32_t yetty_ymux_client_pane_modes(struct yetty_yclass_object * modes_obj);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "modes", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "modes", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-md"));
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, (yetty_ymux_client_pane_modes(client) & 1u) == 0);

    application_print(test, &rig, 0, "\x1b[?1000h");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, (yetty_ymux_client_pane_modes(client) & 1u) != 0);

    application_print(test, &rig, 0, "\x1b[?1000l");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, (yetty_ymux_client_pane_modes(client) & 1u) == 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

static int epoch_reset_probe(void *userdata)
{
    int *fired = userdata;
    ++*fired;
    return 1; /* receiver reset succeeded */
}

/* Fails the FIRST call, succeeds afterwards — the transient-reset rig. */
static int epoch_reset_fail_once_probe(void *userdata)
{
    int *fired = userdata;
    ++*fired;
    return *fired == 1 ? 0 : 1;
}

/* Review #15: a TRANSIENT receiver-reset failure retries from the client
 * step until it succeeds, and only then re-publishes — the fresh epoch's
 * redraw still arrives (no reconnect needed). */
static void test_vtsink_reset_retries_after_transient_failure(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "resetretry");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "retry", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "retry", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-rr"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_NOT_NULL(test, yetty_ymux_client_vtsink_object(client));
    void yetty_ymux_client_set_vtsink_reset_handler(struct yetty_yclass_object * handler_obj,
                                                    int (*handler)(void *userdata), void *userdata);
    int reset_calls = 0;
    yetty_ymux_client_set_vtsink_reset_handler(client, epoch_reset_fail_once_probe, &reset_calls);

    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_force_recover(daemon));
    application_print(test, &rig, 0, "after-retry");
    capture.bytes.len = 0;
    pump(test, daemon, client, NULL);
    /* First call failed, a later step retried and succeeded; the fresh
     * epoch's redraw carries the post-recovery content. */
    YTEST_CHECK(test, reset_calls >= 2);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "after-retry"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

static void test_vtsink_epoch_discards_stalled_backlog(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "vtepoch");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "vtepoch", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "vtepoch", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-ep"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_NOT_NULL(test, yetty_ymux_client_vtsink_object(client));
    /* The receiver barrier (review #13): the embedder's reset handler must
     * fire on VTSINK_RESET, BEFORE the fresh epoch opens. */
    void yetty_ymux_client_set_vtsink_reset_handler(struct yetty_yclass_object * handler_obj,
                                                    int (*handler)(void *userdata), void *userdata);
    int reset_fired = 0;
    yetty_ymux_client_set_vtsink_reset_handler(client, epoch_reset_probe, &reset_fired);

    /* Print markers, force the epoch reset, then scroll them off. In-flight
     * kernel-buffered deltas may legitimately arrive (same as tmux); the
     * QUEUE-level discard of VTSINK_RPC frames is pinned separately in
     * tx-queue-test. What the epoch contract guarantees end-to-end: the
     * reset tears the session down, the client re-publishes, and the fresh
     * epoch opens with a complete redraw of the CURRENT screen. */
    for (int line = 0; line < 2; ++line) {
        char text[64];
        snprintf(text, sizeof(text), "line-%04d", line);
        application_print(test, &rig, 0, text);
        struct yetty_ycore_int_result step_res = yetty_ymux_daemon_step(daemon);
        YTEST_REQUIRE_OK(test, step_res);
    }
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_force_recover(daemon));
    for (int line = 100; line < 110; ++line) {
        char text[64];
        snprintf(text, sizeof(text), "line-%04d", line);
        application_print(test, &rig, 0, text);
        struct yetty_ycore_int_result step_res = yetty_ymux_daemon_step(daemon);
        YTEST_REQUIRE_OK(test, step_res);
    }
    capture.bytes.len = 0;
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, capture.bytes.len > 0);
    /* Early lines scrolled off long ago: with the dead epoch DISCARDED their
     * deltas never reach the client. If recovery had preserved the backlog,
     * these markers would cross the wire. */
    /* The fresh epoch's complete redraw shows the CURRENT screen — the
     * markers scrolled off before any projection could reach the wire, so
     * the re-published sink must never render them. */
    YTEST_CHECK(test, capture.calls > 0);
    YTEST_CHECK(test, reset_fired == 1);
    /* The final screen content arrives via the fresh epoch. */
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "line-0109"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #14: the ATTACH-LEVEL reset ordering on the REAL channels — a
 * SECOND (control) connection sends the RECOVER frame over its own socket,
 * the daemon runs the same slow-client recovery, and the ATTACHED client
 * observes: reset handler fires, THEN the fresh epoch's complete redraw
 * carries the current screen. This is the wire path `ymux recover` drives
 * against a live daemon. */
static void test_recover_frame_resets_attached_epoch(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "recoverwire");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);
    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "recwire", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "recwire", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-rw"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_NOT_NULL(test, yetty_ymux_client_vtsink_object(client));
    void yetty_ymux_client_set_vtsink_reset_handler(struct yetty_yclass_object * handler_obj,
                                                    int (*handler)(void *userdata), void *userdata);
    int reset_fired = 0;
    yetty_ymux_client_set_vtsink_reset_handler(client, epoch_reset_probe, &reset_fired);
    application_print(test, &rig, 0, "pre-recover");
    pump(test, daemon, client, NULL);

    /* The control connection: a separate socket, like the CLI verb. */
    struct yetty_ycore_void_result yetty_ymux_client_request_recover(struct yetty_yclass_object *
                                                                     control_obj);
    struct yetty_yclass_object *control = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, control);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_request_recover(control));
    struct yetty_ycore_int_result recover_step_res = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, recover_step_res);

    application_print(test, &rig, 0, "post-recover");
    capture.bytes.len = 0;
    pump(test, daemon, client, NULL);
    /* Reset fired exactly once, and the fresh epoch's redraw carries the
     * CURRENT screen (both markers still visible on the 6-row pane). */
    YTEST_CHECK(test, reset_fired == 1);
    YTEST_CHECK(test, capture.bytes.len > 0);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "post-recover"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(control));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/* Review #11 P0: a feed whose lane enqueue FAILS must not lose the projected
 * bytes. The projection commits its op-consumed/shadow state before the feed;
 * if the enqueue then fails (tx queue exhausted by an undrained client), the
 * daemon must invalidate the projector so the NEXT projection is a complete
 * redraw carrying the lost content — without that recovery the marker below
 * never reaches the client (the next delta sees no change). */
static void test_vtsink_feed_failure_resends(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "vtsinkfail");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "vtfail", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "vtfail", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-vf"));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_NOT_NULL(test, yetty_ymux_client_vtsink_object(client));

    application_print(test, &rig, 0, "base");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "base"));

    /* Force the ACTUAL lane enqueue to fail for the marker's feed (fault
     * injection — the can_fit pre-check otherwise intercepts queue
     * exhaustion before feed ever runs). */
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_fail_next_vtsink_tx(daemon, 1));
    application_print(test, &rig, 0, "MARKER-AFTER-FAIL");
    struct yetty_ycore_int_result marker_step = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, marker_step);

    /* Drain everything; recovery must deliver a COMPLETE redraw whose settled
     * content includes the marker (the pane grid holds it regardless — the
     * assertion is that the WIRE carries it after the failed enqueue). */
    capture.bytes.len = 0;
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, capture.bytes.len > 0);
    YTEST_CHECK(test, vt_capture_contains(&capture.bytes, "MARKER-AFTER-FAIL"));

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Deferred delivery ACK (#699.6): with defer enabled the demux does NOT
 * auto-ACK after local dispatch — the daemon's acked generation stays put
 * while feeds apply — and the embedder's explicit vtsink_ack (issued once its
 * downstream write completed) is what advances it. End-to-end application,
 * not receipt.
 *-------------------------------------------------------------------------*/
static void test_vtsink_deferred_ack(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "vtdefer");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    struct vtsink_emit_capture capture = {0};
    yetty_ymux_client_enable_vtsink(client, vtsink_emit_sink, &capture);
    yetty_ymux_client_vtsink_defer_ack(client);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(client, "vtdefer", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(client, "vtdefer", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT,
                                                    "tok-vd"));
    pump(test, daemon, client, NULL);

    application_print(test, &rig, 0, "hi");
    pump(test, daemon, client, NULL);
    YTEST_CHECK(test, capture.calls > 0);
    YTEST_CHECK(test, capture.last_generation > 0);

    /* The daemon-side attachment: applied advanced, acked did NOT. */
    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, "vtdefer").value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t attachment_id = yetty_ymux_client_attachment_id(client).value;
    struct yetty_yclass_object *attachment =
        yetty_ymux_session_attachment(session, attachment_id).value;
    YTEST_REQUIRE_NOT_NULL(test, attachment);
    uint64_t published = 0, acked = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_generations(attachment, &published, &acked));
    YTEST_CHECK(test, published >= capture.last_generation);
    YTEST_CHECK(test, acked == 0);

    /* The embedder reports application — only THIS advances the window. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_vtsink_ack(client, capture.last_generation));
    pump(test, daemon, client, NULL);
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_generations(attachment, &published, &acked));
    YTEST_CHECK(test, acked == capture.last_generation);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

/*---------------------------------------------------------------------------
 * Figure-RPC relay authorization (#695). The relay back-channels
 * (RPC_RELAY / RPC_RELAY_CLOSE / FIGURE_INPUT) are accepted only from the
 * session controller holding INPUT permission, and figure input whitelists the
 * two supported wire codes. A non-controller (read-only) client is refused, so
 * it cannot inject bytes into the pane application's live RPC channel or PTY.
 *-------------------------------------------------------------------------*/
static void test_relay_authorization(struct ytest *test)
{
    struct spawn_rig rig = {0};
    struct yetty_ymux_daemon_host host = {.spawn = rig_spawn, .userdata = &rig};
    char path[128];
    socket_path_for(path, sizeof(path), "relayauth");
    struct yetty_yclass_object *daemon = yetty_ymux_daemon_make(path, 24, 80, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *first = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, first);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_session_new(first, "relayauth", 6, 40));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(first, "relayauth", 0, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR, "ctl"));
    pump(test, daemon, first, NULL);
    uint32_t pane_id = yetty_ymux_client_pane_id(first).value;
    YTEST_REQUIRE_EQ_INT(test, yetty_ymux_client_attached(first).value, 1);

    const uint8_t body[4] = {1, 2, 3, 4};

    /* Controller + INPUT: a relay to an unknown channel is ACCEPTED (auth passes;
     * the daemon finds no matching channel and drops it), so no refuse. */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_rpc_relay(first, 99u, body, sizeof(body)));
    pump(test, daemon, first, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(first).value, 0);

    /* Controller + valid figure wire code: accepted (re-emitted to the app). */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_figure_input(first, TEST_FIGURE_MOUSE, body, sizeof(body)));
    pump(test, daemon, first, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(first).value, 0);

    /* Controller + BAD figure wire code: refused (the whitelist). */
    YTEST_REQUIRE_OK(test, yetty_ymux_client_figure_input(first, 424242u, body, sizeof(body)));
    pump(test, daemon, first, NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(first).value,
                       YMUX_PROTO_REFUSE_BAD_FRAME);

    /* A SECOND client attaches — NOT the controller. Every relay back-channel it
     * sends must be refused NOT_PERMITTED. */
    struct yetty_yclass_object *second = yetty_ymux_client_make(path).value;
    YTEST_REQUIRE_NOT_NULL(test, second);
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(second, "relayauth", pane_id, 6, 40, 16,
                                                    YMUX_TERM_CAP_TRUECOLOR, "viewer"));
    pump(test, daemon, first, second);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_rpc_relay(second, 99u, body, sizeof(body)));
    pump(test, daemon, first, second);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(second).value,
                       YMUX_PROTO_REFUSE_NOT_PERMITTED);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_rpc_relay_close(second, 99u));
    pump(test, daemon, first, second);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(second).value,
                       YMUX_PROTO_REFUSE_NOT_PERMITTED);

    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_figure_input(second, TEST_FIGURE_MOUSE, body, sizeof(body)));
    pump(test, daemon, first, second);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_client_last_refuse(second).value,
                       YMUX_PROTO_REFUSE_NOT_PERMITTED);

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(second));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(first));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
    rig_dispose(&rig);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_daemon");
    YTEST_RUN(&test, test_attach_paint_input);
    YTEST_RUN(&test, test_ycat_figure_reserves_rows);
    YTEST_RUN(&test, test_flood_projects_continuously);
    YTEST_RUN(&test, test_detach_reconnect_over_socket);
    YTEST_RUN(&test, test_two_clients_controller_policy);
    YTEST_RUN(&test, test_shutdown_verb);
    YTEST_RUN(&test, test_attach_bridge_geometry);
    YTEST_RUN(&test, test_vtsink_lane_wire);
    YTEST_RUN(&test, test_overlay_input_routing);
    YTEST_RUN(&test, test_input_classifier_cross_chunk);
    YTEST_RUN(&test, test_overlay_measure_matches_retain);
    YTEST_RUN(&test, test_overlay_input_reaches_chrome_seat);
    YTEST_RUN(&test, test_tty_response_per_attachment);
    YTEST_RUN(&test, test_chrome_consumer_lossless_and_scroll);
    YTEST_RUN(&test, test_copy_mode_chrome);
    YTEST_RUN(&test, test_copy_mode_history_view);
    YTEST_RUN(&test, test_copy_mode_large_selection);
    YTEST_RUN(&test, test_paste_truncation_refused);
    YTEST_RUN(&test, test_overlay_fresh_connection_applies);
    YTEST_RUN(&test, test_overlay_concurrent_same_user_no_alias);
    YTEST_RUN(&test, test_attach_terminal_state_model);
    YTEST_RUN(&test, test_pane_modes_push);
    YTEST_RUN(&test, test_vtsink_epoch_discards_stalled_backlog);
    YTEST_RUN(&test, test_recover_frame_resets_attached_epoch);
    YTEST_RUN(&test, test_vtsink_reset_retries_after_transient_failure);
    YTEST_RUN(&test, test_vtsink_feed_failure_resends);
    YTEST_RUN(&test, test_vtsink_deferred_ack);
    YTEST_RUN(&test, test_relay_authorization);
    return ytest_end(&test);
}
