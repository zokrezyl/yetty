/*
 * ygui_osc.c - OSC output to yetty terminal
 */

#include "ygui_internal.h"
#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/yterm/pty-reader.h> /* YETTY_OSC_YPAINT_* */
#include <yetty/ymgui/wire.h>       /* YMGUI_OSC_CS_CARD_*, wire structs */
#include <yetty/ytrace/ytrace.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define write _write
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

/* Base64 encoding table */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode data to base64 into output buffer. Returns bytes written. */
static size_t base64_encode(const uint8_t *data, uint32_t size, char *out, size_t out_size)
{
    size_t out_len = ((size + 2) / 3) * 4;
    if (out_len + 1 > out_size) {
        return 0;
    }

    size_t j = 0;
    for (uint32_t i = 0; i < size; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < size) {
            n |= ((uint32_t)data[i + 1]) << 8;
        }
        if (i + 2 < size) {
            n |= (uint32_t)data[i + 2];
        }

        out[j++] = b64_table[(n >> 18) & 0x3F];
        out[j++] = b64_table[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < size) ? b64_table[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < size) ? b64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}

/* Write OSC sequence to stdout, retrying on partial writes / EINTR /
 * EAGAIN.
 *
 * stdout sits on a non-blocking PTY connected to the parent yetty (the
 * libuv loop sets O_NONBLOCK on the stdio fds). A single write() of a
 * yimage-bearing frame easily exceeds the PTY's atomic-write capacity
 * (the kernel buffer is ~64 KB) and returns EAGAIN once filled. A
 * previous "best effort" implementation silently dropped the
 * remainder of the OSC envelope, so the receiver saw a truncated
 * base64 payload that decoded to garbage. Loop, polling on EAGAIN so
 * we block-with-a-deadline until the PTY drains. */
static void write_osc(const char *data, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(STDOUT_FILENO, data + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {.fd = STDOUT_FILENO, .events = POLLOUT};
                int pr = poll(&pfd, 1, 5000);
                if (pr > 0 && (pfd.revents & POLLOUT)) {
                    continue;
                }
                ydebug("write_osc: poll timeout/error at %zu/%zu (pr=%d revents=0x%x)",
                       total, len, pr, pfd.revents);
                return;
            }
            ydebug("write_osc: write failed at %zu/%zu (errno=%d)", total, len, errno);
            return;
        }
        if (n == 0) {
            ydebug("write_osc: write returned 0 (pipe closed) at %zu/%zu", total, len);
            return;
        }
        total += (size_t)n;
    }
}

/* Vendor ID = yetty ypaint-layer OSC sink (see terminal.c register_osc_sink).
 * The layer's write() handler accepts:
 *   \033]666674;--bin;<base64>\033\ — append a base64 ypaint buffer
 *
 * ygui's semantics are "the whole UI is re-rendered every tick". The
 * buffer's first prim is CMD_ZERO (set by ygui_engine_render), which the
 * receiver applies inline — clearing the canvas + cursor reset in the
 * same envelope as the rest of the frame. This eliminates the inter-write
 * flicker that a separate YPAINT_CLEAR envelope used to cause. */
#define VENDOR_ID "666674"

static struct yetty_ycore_void_result write_bin(const uint8_t *data, uint32_t size)
{
    if (size == 0 || !data) {
        /* Nothing to send. The receiver keeps last frame; intentional
         * "no-op tick" rather than a clear. */
        return YETTY_OK_VOID();
    }

    /* Bin envelope: args = bin meta (compressed=1), payload = LZ4F'd
     * + b64'd ypaint serialized buffer. yetty_yface_emit builds the
     * whole envelope into out_buf and we push it via the blocking
     * write helper. */
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
                                                        &meta, sizeof(meta), data, size, &out);
    if (YETTY_IS_OK(r) && out.size > 0) {
        write_osc((const char *)out.data, out.size);
    }
    yetty_ycore_buffer_destroy(&out);
    return r;
}

