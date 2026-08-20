/*
 * pane.c — one ymux pane: class@ymux:pane (#695 phase 2).
 *
 * Composes the independent terminal engine with the tiered history store:
 * the engine's scroll_out intake streams aged primary rows into history,
 * and the pane exposes one coherent TIMELINE address space over
 * [floor, pushed + live rows): indices below `pushed` resolve from the
 * store, indices at/above it resolve from the engine's live screen. This
 * is the server-side substrate the viewport projector reads.
 *
 * The pane forwards the remaining engine host callbacks (PTY output,
 * clipboard, bell, title) to its own host table — the daemon session layer
 * plugs in there.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>

#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/rich.h>
#include <yetty/yface/yface.h>
#include <yetty/ytrace/ytrace.h>

#include <lz4frame.h>

/* The yetty vendor DCS code carrying rich drawable payloads (row-anchored
 * ydraw records, #695 v1 scope). Other codes forward to the pane host. */
enum { YMUX_PANE_RICH_DCS_CODE = 600001 };

/* The pane — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:pane") yetty_ymux_pane {
    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *history;
    /* Canonical rich state (#695): minted from vendor DCS envelopes, owned
     * by the pane — it exists (and survives) with zero attachments. */
    struct yetty_yclass_object *rich;

    /* The pane's upward host (daemon/session layer). Copied by value. */
    struct yetty_ymux_engine_host host;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_pane_class_get(void);
struct yetty_ymux_pane_ptr_result yetty_ymux_pane_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_pane_ptr, struct yetty_ymux_pane *);

/*===========================================================================
 * Engine-host trampolines: history intake stays inside the pane; the rest
 * forwards to the pane's host.
 *=========================================================================*/

static struct yetty_ycore_void_result pane_scroll_out(const struct yetty_ymux_cell *cells,
                                                      uint32_t cols, uint64_t logical_line_id,
                                                      uint32_t logical_cell_start, int continuation,
                                                      void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    return yetty_ymux_history_push(pane->history, cells, cols, logical_line_id, logical_cell_start,
                                   continuation);
}

static struct yetty_ycore_void_result pane_output(const char *bytes, size_t len, void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    if (!pane->host.output) {
        return YETTY_OK_VOID();
    }
    return pane->host.output(bytes, len, pane->host.userdata);
}

static struct yetty_ycore_void_result pane_clipboard(const char *text, size_t len, int clipboard,
                                                     void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    if (!pane->host.clipboard) {
        return YETTY_OK_VOID();
    }
    return pane->host.clipboard(text, len, clipboard, pane->host.userdata);
}

static struct yetty_ycore_void_result pane_bell(void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    if (!pane->host.bell) {
        return YETTY_OK_VOID();
    }
    return pane->host.bell(pane->host.userdata);
}

static struct yetty_ycore_void_result pane_title(const char *title, size_t len, void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    if (!pane->host.title) {
        return YETTY_OK_VOID();
    }
    return pane->host.title(title, len, pane->host.userdata);
}

/* Vendor DCS intake: the rich drawable code mints into the pane's store
 * (payload base64-decoded here — the engine stays codec-free); other
 * codes forward to the pane host untouched. */
