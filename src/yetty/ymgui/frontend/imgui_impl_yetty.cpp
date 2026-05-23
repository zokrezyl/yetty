/*
 * imgui_impl_yetty.cpp — Dear ImGui ↔ yetty bridge (multi-figure).
 *
 * Every ymgui figure is one placed sub-region of the terminal pane.
 * Each carries its own ImGuiContext (own DisplaySize, own font atlas,
 * own focus state), so an app can render multiple independent ImGui
 * UIs side by side. Wire details live in <yetty/ymgui/wire.h>.
 *
 * Renderer flow per figure:
 *   - CreateFigure ships a CREATE_CHILD admin record on the figure-tree
 *     OSC (yfigure wire) when YMGUI_USE_FIGURE_TREE=1, or a legacy
 *     CARD_PLACE on the per-kind OSC otherwise.
 *   - BeginFigureFrame makes that figure's context current; first time
 *     also uploads the figure's font atlas.
 *   - RenderFigureDrawData serializes ImDrawData into wire format and
 *     ships it as a per-figure record tagged with the figure_id.
 *
 * Input flow:
 *   - The server hit-tests each event against the live figures and
 *     ships the result with a figure_id. The bridge looks up the figure,
 *     switches its ImGuiContext, calls AddXxxEvent.
 */

#include "imgui_impl_yetty.h"
#include "ymgui_encode.h"

#include <yetty/yclient/event-loop.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterm/client-input.h>
#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yterm/osc-codes.h>

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> /* Sleep + DWORD for ImGui_ImplYetty_WaitInput */
#define YMGUI_STDOUT_FD 1
#define YMGUI_STDIN_FD 0
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#define YMGUI_STDOUT_FD STDOUT_FILENO
#define YMGUI_STDIN_FD STDIN_FILENO
#endif

/*===========================================================================
 * Per-figure state
 *=========================================================================*/

struct ymgui_figure_state {
    uint32_t id;
    ImGuiContext *ctx;
    bool atlas_uploaded;
    /* Latest pixel size announced by the server (DisplaySize). 0 until
     * the first RESIZE confirms placement. */
    float w_pixels;
    float h_pixels;
    /* Latest grid coords / cell counts, kept so MoveFigure can re-emit
     * CARD_PLACE without re-asking the app. */
    int col;
    int row;
    uint32_t w_cells;
    uint32_t h_cells;
    /* Stage 1 cache — per-cmd_list content hashes from the last
     * successfully emitted frame. Used to emit REPEAT markers. */
    std::vector<uint64_t> prev_cl_hashes;
    /* Stage 2 cache — per-cmd_list set of per-cmd hashes from the last
     * successfully emitted frame (draw order). A cmd whose hash appears
     * in this set is considered "cached on the server" and shipped as a
     * hash reference only; otherwise we ship the full cmd inline. */
    std::vector<std::vector<uint64_t>> prev_cmd_hashes;
};

struct ymgui_impl_state {
    int out_fd;
    int in_fd;
    struct yetty_yface *yface_out; /* OSC envelope + LZ4F + b64 streamer */
    struct yetty_yface *yface_in;  /* stream-scanner for incoming events */

    int raw_mode_active;
#ifndef _WIN32
    struct termios saved_termios;
#endif

    std::vector<ymgui_figure_state *> figures;
    uint32_t next_auto_figure_id;
    uint32_t focused_figure_id;
    /* Wire-dedup flags (Stage 1 / Stage 2). Both default ON. */
    uint32_t opt_flags;
    /* Cell size used to translate (col, row, w_cells, h_cells) supplied
     * by the app into an absolute pixel rect at CREATE_CHILD time. The
     * figure-tree wire is pixel-addressed; there's no server-side cell
     * resolution any more. Defaults are conservative and can be
     * overridden via env YMGUI_CELL_PX=WxH. */
    float cell_w_px;
    float cell_h_px;
};

static struct ymgui_impl_state g_state = {
    YMGUI_STDOUT_FD,
    YMGUI_STDIN_FD,
    nullptr,
    nullptr,
    0,
#ifndef _WIN32
    {},
#endif
    {},
    1u,
    0u,
    YETTY_YMGUI_OPT_DEDUP_CMDLIST | YETTY_YMGUI_OPT_DEDUP_CMD,
    8.0f,
    16.0f,
};

/*===========================================================================
 * Helpers
 *=========================================================================*/

static ymgui_figure_state *find_figure(uint32_t id)
{
    for (auto *c : g_state.figures) {
        if (c->id == id) {
            return c;
        }
    }
    return nullptr;
}

/* Push everything yface_out accumulated to the wire, blocking until it's
 * fully drained. The pending-write helper queues unfinished tails on
 * EAGAIN, but the libuv loop here doesn't watch POLLOUT, so a queued
 * tail would stall every subsequent emit until the next stdin event
 * incidentally drained it. Cheaper to just block briefly per emit. */
static int flush_yface_to_fd(void)
{
    if (!g_state.yface_out) {
        return -1;
    }
    struct yetty_ycore_buffer *out = yetty_yface_out_buf(g_state.yface_out);
    if (!out || out->size == 0) {
        return 0;
    }
    int rc = yetty_ymgui_pending_write(g_state.out_fd, out->data, out->size);
    yetty_ycore_buffer_clear(out);
    if (rc < 0) {
        return rc;
    }
    if (yetty_ymgui_pending_active()) {
        return yetty_ymgui_pending_drain_blocking(g_state.out_fd);
    }
    return 0;
}

/*===========================================================================
 * Figure-tree record helpers.
 *
 * Each call ships ONE yfigure record on YETTY_OSC_YCOMPOSITOR_BIN.
 * The record layout is:
 *
 *   u32 length        # payload bytes after this header
 *   u32 id            # 0 = admin, !=0 = child figure id
 *   bytes payload[length]
 *
 * For admin records (id=0), the payload starts with u32 admin_op
 * (see yetty_yfigure_wire_admin_op). For child-targeted records, the
 * payload begins with u32 sub_op (see yetty_ymgui_figure_sub_op).
 *=========================================================================*/

static bool emit_record(uint32_t id, bool compressed,
                        const void *body, size_t body_len)
{
    if (!g_state.yface_out) {
        return false;
    }
    struct yetty_yfigure_wire_record hdr = {(uint32_t)body_len, id};
    if (!yetty_yface_start_write(g_state.yface_out, YETTY_OSC_YCOMPOSITOR_BIN,
                                 compressed ? 1 : 0, /*args=*/NULL, 0)
             .ok) {
        return false;
    }
    if (!yetty_yface_write(g_state.yface_out, &hdr, sizeof(hdr)).ok) {
        return false;
    }
    if (body_len > 0 && !yetty_yface_write(g_state.yface_out, body, body_len).ok) {
        return false;
    }
    if (!yetty_yface_finish_write(g_state.yface_out).ok) {
        return false;
    }
    return flush_yface_to_fd() >= 0;
}

/* ADMIN_CREATE_CHILD with kind=YMGUI and no init payload. The receiver
 * uses `rect` as the figure's absolute target rect. */
