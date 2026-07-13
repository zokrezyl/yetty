/*
 * terminal-mime.c — YETTY_DCS_MIME_FILE handler: raw-file envelopes
 * rendered terminal-side.
 *
 * The payload is an unrendered file (prefixed by the ymime prologue:
 * MIME hint, filename hint, render flags). The handler detects the type,
 * applies the config policy, runs the renderer in-process, and feeds the
 * resulting drawable list(s) through the same ingest path an inbound
 * YDRAW_BIN envelope takes — so the content anchors and scrolls exactly
 * like ycat-rendered output.
 *
 * Memory discipline: the wire layer never buffers an envelope; this
 * handler is the single place file bytes accumulate, and every byte is
 * policy-gated BEFORE any parser runs:
 *
 *   - a fixed head window (prologue + sniff) is read first; detection and
 *     the enable/cap policy run on that,
 *   - the policy is keyed on the FINAL dispatch type (a hint cannot
 *     smuggle a disabled type past a sniff override),
 *   - the cap (mime/types/<type>/max-size-mb, falling back to
 *     mime/max-size-mb) bounds the DECOMPRESSED payload and is enforced
 *     incrementally against actual decoded bytes — the meta may lie,
 *   - PDF spills to an 0600 temp file instead of RAM (pdfio only opens by
 *     path anyway); everything else accumulates in one capped buffer,
 *   - over-cap / disabled / unknown envelopes are drained-and-discarded
 *     without storing further bytes and without killing the handler
 *     coroutine.
 *
 * Continuation (stream_id / sequence / FIRST / LAST in the file meta) is
 * part of the wire format from day one, but v1 accepts only single-shot
 * envelopes (FIRST|LAST). Chunked streams — needed for terminal-side
 * video decode — are v2; they arrive here as extra meta validation plus a
 * per-terminal open-stream table, no wire change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> /* GetTempPathA / GetTempFileNameA / MAX_PATH */
#include <io.h>
#include <fcntl.h>
typedef long long ssize_t;
#define write(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#define close _close
#define unlink _unlink
#else
#include <unistd.h>
#endif

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ymime/mime.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#ifdef YETTY_HAS_YSVG
#include <yetty/ysvg/ysvg.h>
#endif
#ifdef YETTY_HAS_YMARKDOWN
#include <yetty/ymarkdown/ymarkdown.h>
#endif
#ifdef YETTY_HAS_YPDF
#include <pdfio.h>
#include <yetty/ypdf/ypdf.h>
#endif
#ifdef YETTY_HAS_YIMAGE
#include <yetty/yimage/yimage.h>
#endif
#ifdef YETTY_HAS_YMUSIC
#include <yetty/ymusic/music.h>
#endif
#ifdef YETTY_HAS_YCIRCUIT
#include <yetty/ycircuit/circuit.h>
#endif
#ifdef YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh.h>
#endif
#ifdef YETTY_HAS_YMSOFFICE
#include <yetty/ymsoffice/msoffice.h>
#include <yetty/ymsoffice/render.h>
#endif

#include "terminal-mime.h"

/* Head window: enough for the largest prologue plus the sniff window —
 * the only bytes committed before the type/policy decision. */
#define MIME_HEAD_WINDOW (YETTY_YMIME_PROLOGUE_MAX + YETTY_YMIME_SNIFF_WINDOW)

/* Default decompressed-payload cap when the config carries no
 * mime/max-size-mb (and no per-type override). */
#define MIME_DEFAULT_MAX_SIZE_MB 64

/* MIME / filename hints are u8-length-prefixed on the wire (<= 255). */
#define MIME_HINT_MAX 256

/*=============================================================================
 * Config policy
 *===========================================================================*/

static int mime_master_enabled(struct yetty_yconfig_config *config)
{
    if (!config) {
        return 1;
    }
    return config->ops->get_bool(config, YETTY_YCONFIG_KEY_MIME_ENABLED, 1);
}

