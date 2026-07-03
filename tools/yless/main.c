/*
 * yless — a `less`-semantics pager that renders into a server-side scrollable
 * figure (yview) instead of the scrolling buffer.
 *
 * Contrast with ycat: ycat dumps rendered content into the scrollback (DCS
 * YDRAW_BIN); it ages out of history like any text. yless ships the content
 * ONCE as a positioned, viewport-anchored figure over the yclass-RPC figure
 * path and then stays in the foreground translating keystrokes into scroll
 * commands. Scrolling is server-side state: each key sends a tiny
 * set-child-scroll call, never re-shipping the content. On exit the figure is
 * deleted, so the surface is cleared.
 *
 * Content comes from a file argument or stdin; keystrokes are read from stdin
 * via the cross-platform yplatform TTY abstraction (raw mode, size, byte
 * reads) — so the tool builds on Windows too. Interactive paging needs stdin
 * to be a terminal (i.e. give the content as a FILE argument). Envelopes are
 * written to stdout — the PTY connected to the parent yetty.
 */
#include <yetty/ycat/ycat.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yconfig/config.h> /* read brand colours from the yetty config */
#include <yetty/ymusic/music.h>   /* yetty_ymusic_* — LilyPond score rendering */
#include <yetty/yplatform/getopt.h>
#include <yetty/yplatform/term.h> /* yetty_yplatform_term_get_size */
#include <yetty/yplatform/time.h> /* monotonic clock + sleep (--duration) */
#include <yetty/yplatform/tty.h>  /* raw mode + stdin read, cross-platform */
#include <yetty/yview/view.h>     /* yetty_yview_configure / _set_content / _scroll_* / _destroy */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>      /* _fileno */
#include <process.h> /* _getpid */
#else
#include <unistd.h> /* STDOUT_FILENO, getpid */
#endif

/* Portable fd of stdout (the PTY to the parent yetty) + process id. */
#ifdef _WIN32
#define YLESS_STDOUT_FD (_fileno(stdout))
#define YLESS_GETPID() ((uint32_t)_getpid())
#else
#define YLESS_STDOUT_FD (STDOUT_FILENO)
#define YLESS_GETPID() ((uint32_t)getpid())
#endif

/*=============================================================================
 * Options
 *===========================================================================*/

/* One position/size argument. `value` is a cell count, or a percentage of the
 * pane along its axis when `is_percent`. `set` distinguishes an explicit 0 (or
 * 0%) from "not given" so an unset width/height still means "to the edge". */
struct yless_dim {
    float value;
    bool is_percent;
    bool set;
};

struct yless_opts {
    struct yless_dim origin_x; /* origin column; unset = pane left */
    struct yless_dim origin_y; /* origin row;    unset = pane top  */
    struct yless_dim width;    /* width;  unset = to right edge  */
    struct yless_dim height;   /* height; unset = to bottom edge */
    float opacity;             /* background opacity 0.0..1.0 (1.0 = opaque) */
    float duration_sec;        /* auto-exit after this many seconds; 0 = run until quit */
};

/* Parse a geometry argument: a cell count, or "N%" as a percentage of the pane
 * along its axis. Negative values are clamped to 0. */
static struct yless_dim parse_dim(const char *arg)
{
    struct yless_dim dim = {.set = true};
    size_t len = arg ? strlen(arg) : 0;
    if (len > 0 && arg[len - 1] == '%') {
        dim.is_percent = true;
        dim.value = (float)atof(arg); /* atof stops at the '%' */
    } else {
        dim.value = (float)atoi(arg);
    }
    if (dim.value < 0.0f) {
        dim.value = 0.0f;
    }
    return dim;
}

/* Resolve a geometry dimension to pixels. `axis_px` is the pane extent along
 * this axis (width for x/w, height for y/h); `cell_px` is the cell size on that
 * axis. An unset dimension yields `fallback_px`. */
static float resolve_dim_px(const struct yless_dim *dim, float axis_px, float cell_px,
                            float fallback_px)
{
    if (!dim->set) {
        return fallback_px;
    }
    if (dim->is_percent) {
        return dim->value / 100.0f * axis_px;
    }
    return dim->value * cell_px;
}

/* Brand near-black background (#0B1014), per the palette. RGB only — the
 * opacity flag supplies the alpha byte. */