static bool emit_admin_create_child_ymgui(uint32_t child_id,
                                          float x0, float y0, float x1, float y1)
{
    uint8_t body[8 + 4 + 4 + 16 + 4]; /* admin_op + child_id + kind + rect[4] + init_size */
    uint32_t admin_op = YETTY_YFIGURE_ADMIN_CREATE_CHILD;
    uint32_t kind = YETTY_YFIGURE_KIND_YMGUI;
    uint32_t init_size = 0;
    float r[4] = {x0, y0, x1, y1};
    size_t off = 0;
    memcpy(body + off, &admin_op, 4); off += 4;
    memcpy(body + off, &child_id, 4); off += 4;
    memcpy(body + off, &kind, 4);     off += 4;
    memcpy(body + off, r, 16);        off += 16;
    memcpy(body + off, &init_size, 4); off += 4;
    return emit_record(/*id=*/0, /*compressed=*/false, body, off);
}

/* ADMIN_SET_CHILD_RECT — update an existing figure's rect. */
static bool emit_admin_set_child_rect(uint32_t child_id,
                                      float x0, float y0, float x1, float y1)
{
    uint8_t body[4 + 4 + 16];
    uint32_t admin_op = YETTY_YFIGURE_ADMIN_SET_CHILD_RECT;
    float r[4] = {x0, y0, x1, y1};
    memcpy(body + 0, &admin_op, 4);
    memcpy(body + 4, &child_id, 4);
    memcpy(body + 8, r, 16);
    return emit_record(/*id=*/0, /*compressed=*/false, body, sizeof(body));
}

/* ADMIN_DELETE_CHILD. */
static bool emit_admin_delete_child(uint32_t child_id)
{
    uint8_t body[4 + 4];
    uint32_t admin_op = YETTY_YFIGURE_ADMIN_DELETE_CHILD;
    memcpy(body + 0, &admin_op, 4);
    memcpy(body + 4, &child_id, 4);
    return emit_record(/*id=*/0, /*compressed=*/false, body, sizeof(body));
}

/* Read the PTY winsize so w_cells=0 / h_cells=0 (= "fill to edge") can
 * resolve to the full pane width / height. yetty's PTY only sets
 * ws_col / ws_row (cell counts); ws_xpixel / ws_ypixel are zero, so we
 * multiply cells by cell_w_px / cell_h_px ourselves. Returns false if
 * the ioctl fails (out_fd not a tty, headless test, …). */
static bool query_pane_cells(uint32_t *cols, uint32_t *rows)
{
#ifdef _WIN32
    (void)cols; (void)rows;
    return false;
#else
    struct winsize ws = {};
    int fd = g_state.out_fd >= 0 ? g_state.out_fd : STDOUT_FILENO;
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        return false;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return true;
#endif
}

/* Pre-compute the pixel rect a figure maps to when the producer emits in
 * figure-tree mode. Uses the figure's last-known w_pixels/h_pixels when
 * available (set by SC_RESIZE) otherwise falls back to cell-size × cells.
 * The placement still anchors at (col*cell_w, row*cell_h) — the host
 * has no col/row concept on the new wire, so the producer takes
 * responsibility for the layout. w_cells/h_cells == 0 means "fill to the
 * right/bottom edge" — resolved against the PTY winsize. */
static void figure_tree_rect_for_figure(const ymgui_figure_state *c,
                                      float *x0, float *y0,
                                      float *x1, float *y1)
{
    *x0 = (float)c->col * g_state.cell_w_px;
    *y0 = (float)c->row * g_state.cell_h_px;

    uint32_t pane_cols = 0, pane_rows = 0;
    bool have_pane = (c->w_cells == 0 || c->h_cells == 0)
                     && query_pane_cells(&pane_cols, &pane_rows);

    uint32_t eff_w_cells = c->w_cells;
    uint32_t eff_h_cells = c->h_cells;
    if (eff_w_cells == 0 && have_pane && pane_cols > (uint32_t)c->col) {
        eff_w_cells = pane_cols - (uint32_t)c->col;
    }
    if (eff_h_cells == 0 && have_pane && pane_rows > (uint32_t)c->row) {
        eff_h_cells = pane_rows - (uint32_t)c->row;
    }

    float w_px = (c->w_pixels > 0.0f) ? c->w_pixels
                                       : (float)eff_w_cells * g_state.cell_w_px;
    float h_px = (c->h_pixels > 0.0f) ? c->h_pixels
                                       : (float)eff_h_cells * g_state.cell_h_px;
    *x1 = *x0 + w_px;
    *y1 = *y0 + h_px;
}

/* Build a no-args OSC envelope around `payload` and ship it. flush_yface
 * blocks until the entire envelope is drained, so every emit either
 * fully reaches the server or returns an error. */
static bool emit_osc(int osc_code, bool compressed, const void *payload, size_t len)
{
    if (!g_state.yface_out) {
        return false;
    }
    if (!yetty_yface_start_write(g_state.yface_out, osc_code, compressed ? 1 : 0,
                                 /*args=*/NULL, /*args_len=*/0)
             .ok) {
        return false;
    }
    if (!yetty_yface_write(g_state.yface_out, payload, len).ok) {
        return false;
    }
    if (!yetty_yface_finish_write(g_state.yface_out).ok) {
        return false;
    }
    return flush_yface_to_fd() >= 0;
}

/*===========================================================================
 * Backend lifecycle
 *=========================================================================*/

void yetty_ymgui_ImGui_ImplYetty_SetOptimizations(uint32_t flags)
{
    g_state.opt_flags = flags;
    /* Hash caches are now stale w.r.t. the new policy — drop them so the
     * next frame starts fresh on every figure. */
    for (auto *c : g_state.figures) {
        c->prev_cl_hashes.clear();
        c->prev_cmd_hashes.clear();
    }
}

uint32_t yetty_ymgui_ImGui_ImplYetty_GetOptimizations(void)
{
    return g_state.opt_flags;
}

void yetty_ymgui_ImGui_ImplYetty_SetOutputFd(int fd)
{
    g_state.out_fd = fd;
}
void yetty_ymgui_ImGui_ImplYetty_SetInputFd(int fd)
{
    g_state.in_fd = fd;
}

bool yetty_ymgui_ImGui_ImplYetty_Init(void)
{
    /* Two yface instances — one each for the encode/decode pipelines. */
    {
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        if (!yr.ok) {
            return false;
        }
        g_state.yface_out = yr.value;
    }
    {
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        if (!yr.ok) {
            yetty_yface_destroy(g_state.yface_out);
            g_state.yface_out = nullptr;
            return false;
        }
        g_state.yface_in = yr.value;
    }

    {
        const char *env = getenv("YMGUI_CELL_PX");
        if (env && env[0]) {
            float w = 0.0f, h = 0.0f;
            if (sscanf(env, "%fx%f", &w, &h) == 2 && w > 0.0f && h > 0.0f) {
                g_state.cell_w_px = w;
                g_state.cell_h_px = h;
            }
        }
    }
    return true;
}

void yetty_ymgui_ImGui_ImplYetty_Shutdown(void)
{
    /* Destroy any remaining figures (does NOT emit figure removal — that's
     * the app's call via Clear()). */
    for (auto *c : g_state.figures) {
        if (c->ctx) {
            ImGui::DestroyContext(c->ctx);
        }
        delete c;
    }
    g_state.figures.clear();

    if (g_state.yface_out) {
        yetty_yface_destroy(g_state.yface_out);
        g_state.yface_out = nullptr;
    }
    if (g_state.yface_in) {
        yetty_yface_destroy(g_state.yface_in);
        g_state.yface_in = nullptr;
    }
}