static int mime_type_enabled(struct yetty_yconfig_config *config, const char *type_name)
{
    if (!config) {
        return 1;
    }
    char key[64];
    snprintf(key, sizeof(key), "mime/types/%s/enabled", type_name);
    return config->ops->get_bool(config, key, 1);
}

static uint64_t mime_type_cap_bytes(struct yetty_yconfig_config *config, const char *type_name)
{
    int cap_mb = MIME_DEFAULT_MAX_SIZE_MB;
    if (config) {
        int global_mb = config->ops->get_int(config, YETTY_YCONFIG_KEY_MIME_MAX_SIZE_MB, cap_mb);
        char key[64];
        snprintf(key, sizeof(key), "mime/types/%s/max-size-mb", type_name);
        cap_mb = config->ops->get_int(config, key, global_mb);
    }
    if (cap_mb < 1) {
        cap_mb = 1;
    }
    return (uint64_t)cap_mb * 1024ull * 1024ull;
}

/*=============================================================================
 * Wire helpers
 *===========================================================================*/

/* Read-and-discard the rest of the envelope body so the statemachine
 * reaches the terminator cleanly. Wire/codec errors propagate — the SM's
 * respawn machinery owns those. */
static struct yetty_ycore_void_result mime_drain(struct yetty_ywire_wire_statemachine *sm)
{
    uint8_t scratch[4096];
    while (!yetty_ywire_wire_statemachine_at_end(sm)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "mime: drain read");
        if (read_res.value == 0 && !yetty_ywire_wire_statemachine_at_end(sm)) {
            yetty_yplatform_coro_yield();
        }
    }
    return YETTY_OK_VOID();
}

/* After filling exactly to a cap: decide whether the envelope truly ended
 * or still carries bytes (over the cap). at_end() only flips once a read
 * call scans up to the terminator, so this pulls at most one extra byte,
 * yielding while starved. */
static struct yetty_ycore_void_result mime_probe_over_cap(struct yetty_ywire_wire_statemachine *sm,
                                                          int *over_cap)
{
    uint8_t probe;
    *over_cap = 0;
    while (!yetty_ywire_wire_statemachine_at_end(sm)) {
        struct yetty_ycore_size_result read_res = yetty_ywire_wire_statemachine_read(sm, &probe, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "mime: cap probe read");
        if (read_res.value > 0) {
            *over_cap = 1;
            return YETTY_OK_VOID();
        }
        if (!yetty_ywire_wire_statemachine_at_end(sm)) {
            yetty_yplatform_coro_yield();
        }
    }
    return YETTY_OK_VOID();
}

/* Accumulate decoded body bytes into `accum` until it holds `target`
 * bytes or the envelope ends. Yields when starved. Never stores past
 * `target` — the caller decides what an over-target envelope means. */
static struct yetty_ycore_void_result mime_fill_buffer(struct yetty_ywire_wire_statemachine *sm,
                                                       struct yetty_ycore_buffer *accum,
                                                       size_t target)
{
    uint8_t chunk[8192];
    while (accum->size < target && !yetty_ywire_wire_statemachine_at_end(sm)) {
        size_t want = target - accum->size;
        if (want > sizeof(chunk)) {
            want = sizeof(chunk);
        }
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, chunk, want);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "mime: body read");
        if (read_res.value == 0) {
            if (!yetty_ywire_wire_statemachine_at_end(sm)) {
                yetty_yplatform_coro_yield();
            }
            continue;
        }
        struct yetty_ycore_void_result write_res =
            yetty_ycore_buffer_write(accum, chunk, read_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "mime: body accumulate");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Ingest bridge
 *===========================================================================*/

#if defined(YETTY_HAS_YSVG) || defined(YETTY_HAS_YMARKDOWN) || defined(YETTY_HAS_YPDF) ||          \
    defined(YETTY_HAS_YIMAGE)
/* Serialize one rendered drawable list and hand it to the terminal's
 * rich-content ingest (same anchoring path as an inbound YDRAW_BIN
 * envelope). */
