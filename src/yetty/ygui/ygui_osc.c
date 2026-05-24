/*
 * ygui_osc.c — OSC envelope emission.
 *
 * Every OSC byte that ygui writes goes through
 * output_pty->ops->write — the pty abstraction owns the actual delivery
 * mechanism. Two backends are in use:
 *
 *   - yui (in-process): memory-ring pty pair, write is a memcpy.
 *   - standalone (ygreeter, ytop, …): libuv-backed uv_pipe_t wrapping
 *     output_fd; write queues via uv_write and the kernel buffer
 *     drains in the background.
 *
 * In both cases, ops->write is non-blocking from the producer's
 * perspective — accepting all bytes, returning OK immediately. This
 * file does no I/O of its own beyond yface_emit (envelope encoding).
 *
 * `output_pty` is required for every OSC function — passing NULL is an
 * error. The libuv layer (ygui_engine_uv.c) installs a default one
 * wrapping output_fd; consumers that bypass that layer (yui) must
 * install their own before any ygui_osc_* call.
 */

#include "ygui_internal.h"
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Vendor ID for the legacy ydraw-layer OSC sink (zoom / scroll
 * commands still ride this tag for now). */
#define VENDOR_ID "666674"

/* Push `len` bytes to the engine's output pty. The pty's ops->write is
 * defined to accept the full payload (it queues internally for async
 * delivery in the libuv backend, or memcpy's into the ring for the
 * memory-pty backend), so a short return is always treated as failure. */
static struct yetty_ycore_void_result write_pty_all(struct yetty_platform_pty *pty,
                                                    const char *data, size_t len)
{
    if (!pty) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: output_pty is NULL");
    }
    if (!pty->ops || !pty->ops->write) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: output_pty has no write op");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_size_result r = pty->ops->write(pty, data, len);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: pty write failed", r);
    }
    if (r.value != len) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: pty short write");
    }
    return YETTY_OK_VOID();
}

/* Build the OSC envelope for a binary ydraw payload (compressed) and
 * push it through output_pty. Always ships to YCOMPOSITOR_BIN — the
 * legacy YDRAW_SCENE_BIN path was retired with scene-canvas. */
static struct yetty_ycore_void_result write_bin(struct yetty_platform_pty *output_pty,
                                                const uint8_t *data, uint32_t size)
{
    if (size == 0 || !data) {
        /* Nothing to send. The receiver keeps last frame; intentional
         * "no-op tick" rather than a clear. */
        return YETTY_OK_VOID();
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(YETTY_OSC_YCOMPOSITOR_BIN, /*compressed=*/1,
                                                        &meta, sizeof(meta), data, size, &out);
    ydebug("write_bin: raw_size=%u envelope_bytes=%zu emit_ok=%d", size, out.size, YETTY_IS_OK(r));
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&out);
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: yface_emit (bin) failed", r);
    }
    if (out.size > 0) {
        struct yetty_ycore_void_result wr =
            write_pty_all(output_pty, (const char *)out.data, out.size);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_buffer_destroy(&out);
            return YETTY_ERR(yetty_ycore_void, "ygui_osc: write_bin write failed", wr);
        }
    }
    yetty_ycore_buffer_destroy(&out);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_osc_create_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, int x, int y, int w,
                                                          int h, const uint8_t *data, uint32_t size)
{
    (void)name;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return write_bin(output_pty, data, size);
}

struct yetty_ycore_void_result yetty_ygui_osc_update_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, const uint8_t *data,
                                                          uint32_t size)
{
    (void)name;
    return write_bin(output_pty, data, size);
}

struct yetty_ycore_void_result yetty_ygui_osc_kill_card(struct yetty_platform_pty *output_pty,
                                                        const char *name)
{
    /* No named-card concept on this sink — kill = clear the canvas. */
    (void)name;
    static const char clear_seq[] = "\033]600000;;\033\\";
    return write_pty_all(output_pty, clear_seq, sizeof(clear_seq) - 1);
}

struct yetty_ycore_void_result yetty_ygui_osc_subscribe_clicks(
    struct yetty_platform_pty *output_pty, int enable)
{
    if (enable) {
        return write_pty_all(output_pty, "\033[?1500h", 8);
    }
    return write_pty_all(output_pty, "\033[?1500l", 8);
}