void yetty_ymgui_ImGui_ImplYetty_Clear(bool keep_visible)
{
    /* Figure-tree CLEAR_ALL admin record on the root. keep_visible has
     * no archive path yet on the new wire — always drop. */
    (void)keep_visible;
    uint8_t body[4];
    uint32_t admin_op = YETTY_YFIGURE_ADMIN_CLEAR_ALL;
    memcpy(body, &admin_op, 4);
    emit_record(/*id=*/0, /*compressed=*/false, body, sizeof(body));
}

/*===========================================================================
 * Figure lifecycle
 *=========================================================================*/

uint32_t yetty_ymgui_ImGui_ImplYetty_CreateFigure(uint32_t figure_id, int col, int row,
                                                uint32_t w_cells, uint32_t h_cells)
{
    if (figure_id == YMGUI_FIGURE_ID_NONE) {
        figure_id = g_state.next_auto_figure_id++;
    } else if (figure_id >= g_state.next_auto_figure_id) {
        g_state.next_auto_figure_id = figure_id + 1u;
    }

    if (find_figure(figure_id)) {
        /* Already exists — treat as move. */
        yetty_ymgui_ImGui_ImplYetty_MoveFigure(figure_id, col, row, w_cells, h_cells);
        return figure_id;
    }

    auto *c = new ymgui_figure_state{};
    c->id = figure_id;
    c->ctx = ImGui::CreateContext();
    c->col = col;
    c->row = row;
    c->w_cells = w_cells;
    c->h_cells = h_cells;
    c->w_pixels = 0.0f;
    c->h_pixels = 0.0f;
    g_state.figures.push_back(c);

    /* Configure the figure's ImGui context defaults. */
    {
        ImGuiContext *prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(c->ctx);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1.0f, 1.0f); /* placeholder */
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendRendererName = "imgui_impl_yetty";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        ImGui::SetCurrentContext(prev);
    }

    /* Producer computes the pixel rect and ships a single CREATE_CHILD
     * on OSC 630000. Placement is purely client-driven — no rolling-row
     * anchor, no cursor advance, no server-side SC_RESIZE echo. We set
     * DisplaySize ourselves so the app's "skip until DisplaySize is
     * real" gate clears immediately. */
    float x0, y0, x1, y1;
    figure_tree_rect_for_figure(c, &x0, &y0, &x1, &y1);
    emit_admin_create_child_ymgui(figure_id, x0, y0, x1, y1);
    yetty_ymgui_ImGui_ImplYetty_OnFigureResize(figure_id, x1 - x0, y1 - y0);
    return figure_id;
}

void yetty_ymgui_ImGui_ImplYetty_MoveFigure(uint32_t figure_id, int col, int row, uint32_t w_cells,
                                          uint32_t h_cells)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    c->col = col;
    c->row = row;
    c->w_cells = w_cells;
    c->h_cells = h_cells;
    /* Drop the cached pixel size so the new cell counts drive the rect
     * rather than the stale dimensions of the previous frame. */
    c->w_pixels = 0.0f;
    c->h_pixels = 0.0f;
    float x0, y0, x1, y1;
    figure_tree_rect_for_figure(c, &x0, &y0, &x1, &y1);
    emit_admin_set_child_rect(figure_id, x0, y0, x1, y1);
    yetty_ymgui_ImGui_ImplYetty_OnFigureResize(figure_id, x1 - x0, y1 - y0);
}

void yetty_ymgui_ImGui_ImplYetty_RemoveFigure(uint32_t figure_id, bool keep_visible)
{
    (void)keep_visible; /* no static-archive path yet on the new wire */
    emit_admin_delete_child(figure_id);

    /* Then drop our local state. */
    for (auto it = g_state.figures.begin(); it != g_state.figures.end(); ++it) {
        if ((*it)->id == figure_id) {
            if ((*it)->ctx) {
                ImGui::DestroyContext((*it)->ctx);
            }
            delete *it;
            g_state.figures.erase(it);
            break;
        }
    }
    if (g_state.focused_figure_id == figure_id) {
        g_state.focused_figure_id = 0;
    }
}

ImGuiContext *yetty_ymgui_ImGui_ImplYetty_GetFigureContext(uint32_t figure_id)
{
    auto *c = find_figure(figure_id);
    return c ? c->ctx : nullptr;
}

uint32_t yetty_ymgui_ImGui_ImplYetty_FocusedFigure(void)
{
    return g_state.focused_figure_id;
}

/*===========================================================================
 * Per-figure frame
 *=========================================================================*/

static bool upload_figure_atlas(ymgui_figure_state *c)
{
    /* Caller has ImGui::SetCurrentContext(c->ctx). */
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &w, &h);
    if (!pixels || w <= 0 || h <= 0) {
        return false;
    }

    size_t pixel_bytes = (size_t)w * (size_t)h;
    size_t payload_sz = sizeof(struct yetty_ymgui_wire_tex) + pixel_bytes;

    struct yetty_ymgui_wire_tex hdr = {};
    hdr.magic = YMGUI_WIRE_MAGIC_TEX;
    hdr.version = YMGUI_WIRE_VERSION;
    hdr.figure_id = c->id;
    hdr.tex_id = YMGUI_TEX_ID_FONT_ATLAS;
    hdr.format = YMGUI_TEX_FMT_R8;
    hdr.width = (uint32_t)w;
    hdr.height = (uint32_t)h;
    hdr.total_size = (uint32_t)payload_sz;

    /* Ship the texture as one record targeted at the figure's child id.
     * Record body = [u32 sub_op] + [yetty_ymgui_wire_tex hdr] + [pixels].
     * The outer record length covers all three. */
    uint32_t sub_op = YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD;
    uint32_t body_len = (uint32_t)(sizeof(sub_op) + sizeof(hdr) + pixel_bytes);
    struct yetty_yfigure_wire_record outer = {body_len, c->id};
    if (!yetty_yface_start_write(g_state.yface_out, YETTY_OSC_YCOMPOSITOR_BIN,
                                 /*compressed=*/1, /*args=*/NULL, 0).ok) {
        return false;
    }
    if (!yetty_yface_write(g_state.yface_out, &outer, sizeof(outer)).ok) return false;
    if (!yetty_yface_write(g_state.yface_out, &sub_op, sizeof(sub_op)).ok) return false;
    if (!yetty_yface_write(g_state.yface_out, &hdr, sizeof(hdr)).ok) return false;
    if (!yetty_yface_write(g_state.yface_out, pixels, pixel_bytes).ok) return false;
    if (!yetty_yface_finish_write(g_state.yface_out).ok) return false;
    if (flush_yface_to_fd() < 0) return false;

    io.Fonts->SetTexID((ImTextureID)(intptr_t)YMGUI_TEX_ID_FONT_ATLAS);
    c->atlas_uploaded = true;
    return true;
}

void yetty_ymgui_ImGui_ImplYetty_BeginFigureFrame(uint32_t figure_id)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    ImGui::SetCurrentContext(c->ctx);
    if (!c->atlas_uploaded) {
        upload_figure_atlas(c);
    }
}