static struct yetty_ycore_void_result mime_ingest_list(struct yetty_yterminal_terminal *terminal,
                                                       struct yetty_ydraw_drawable_list *list)
{
    const uint8_t *serialized = NULL;
    size_t serialized_len = yetty_ydraw_drawable_list_serialize(list, &serialized);
    if (serialized_len == 0 || !serialized) {
        return YETTY_ERR(yetty_ycore_void, "mime: serialize produced no bytes");
    }
    return yetty_yterminal_mime_ingest_serialized(terminal, serialized, serialized_len);
}
#endif

/*=============================================================================
 * Per-type renderers
 *===========================================================================*/

#ifdef YETTY_HAS_YSVG
static struct yetty_ycore_void_result mime_render_svg(struct yetty_yterminal_terminal *terminal,
                                                      const struct yetty_yterminal_mime_env *env,
                                                      const uint8_t *content, size_t content_len,
                                                      const char *render_args,
                                                      size_t render_args_len)
{
    struct yetty_ysvg_render_config svg_config = {
        .cell_width = env->cell_width,
        .cell_height = env->cell_height,
        .width_cells = env->cols,
        .height_cells = env->rows,
    };
    struct yetty_ysvg_render_result render_res = yetty_ysvg_render(
        (const char *)content, content_len, render_args, render_args_len, &svg_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ysvg render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value.buffer);
    yetty_ydraw_drawable_list_destroy(render_res.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: svg ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YMARKDOWN
static struct yetty_ycore_void_result mime_render_markdown(
    struct yetty_yterminal_terminal *terminal, const struct yetty_yterminal_mime_env *env,
    const uint8_t *content, size_t content_len, const char *render_args, size_t render_args_len)
{
    struct yetty_ymarkdown_render_config markdown_config = {
        .cell_width = env->cell_width,
        .cell_height = env->cell_height,
        .width_cells = env->cols,
        .height_cells = env->rows,
    };
    struct yetty_ymarkdown_render_result render_res = yetty_ymarkdown_render(
        (const char *)content, content_len, render_args, render_args_len, &markdown_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ymarkdown render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value.buffer);
    yetty_ydraw_drawable_list_destroy(render_res.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: markdown ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YMSOFFICE
static struct yetty_ycore_void_result mime_render_msoffice(
    struct yetty_yterminal_terminal *terminal, const struct yetty_yterminal_mime_env *env,
    const uint8_t *content, size_t content_len)
{
    struct yetty_ymsoffice_document_ptr_result parse_res =
        yetty_ymsoffice_parse(content, content_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parse_res, "mime: ymsoffice parse");

    struct yetty_ymsoffice_render_config msoffice_config = {
        .cell_width = env->cell_width,
        .cell_height = env->cell_height,
        .width_cells = env->cols,
        .height_cells = env->rows,
    };
    struct yetty_ymsoffice_render_result render_res =
        yetty_ymsoffice_render(parse_res.value, &msoffice_config);
    yetty_ymsoffice_document_destroy(parse_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ymsoffice render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value.buffer);
    yetty_ydraw_drawable_list_destroy(render_res.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: msoffice ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YIMAGE
static struct yetty_ycore_void_result mime_render_image(struct yetty_yterminal_terminal *terminal,
                                                        const struct yetty_yterminal_mime_env *env,
                                                        const uint8_t *content, size_t content_len)
{
    struct yetty_yimage_render_config image_config = {0};
    /* Native size when it fits; downscale (aspect-preserving) to the pane
     * width when the source is wider. */
    int source_width = 0, source_height = 0;
    float pane_width_px = (float)(env->cols * env->cell_width);
    if (pane_width_px > 0.0f &&
        yetty_yimage_probe_size(content, content_len, &source_width, &source_height) == 0 &&
        source_width > 0 && (float)source_width > pane_width_px) {
        image_config.bounds_w = pane_width_px;
        image_config.bounds_h = pane_width_px * (float)source_height / (float)source_width;
    }
    struct yetty_ydraw_drawable_list_result render_res =
        yetty_yimage_render(content, content_len, &image_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: yimage render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value);
    yetty_ydraw_drawable_list_destroy(render_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: image ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YMUSIC
/* LilyPond score → engraved staff, same model as ycat's music handler:
 * systems wrap to the pane width, staff spacing tracks the cell height. The
 * Emmentaler font is referenced by name in the drawable list; the terminal
 * resolves it from its install like any wire-received score. */
static struct yetty_ycore_void_result mime_render_music(struct yetty_yterminal_terminal *terminal,
                                                        const struct yetty_yterminal_mime_env *env,
                                                        const uint8_t *content, size_t content_len)
{
    struct yetty_ycore_void_result register_res = yetty_ymusic_register();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, register_res, "mime: ymusic register");
    struct yetty_yclass_object_ptr_result object_res = yetty_ymusic_music_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_res, "mime: ymusic create");
    struct yetty_yclass_object *music = object_res.value;

    float cell_width = env->cell_width ? (float)env->cell_width : 8.0f;
    float cell_height = env->cell_height ? (float)env->cell_height : 16.0f;
    float width_px = (float)(env->cols ? env->cols : 80u) * cell_width;
    float staff_space = cell_height * 0.85f;
    if (staff_space < 8.0f) {
        staff_space = 8.0f;
    }

    struct yetty_ycore_void_result step_res =
        yetty_ymusic_configure(music, width_px, staff_space, YETTY_YMUSIC_FLAG_NONE);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ymusic_parse(music, (const char *)content, content_len);
    }
    struct yetty_ydraw_drawable_list_result render_res = {0};
    int have_list = 0;
    if (YETTY_IS_OK(step_res)) {
        render_res = yetty_ymusic_render(music);
        have_list = YETTY_IS_OK(render_res);
    }
    /* Best-effort teardown — the rendered list owns its own bytes. */
    struct yetty_ycore_void_result destroy_res = yetty_ymusic_destroy(music);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, step_res, "mime: ymusic configure/parse");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ymusic render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value);
    if (have_list) {
        yetty_ydraw_drawable_list_destroy(render_res.value);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: music ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YCIRCUIT
/* ycircuit schematic DSL → SDF wires/bodies + MSDF labels; grid pitch
 * tracks the cell height, same model as ycat's circuit handler. */
static struct yetty_ycore_void_result mime_render_circuit(
    struct yetty_yterminal_terminal *terminal, const struct yetty_yterminal_mime_env *env,
    const uint8_t *content, size_t content_len)
{
    struct yetty_ycore_void_result register_res = yetty_ycircuit_register();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, register_res, "mime: ycircuit register");
    struct yetty_yclass_object_ptr_result object_res = yetty_ycircuit_circuit_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_res, "mime: ycircuit create");
    struct yetty_yclass_object *circuit = object_res.value;

    float cell_height = env->cell_height ? (float)env->cell_height : 16.0f;
    float grid_px = cell_height * 0.9f;
    if (grid_px < 10.0f) {
        grid_px = 10.0f;
    }

    struct yetty_ycore_void_result step_res =
        yetty_ycircuit_configure(circuit, grid_px, YETTY_YCIRCUIT_FLAG_NONE);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ycircuit_parse(circuit, (const char *)content, content_len);
    }
    struct yetty_ydraw_drawable_list_result render_res = {0};
    int have_list = 0;
    if (YETTY_IS_OK(step_res)) {
        render_res = yetty_ycircuit_render(circuit);
        have_list = YETTY_IS_OK(render_res);
    }
    struct yetty_ycore_void_result destroy_res = yetty_ycircuit_destroy(circuit);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, step_res, "mime: ycircuit configure/parse");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ycircuit render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value);
    if (have_list) {
        yetty_ydraw_drawable_list_destroy(render_res.value);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: circuit ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YMESH
/* glTF binary → one ymesh composite figure; the GPU-side factory for the
 * record type is already registered by terminal_create. */
static struct yetty_ycore_void_result mime_render_mesh(struct yetty_yterminal_terminal *terminal,
                                                       const uint8_t *content, size_t content_len)
{
    struct yetty_ymesh_render_config mesh_config = {0}; /* defaults: fixed size, solid */
    struct yetty_ydraw_drawable_list_result render_res =
        yetty_ymesh_render(content, content_len, &mesh_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ymesh render");
    struct yetty_ycore_void_result ingest_res = mime_ingest_list(terminal, render_res.value);
    yetty_ydraw_drawable_list_destroy(render_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "mime: mesh ingest");
    return YETTY_OK_VOID();
}
#endif

#ifdef YETTY_HAS_YPDF

struct mime_pdf_ingest_ctx {
    struct yetty_yterminal_terminal *terminal;
};

static struct yetty_ycore_void_result mime_pdf_on_page(
    void *user_data, int page_index, int page_count,
    const struct yetty_ydraw_drawable_list *envelope)
{
    (void)page_index;
    (void)page_count;
    struct mime_pdf_ingest_ctx *ctx = user_data;
    /* serialize() mutates internal scratch only — the cast mirrors
     * ycat's emit path. */
    return mime_ingest_list(ctx->terminal, (struct yetty_ydraw_drawable_list *)envelope);
}

static struct yetty_ycore_void_result mime_render_pdf_path(
    struct yetty_yterminal_terminal *terminal, const char *path)
{
    struct _pdfio_file_s *pdf = pdfioFileOpen(path, NULL, NULL, NULL, NULL);
    if (!pdf) {
        return YETTY_ERR(yetty_ycore_void, "mime: pdfioFileOpen failed");
    }
    struct mime_pdf_ingest_ctx ctx = {.terminal = terminal};
    struct yetty_ypdf_stream_render_result render_res =
        yetty_ypdf_render_pdf_streaming(pdf, mime_pdf_on_page, &ctx);
    pdfioFileClose(pdf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "mime: ypdf streaming render");
    return YETTY_OK_VOID();
}

/* Create a fresh 0600 temp file for the PDF spill; fills `path_out`.
 * Returns the open fd or -1. */
static int mime_tempfile_create(char *path_out, size_t path_cap)
{
#ifdef _WIN32
    char tmpdir[MAX_PATH];
    if (GetTempPathA((DWORD)sizeof(tmpdir), tmpdir) == 0) {
        return -1;
    }
    if (path_cap < MAX_PATH || GetTempFileNameA(tmpdir, "ymim", 0, path_out) == 0) {
        return -1;
    }
    return _open(path_out, _O_WRONLY | _O_BINARY);
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) {
        tmpdir = "/tmp";
    }
    int written = snprintf(path_out, path_cap, "%s/yetty-mime-XXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= path_cap) {
        return -1;
    }
    return mkstemp(path_out); /* 0600 by contract */
#endif
}

static int mime_write_all(int fd, const uint8_t *bytes, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t step = write(fd, bytes + written, len - written);
        if (step < 0) {
            return -1;
        }
        written += (size_t)step;
    }
    return 0;
}

/* Stream the remaining envelope body straight to `fd`, enforcing the cap
 * against the cumulative decompressed count. Sets *over_cap instead of
 * storing past the cap. */
static struct yetty_ycore_void_result mime_spill_stream(struct yetty_ywire_wire_statemachine *sm,
                                                        int fd, uint64_t already_written,
                                                        uint64_t cap_bytes, int *over_cap,
                                                        int *io_error)
{
    uint8_t chunk[8192];
    uint64_t total = already_written;
    *over_cap = 0;
    *io_error = 0;
    while (!yetty_ywire_wire_statemachine_at_end(sm)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, chunk, sizeof(chunk));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "mime: spill read");
        if (read_res.value == 0) {
            if (!yetty_ywire_wire_statemachine_at_end(sm)) {
                yetty_yplatform_coro_yield();
            }
            continue;
        }
        total += read_res.value;
        if (total > cap_bytes) {
            *over_cap = 1;
            return YETTY_OK_VOID();
        }
        if (mime_write_all(fd, chunk, read_res.value) != 0) {
            *io_error = 1;
            return YETTY_OK_VOID();
        }
    }
    return YETTY_OK_VOID();
}