struct yetty_ycore_void_result yetty_ygui_osc_create_card(const char *name, int x, int y, int w,
                                                          int h, const uint8_t *data, uint32_t size)
{
    (void)name;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return write_bin(data, size);
}

struct yetty_ycore_void_result yetty_ygui_osc_update_card(const char *name, const uint8_t *data,
                                                          uint32_t size)
{
    (void)name;
    return write_bin(data, size);
}

void yetty_ygui_osc_kill_card(const char *name)
{
    /* No named-card concept on this sink — kill = clear the canvas. */
    (void)name;
    static const char clear_seq[] = "\033]600000;;\033\\";
    write_osc(clear_seq, sizeof(clear_seq) - 1);
}

void yetty_ygui_osc_subscribe_clicks(int enable)
{
    if (enable) {
        write_osc("\033[?1500h", 8);
    } else {
        write_osc("\033[?1500l", 8);
    }
}

void yetty_ygui_osc_subscribe_moves(int enable)
{
    if (enable) {
        write_osc("\033[?1501h", 8);
    } else {
        write_osc("\033[?1501l", 8);
    }
}

void yetty_ygui_osc_query_cell_size(void)
{
    /* CSI 16 t - request cell size in pixels */
    write_osc("\033[16t", 5);
}

void yetty_ygui_osc_subscribe_view_changes(int enable)
{
    /* DEC mode 1502 - subscribe to card view change events */
    if (enable) {
        write_osc("\033[?1502h", 8);
    } else {
        write_osc("\033[?1502l", 8);
    }
}

/* Register a card with the ymgui layer. The server then hit-tests this card
 * and routes mouse events to it as YMGUI_OSC_SC_MOUSE with card-local
 * coords. Without this, the server's hit table has no entry for our UI and
 * no mouse traffic ever flows back. col/row/w_cells/h_cells are in grid
 * cells (matching ygui_engine_create's x,y,cols,rows). */
struct yetty_ycore_void_result yetty_ygui_osc_card_place(uint32_t card_id, int col, int row,
                                                         uint32_t w_cells, uint32_t h_cells)
{
    struct yetty_ymgui_wire_card_place msg = {
        .magic = YMGUI_WIRE_MAGIC_CARD_PLACE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = card_id,
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
    if (YETTY_IS_OK(r) && out.size > 0) {
        write_osc((const char *)out.data, out.size);
    }
    yetty_ycore_buffer_destroy(&out);
    return r;
}

struct yetty_ycore_void_result yetty_ygui_osc_card_remove(uint32_t card_id)
{
    struct yetty_ymgui_wire_card_remove msg = {
        .magic = YMGUI_WIRE_MAGIC_CARD_REMOVE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = card_id,
        .flags = 0,
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(YMGUI_OSC_CS_CARD_REMOVE, /*compressed=*/0,
                         /*args=*/NULL, /*args_len=*/0, &msg, sizeof(msg), &out);
    if (YETTY_IS_OK(r) && out.size > 0) {
        write_osc((const char *)out.data, out.size);
    }
    yetty_ycore_buffer_destroy(&out);
    return r;
}

void yetty_ygui_osc_zoom_card(const char *name, float level)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "\033]" VENDOR_ID ";zoom --name %s --level %.2f\033\\",
                       name, level);
    write_osc(buf, len);
}

void yetty_ygui_osc_scroll_card(const char *name, float x, float y, int absolute)
{
    char buf[256];
    int len;
    if (absolute) {
        /* Absolute scroll position: -x/-y */
        len = snprintf(buf, sizeof(buf),
                       "\033]" VENDOR_ID ";scroll --name %s -x %.0f -y %.0f\033\\", name, x, y);
    } else {
        /* Relative scroll delta: --dx/--dy */
        len = snprintf(buf, sizeof(buf),
                       "\033]" VENDOR_ID ";scroll --name %s --dx %.0f --dy %.0f\033\\", name, x, y);
    }
    write_osc(buf, len);
}

void yetty_ygui_osc_scroll_card_delta(const char *name, float dx, float dy)
{
    yetty_ygui_osc_scroll_card(name, dx, dy, 0);
}