/* FNV-1a 64-bit. Stream-friendly, fast enough at our scale (~30-80 KB
 * per cmd_list) that the hash cost is well under the bandwidth saved
 * when a cmd_list is unchanged. */
static inline uint64_t fnv64_update(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Stage 1 — hash the byte sequence we'd put on the wire for one cmd_list
 * (vtx + idx + non-callback cmds), salted with the counts so different
 * shapes can't collide. Matches the full-emit write loop — if the hash
 * equals last frame's hash at the same slot, the wire bytes would be
 * byte-identical and the server can reuse its cached slot. */
static uint64_t hash_cmd_list(const ImDrawList *cl)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    uint32_t v = (uint32_t)cl->VtxBuffer.Size;
    uint32_t i = (uint32_t)cl->IdxBuffer.Size;
    uint32_t c = (uint32_t)cl->CmdBuffer.Size;
    uint32_t isize = (uint32_t)sizeof(ImDrawIdx);
    h = fnv64_update(h, &v, sizeof(v));
    h = fnv64_update(h, &i, sizeof(i));
    h = fnv64_update(h, &c, sizeof(c));
    h = fnv64_update(h, &isize, sizeof(isize));
    if (cl->VtxBuffer.Size) {
        h = fnv64_update(h, cl->VtxBuffer.Data,
                         (size_t)cl->VtxBuffer.Size * sizeof(ImDrawVert));
    }
    if (cl->IdxBuffer.Size) {
        h = fnv64_update(h, cl->IdxBuffer.Data,
                         (size_t)cl->IdxBuffer.Size * sizeof(ImDrawIdx));
    }
    for (int k = 0; k < cl->CmdBuffer.Size; k++) {
        const ImDrawCmd *dc = &cl->CmdBuffer[k];
        if (dc->UserCallback) {
            continue;
        }
        struct yetty_ymgui_wire_cmd wc = {};
        wc.clip_min_x = dc->ClipRect.x;
        wc.clip_min_y = dc->ClipRect.y;
        wc.clip_max_x = dc->ClipRect.z;
        wc.clip_max_y = dc->ClipRect.w;
        wc.tex_id = (uint32_t)(intptr_t)dc->GetTexID();
        wc.vtx_offset = dc->VtxOffset;
        wc.idx_offset = dc->IdxOffset;
        wc.elem_count = dc->ElemCount;
        h = fnv64_update(h, &wc, sizeof(wc));
    }
    return h;
}

/* Stage 2 — hash a single cmd's drawable content. The hash MUST match
 * the server-side helper that walks the previous cmd_list slot, so any
 * change here needs a matching update server-side. Composition:
 *
 *   { clip_min_x, clip_min_y, clip_max_x, clip_max_y,
 *     tex_id, elem_count, vtx_count, idx_bpe,
 *     vtx[vtx_offset .. vtx_offset+vtx_count]    (raw bytes),
 *     idx[idx_offset .. idx_offset+elem_count]    (raw bytes) }
 *
 * `vtx_count` is the number of vertices this cmd actually references,
 * computed as max(idx_slice) + 1 — caller passes it in. We deliberately
 * exclude `vtx_offset` and `idx_offset` from the hash because they're
 * positions inside the cmd_list, not part of "what this cmd draws";
 * two cmds with identical content but different offsets must hash the
 * same so the server can reuse them across packing changes. */
static uint64_t hash_cmd(const ImDrawCmd *dc, const ImDrawList *cl, uint32_t vtx_count)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    float clip[4] = {dc->ClipRect.x, dc->ClipRect.y, dc->ClipRect.z, dc->ClipRect.w};
    uint32_t tex = (uint32_t)(intptr_t)dc->GetTexID();
    uint32_t ec = dc->ElemCount;
    uint32_t isize = (uint32_t)sizeof(ImDrawIdx);
    h = fnv64_update(h, clip, sizeof(clip));
    h = fnv64_update(h, &tex, sizeof(tex));
    h = fnv64_update(h, &ec, sizeof(ec));
    h = fnv64_update(h, &vtx_count, sizeof(vtx_count));
    h = fnv64_update(h, &isize, sizeof(isize));
    if (vtx_count && cl->VtxBuffer.Size) {
        h = fnv64_update(h, cl->VtxBuffer.Data + dc->VtxOffset,
                         (size_t)vtx_count * sizeof(ImDrawVert));
    }
    if (ec && cl->IdxBuffer.Size) {
        h = fnv64_update(h, cl->IdxBuffer.Data + dc->IdxOffset,
                         (size_t)ec * sizeof(ImDrawIdx));
    }
    return h;
}

/* Number of vertices this cmd actually references inside the cmd_list's
 * vtx buffer. = max(idx[idx_offset..idx_offset+elem_count]) + 1. Zero for
 * empty cmds. */
static uint32_t cmd_vtx_count(const ImDrawCmd *dc, const ImDrawList *cl)
{
    if (dc->ElemCount == 0) {
        return 0;
    }
    const ImDrawIdx *idx = cl->IdxBuffer.Data + dc->IdxOffset;
    uint32_t max_idx = 0;
    for (uint32_t i = 0; i < dc->ElemCount; i++) {
        if ((uint32_t)idx[i] > max_idx) {
            max_idx = (uint32_t)idx[i];
        }
    }
    return max_idx + 1u;
}

/* Per-slot emit plan computed before we start writing. */
enum slot_mode {
    SLOT_FULL = 0,     /* legacy: cmd_list_hdr + vtx + idx + cmds */
    SLOT_REPEAT = 1,   /* Stage 1: only cmd_list_hdr */
    SLOT_CMD_DIFF = 2, /* Stage 2: hashes + inline cmds */
};

struct slot_plan {
    enum slot_mode mode;
    /* For SLOT_CMD_DIFF: */
    std::vector<uint64_t> cmd_hashes;       /* draw order, non-callback only */
    std::vector<uint32_t> cmd_vtx_counts;   /* parallel to cmd_hashes */
    std::vector<const ImDrawCmd *> cmd_ptr; /* parallel — for emit/inline */
    std::vector<uint8_t> cmd_inline;        /* 1 = inline, 0 = hash-only */
    uint32_t inline_count;
    /* SLOT_CMD_DIFF rebuilds prev_cmd_hashes from cmd_hashes after emit;
     * SLOT_FULL / SLOT_REPEAT also need to keep the cache consistent
     * with what the server now holds for this slot. SLOT_FULL replaces
     * the cache from scratch; SLOT_REPEAT keeps it as is. */
};