static struct yetty_ycore_void_result pane_rich(uint32_t code, const char *payload, size_t len,
                                                uint64_t logical_line_id,
                                                uint32_t logical_cell_offset, void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    if (code != YMUX_PANE_RICH_DCS_CODE) {
        if (!pane->host.rich) {
            return YETTY_OK_VOID();
        }
        return pane->host.rich(code, payload, len, logical_line_id, logical_cell_offset,
                               pane->host.userdata);
    }
    /* Envelope forms with an empty args slot lead with ';' — skip it. */
    if (len && payload[0] == ';') {
        ++payload;
        --len;
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    char *decoded = malloc(len); /* decoded is always shorter than b64 */
    if (!decoded) {
        return YETTY_ERR(yetty_ycore_void, "ymux pane rich: decode alloc");
    }
    /* The yface wire framing joins independently-base64-padded chunks with ';'.
     * Decode each ';'-separated chunk on its own and concatenate — a single
     * base64 pass stops at the first chunk's '=' padding + ';' separator, which
     * left only the leading container header decoded and dropped the whole
     * figure (ycat SVG/PDF/image rendered nothing under ymux). */
    size_t decoded_len = 0;
    size_t chunk_start = 0;
    for (size_t index = 0; index <= len; ++index) {
        if (index == len || payload[index] == ';') {
            size_t chunk_len = index - chunk_start;
            if (chunk_len > 0 && decoded_len < len) {
                decoded_len += yetty_ycore_base64_decode(payload + chunk_start, chunk_len,
                                                         decoded + decoded_len, len - decoded_len);
            }
            chunk_start = index + 1;
        }
    }
    ydebug("ymux pane_rich: code=%u b64_len=%zu decoded_len=%zu (%zu words)", code, len,
           decoded_len, decoded_len / sizeof(uint32_t));
    /* The decoded bytes are a yface BIN envelope: a 32-byte bin_meta header
     * (magic + compressed flag + raw_size) then the ydraw payload, LZ4F-
     * compressed when the meta says so. The normal terminal unwraps this in its
     * wire state machine; the daemon's libvterm does not, so unwrap here to hand
     * the rich store the raw ydraw records. */
    if (decoded_len >= sizeof(struct yetty_yface_bin_meta)) {
        struct yetty_yface_bin_meta meta;
        memcpy(&meta, decoded, sizeof(meta));
        if (meta.magic == YETTY_YFACE_BIN_MAGIC) {
            const char *body = decoded + sizeof(meta);
            size_t body_len = decoded_len - sizeof(meta);
            if (meta.compressed == YETTY_YFACE_COMP_LZ4F) {
                size_t raw_cap = meta.raw_size ? (size_t)meta.raw_size : body_len * 8 + 64;
                char *raw = malloc(raw_cap);
                if (!raw) {
                    free(decoded);
                    return YETTY_ERR(yetty_ycore_void, "ymux pane rich: lz4 out alloc");
                }
                LZ4F_dctx *dctx = NULL;
                if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION))) {
                    free(raw);
                    free(decoded);
                    return YETTY_ERR(yetty_ycore_void, "ymux pane rich: lz4 ctx");
                }
                size_t out_pos = 0, in_pos = 0;
                while (in_pos < body_len && out_pos < raw_cap) {
                    size_t out_left = raw_cap - out_pos;
                    size_t in_left = body_len - in_pos;
                    size_t hint = LZ4F_decompress(dctx, raw + out_pos, &out_left, body + in_pos,
                                                  &in_left, NULL);
                    out_pos += out_left;
                    in_pos += in_left;
                    if (LZ4F_isError(hint) || hint == 0 || (out_left == 0 && in_left == 0)) {
                        break;
                    }
                }
                LZ4F_freeDecompressionContext(dctx);
                free(decoded);
                decoded = raw;
                decoded_len = out_pos;
            } else {
                memmove(decoded, body, body_len);
                decoded_len = body_len;
            }
            ydebug("ymux pane_rich: unwrapped BIN -> %zu bytes (%zu words) compressed=%u",
                   decoded_len, decoded_len / sizeof(uint32_t), meta.compressed);
        }
    }
    if (decoded_len == 0 || (decoded_len % sizeof(uint32_t)) != 0) {
        free(decoded);
        return YETTY_ERR(yetty_ycore_void, "ymux pane rich: bad payload");
    }
    /* Reserve the figure's row span in the pane — advance the cursor past it
     * (fed after this callback returns, in engine_feed) so the next prompt lands
     * BELOW the figure and it scrolls with the text, exactly like a local pane.
     * The figure's pixel height is the YPB1 container's scene_max_y (offset 16). */
    uint32_t span_rows = 1;
    if (decoded_len >= 24 && *(const uint32_t *)decoded == 0x31425059u /* 'YPB1' */) {
        float scene_max_y = 0.0f;
        memcpy(&scene_max_y, decoded + 16, sizeof(scene_max_y));
        if (scene_max_y > 0.0f) {
            struct yetty_ycore_uint32_result reserve_res =
                yetty_ymux_engine_reserve_rich_rows(pane->engine, (uint32_t)scene_max_y);
            if (YETTY_IS_OK(reserve_res)) {
                span_rows = reserve_res.value;
            } else {
                yetty_ycore_error_destroy(reserve_res.error);
            }
        }
    }
    struct yetty_ycore_uint64_result mint_res = yetty_ymux_rich_mint(
        pane->rich, (const uint32_t *)decoded, (uint32_t)(decoded_len / sizeof(uint32_t)),
        YETTY_YMUX_RICH_ANCHOR_PRIMARY, logical_line_id, logical_cell_offset, span_rows);
    free(decoded);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mint_res, "ymux pane rich: mint");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_pane_make(
    uint32_t rows, uint32_t cols, uint32_t hot_rows, uint64_t total_row_cap,
    const struct yetty_ymux_engine_host *host)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_pane_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux pane_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux pane_make: alloc");
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(object_res.value);
    if (YETTY_IS_ERR(pane_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux pane_make: from_obj", pane_res);
    }
    struct yetty_ymux_pane *pane = pane_res.value;
    if (host) {
        pane->host = *host;
    }

    struct yetty_yclass_object_ptr_result history_res =
        yetty_ymux_history_make(hot_rows, total_row_cap);
    if (YETTY_IS_ERR(history_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux pane_make: history", history_res);
    }
    pane->history = history_res.value;

    struct yetty_yclass_object_ptr_result rich_res = yetty_ymux_rich_make();
    if (YETTY_IS_ERR(rich_res)) {
        struct yetty_ycore_void_result history_dispose_res =
            yetty_ymux_history_dispose(pane->history);
        if (YETTY_IS_ERR(history_dispose_res)) {
            yetty_ycore_error_destroy(history_dispose_res.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux pane_make: rich", rich_res);
    }
    pane->rich = rich_res.value;

    struct yetty_ymux_engine_host engine_host = {
        .output = pane_output,
        .clipboard = pane_clipboard,
        .bell = pane_bell,
        .title = pane_title,
        .scroll_out = pane_scroll_out,
        .rich = pane_rich,
        .userdata = pane,
    };
    struct yetty_yclass_object_ptr_result engine_res =
        yetty_ymux_engine_make(rows, cols, &engine_host);
    if (YETTY_IS_ERR(engine_res)) {
        struct yetty_ycore_void_result rich_dispose_res = yetty_ymux_rich_dispose(pane->rich);
        if (YETTY_IS_ERR(rich_dispose_res)) {
            yetty_ycore_error_destroy(rich_dispose_res.error);
        }
        struct yetty_ycore_void_result history_dispose_res =
            yetty_ymux_history_dispose(pane->history);
        if (YETTY_IS_ERR(history_dispose_res)) {
            yetty_ycore_error_destroy(history_dispose_res.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux pane_make: engine", engine_res);
    }
    pane->engine = engine_res.value;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_pane_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pane_res, "ymux pane_dispose: from_obj");
    struct yetty_ymux_pane *pane = pane_res.value;
    struct yetty_ycore_error first_error = {0};
    int have_error = 0;
    if (pane->engine) {
        struct yetty_ycore_void_result engine_res = yetty_ymux_engine_dispose(pane->engine);
        if (YETTY_IS_ERR(engine_res)) {
            first_error = engine_res.error;
            have_error = 1;
        }
        pane->engine = NULL;
    }
    if (pane->history) {
        struct yetty_ycore_void_result history_res = yetty_ymux_history_dispose(pane->history);
        if (YETTY_IS_ERR(history_res)) {
            if (!have_error) {
                first_error = history_res.error;
                have_error = 1;
            } else {
                yetty_ycore_error_destroy(history_res.error);
            }
        }
        pane->history = NULL;
    }
    if (pane->rich) {
        struct yetty_ycore_void_result rich_res = yetty_ymux_rich_dispose(pane->rich);
        if (YETTY_IS_ERR(rich_res)) {
            if (!have_error) {
                first_error = rich_res.error;
                have_error = 1;
            } else {
                yetty_ycore_error_destroy(rich_res.error);
            }
        }
        pane->rich = NULL;
    }
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(free_res)) {
        if (!have_error) {
            first_error = free_res.error;
            have_error = 1;
        } else {
            yetty_ycore_error_destroy(free_res.error);
        }
    }
    if (have_error) {
        struct yetty_ycore_void_result out = {.ok = false, .error = first_error};
        return out;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Passthroughs + timeline resolution.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_pane_feed(struct yetty_yclass_object *obj,
                                                    const char *bytes, size_t len)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pane_res, "ymux pane_feed: from_obj");
    return yetty_ymux_engine_feed(pane_res.value->engine, bytes, len);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_pane_resize(struct yetty_yclass_object *obj,
                                                      uint32_t rows, uint32_t cols)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pane_res, "ymux pane_resize: from_obj");
    return yetty_ymux_engine_resize(pane_res.value->engine, rows, cols);
}

/* The composed engine (input encoding, snapshot, cursor, dims — callers use
 * the engine API directly on this object). Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_pane_engine(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, pane_res, "ymux pane_engine: from_obj");
    return YETTY_OK(yetty_yclass_object_ptr, pane_res.value->engine);
}

/* The composed history store. Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_pane_history(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, pane_res, "ymux pane_history: from_obj");
    return YETTY_OK(yetty_yclass_object_ptr, pane_res.value->history);
}

/* The pane's canonical rich store (#695). Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_pane_rich_store(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, pane_res, "ymux pane_rich_store: from_obj");
    return YETTY_OK(yetty_yclass_object_ptr, pane_res.value->rich);
}

/* Timeline geometry: [floor, live_top) is history; [live_top,
 * live_top + rows) is the live screen (live_top == pushed rows). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_pane_timeline(struct yetty_yclass_object *obj,
                                                        uint64_t *out_floor, uint64_t *out_live_top)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pane_res, "ymux pane_timeline: from_obj");
    struct yetty_ymux_pane *pane = pane_res.value;
    if (out_floor) {
        struct yetty_ycore_uint64_result floor_res = yetty_ymux_history_floor(pane->history);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, floor_res, "ymux pane_timeline: floor");
        *out_floor = floor_res.value;
    }
    if (out_live_top) {
        struct yetty_ycore_uint64_result pushed_res = yetty_ymux_history_pushed_rows(pane->history);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pushed_res, "ymux pane_timeline: pushed");
        *out_live_top = pushed_res.value;
    }
    return YETTY_OK_VOID();
}

/* Resolve ANY timeline row (history or live). Pointers are borrowed and
 * valid only until the next mutation — the projector copies. Alt-screen
 * panes serve the alt surface for live rows; history rows always come from
 * the primary timeline. */
YETTY_ANNOTATE("expose")
struct yetty_ymux_history_row_result yetty_ymux_pane_resolve_row(struct yetty_yclass_object *obj,
                                                                 uint64_t timeline_idx)
{
    struct yetty_ymux_pane_ptr_result pane_res = yetty_ymux_pane_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, pane_res, "ymux pane_resolve_row: from_obj");
    struct yetty_ymux_pane *pane = pane_res.value;
    struct yetty_ycore_uint64_result pushed_res = yetty_ymux_history_pushed_rows(pane->history);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, pushed_res, "ymux pane_resolve_row: pushed");
    if (timeline_idx < pushed_res.value) {
        return yetty_ymux_history_resolve(pane->history, timeline_idx);
    }
    uint32_t rows = 0;
    struct yetty_ycore_void_result dims_res = yetty_ymux_engine_dims(pane->engine, &rows, NULL);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, dims_res, "ymux pane_resolve_row: dims");
    uint64_t live_index = timeline_idx - pushed_res.value;
    if (live_index >= rows) {
        return YETTY_ERR(yetty_ymux_history_row, "ymux pane_resolve_row: beyond live bottom");
    }
    struct yetty_ymux_cell_const_ptr_result cells_res =
        yetty_ymux_engine_row_cells(pane->engine, (uint32_t)live_index);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, cells_res, "ymux pane_resolve_row: cells");
    if (!cells_res.value) {
        return YETTY_ERR(yetty_ymux_history_row, "ymux pane_resolve_row: row unavailable");
    }
    uint64_t logical_line_id = 0;
    uint32_t logical_cell_start = 0;
    int continuation = 0;
    struct yetty_ycore_void_result identity_res = yetty_ymux_engine_row_identity(
        pane->engine, (uint32_t)live_index, &logical_line_id, &logical_cell_start, &continuation);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, identity_res, "ymux pane_resolve_row: identity");
    uint32_t cols = 0;
    struct yetty_ycore_void_result cols_res = yetty_ymux_engine_dims(pane->engine, NULL, &cols);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, cols_res, "ymux pane_resolve_row: cols");
    struct yetty_ymux_history_row row = {
        .cells = cells_res.value,
        .cols = cols,
        .logical_line_id = logical_line_id,
        .logical_cell_start = logical_cell_start,
        .continuation = continuation,
    };
    return YETTY_OK(yetty_ymux_history_row, row);
}

#include "yetty/gen/impl/ymux/pane.c"