struct yetty_ycore_void_result yetty_ygui_osc_subscribe_moves(struct yetty_platform_pty *output_pty,
                                                              int enable)
{
    if (enable) {
        return write_pty_all(output_pty, "\033[?1501h", 8);
    }
    return write_pty_all(output_pty, "\033[?1501l", 8);
}

struct yetty_ycore_void_result yetty_ygui_osc_query_cell_size(struct yetty_platform_pty *output_pty)
{
    /* CSI 16 t - request cell size in pixels */
    return write_pty_all(output_pty, "\033[16t", 5);
}

struct yetty_ycore_void_result yetty_ygui_osc_subscribe_view_changes(
    struct yetty_platform_pty *output_pty, int enable)
{
    /* DEC mode 1502 - subscribe to card view change events */
    if (enable) {
        return write_pty_all(output_pty, "\033[?1502h", 8);
    }
    return write_pty_all(output_pty, "\033[?1502l", 8);
}

/* Register a card with the ymgui layer. The server then hit-tests this
 * card and routes mouse events to it as YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE with
 * card-local coords. */
struct yetty_ycore_void_result yetty_ygui_osc_card_place(struct yetty_platform_pty *output_pty,
                                                         uint32_t figure_id, int col, int row,
                                                         uint32_t w_cells, uint32_t h_cells)
{
    struct yetty_ymgui_wire_card_place msg = {
        .magic = YMGUI_WIRE_MAGIC_CARD_PLACE,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .flags = 0,
        .col = col,
        .row = row,
        .w_cells = w_cells,
        .h_cells = h_cells,
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(YMGUI_OSC_CS_CARD_PLACE, /*compressed=*/0,
                         /*args=*/NULL, /*args_len=*/0, &msg, sizeof(msg), &out);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&out);
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: card_place emit failed", r);
    }
    if (out.size > 0) {
        struct yetty_ycore_void_result wr =
            write_pty_all(output_pty, (const char *)out.data, out.size);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_buffer_destroy(&out);
            return YETTY_ERR(yetty_ycore_void, "ygui_osc: card_place write failed", wr);
        }
    }
    yetty_ycore_buffer_destroy(&out);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_osc_card_remove(struct yetty_platform_pty *output_pty,
                                                          uint32_t figure_id)
{
    struct yetty_ymgui_wire_card_remove msg = {
        .magic = YMGUI_WIRE_MAGIC_CARD_REMOVE,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .flags = 0,
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(YMGUI_OSC_CS_CARD_REMOVE, /*compressed=*/0,
                         /*args=*/NULL, /*args_len=*/0, &msg, sizeof(msg), &out);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&out);
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: card_remove emit failed", r);
    }
    if (out.size > 0) {
        struct yetty_ycore_void_result wr =
            write_pty_all(output_pty, (const char *)out.data, out.size);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_buffer_destroy(&out);
            return YETTY_ERR(yetty_ycore_void, "ygui_osc: card_remove write failed", wr);
        }
    }
    yetty_ycore_buffer_destroy(&out);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_osc_zoom_card(struct yetty_platform_pty *output_pty,
                                                        const char *name, float level)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "\033]" VENDOR_ID ";zoom --name %s --level %.2f\033\\",
                       name, level);
    if (len < 0 || (size_t)len >= sizeof(buf)) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: zoom_card snprintf overflow");
    }
    return write_pty_all(output_pty, buf, (size_t)len);
}

struct yetty_ycore_void_result yetty_ygui_osc_scroll_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, float x, float y,
                                                          int absolute)
{
    char buf[256];
    int len;
    if (absolute) {
        len = snprintf(buf, sizeof(buf),
                       "\033]" VENDOR_ID ";scroll --name %s -x %.0f -y %.0f\033\\", name, x, y);
    } else {
        len = snprintf(buf, sizeof(buf),
                       "\033]" VENDOR_ID ";scroll --name %s --dx %.0f --dy %.0f\033\\", name, x, y);
    }
    if (len < 0 || (size_t)len >= sizeof(buf)) {
        return YETTY_ERR(yetty_ycore_void, "ygui_osc: scroll_card snprintf overflow");
    }
    return write_pty_all(output_pty, buf, (size_t)len);
}

struct yetty_ycore_void_result yetty_ygui_osc_scroll_card_delta(
    struct yetty_platform_pty *output_pty, const char *name, float dx, float dy)
{
    return yetty_ygui_osc_scroll_card(output_pty, name, dx, dy, /*absolute=*/0);
}