void yetty_ymgui_ImGui_ImplYetty_RenderFigureDrawData(uint32_t figure_id, ImDrawData *draw_data)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    if (!draw_data || draw_data->CmdListsCount <= 0) {
        return;
    }
    if (!g_state.yface_out) {
        return;
    }
    /* Static-size sanity — we memcpy ImDrawVert straight onto the wire. */
    if (sizeof(ImDrawVert) != sizeof(struct yetty_ymgui_wire_vertex)) {
        return;
    }

    const bool stage1 = (g_state.opt_flags & YETTY_YMGUI_OPT_DEDUP_CMDLIST) != 0;
    const bool stage2 = (g_state.opt_flags & YETTY_YMGUI_OPT_DEDUP_CMD) != 0;

    int cl_count = draw_data->CmdListsCount;
    std::vector<uint64_t> new_cl_hashes((size_t)cl_count);
    std::vector<slot_plan> plans((size_t)cl_count);

    /* Pass 1: plan each slot. Stage 1 first (cheap byte-match against
     * last frame); if it misses and Stage 2 is on, hash each cmd and
     * decide inline-vs-reference per cmd using prev_cmd_hashes. */
    for (int n = 0; n < cl_count; n++) {
        const ImDrawList *cl = draw_data->CmdLists[n];
        slot_plan &p = plans[(size_t)n];

        uint64_t h = hash_cmd_list(cl);
        new_cl_hashes[(size_t)n] = h;

        if (stage1 && (size_t)n < c->prev_cl_hashes.size() &&
            c->prev_cl_hashes[(size_t)n] == h) {
            p.mode = SLOT_REPEAT;
            continue;
        }

        if (!stage2) {
            p.mode = SLOT_FULL;
            continue;
        }

        /* Stage 2 path — hash each non-callback cmd. */
        const std::vector<uint64_t> *prev =
            ((size_t)n < c->prev_cmd_hashes.size()) ? &c->prev_cmd_hashes[(size_t)n] : nullptr;
        p.mode = SLOT_CMD_DIFF;
        for (int k = 0; k < cl->CmdBuffer.Size; k++) {
            const ImDrawCmd *dc = &cl->CmdBuffer[k];
            if (dc->UserCallback || dc->ElemCount == 0) {
                continue;
            }
            uint32_t vc = cmd_vtx_count(dc, cl);
            uint64_t ch = hash_cmd(dc, cl, vc);
            p.cmd_hashes.push_back(ch);
            p.cmd_vtx_counts.push_back(vc);
            p.cmd_ptr.push_back(dc);
            bool found = false;
            if (prev) {
                for (uint64_t ph : *prev) {
                    if (ph == ch) {
                        found = true;
                        break;
                    }
                }
            }
            p.cmd_inline.push_back(found ? 0 : 1);
            if (!found) {
                p.inline_count++;
            }
        }

        /* A SLOT_CMD_DIFF with zero cmds total degenerates — fall back
         * to SLOT_FULL (frees the bookkeeping vectors). */
        if (p.cmd_hashes.empty()) {
            p.mode = SLOT_FULL;
            p.cmd_hashes.clear();
            p.cmd_vtx_counts.clear();
            p.cmd_ptr.clear();
            p.cmd_inline.clear();
            p.inline_count = 0;
        }
    }

    /* Pass 2: compute total size for the frame header's total_size. */
    size_t total_size = sizeof(struct yetty_ymgui_wire_frame);
    for (int n = 0; n < cl_count; ++n) {
        const ImDrawList *cl = draw_data->CmdLists[n];
        const slot_plan &p = plans[(size_t)n];
        total_size += sizeof(struct yetty_ymgui_wire_cmd_list);
        if (p.mode == SLOT_REPEAT) {
            continue;
        }
        if (p.mode == SLOT_CMD_DIFF) {
            /* hash_count(4) + inline_count(4) + draw_hashes(N*8) +
             * per inline: (inline_hdr 16) + (wire_cmd 32) + vtx + idx_padded. */
            total_size += 8u;
            total_size += p.cmd_hashes.size() * sizeof(uint64_t);
            for (size_t k = 0; k < p.cmd_hashes.size(); k++) {
                if (!p.cmd_inline[k]) {
                    continue;
                }
                total_size += sizeof(struct yetty_ymgui_wire_cmd_inline);
                total_size += sizeof(struct yetty_ymgui_wire_cmd);
                total_size += (size_t)p.cmd_vtx_counts[k] * sizeof(ImDrawVert);
                size_t ibytes = (size_t)p.cmd_ptr[k]->ElemCount * sizeof(ImDrawIdx);
                if (ibytes & 3u) {
                    ibytes += 4u - (ibytes & 3u);
                }
                total_size += ibytes;
            }
            continue;
        }
        /* SLOT_FULL */
        size_t idx_bytes = (size_t)cl->IdxBuffer.Size * sizeof(ImDrawIdx);
        if (idx_bytes & 3u) {
            idx_bytes += 4u - (idx_bytes & 3u);
        }
        total_size += (size_t)cl->VtxBuffer.Size * sizeof(ImDrawVert) + idx_bytes +
                      (size_t)cl->CmdBuffer.Size * sizeof(struct yetty_ymgui_wire_cmd);
    }

    struct yetty_ymgui_wire_frame fh = {};
    fh.magic = YMGUI_WIRE_MAGIC_FRAME;
    fh.version = YMGUI_WIRE_VERSION;
    fh.flags = (sizeof(ImDrawIdx) == 4) ? YMGUI_FRAME_FLAG_IDX32 : 0;
    fh.total_size = (uint32_t)total_size;
    fh.figure_id = figure_id;
    fh.cmd_list_count = (uint32_t)cl_count;
    fh.display_pos_x = draw_data->DisplayPos.x;
    fh.display_pos_y = draw_data->DisplayPos.y;
    fh.display_size_x = draw_data->DisplaySize.x;
    fh.display_size_y = draw_data->DisplaySize.y;
    fh.fb_scale_x = draw_data->FramebufferScale.x;
    fh.fb_scale_y = draw_data->FramebufferScale.y;

    /* Ship the frame as one record targeted at the figure's child id.
     * Payload = [u32 sub_op] + [yetty_ymgui_wire_frame] + cmd lists.
     * Outer length covers the sub_op + the entire frame total_size. */
    uint32_t sub_op = YETTY_YMGUI_FIGURE_SUB_FRAME;
    uint32_t outer_len = (uint32_t)(sizeof(sub_op) + total_size);
    struct yetty_yfigure_wire_record outer = {outer_len, figure_id};
    if (!yetty_yface_start_write(g_state.yface_out, YETTY_OSC_YCOMPOSITOR_BIN,
                                 /*compressed=*/1, /*args=*/NULL, 0).ok) {
        return;
    }
    if (!yetty_yface_write(g_state.yface_out, &outer, sizeof(outer)).ok) return;
    if (!yetty_yface_write(g_state.yface_out, &sub_op, sizeof(sub_op)).ok) return;
    if (!yetty_yface_write(g_state.yface_out, &fh, sizeof(fh)).ok) {
        return;
    }

    static const uint8_t pad[4] = {0, 0, 0, 0};

    /* Pass 3: emit. */
    for (int n = 0; n < cl_count; ++n) {
        const ImDrawList *cl = draw_data->CmdLists[n];
        const slot_plan &p = plans[(size_t)n];

        struct yetty_ymgui_wire_cmd_list cl_hdr = {};

        if (p.mode == SLOT_REPEAT) {
            cl_hdr.flags = YMGUI_CMDLIST_FLAG_REPEAT;
            if (!yetty_yface_write(g_state.yface_out, &cl_hdr, sizeof(cl_hdr)).ok) {
                return;
            }
            continue;
        }

        if (p.mode == SLOT_CMD_DIFF) {
            cl_hdr.vtx_count = 0;
            cl_hdr.idx_count = 0;
            cl_hdr.cmd_count = (uint32_t)p.cmd_hashes.size();
            cl_hdr.flags = YMGUI_CMDLIST_FLAG_CMD_DIFF;
            if (!yetty_yface_write(g_state.yface_out, &cl_hdr, sizeof(cl_hdr)).ok) {
                return;
            }
            uint32_t hash_count = (uint32_t)p.cmd_hashes.size();
            uint32_t inl = p.inline_count;
            if (!yetty_yface_write(g_state.yface_out, &hash_count, sizeof(hash_count)).ok) {
                return;
            }
            if (!yetty_yface_write(g_state.yface_out, &inl, sizeof(inl)).ok) {
                return;
            }
            if (hash_count) {
                if (!yetty_yface_write(g_state.yface_out, p.cmd_hashes.data(),
                                       hash_count * sizeof(uint64_t))
                         .ok) {
                    return;
                }
            }
            for (size_t k = 0; k < p.cmd_hashes.size(); k++) {
                if (!p.cmd_inline[k]) {
                    continue;
                }
                const ImDrawCmd *dc = p.cmd_ptr[k];
                uint32_t vc = p.cmd_vtx_counts[k];
                struct yetty_ymgui_wire_cmd_inline ih = {};
                ih.hash = p.cmd_hashes[k];
                ih.vtx_count = vc;
                ih.flags = 0;
                if (!yetty_yface_write(g_state.yface_out, &ih, sizeof(ih)).ok) {
                    return;
                }
                struct yetty_ymgui_wire_cmd wc = {};
                wc.clip_min_x = dc->ClipRect.x;
                wc.clip_min_y = dc->ClipRect.y;
                wc.clip_max_x = dc->ClipRect.z;
                wc.clip_max_y = dc->ClipRect.w;
                wc.tex_id = (uint32_t)(intptr_t)dc->GetTexID();
                wc.vtx_offset = 0; /* server reassigns when packing */
                wc.idx_offset = 0;
                wc.elem_count = dc->ElemCount;
                if (!yetty_yface_write(g_state.yface_out, &wc, sizeof(wc)).ok) {
                    return;
                }
                if (vc) {
                    if (!yetty_yface_write(g_state.yface_out,
                                           cl->VtxBuffer.Data + dc->VtxOffset,
                                           (size_t)vc * sizeof(ImDrawVert))
                             .ok) {
                        return;
                    }
                }
                if (dc->ElemCount) {
                    size_t nbytes = (size_t)dc->ElemCount * sizeof(ImDrawIdx);
                    if (!yetty_yface_write(g_state.yface_out,
                                           cl->IdxBuffer.Data + dc->IdxOffset, nbytes)
                             .ok) {
                        return;
                    }
                    size_t rem = nbytes & 3u;
                    if (rem) {
                        if (!yetty_yface_write(g_state.yface_out, pad, 4u - rem).ok) {
                            return;
                        }
                    }
                }
            }
            continue;
        }

        /* SLOT_FULL */
        cl_hdr.vtx_count = (uint32_t)cl->VtxBuffer.Size;
        cl_hdr.idx_count = (uint32_t)cl->IdxBuffer.Size;
        cl_hdr.cmd_count = (uint32_t)cl->CmdBuffer.Size;
        cl_hdr.flags = 0;
        if (!yetty_yface_write(g_state.yface_out, &cl_hdr, sizeof(cl_hdr)).ok) {
            return;
        }

        if (cl_hdr.vtx_count) {
            size_t nbytes = (size_t)cl_hdr.vtx_count * sizeof(ImDrawVert);
            if (!yetty_yface_write(g_state.yface_out, cl->VtxBuffer.Data, nbytes).ok) {
                return;
            }
        }
        if (cl_hdr.idx_count) {
            size_t nbytes = (size_t)cl_hdr.idx_count * sizeof(ImDrawIdx);
            if (!yetty_yface_write(g_state.yface_out, cl->IdxBuffer.Data, nbytes).ok) {
                return;
            }
            size_t rem = nbytes & 3u;
            if (rem) {
                if (!yetty_yface_write(g_state.yface_out, pad, 4u - rem).ok) {
                    return;
                }
            }
        }

        for (int i = 0; i < cl->CmdBuffer.Size; ++i) {
            const ImDrawCmd *dc = &cl->CmdBuffer[i];
            if (dc->UserCallback) {
                continue;
            }
            struct yetty_ymgui_wire_cmd wc = {};
            wc.clip_min_x = dc->ClipRect.x;
            wc.clip_min_y = dc->ClipRect.y;
            wc.clip_max_x = dc->ClipRect.z;
            wc.clip_max_y = dc->ClipRect.w;
            wc.tex_id = (uint32_t)(intptr_t)dc->GetTexID();
            wc.vtx_offset = dc->VtxOffset;
            wc.idx_offset = dc->IdxOffset;
            wc.elem_count = dc->ElemCount;
            if (!yetty_yface_write(g_state.yface_out, &wc, sizeof(wc)).ok) {
                return;
            }
        }
    }

    if (!yetty_yface_finish_write(g_state.yface_out).ok) {
        return;
    }
    if (flush_yface_to_fd() < 0) {
        return;
    }

    /* Frame fully shipped — refresh caches so they match what the server
     * now holds. Stage 1: cl_hashes. Stage 2: per-slot cmd_hashes. */
    c->prev_cl_hashes = std::move(new_cl_hashes);
    c->prev_cmd_hashes.resize((size_t)cl_count);
    for (int n = 0; n < cl_count; n++) {
        slot_plan &p = plans[(size_t)n];
        if (p.mode == SLOT_REPEAT) {
            /* Server kept its prev — our cache also kept its prev. */
            continue;
        }
        if (p.mode == SLOT_CMD_DIFF) {
            c->prev_cmd_hashes[(size_t)n] = std::move(p.cmd_hashes);
            continue;
        }
        /* SLOT_FULL — recompute fresh per-cmd hashes from the just-sent
         * cmd_list so the next frame can do Stage 2 against it. */
        const ImDrawList *cl = draw_data->CmdLists[n];
        std::vector<uint64_t> &dst = c->prev_cmd_hashes[(size_t)n];
        dst.clear();
        for (int k = 0; k < cl->CmdBuffer.Size; k++) {
            const ImDrawCmd *dc = &cl->CmdBuffer[k];
            if (dc->UserCallback || dc->ElemCount == 0) {
                continue;
            }
            uint32_t vc = cmd_vtx_count(dc, cl);
            dst.push_back(hash_cmd(dc, cl, vc));
        }
    }
}