#define YLESS_BG_RGB 0x0B1014u
/* LilyPond scores render on the brand mint, read live from the yetty config
 * (style/ygui/accent). This is only the fallback if the config can't be read. */
#define YLESS_MUSIC_BG_RGB 0x6BA892u
#define YLESS_MUSIC_BG_KEY "style/ygui/accent"
/* Scale the config mint down to a dark mint for the page background. */
#define YLESS_MUSIC_BG_DARKEN 0.08f

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] [file]\n"
            "\n"
            "A pager that renders FILE (or stdin) into a positioned, server-side\n"
            "scrollable figure inside yetty — like `less`, but the content is a\n"
            "graphical surface (syntax-highlighted source, image, svg, diagram,\n"
            "pdf) that scrolls on the server. Unlike ycat it does NOT go to the\n"
            "scrollback; on exit the surface is cleared.\n"
            "\n"
            "Supported content: source files (tree-sitter), images, svg, mermaid\n"
            "diagrams, pdf (first page), and LilyPond scores (.ly — engraved with\n"
            "the Emmentaler music font).\n"
            "\n"
            "Options (position/size take terminal CELLS or a pane percentage\n"
            "like 50%%; default = whole pane):\n"
            "  -x, --x=N        origin column   (cells or %%; default 0)\n"
            "  -y, --y=N        origin row      (cells or %%; default 0)\n"
            "  -w, --width=N    width           (cells or %%; default: to right edge)\n"
            "  -H, --height=N   height          (cells or %%; default: to bottom edge)\n"
            "  -a, --alpha=F    background opacity 0.0..1.0 (default 1.0 = opaque;\n"
            "                   0.0 = transparent, terminal text shows through)\n"
            "  -d, --duration=F run for F seconds, then auto-exit (clears the\n"
            "                   surface); default 0 = run until quit. Lets a\n"
            "                   non-interactive caller (e.g. a demo driver) hold\n"
            "                   the view on screen for a fixed time; with a\n"
            "                   keyboard, q still quits earlier.\n"
            "  -h, --help       show this help\n"
            "\n"
            "Keys (interactive):\n"
            "  j / Down / Enter   scroll down one line\n"
            "  k / Up             scroll up one line\n"
            "  Space / f / PgDn   page down\n"
            "  b / PgUp           page up\n"
            "  g / Home           jump to top\n"
            "  G / End            jump to bottom\n"
            "  q / Ctrl-C / Ctrl-D / Esc   quit (clears the surface)\n"
            "\n"
            "Examples:\n"
            "  %s main.c                 # page a source file, whole pane\n"
            "  %s -w 40 -H 20 a.svg      # an svg in a 40x20-cell box at the top-left\n"
            "  %s -x 50%% -w 50%% b.pdf     # a pdf in the right-hand half\n"
            "  %s --duration 3 a.svg     # show for 3s, then exit (no keyboard needed)\n",
            prog, prog, prog, prog, prog);
}

/*=============================================================================
 * Input read helpers
 *===========================================================================*/

struct byte_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static int byte_buf_append(struct byte_buf *buffer, const uint8_t *src, size_t count)
{
    if (buffer->len + count > buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 65536;
        while (new_cap < buffer->len + count) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(buffer->data, new_cap);
        if (!grown) {
            return -1;
        }
        buffer->data = grown;
        buffer->cap = new_cap;
    }
    memcpy(buffer->data + buffer->len, src, count);
    buffer->len += count;
    return 0;
}

static int read_all_stream(FILE *stream, struct byte_buf *out)
{
    uint8_t chunk[65536];
    for (;;) {
        size_t count = fread(chunk, 1, sizeof(chunk), stream);
        if (count > 0 && byte_buf_append(out, chunk, count) < 0) {
            return -1;
        }
        if (count < sizeof(chunk)) {
            return ferror(stream) ? -1 : 0;
        }
    }
}

/*=============================================================================
 * Geometry — viewport rect in target pixels from the tty winsize
 *===========================================================================*/

struct viewport {
    float pixel_w;
    float pixel_h;
    uint32_t cell_w;
    uint32_t cell_h;
    uint32_t cols;
};

