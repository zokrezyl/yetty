/*
 * yless — a `less`-semantics pager that renders into a server-side scrollable
 * figure (yview) instead of the scrolling buffer.
 *
 * Contrast with ycat: ycat dumps rendered content into the scrollback (DCS
 * YDRAW_BIN); it ages out of history like any text. yless ships the content
 * ONCE as a positioned, viewport-anchored figure (DCS YCOMPOSITOR_BIN) and
 * then stays in the foreground translating keystrokes into scroll commands.
 * Scrolling is server-side state: each key sends a tiny SET_CHILD_SCROLL
 * record, never re-shipping the content. On exit the figure is removed
 * (DELETE_CHILD), so the surface is cleared.
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
#include <yetty/ymusic/rpc.h>     /* yetty_ymusic_music_create / _register */
#include <yetty/yplatform/getopt.h>
#include <yetty/yplatform/term.h> /* yetty_yplatform_term_get_size */
#include <yetty/yplatform/tty.h>  /* raw mode + stdin read, cross-platform */
#include <yetty/yview/rpc.h>      /* yetty_yview_view_create */
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

struct yless_opts {
    int x_cells;   /* origin column (cells); 0 = pane left */
    int y_cells;   /* origin row (cells);    0 = pane top  */
    int w_cells;   /* width  in cells; 0 = to right edge */
    int h_cells;   /* height in cells; 0 = to bottom edge */
    float opacity; /* background opacity 0.0..1.0 (1.0 = opaque) */
};

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
            "Options (position/size are in terminal CELLS; default = whole pane):\n"
            "  -x, --x=N        origin column   (default 0)\n"
            "  -y, --y=N        origin row      (default 0)\n"
            "  -w, --width=N    width in cells  (default: to right edge)\n"
            "  -H, --height=N   height in cells (default: to bottom edge)\n"
            "  -a, --alpha=F    background opacity 0.0..1.0 (default 1.0 = opaque;\n"
            "                   0.0 = transparent, terminal text shows through)\n"
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
            "  %s -x 42 -w 38 b.pdf      # a pdf in the right-hand half\n",
            prog, prog, prog, prog);
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
    /* yplatform exposes the terminal size in CELLS (portable). Pixels are
     * approximated with a fixed cell size — good enough to place the figure;
     * the server clips to the rect regardless. */
    struct viewport vp = {.cell_w = 8, .cell_h = 16, .cols = 80};
    int cols = 80, rows = 24;
    (void)yetty_yplatform_term_get_size(&cols, &rows);
    if (cols <= 0) {
        cols = 80;
    }
    if (rows <= 0) {
        rows = 24;
    }
    vp.cols = (uint32_t)cols;
    vp.pixel_w = (float)(cols * (int)vp.cell_w);
    vp.pixel_h = (float)(rows * (int)vp.cell_h);
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

static bool file_exists(const char *path)
{
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        return false;
    }
    fclose(probe);
    return true;
}

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

/* Locate the Emmentaler music font: explicit override, then the runtime fonts
 * dir the parent yetty exports, then dev-tree fallbacks. Returns out on
 * success (a readable path), or NULL. */