/*===========================================================================
 * Platform — raw mode + DEC ?1500/?1501 subscription
 *=========================================================================*/

#ifndef _WIN32
static bool platform_set_raw_mode(int fd, struct termios *saved)
{
    if (tcgetattr(fd, saved) != 0) {
        return false;
    }
    struct termios raw = *saved;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        return false;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return true;
}

static void platform_restore_termios(int fd, const struct termios *saved)
{
    tcsetattr(fd, TCSANOW, saved);
}
#endif

bool yetty_ymgui_ImGui_ImplYetty_PlatformInit(void)
{
#ifdef _WIN32
    return false;
#else
    if (!platform_set_raw_mode(g_state.in_fd, &g_state.saved_termios)) {
        return false;
    }
    g_state.raw_mode_active = 1;

    /* Stdout stays in default (blocking) mode so partial OSC writes can
     * never happen — kernel just blocks the writer until the receiver
     * drains. flush_yface_to_fd's blocking-drain path covers the case
     * if anything else flips O_NONBLOCK on the fd. */

    /* Subscribe: \e[?1500h \e[?1501h. */
    static const char subscribe[] = "\033[?1500h\033[?1501h";
    ssize_t w = write(g_state.out_fd, subscribe, sizeof(subscribe) - 1);
    (void)w;
    return true;
#endif
}