static struct viewport probe_viewport(void)
{
    /* A `-e` child is forked with the default 80x24 / 0px winsize; the real
     * grid and pane pixel area arrive shortly after (a resize + SIGWINCH once
     * yetty lays the pane out — typically ~1s). Poll until the reported size
     * stops changing so the figure fills the ACTUAL pane instead of a
     * stale-default corner. The pixel fields (ws_xpixel/ws_ypixel) give the
     * true cell size; without them (a plain terminal that doesn't report
     * pixels) we settle on the cell counts and fall back to a nominal cell. */
    int cols = 0, rows = 0, pixel_w = 0, pixel_h = 0;
    int prev_cols = -1, prev_rows = -1, prev_pixel_w = -1, prev_pixel_h = -1;
    int stable_reads = 0;
    int waited_ms = 0;
    const int poll_ms = 30;
    const int settle_ms = 300;    /* size must hold this long to count as settled */
    const int min_ramp_ms = 1200; /* the pane grows in steps for ~1s after spawn;
                                    * don't trust a stable size before then, or we
                                    * latch an early, smaller step of the ramp */
    const int timeout_ms = 4000;  /* hard cap so startup never hangs */
    for (;;) {
        int probe_cols = 0, probe_rows = 0, probe_pixel_w = 0, probe_pixel_h = 0;
        if (yetty_yplatform_term_get_size_pixels(&probe_cols, &probe_rows, &probe_pixel_w,
                                                 &probe_pixel_h) == 0 &&
            probe_cols > 0 && probe_rows > 0) {
            cols = probe_cols;
            rows = probe_rows;
            pixel_w = probe_pixel_w;
            pixel_h = probe_pixel_h;
            bool unchanged = probe_cols == prev_cols && probe_rows == prev_rows &&
                             probe_pixel_w == prev_pixel_w && probe_pixel_h == prev_pixel_h;
            stable_reads = unchanged ? stable_reads + 1 : 0;
            prev_cols = probe_cols;
            prev_rows = probe_rows;
            prev_pixel_w = probe_pixel_w;
            prev_pixel_h = probe_pixel_h;
            /* A pixel area means we're under yetty, whose pane ramps up in
             * steps for ~1s after spawn — wait past the ramp before trusting a
             * stable reading. A terminal with no pixel area has a fixed size,
             * so accept as soon as it stops changing. */
            bool settled = stable_reads * poll_ms >= settle_ms;
            int min_wait = (probe_pixel_w > 0 && probe_pixel_h > 0) ? min_ramp_ms : 0;
            if (settled && waited_ms >= min_wait) {
                break;
            }
        }
        if (waited_ms >= timeout_ms) {
            break;
        }
        yetty_yplatform_ytime_sleep_ms((unsigned)poll_ms);
        waited_ms += poll_ms;
    }

    if (cols <= 0) {
        cols = 80;
    }
    if (rows <= 0) {
        rows = 24;
    }

    struct viewport vp = {.cols = (uint32_t)cols};
    if (pixel_w > 0 && pixel_h > 0) {
        vp.pixel_w = (float)pixel_w;
        vp.pixel_h = (float)pixel_h;
        vp.cell_w = (uint32_t)(pixel_w / cols);
        vp.cell_h = (uint32_t)(pixel_h / rows);
    }
    /* Nominal cell size when the terminal doesn't report pixels. */
    if (vp.cell_w == 0) {
        vp.cell_w = 8;
    }
    if (vp.cell_h == 0) {
        vp.cell_h = 16;
    }
    if (vp.pixel_w <= 0.0f) {
        vp.pixel_w = (float)(cols * (int)vp.cell_w);
    }
    if (vp.pixel_h <= 0.0f) {
        vp.pixel_h = (float)(rows * (int)vp.cell_h);
    }
    return vp;
}

/*=============================================================================
 * Content rendering — ycat's MIME dispatch, collapsed to one drawable list
 *===========================================================================*/

/* Streaming handlers (markdown/pdf) emit one envelope per tile/page. A single
 * scrollable figure wants one list, so we keep the FIRST envelope (page 1 of a
 * pdf, first tile of markdown). Merging all pages into one tall figure is a
 * follow-up. The captured list is cloned because the handler frees its own. */
struct stream_capture {
    struct yetty_ydraw_drawable_list *first;
};