static const char *resolve_music_font(char *out, size_t cap)
{
    const char *env_font = getenv("YETTY_YMUSIC_FONT");
    if (env_font && env_font[0] && file_exists(env_font)) {
        snprintf(out, cap, "%s", env_font);
        return out;
    }
    const char *fonts_dir = getenv("YETTY_FONTS_DIR");
    if (fonts_dir && fonts_dir[0]) {
        snprintf(out, cap, "%s/Emmentaler-20.otf", fonts_dir);
        if (file_exists(out)) {
            return out;
        }
    }
    const char *data_dir = getenv("YETTY_DATA_DIR");
    if (data_dir && data_dir[0]) {
        snprintf(out, cap, "%s/fonts/Emmentaler-20.otf", data_dir);
        if (file_exists(out)) {
            return out;
        }
    }
    static const char *dev_paths[] = {
        "assets/fonts/Emmentaler-20.otf",
        "build-desktop-ytrace-release/assets/fonts/Emmentaler-20.otf",
    };
    for (size_t i = 0; i < sizeof(dev_paths) / sizeof(dev_paths[0]); i++) {
        if (file_exists(dev_paths[i])) {
            snprintf(out, cap, "%s", dev_paths[i]);
            return out;
        }
    }
    return NULL;
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
    struct yetty_yconfig_paths paths = {
        .shaders_dir = getenv("YETTY_SHADERS_DIR"),
        .fonts_dir = getenv("YETTY_FONTS_DIR"),
        .runtime_dir = getenv("YETTY_RUNTIME_DIR"),
        .bin_dir = getenv("YETTY_BIN_DIR"),
        .config_dir = getenv("YETTY_CONFIG_DIR"),
    };
    char prog[] = "yless";
    char *argv[] = {prog, NULL};
    struct yetty_yconfig_result config_r = yetty_yconfig_create(1, argv, &paths);
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
    char font_path[1024];
    if (!resolve_music_font(font_path, sizeof(font_path))) {
        fprintf(stderr, "yless: music font not found; set YETTY_YMUSIC_FONT to an "
                        "Emmentaler/SMuFL .otf\n");
        return NULL;
    }

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
    (void)yetty_ymusic_configure(NULL, music, width_px, staff_space, 0);
    (void)yetty_ymusic_set_font_path(NULL, music, font_path);

    struct yetty_ycore_void_result pr = yetty_ymusic_parse(NULL, music, (const char *)bytes, len);
    if (YETTY_IS_ERR(pr)) {
        fprintf(stderr, "yless: lilypond parse failed: %s\n", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        (void)yetty_ymusic_destroy(NULL, music);
        return NULL;
    }

    struct yetty_ydraw_drawable_list_result rr = yetty_ymusic_render(NULL, music);
    /* The rendered list owns its own bytes (font + primitives), so the model
     * can go now. */
    (void)yetty_ymusic_destroy(NULL, music);
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
        {"x", required_argument, NULL, 'x'},
        {"y", required_argument, NULL, 'y'},
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"alpha", required_argument, NULL, 'a'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "x:y:w:H:a:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'x':
            opts.x_cells = atoi(yetty_yplatform_optarg);
            break;
        case 'y':
            opts.y_cells = atoi(yetty_yplatform_optarg);
            break;
        case 'w':
            opts.w_cells = atoi(yetty_yplatform_optarg);
            break;
        case 'H':
            opts.h_cells = atoi(yetty_yplatform_optarg);
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

    /* Resolve the viewport rect (cells → pixels), defaulting to the pane. */
    float origin_x = (float)opts.x_cells * (float)vp.cell_w;
    float origin_y = (float)opts.y_cells * (float)vp.cell_h;
    float rect_w =
        opts.w_cells > 0 ? (float)opts.w_cells * (float)vp.cell_w : vp.pixel_w - origin_x;
    float rect_h =
        opts.h_cells > 0 ? (float)opts.h_cells * (float)vp.cell_h : vp.pixel_h - origin_y;

    struct yetty_ycat_config cfg = {
        .cell_width = vp.cell_w,
        .cell_height = vp.cell_h,
        .width_cells = opts.w_cells > 0 ? (uint32_t)opts.w_cells : vp.cols,
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

    struct yetty_ycore_void_result cfg_r =
        yetty_yview_configure(NULL, view, YLESS_STDOUT_FD, YLESS_GETPID(), /*kind=*/0u, bg_color,
                              origin_x, origin_y, origin_x + rect_w, origin_y + rect_h);
    if (YETTY_IS_ERR(cfg_r)) {
        fprintf(stderr, "yless: configure failed: %s\n", cfg_r.error.msg);
        yetty_ycore_error_destroy(cfg_r.error);
        struct yetty_ycore_void_result dr = yetty_yview_destroy(NULL, view);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        yetty_ydraw_drawable_list_destroy(content);
        free(input.data);
        return 1;
    }

    struct yetty_ycore_void_result content_r = yetty_yview_set_content(NULL, view, content);
    yetty_ydraw_drawable_list_destroy(content); /* envelope copied the bytes */
    if (YETTY_IS_ERR(content_r)) {
        fprintf(stderr, "yless: set_content failed: %s\n", content_r.error.msg);
        yetty_ycore_error_destroy(content_r.error);
        struct yetty_ycore_void_result dr = yetty_yview_destroy(NULL, view);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        free(input.data);
        return 1;
    }

    /* Raw stdin (cross-platform): byte-at-a-time, no echo, no signal
     * generation — so Ctrl-C/D arrive as bytes and the normal exit path (which
     * clears the surface) always runs. Interactive only when stdin is a tty;
     * with piped content there is no keyboard, so just emit and exit. */
    const float line = (float)vp.cell_h;
    const float page = rect_h > line ? rect_h - line : line;
    const float to_bottom = 1.0e9f; /* clamped server-side and in yview */

    if (yetty_yplatform_tty_stdin_is_tty()) {
        yetty_yplatform_tty_binary_io();
        if (yetty_yplatform_tty_set_raw() == 0) {
            char buf[64];
            bool running = true;
            while (running) {
                int rdy = yetty_yplatform_tty_stdin_wait(200);
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
                        sr = yetty_yview_scroll_by(NULL, view, 0.0f, line);
                        break;
                    case KEY_LINE_UP:
                        sr = yetty_yview_scroll_by(NULL, view, 0.0f, -line);
                        break;
                    case KEY_PAGE_DOWN:
                        sr = yetty_yview_scroll_by(NULL, view, 0.0f, page);
                        break;
                    case KEY_PAGE_UP:
                        sr = yetty_yview_scroll_by(NULL, view, 0.0f, -page);
                        break;
                    case KEY_TOP:
                        sr = yetty_yview_scroll_to(NULL, view, 0.0f, 0.0f);
                        break;
                    case KEY_BOTTOM:
                        sr = yetty_yview_scroll_to(NULL, view, 0.0f, to_bottom);
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
        }
    }

    /* Clear the surface: DELETE_CHILD removes the figure from the container. */
    struct yetty_ycore_void_result dr = yetty_yview_destroy(NULL, view);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    free(input.data);
    return 0;
}