void yetty_ymgui_ImGui_ImplYetty_PlatformShutdown(void)
{
#ifndef _WIN32
    if (g_state.raw_mode_active) {
        static const char unsubscribe[] = "\033[?1500l\033[?1501l";
        ssize_t w = write(g_state.out_fd, unsubscribe, sizeof(unsubscribe) - 1);
        (void)w;
        platform_restore_termios(g_state.in_fd, &g_state.saved_termios);
        g_state.raw_mode_active = 0;
    }
#endif
}

/*===========================================================================
 * Push-input hooks — feed events into the right figure's ImGuiIO
 *=========================================================================*/

namespace
{
struct context_scope {
    ImGuiContext *prev;
    explicit context_scope(ImGuiContext *next) : prev(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(next);
    }
    ~context_scope()
    {
        ImGui::SetCurrentContext(prev);
    }
};
} // namespace

void yetty_ymgui_ImGui_ImplYetty_OnFigureMousePos(uint32_t figure_id, double x, double y,
                                                uint32_t buttons_held)
{
    (void)buttons_held;
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    context_scope cs(c->ctx);
    ImGui::GetIO().AddMousePosEvent((float)x, (float)y);
}

void yetty_ymgui_ImGui_ImplYetty_OnFigureMouseButton(uint32_t figure_id, int button, int pressed,
                                                   double x, double y)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    context_scope cs(c->ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)x, (float)y);
    if (button >= 0 && button < 5) {
        io.AddMouseButtonEvent(button, pressed != 0);
    }
}

void yetty_ymgui_ImGui_ImplYetty_OnFigureMouseWheel(uint32_t figure_id, double dy, double x, double y)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    context_scope cs(c->ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)x, (float)y);
    io.AddMouseWheelEvent(0.0f, (float)dy);
}

void yetty_ymgui_ImGui_ImplYetty_OnFigureResize(uint32_t figure_id, double width, double height)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    if (width <= 0.0 || height <= 0.0) {
        return;
    }
    c->w_pixels = (float)width;
    c->h_pixels = (float)height;
    context_scope cs(c->ctx);
    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
}

void yetty_ymgui_ImGui_ImplYetty_OnFigureFocus(uint32_t figure_id, int gained)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    if (gained) {
        g_state.focused_figure_id = figure_id;
    } else if (g_state.focused_figure_id == figure_id) {
        g_state.focused_figure_id = 0;
    }

    context_scope cs(c->ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.AddFocusEvent(gained != 0);
}

/* Map a GLFW keycode to ImGui's key enum. Only covers the common keys
 * the demo and typical apps use; expand as needed. Unknown keys map to
 * ImGuiKey_None — the codepoint path still works for character entry. */
static ImGuiKey glfw_to_imgui_key(int key)
{
    switch (key) {
    case 32:
        return ImGuiKey_Space;
    case 39:
        return ImGuiKey_Apostrophe;
    case 44:
        return ImGuiKey_Comma;
    case 45:
        return ImGuiKey_Minus;
    case 46:
        return ImGuiKey_Period;
    case 47:
        return ImGuiKey_Slash;
    case 48:
        return ImGuiKey_0;
    case 49:
        return ImGuiKey_1;
    case 50:
        return ImGuiKey_2;
    case 51:
        return ImGuiKey_3;
    case 52:
        return ImGuiKey_4;
    case 53:
        return ImGuiKey_5;
    case 54:
        return ImGuiKey_6;
    case 55:
        return ImGuiKey_7;
    case 56:
        return ImGuiKey_8;
    case 57:
        return ImGuiKey_9;
    case 59:
        return ImGuiKey_Semicolon;
    case 61:
        return ImGuiKey_Equal;
    case 65:
        return ImGuiKey_A;
    case 66:
        return ImGuiKey_B;
    case 67:
        return ImGuiKey_C;
    case 68:
        return ImGuiKey_D;
    case 69:
        return ImGuiKey_E;
    case 70:
        return ImGuiKey_F;
    case 71:
        return ImGuiKey_G;
    case 72:
        return ImGuiKey_H;
    case 73:
        return ImGuiKey_I;
    case 74:
        return ImGuiKey_J;
    case 75:
        return ImGuiKey_K;
    case 76:
        return ImGuiKey_L;
    case 77:
        return ImGuiKey_M;
    case 78:
        return ImGuiKey_N;
    case 79:
        return ImGuiKey_O;
    case 80:
        return ImGuiKey_P;
    case 81:
        return ImGuiKey_Q;
    case 82:
        return ImGuiKey_R;
    case 83:
        return ImGuiKey_S;
    case 84:
        return ImGuiKey_T;
    case 85:
        return ImGuiKey_U;
    case 86:
        return ImGuiKey_V;
    case 87:
        return ImGuiKey_W;
    case 88:
        return ImGuiKey_X;
    case 89:
        return ImGuiKey_Y;
    case 90:
        return ImGuiKey_Z;
    case 256:
        return ImGuiKey_Escape;
    case 257:
        return ImGuiKey_Enter;
    case 258:
        return ImGuiKey_Tab;
    case 259:
        return ImGuiKey_Backspace;
    case 260:
        return ImGuiKey_Insert;
    case 261:
        return ImGuiKey_Delete;
    case 262:
        return ImGuiKey_RightArrow;
    case 263:
        return ImGuiKey_LeftArrow;
    case 264:
        return ImGuiKey_DownArrow;
    case 265:
        return ImGuiKey_UpArrow;
    case 266:
        return ImGuiKey_PageUp;
    case 267:
        return ImGuiKey_PageDown;
    case 268:
        return ImGuiKey_Home;
    case 269:
        return ImGuiKey_End;
    case 280:
        return ImGuiKey_CapsLock;
    case 290:
        return ImGuiKey_F1;
    case 291:
        return ImGuiKey_F2;
    case 292:
        return ImGuiKey_F3;
    case 293:
        return ImGuiKey_F4;
    case 294:
        return ImGuiKey_F5;
    case 295:
        return ImGuiKey_F6;
    case 296:
        return ImGuiKey_F7;
    case 297:
        return ImGuiKey_F8;
    case 298:
        return ImGuiKey_F9;
    case 299:
        return ImGuiKey_F10;
    case 300:
        return ImGuiKey_F11;
    case 301:
        return ImGuiKey_F12;
    case 340:
        return ImGuiKey_LeftShift;
    case 341:
        return ImGuiKey_LeftCtrl;
    case 342:
        return ImGuiKey_LeftAlt;
    case 343:
        return ImGuiKey_LeftSuper;
    case 344:
        return ImGuiKey_RightShift;
    case 345:
        return ImGuiKey_RightCtrl;
    case 346:
        return ImGuiKey_RightAlt;
    case 347:
        return ImGuiKey_RightSuper;
    default:
        return ImGuiKey_None;
    }
}