static struct yetty_ycore_void_result capture_first_envelope(
    void *user_data, const struct yetty_ydraw_drawable_list *envelope)
{
    struct stream_capture *capture = user_data;
    if (capture->first) {
        return YETTY_OK_VOID(); /* keep only the first */
    }
    const uint8_t *blob = NULL;
    size_t blob_len =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)envelope, &blob);
    if (blob_len == 0 || !blob) {
        return YETTY_ERR(yetty_ycore_void, "capture_first_envelope: serialize empty");
    }
    struct yetty_ydraw_drawable_list_result clone_r =
        yetty_ydraw_drawable_list_create_from_bytes(blob, blob_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clone_r, "capture_first_envelope: clone");
    capture->first = clone_r.value;
    return YETTY_OK_VOID();
}

/* Render `bytes` into a single drawable list, or NULL on failure (message on
 * stderr). Mirrors ycat's dispatch: streaming handler (pdf/markdown) → single-
 * shot handler (image/svg/mermaid) → tree-sitter (source). */
static struct yetty_ydraw_drawable_list *render_content(const uint8_t *bytes, size_t len,
                                                        const char *path,
                                                        const struct yetty_ycat_config *cfg)
{
    enum yetty_ycat_type type = yetty_ycat_detect(bytes, len, path);

    yetty_ycat_handler_streaming_fn stream_fn = yetty_ycat_get_handler_streaming(type);
    if (stream_fn) {
        struct stream_capture capture = {0};
        struct yetty_ycore_void_result sr =
            stream_fn(bytes, len, path, cfg, capture_first_envelope, &capture);
        if (YETTY_IS_OK(sr) && capture.first) {
            return capture.first;
        }
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
        if (capture.first) {
            yetty_ydraw_drawable_list_destroy(capture.first);
        }
        /* fall through to the other paths */
    }

    yetty_ycat_handler_fn handler_fn = yetty_ycat_get_handler(type);
    if (handler_fn) {
        struct yetty_ydraw_drawable_list_result r = handler_fn(bytes, len, path, cfg);
        if (YETTY_IS_OK(r)) {
            return r.value;
        }
        yetty_ycore_error_destroy(r.error);
    }

    const char *grammar = yetty_ycat_grammar_lookup(NULL, path);
    if (grammar) {
        struct yetty_ydraw_drawable_list_result r = yetty_ycat_ts_render(bytes, len, grammar, cfg);
        if (YETTY_IS_OK(r)) {
            return r.value;
        }
        fprintf(stderr, "yless: render failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }

    fprintf(stderr, "yless: no renderer for this content type "
                    "(supported: source files, images, svg, mermaid, pdf)\n");
    return NULL;
}

/*=============================================================================
 * LilyPond — render .ly scores directly via ymusic (clefs/noteheads/rests as
 * Emmentaler MSDF glyphs; staff/stems/beams as SDF). Bypasses ycat's MIME
 * dispatch (there is no tree-sitter LilyPond grammar) and produces the same
 * single scrollable drawable list.
 *===========================================================================*/

static bool ends_with_ci(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }
    const char *tail = text + (text_len - suffix_len);
    for (size_t i = 0; i < suffix_len; i++) {
        char a = tail[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool window_contains(const uint8_t *hay, size_t hay_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > hay_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

/* LilyPond if the path ends .ly/.ily, or the first few KB carry a telltale
 * LilyPond command. */
static bool is_lilypond(const uint8_t *bytes, size_t len, const char *path)
{
    if (path && (ends_with_ci(path, ".ly") || ends_with_ci(path, ".ily"))) {
        return true;
    }
    size_t scan = len < 4096 ? len : 4096;
    static const char *markers[] = {"\\relative",  "\\version", "\\score",
                                    "\\new Staff", "\\clef",    "\\time"};
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if (window_contains(bytes, scan, markers[i])) {
            return true;
        }
    }
    return false;
}

/* Pack an 0xRRGGBB colour + opacity into the ABGR word (byte0=R, byte1=G,
 * byte2=B, byte3=A) that ydraw / yview backgrounds expect. */
static uint32_t pack_bg_abgr(uint32_t rrggbb, float opacity)
{
    uint32_t r = (rrggbb >> 16) & 0xFFu;
    uint32_t g = (rrggbb >> 8) & 0xFFu;
    uint32_t b = rrggbb & 0xFFu;
    uint32_t a = (uint32_t)(opacity * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* Parse a "#RRGGBB" (or "RRGGBB") colour into 0xRRGGBB. Returns fallback on any
 * malformed input. */
static uint32_t parse_hex_rgb(const char *text, uint32_t fallback)
{
    if (!text) {
        return fallback;
    }
    if (*text == '#') {
        text++;
    }
    uint32_t value = 0;
    int digits = 0;
    for (; digits < 6 && text[digits]; digits++) {
        char ch = text[digits];
        uint32_t nibble;
        if (ch >= '0' && ch <= '9') {
            nibble = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = (uint32_t)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = (uint32_t)(ch - 'A' + 10);
        } else {
            return fallback;
        }
        value = (value << 4) | nibble;
    }
    return digits == 6 ? value : fallback;
}

/* Read the brand mint (style/ygui/accent) from the yetty config the parent
 * exported via the YETTY_*_DIR env vars, so the colour is never hardcoded.
 * Falls back to YLESS_MUSIC_BG_RGB only if the config can't be loaded. */
static uint32_t music_bg_rgb_from_config(void)
{
    char prog[] = "yless";
    char *argv[] = {prog, NULL};
    struct yetty_yconfig_result config_r = yetty_yconfig_create(1, argv);
    if (YETTY_IS_ERR(config_r)) {
        yetty_ycore_error_destroy(config_r.error);
        return YLESS_MUSIC_BG_RGB;
    }
    struct yetty_yconfig_config *config = config_r.value;
    const char *mint = config->ops->get_string(config, YLESS_MUSIC_BG_KEY, NULL);
    uint32_t rgb = parse_hex_rgb(mint, YLESS_MUSIC_BG_RGB);
    config->ops->destroy(config);

    /* The config mint (style/ygui/accent) is a bright accent; for a page
     * background we want a DARK mint — same hue, scaled down — so the off-white
     * notes stay legible. The hue still comes from the config, not a literal. */
    uint32_t r = (uint32_t)(((rgb >> 16) & 0xFFu) * YLESS_MUSIC_BG_DARKEN);
    uint32_t g = (uint32_t)(((rgb >> 8) & 0xFFu) * YLESS_MUSIC_BG_DARKEN);
    uint32_t b = (uint32_t)((rgb & 0xFFu) * YLESS_MUSIC_BG_DARKEN);
    return (r << 16) | (g << 8) | b;
}

/* Render LilyPond `bytes` into one drawable list, wrapped into systems that fit
 * `width_px`. Returns NULL on failure (message on stderr). */
static struct yetty_ydraw_drawable_list *render_lilypond(const uint8_t *bytes, size_t len,
                                                         float width_px, float cell_h)
{
    struct yetty_ycore_void_result reg = yetty_ymusic_register();
    if (YETTY_IS_ERR(reg)) {
        fprintf(stderr, "yless: ymusic register failed: %s\n", reg.error.msg);
        yetty_ycore_error_destroy(reg.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_ymusic_music_create(NULL);
    if (YETTY_IS_ERR(obj_r)) {
        fprintf(stderr, "yless: ymusic create failed: %s\n", obj_r.error.msg);
        yetty_ycore_error_destroy(obj_r.error);
        return NULL;
    }
    struct yetty_yclass_object *music = obj_r.value;

    float staff_space = cell_h * 0.85f;
    if (staff_space < 8.0f) {
        staff_space = 8.0f;
    }
    (void)yetty_ymusic_configure(music, width_px, staff_space, 0);

    struct yetty_ycore_void_result pr = yetty_ymusic_parse(music, (const char *)bytes, len);
    if (YETTY_IS_ERR(pr)) {
        fprintf(stderr, "yless: lilypond parse failed: %s\n", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        (void)yetty_ymusic_destroy(music);
        return NULL;
    }

    struct yetty_ydraw_drawable_list_result rr = yetty_ymusic_render(music);
    /* The rendered list owns its own bytes (primitives + a font-name ref, not
     * the font itself), so the model can go now. */
    (void)yetty_ymusic_destroy(music);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yless: lilypond render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *cause = rr.error.cause; cause; cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        return NULL;
    }
    return rr.value;
}

/*=============================================================================
 * Raw-mode keyboard
 *===========================================================================*/

enum key {
    KEY_NONE = 0,
    KEY_QUIT,
    KEY_LINE_DOWN,
    KEY_LINE_UP,
    KEY_PAGE_DOWN,
    KEY_PAGE_UP,
    KEY_TOP,
    KEY_BOTTOM,
};

/* Decode one logical key from `buf` starting at *idx, advancing *idx past the
 * bytes consumed. Handles plain keys + the common CSI arrow/page sequences
 * (when the sequence is contained in this chunk). Ctrl-C (0x03) / Ctrl-D
 * (0x04) map to quit — raw mode disables signal generation, so they arrive as
 * bytes and the normal cleanup (which clears the surface) still runs. */
static enum key decode_key(const char *buf, int n, int *idx)
{
    unsigned char c = (unsigned char)buf[(*idx)++];
    switch (c) {
    case 'q':
    case 'Q':
    case 0x03: /* Ctrl-C */
    case 0x04: /* Ctrl-D */
        return KEY_QUIT;
    case 'j':
    case '\r':
    case '\n':
        return KEY_LINE_DOWN;
    case 'k':
        return KEY_LINE_UP;
    case ' ':
    case 'f':
        return KEY_PAGE_DOWN;
    case 'b':
        return KEY_PAGE_UP;
    case 'g':
        return KEY_TOP;
    case 'G':
        return KEY_BOTTOM;
    case 0x1b:
        break; /* ESC — maybe a CSI sequence */
    default:
        return KEY_NONE;
    }
    /* ESC: '[' or 'O' then a final byte, all within this chunk. */
    if (*idx >= n) {
        return KEY_QUIT; /* lone ESC → quit, like less */
    }
    char intro = buf[*idx];
    if (intro != '[' && intro != 'O') {
        return KEY_NONE;
    }
    (*idx)++;
    if (*idx >= n) {
        return KEY_NONE;
    }
    switch (buf[(*idx)++]) {
    case 'A':
        return KEY_LINE_UP;
    case 'B':
        return KEY_LINE_DOWN;
    case 'H':
        return KEY_TOP;
    case 'F':
        return KEY_BOTTOM;
    case '5':
        return KEY_PAGE_UP; /* CSI 5 ~ */
    case '6':
        return KEY_PAGE_DOWN; /* CSI 6 ~ */
    default:
        return KEY_NONE;
    }
}

/*=============================================================================
 * main
 *===========================================================================*/

int main(int argc, char **argv)
{
    struct yless_opts opts = {.opacity = 1.0f};

    static const struct yetty_yplatform_option long_opts[] = {
        {"x", required_argument, NULL, 'x'},     {"y", required_argument, NULL, 'y'},
        {"width", required_argument, NULL, 'w'}, {"height", required_argument, NULL, 'H'},
        {"alpha", required_argument, NULL, 'a'}, {"duration", required_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},        {NULL, 0, NULL, 0},
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "x:y:w:H:a:d:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'x':
            opts.origin_x = parse_dim(yetty_yplatform_optarg);
            break;
        case 'y':
            opts.origin_y = parse_dim(yetty_yplatform_optarg);
            break;
        case 'w':
            opts.width = parse_dim(yetty_yplatform_optarg);
            break;
        case 'H':
            opts.height = parse_dim(yetty_yplatform_optarg);
            break;
        case 'a':
            opts.opacity = (float)atof(yetty_yplatform_optarg);
            if (opts.opacity < 0.0f) {
                opts.opacity = 0.0f;
            }
            if (opts.opacity > 1.0f) {
                opts.opacity = 1.0f;
            }
            break;
        case 'd':
            opts.duration_sec = (float)atof(yetty_yplatform_optarg);
            if (opts.duration_sec < 0.0f) {
                opts.duration_sec = 0.0f;
            }
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    const char *path = (yetty_yplatform_optind < argc) ? argv[yetty_yplatform_optind] : NULL;

    /* Read the content bytes. */
    struct byte_buf input = {0};
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "yless: %s: cannot open\n", path);
            return 1;
        }
        int rc = read_all_stream(f, &input);
        fclose(f);
        if (rc < 0) {
            fprintf(stderr, "yless: %s: read failed\n", path);
            free(input.data);
            return 1;
        }
    } else if (read_all_stream(stdin, &input) < 0) {
        fprintf(stderr, "yless: stdin: read failed\n");
        free(input.data);
        return 1;
    }

    struct viewport vp = probe_viewport();

    /* Resolve the viewport rect to pixels (cells or percent of the pane),
     * defaulting to the whole pane, then clamp it inside the pane. */
    float origin_x = resolve_dim_px(&opts.origin_x, vp.pixel_w, (float)vp.cell_w, 0.0f);
    float origin_y = resolve_dim_px(&opts.origin_y, vp.pixel_h, (float)vp.cell_h, 0.0f);
    float rect_w = resolve_dim_px(&opts.width, vp.pixel_w, (float)vp.cell_w, vp.pixel_w - origin_x);
    float rect_h =
        resolve_dim_px(&opts.height, vp.pixel_h, (float)vp.cell_h, vp.pixel_h - origin_y);
    if (rect_w > vp.pixel_w - origin_x) {
        rect_w = vp.pixel_w - origin_x;
    }
    if (rect_h > vp.pixel_h - origin_y) {
        rect_h = vp.pixel_h - origin_y;
    }
    if (rect_w < 0.0f) {
        rect_w = 0.0f;
    }
    if (rect_h < 0.0f) {
        rect_h = 0.0f;
    }

    /* Content wraps / rasterizes to the box width in whole cells. */
    uint32_t width_cells = (uint32_t)(rect_w / (float)vp.cell_w);
    if (width_cells == 0) {
        width_cells = vp.cols;
    }

    struct yetty_ycat_config cfg = {
        .cell_width = vp.cell_w,
        .cell_height = vp.cell_h,
        .width_cells = width_cells,
        .height_cells = 0,
    };

    bool is_ly = is_lilypond(input.data, input.len, path);
    struct yetty_ydraw_drawable_list *content = NULL;
    if (is_ly) {
        /* LilyPond scores wrap into systems sized to the viewport width and
         * scroll vertically like the rest of yless's content. */
        content = render_lilypond(input.data, input.len, rect_w, (float)vp.cell_h);
    } else {
        content = render_content(input.data, input.len, path, &cfg);
    }
    if (!content) {
        free(input.data);
        return 1;
    }

    /* Mint the scrollable view (yclass object) and configure it over the
     * resolved rect. ctx is NULL — a view is always a local in-process
     * emitter. */
    uint32_t bg_rgb = is_ly ? music_bg_rgb_from_config() : YLESS_BG_RGB;
    uint32_t bg_color = pack_bg_abgr(bg_rgb, opts.opacity);
    struct yetty_yclass_object_ptr_result view_r = yetty_yview_view_create(NULL);
    if (YETTY_IS_ERR(view_r)) {
        fprintf(stderr, "yless: view create failed: %s\n", view_r.error.msg);
        yetty_ycore_error_destroy(view_r.error);
        yetty_ydraw_drawable_list_destroy(content);
        free(input.data);
        return 1;
    }
    struct yetty_yclass_object *view = view_r.value;

    /* When stdin is the controlling tty, enter raw mode BEFORE configure():
     * configure() attaches the server figure via a yclass-RPC handshake that
     * writes a request to stdout and BLOCKS reading the binary reply from
     * stdin. In canonical mode the line discipline withholds that reply until a
     * newline (and mangles control bytes), so the handshake would stall to its
     * timeout and the attach would fail. Raw/binary mode makes it readable at
     * once. set_raw runs exactly once (the interactive loop below reuses it). */
    bool stdin_tty = yetty_yplatform_tty_stdin_is_tty();
    if (stdin_tty) {
        yetty_yplatform_tty_binary_io();
        if (yetty_yplatform_tty_set_raw() < 0) {
            fprintf(stderr, "yless: cannot put stdin into raw mode\n");
            struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            yetty_ydraw_drawable_list_destroy(content);
            free(input.data);
            return 1;
        }
    }

    struct yetty_ycore_void_result cfg_r =
        yetty_yview_configure(view, YLESS_STDOUT_FD, YLESS_GETPID(), /*kind=*/0u, bg_color,
                              origin_x, origin_y, origin_x + rect_w, origin_y + rect_h);
    if (YETTY_IS_ERR(cfg_r)) {
        fprintf(stderr, "yless: configure failed: %s\n", cfg_r.error.msg);
        yetty_ycore_error_destroy(cfg_r.error);
        struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        yetty_ydraw_drawable_list_destroy(content);
        if (stdin_tty) {
            yetty_yplatform_tty_restore();
        }
        free(input.data);
        return 1;
    }

    struct yetty_ycore_void_result content_r = yetty_yview_set_content(view, content);
    yetty_ydraw_drawable_list_destroy(content); /* envelope copied the bytes */
    if (YETTY_IS_ERR(content_r)) {
        fprintf(stderr, "yless: set_content failed: %s\n", content_r.error.msg);
        yetty_ycore_error_destroy(content_r.error);
        struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        if (stdin_tty) {
            yetty_yplatform_tty_restore();
        }
        free(input.data);
        return 1;
    }

    /* Raw stdin (cross-platform): byte-at-a-time, no echo, no signal
     * generation — so Ctrl-C/D arrive as bytes and the normal exit path (which
     * clears the surface) always runs. Interactive only when stdin is a tty;
     * with piped content there is no keyboard, so just emit and exit. Raw mode
     * was already entered before configure() above (the RPC attach needs it);
     * here we only run the key loop and restore on the way out. */
    const float line = (float)vp.cell_h;
    const float page = rect_h > line ? rect_h - line : line;
    const float to_bottom = 1.0e9f; /* clamped server-side and in yview */

    /* --duration: auto-exit after this many seconds, measured from here (the
     * content is on screen now). A deadline of 0 means "run until the user
     * quits" — the default. */
    const bool have_deadline = opts.duration_sec > 0.0f;
    const double deadline = yetty_yplatform_ytime_monotonic_sec() + (double)opts.duration_sec;

    if (stdin_tty) {
        char buf[64];
        bool running = true;
        while (running) {
            unsigned wait_ms = 200;
            if (have_deadline) {
                double remaining = deadline - yetty_yplatform_ytime_monotonic_sec();
                if (remaining <= 0.0) {
                    break;
                }
                /* Don't poll past the deadline — clamp so the auto-exit lands
                 * on time rather than up to one full poll interval late. */
                double remaining_ms = remaining * 1000.0;
                if (remaining_ms < (double)wait_ms) {
                    wait_ms = remaining_ms >= 1.0 ? (unsigned)remaining_ms : 1u;
                }
            }
            int rdy = yetty_yplatform_tty_stdin_wait(wait_ms);
            if (rdy < 0) {
                break;
            }
            if (rdy == 0) {
                continue;
            }
            int n = yetty_yplatform_tty_stdin_read(buf, sizeof(buf));
            if (n <= 0) {
                break; /* EOF / error */
            }
            int i = 0;
            while (i < n && running) {
                struct yetty_ycore_void_result sr = YETTY_OK_VOID();
                switch (decode_key(buf, n, &i)) {
                case KEY_QUIT:
                    running = false;
                    break;
                case KEY_LINE_DOWN:
                    sr = yetty_yview_scroll_by(view, 0.0f, line);
                    break;
                case KEY_LINE_UP:
                    sr = yetty_yview_scroll_by(view, 0.0f, -line);
                    break;
                case KEY_PAGE_DOWN:
                    sr = yetty_yview_scroll_by(view, 0.0f, page);
                    break;
                case KEY_PAGE_UP:
                    sr = yetty_yview_scroll_by(view, 0.0f, -page);
                    break;
                case KEY_TOP:
                    sr = yetty_yview_scroll_to(view, 0.0f, 0.0f);
                    break;
                case KEY_BOTTOM:
                    sr = yetty_yview_scroll_to(view, 0.0f, to_bottom);
                    break;
                case KEY_NONE:
                default:
                    break;
                }
                if (YETTY_IS_ERR(sr)) {
                    yetty_ycore_error_destroy(sr.error);
                    running = false;
                }
            }
        }
        yetty_yplatform_tty_restore();
    } else if (have_deadline) {
        /* No keyboard (stdin is not a tty): hold the figure on screen for the
         * requested duration, then fall through to clear it. Keeps the flag's
         * contract — "run for F seconds" — honoured on the non-interactive
         * path too. */
        yetty_yplatform_ytime_sleep_ms((unsigned)(opts.duration_sec * 1000.0f + 0.5f));
    }

    /* Clear the surface: DELETE_CHILD removes the figure from the container. */
    struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    free(input.data);
    return 0;
}