#endif /* YETTY_HAS_YPDF */

/*=============================================================================
 * Envelope consumption
 *===========================================================================*/

/* Consume one YETTY_DCS_MIME_FILE envelope. Policy violations and render
 * failures are absorbed (warn + drain + discard); only wire/codec errors
 * propagate to the caller. */
static struct yetty_ycore_void_result mime_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    /* --- meta ------------------------------------------------------------ */
    struct yetty_ywire_wire_statemachine_args args = yetty_ywire_wire_statemachine_args(sm);
    struct yetty_yface_file_meta meta;
    if (!args.bytes || args.len != sizeof(meta)) {
        ywarn("mime: bad args slot (%zu bytes) — discarding envelope", args.len);
        return mime_drain(sm);
    }
    memcpy(&meta, args.bytes, sizeof(meta));
    if (meta.magic != YETTY_YFACE_FILE_MAGIC || meta.version != YETTY_YFACE_FILE_VERSION) {
        ywarn("mime: bad file meta (magic=0x%08x version=%u) — discarding", meta.magic,
              meta.version);
        return mime_drain(sm);
    }
    if (meta.flags & YETTY_YFACE_FILE_FLAG_ABORT) {
        return mime_drain(sm);
    }
    if ((meta.flags & (YETTY_YFACE_FILE_FLAG_FIRST | YETTY_YFACE_FILE_FLAG_LAST)) !=
        (YETTY_YFACE_FILE_FLAG_FIRST | YETTY_YFACE_FILE_FLAG_LAST)) {
        ywarn("mime: chunked file streams not supported yet (flags=0x%x stream=%u seq=%u) — "
              "discarding",
              meta.flags, meta.stream_id, meta.sequence);
        return mime_drain(sm);
    }

    struct yetty_yterminal_mime_env env = yetty_yterminal_mime_env_get(terminal);
    if (!mime_master_enabled(env.config)) {
        ydebug("mime: disabled by config — discarding envelope");
        return mime_drain(sm);
    }

    /* --- head window: prologue + sniff ----------------------------------- */
    struct yetty_ycore_buffer accum = {0};
    struct yetty_ycore_void_result fill_res = mime_fill_buffer(sm, &accum, MIME_HEAD_WINDOW);
    if (YETTY_IS_ERR(fill_res)) {
        yetty_ycore_buffer_destroy(&accum);
        return YETTY_ERR(yetty_ycore_void, "mime: head fill", fill_res);
    }

    struct yetty_ymime_prologue prologue;
    struct yetty_ycore_size_result prologue_res =
        yetty_ymime_prologue_decode(accum.data, accum.size, &prologue);
    if (YETTY_IS_ERR(prologue_res)) {
        ywarn("mime: prologue decode failed (%s) — discarding", prologue_res.error.msg);
        yetty_ycore_error_destroy(prologue_res.error);
        yetty_ycore_buffer_destroy(&accum);
        return mime_drain(sm);
    }
    size_t prologue_size = prologue_res.value;

    /* Hints must survive buffer reallocation during the body fill — copy
     * them out of the accumulation buffer now. */
    char mime_hint[MIME_HINT_MAX] = {0};
    char name_hint[MIME_HINT_MAX] = {0};
    memcpy(mime_hint, prologue.mime, prologue.mime_len);
    memcpy(name_hint, prologue.name, prologue.name_len);

    /* --- detect + policy -------------------------------------------------- */
    enum yetty_ymime_type type =
        yetty_ymime_detect(mime_hint[0] ? mime_hint : NULL, name_hint[0] ? name_hint : NULL,
                           accum.data + prologue_size, accum.size - prologue_size);
    const char *type_name = yetty_ymime_type_name(type);

    int renderable;
    switch (type) {
    case YETTY_YMIME_TYPE_SVG:
    case YETTY_YMIME_TYPE_MARKDOWN:
    case YETTY_YMIME_TYPE_PDF:
    case YETTY_YMIME_TYPE_IMAGE:
    case YETTY_YMIME_TYPE_MUSIC:
    case YETTY_YMIME_TYPE_CIRCUIT:
    case YETTY_YMIME_TYPE_MESH:
    case YETTY_YMIME_TYPE_DOCX:
    case YETTY_YMIME_TYPE_XLSX:
    case YETTY_YMIME_TYPE_PPTX:
        renderable = 1;
        break;
    default:
        renderable = 0;
        break;
    }
    if (!renderable) {
        ywarn("mime: no terminal-side renderer for type '%s' (hint='%s' name='%s') — discarding",
              type_name, mime_hint, name_hint);
        yetty_ycore_buffer_destroy(&accum);
        return mime_drain(sm);
    }
    if (!mime_type_enabled(env.config, type_name)) {
        ywarn("mime: type '%s' disabled by config — discarding", type_name);
        yetty_ycore_buffer_destroy(&accum);
        return mime_drain(sm);
    }
    /* The cap bounds the whole decompressed payload (prologue included —
     * the prologue is bounded and tiny next to any real cap). */
    uint64_t cap_bytes = mime_type_cap_bytes(env.config, type_name);
    if (meta.total_raw_size > 0 && meta.total_raw_size > cap_bytes) {
        ywarn("mime: declared size %llu exceeds cap %llu for type '%s' — discarding",
              (unsigned long long)meta.total_raw_size, (unsigned long long)cap_bytes, type_name);
        yetty_ycore_buffer_destroy(&accum);
        return mime_drain(sm);
    }

    ydebug("mime: envelope type='%s' hint='%s' name='%s' declared=%llu cap=%llu", type_name,
           mime_hint, name_hint, (unsigned long long)meta.total_raw_size,
           (unsigned long long)cap_bytes);

    struct yetty_ycore_void_result render_res = YETTY_OK_VOID();
    int rendered = 0;

    if (type == YETTY_YMIME_TYPE_PDF) {
#ifdef YETTY_HAS_YPDF
        /* Disk spill — pdfio opens by path, and the file never has to sit
         * in RAM. Head bytes first, then stream the rest straight to fd. */
        char spill_path[512];
        int spill_fd = mime_tempfile_create(spill_path, sizeof(spill_path));
        if (spill_fd < 0) {
            ywarn("mime: pdf spill tempfile creation failed — discarding");
            yetty_ycore_buffer_destroy(&accum);
            return mime_drain(sm);
        }
        int io_error =
            mime_write_all(spill_fd, accum.data + prologue_size, accum.size - prologue_size);
        uint64_t head_content = accum.size; /* prologue counts toward the cap */
        yetty_ycore_buffer_destroy(&accum);
        int over_cap = 0;
        if (io_error == 0) {
            int stream_io_error = 0;
            struct yetty_ycore_void_result spill_res = mime_spill_stream(
                sm, spill_fd, head_content, cap_bytes, &over_cap, &stream_io_error);
            if (YETTY_IS_ERR(spill_res)) {
                close(spill_fd);
                unlink(spill_path);
                return YETTY_ERR(yetty_ycore_void, "mime: pdf spill", spill_res);
            }
            io_error = stream_io_error;
        }
        close(spill_fd);
        if (io_error != 0 || over_cap != 0) {
            ywarn("mime: pdf spill aborted (%s) — discarding",
                  over_cap ? "over size cap" : "temp file write failed");
            unlink(spill_path);
            return mime_drain(sm);
        }
        render_res = mime_render_pdf_path(terminal, spill_path);
        unlink(spill_path);
        rendered = 1;
#else
        ywarn("mime: pdf renderer not compiled in — discarding");
        yetty_ycore_buffer_destroy(&accum);
        return mime_drain(sm);
#endif
    } else {
        /* RAM accumulation, cap-enforced: fill to cap; if the envelope
         * still has bytes after that, it is over the cap. */
        size_t cap_total = cap_bytes < (uint64_t)SIZE_MAX ? (size_t)cap_bytes : SIZE_MAX;
        fill_res = mime_fill_buffer(sm, &accum, cap_total);
        if (YETTY_IS_ERR(fill_res)) {
            yetty_ycore_buffer_destroy(&accum);
            return YETTY_ERR(yetty_ycore_void, "mime: body fill", fill_res);
        }
        int over_cap = 0;
        struct yetty_ycore_void_result probe_res = mime_probe_over_cap(sm, &over_cap);
        if (YETTY_IS_ERR(probe_res)) {
            yetty_ycore_buffer_destroy(&accum);
            return YETTY_ERR(yetty_ycore_void, "mime: cap probe", probe_res);
        }
        if (over_cap) {
            ywarn("mime: payload exceeds cap %llu for type '%s' — discarding",
                  (unsigned long long)cap_bytes, type_name);
            yetty_ycore_buffer_destroy(&accum);
            return mime_drain(sm);
        }

        /* Re-decode the prologue: the buffer may have been reallocated, and
         * the render-args view must point at the final storage. */
        prologue_res = yetty_ymime_prologue_decode(accum.data, accum.size, &prologue);
        if (YETTY_IS_ERR(prologue_res)) {
            yetty_ycore_error_destroy(prologue_res.error);
            yetty_ycore_buffer_destroy(&accum);
            return YETTY_ERR(yetty_ycore_void, "mime: prologue re-decode diverged");
        }

        const uint8_t *content = accum.data + prologue_size;
        size_t content_len = accum.size - prologue_size;

        switch (type) {
        case YETTY_YMIME_TYPE_SVG:
#ifdef YETTY_HAS_YSVG
            render_res = mime_render_svg(terminal, &env, content, content_len, prologue.args,
                                         prologue.args_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_MARKDOWN:
#ifdef YETTY_HAS_YMARKDOWN
            render_res = mime_render_markdown(terminal, &env, content, content_len, prologue.args,
                                              prologue.args_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_IMAGE:
#ifdef YETTY_HAS_YIMAGE
            render_res = mime_render_image(terminal, &env, content, content_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_MUSIC:
#ifdef YETTY_HAS_YMUSIC
            render_res = mime_render_music(terminal, &env, content, content_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_CIRCUIT:
#ifdef YETTY_HAS_YCIRCUIT
            render_res = mime_render_circuit(terminal, &env, content, content_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_MESH:
#ifdef YETTY_HAS_YMESH
            render_res = mime_render_mesh(terminal, content, content_len);
            rendered = 1;
#endif
            break;
        case YETTY_YMIME_TYPE_DOCX:
        case YETTY_YMIME_TYPE_XLSX:
        case YETTY_YMIME_TYPE_PPTX:
#ifdef YETTY_HAS_YMSOFFICE
            render_res = mime_render_msoffice(terminal, &env, content, content_len);
            rendered = 1;
#endif
            break;
        default:
            break;
        }
        yetty_ycore_buffer_destroy(&accum);
        if (!rendered) {
            ywarn("mime: renderer for type '%s' not compiled in — discarding", type_name);
            return mime_drain(sm);
        }
    }

    if (YETTY_IS_ERR(render_res)) {
        ywarn("mime: render failed for type '%s': %s", type_name, render_res.error.msg);
        yetty_ycore_error_destroy(render_res.error);
    } else if (rendered) {
        yetty_yterminal_mime_request_render(terminal);
    }
    /* Body already fully consumed on the success paths; drain covers the
     * failure paths where the terminator has not been crossed yet. */
    return mime_drain(sm);
}

struct yetty_ycore_void_result yetty_yterminal_mime_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        struct yetty_ycore_void_result envelope_res = mime_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, envelope_res, "mime: envelope");
        yetty_yplatform_coro_yield();
    }
}