void yetty_ymgui_ImGui_ImplYetty_OnFigureKey(uint32_t figure_id, int kind, int key, int mods,
                                           uint32_t codepoint)
{
    auto *c = find_figure(figure_id);
    if (!c) {
        return;
    }
    context_scope cs(c->ctx);
    ImGuiIO &io = ImGui::GetIO();
    /* GLFW mods bitmask: SHIFT=1, CTRL=2, ALT=4, SUPER=8. */
    io.AddKeyEvent(ImGuiMod_Shift, (mods & 1) != 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & 2) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & 4) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & 8) != 0);

    if (kind == YETTY_YMGUI_INPUT_KEY_CHAR) {
        if (codepoint) {
            io.AddInputCharacter(codepoint);
        }
    } else {
        ImGuiKey ikey = glfw_to_imgui_key(key);
        if (ikey != ImGuiKey_None) {
            io.AddKeyEvent(ikey, kind == YETTY_YMGUI_INPUT_KEY_DOWN);
        }
    }
}

/*===========================================================================
 * Sync PollInput — drains stdin through yface
 *=========================================================================*/

extern "C" {
static void poll_on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                        const uint8_t *payload, size_t len)
{
    (void)user;
    (void)args;
    (void)args_len;
    switch (osc_code) {
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE: {
        if (len < sizeof(struct yetty_client_input_mouse)) {
            return;
        }
        const struct yetty_client_input_mouse *m =
            (const struct yetty_client_input_mouse *)payload;
        if (m->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            return;
        }
        switch (m->kind) {
        case YETTY_YMGUI_INPUT_MOUSE_POS:
            yetty_ymgui_ImGui_ImplYetty_OnFigureMousePos(m->figure_id, m->x, m->y, m->buttons_held);
            break;
        case YETTY_YMGUI_INPUT_MOUSE_BUTTON:
            yetty_ymgui_ImGui_ImplYetty_OnFigureMouseButton(m->figure_id, m->button, m->pressed, m->x,
                                                          m->y);
            break;
        case YETTY_YMGUI_INPUT_MOUSE_WHEEL:
            yetty_ymgui_ImGui_ImplYetty_OnFigureMouseWheel(m->figure_id, m->wheel_dy, m->x, m->y);
            break;
        }
        break;
    }
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE: {
        if (len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *r =
            (const struct yetty_client_input_resize *)payload;
        if (r->magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC) {
            return;
        }
        yetty_ymgui_ImGui_ImplYetty_OnFigureResize(r->figure_id, r->width, r->height);
        break;
    }
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS: {
        if (len < sizeof(struct yetty_client_input_focus)) {
            return;
        }
        const struct yetty_client_input_focus *f =
            (const struct yetty_client_input_focus *)payload;
        if (f->magic != YETTY_CLIENT_INPUT_FOCUS_MAGIC) {
            return;
        }
        yetty_ymgui_ImGui_ImplYetty_OnFigureFocus(f->figure_id, f->gained);
        break;
    }
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY: {
        if (len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *k =
            (const struct yetty_client_input_key *)payload;
        if (k->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        yetty_ymgui_ImGui_ImplYetty_OnFigureKey(k->figure_id, (int)k->kind, k->key, k->mods,
                                              k->codepoint);
        break;
    }
    default:
        break;
    }
}
} /* extern "C" */

void yetty_ymgui_ImGui_ImplYetty_PollInput(void)
{
#ifdef _WIN32
    return;
#else
    if (!g_state.raw_mode_active || !g_state.yface_in) {
        return;
    }

    static int handlers_set = 0;
    if (!handlers_set) {
        yetty_yface_set_handlers(g_state.yface_in, poll_on_osc, nullptr, &g_state);
        handlers_set = 1;
    }

    char buf[4096];
    for (;;) {
        ssize_t n = read(g_state.in_fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        yetty_yface_feed_bytes(g_state.yface_in, buf, (size_t)n);
    }
#endif
}

bool yetty_ymgui_ImGui_ImplYetty_WaitInput(int timeout_ms)
{
#ifdef _WIN32
    if (timeout_ms > 0) {
        Sleep((DWORD)timeout_ms);
    }
    return false;
#else
    struct pollfd pfd[2];
    int nfds = 1;
    pfd[0].fd = g_state.in_fd;
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;
    if (yetty_ymgui_pending_active()) {
        pfd[1].fd = g_state.out_fd;
        pfd[1].events = POLLOUT;
        pfd[1].revents = 0;
        nfds = 2;
    }
    int n = poll(pfd, (nfds_t)nfds, timeout_ms);
    if (n <= 0) {
        return false;
    }
    if (nfds == 2 && (pfd[1].revents & POLLOUT)) {
        yetty_ymgui_pending_flush(g_state.out_fd);
    }
    return (pfd[0].revents & POLLIN) != 0;
#endif
}

/*===========================================================================
 * Async — bridge ymgui_event_loop callbacks into per-figure ImGuiIO
 *=========================================================================*/

extern "C" {
static void loop_on_pos(void *u, uint32_t figure_id, double x, double y, uint32_t held)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureMousePos(figure_id, x, y, held);
}
static void loop_on_btn(void *u, uint32_t figure_id, int b, int p, double x, double y)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureMouseButton(figure_id, b, p, x, y);
}
static void loop_on_wheel(void *u, uint32_t figure_id, double dy, double x, double y)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureMouseWheel(figure_id, dy, x, y);
}
static void loop_on_resize(void *u, uint32_t figure_id, double w, double h)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureResize(figure_id, w, h);
}
static void loop_on_focus(void *u, uint32_t figure_id, int gained)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureFocus(figure_id, gained);
}
static void loop_on_key(void *u, uint32_t figure_id, int kind, int key, int mods, uint32_t codepoint)
{
    (void)u;
    yetty_ymgui_ImGui_ImplYetty_OnFigureKey(figure_id, kind, key, mods, codepoint);
}
} /* extern "C" */

void yetty_ymgui_ImGui_ImplYetty_AttachEventLoop(struct yetty_yclient_event_loop *loop)
{
    if (!loop) {
        return;
    }
    yetty_yclient_event_loop_set_user(loop, &g_state);
    yetty_yclient_event_loop_set_mouse_pos_cb(loop, loop_on_pos);
    yetty_yclient_event_loop_set_mouse_button_cb(loop, loop_on_btn);
    yetty_yclient_event_loop_set_mouse_wheel_cb(loop, loop_on_wheel);
    yetty_yclient_event_loop_set_resize_cb(loop, loop_on_resize);
    yetty_yclient_event_loop_set_focus_cb(loop, loop_on_focus);
    yetty_yclient_event_loop_set_key_cb(loop, loop_on_key);
    /* The new figure-tree wire no longer triggers an initial server-side
     * SC_RESIZE, so the render-on-input loop would never fire on_frame
     * without an explicit nudge. Kick once so any figures already created
     * before attach get a first render. */
    yetty_yclient_event_loop_request_frame(loop);
}
