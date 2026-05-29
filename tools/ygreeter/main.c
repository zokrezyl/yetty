/*
 * ygreeter — first-contact tool. Dual-mode.
 *
 *   TERM_PROGRAM=yetty  → CLIENT mode. The hosting yetty terminal
 *                         consumes our OSC envelopes; we ship them
 *                         over stdout. stdin delivers real keystrokes.
 *   otherwise           → STANDALONE mode. We open our own window via
 *                         yinit_run + yframework_create, spin up a
 *                         local yfigure_container that's fed by an
 *                         in-process wire_statemachine reading from
 *                         the consumer end of a memory pty pair (the
 *                         producer end is ygui's output pty). yetty
 *                         framework's KEY events get serialised to
 *                         the same CSI escape sequences a terminal
 *                         would emit and pushed into ygui via
 *                         yetty_ygui_framework_feed_input.
 *
 * From ygui's perspective the two modes are identical: it has an
 * output_pty to write OSC envelopes to, and someone calls
 * framework_feed_input with byte-stream keystrokes. The framework has
 * no knowledge of which mode it's in.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/rpc.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yterm/client-input.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yplot/yplot.h>

#include "embedded-assets.h"

#ifdef YETTY_YGREETER_HAS_STANDALONE
/* Headers below pull <yetty/yetty/yetty.h> (or <webgpu/webgpu.h> directly)
 * via their public API surface. Standalone mode needs them; client mode
 * doesn't, and on platforms without WebGPU (riscv64 cross) including
 * them breaks the build. Keep gated. */
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yrender/render-target.h>
#endif

#ifdef YETTY_YGUI_HAS_UV
#include <uv.h>
#endif

#include <sys/ioctl.h>
#include <termios.h>

/*=============================================================================
 * Tab descriptors — mirrors the main-branch ygreeter layout:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ [Welcome] [Plots] [Images] [Code]                         │
 *   ├──────────────────┬───────────────────────────────────────┤
 *   │  Intro           │                                        │
 *   │  Quick start     │   <content widget for selected entry> │
 *   │  Capabilities    │                                        │
 *   └──────────────────┴───────────────────────────────────────┘
 * Per-tab body is an hbox: a fixed-width nav vbox of buttons on the
 * left, a content widget on the right (rich / yplot / yimage).
 *===========================================================================*/

enum tab_kind {
    TAB_KIND_RICH = 0,    /* per-row span list, rendered by `rich` widget */
    TAB_KIND_PLOTS,       /* per-row yplot source, rendered by `yplot` figure */
    TAB_KIND_IMAGES,      /* per-row logo path, rendered by `yimage` figure */
    TAB_KIND_VIDEO,       /* per-row mp4 path, rendered by `yvideo` figure */
    TAB_KIND_ELEMENTS,    /* showcase of ygui widgets in a scrollarea */
    TAB_KIND_YREADME,     /* extracted README.md, rendered by `ymarkdown` */
    TAB_KIND_YBROWSER,    /* inline HTML, rendered by `ybrowser` */
};

#define TAB_COUNT 8

static const char *TAB_LABELS[TAB_COUNT] = {"Welcome",  "Plots",    "Images",  "Code",
                                            "Video",    "Elements", "YReadme", "YBrowser"};

/* Brand palette — packed RGBA, R in low byte. Per rules/08-branding.md. */
#define BRAND_TEXT 0xFFE4E5E0u
#define BRAND_MUTED 0xFFA8A79Fu
#define BRAND_ACCENT 0xFF92A86Bu
#define BRAND_ACCENT_BRIGHT 0xFFA5C574u
#define BRAND_BG 0xFF14100Bu
#define BRAND_BG_LIFTED 0xFF1F1A14u
#define BRAND_BG_ROW 0xFF2C261Eu
#define BRAND_BORDER 0xFF474A36u
/* Code-snippet syntax colours (off-palette by design — code highlighting
 * exists in every editor and uses its own conventions). */
#define CODE_KEYWORD 0xFF4E8BECu  /* warm orange — int / return */
#define CODE_TYPE 0xFFFFD8B9u     /* light blue — struct names */
#define CODE_STRING 0xFFA8E0A8u   /* mint — literals */
#define CODE_COMMENT 0xFF8B8B8Bu  /* gray */
#define CODE_PUNCT 0xFFC0C0C0u    /* off-white */

/* One nav entry binds a row label to a producer-specific payload. The
 * tab kind picks which field is used:
 *   TAB_KIND_RICH    — the build() callback writes spans into the rich.
 *   TAB_KIND_PLOTS   — the plot source string drives yplot_set_source.
 *   TAB_KIND_IMAGES  — the absolute path is read at click time and the
 *                      file bytes feed yimage_set_bytes. */
struct nav_entry {
    const char *label;
    const char *payload;
    float x_min, x_max, y_min, y_max;
};

/*=============================================================================
 * App state.
 *===========================================================================*/

/* Forward decl: client-mode loop state is opaque to non-client code. */
struct client_state;

struct tab_state {
    enum tab_kind kind;
    /* Content widget on the right of the body hbox — rich / yplot / yimage. */
    struct yetty_ygui_object *content;
    const struct nav_entry *entries;
    int n_entries;
    int active_entry;
};

struct app {
    struct yetty_ygui_runtime *engine;
    struct yetty_ygui_object *root;
    struct yetty_ygui_object *tabbar;
    struct yetty_ygui_object *body_panel;
    struct yetty_ygui_object *statusbar;

    struct tab_state tabs[TAB_COUNT];

    /* Image-path scratch: filled from get_data_dir/logo-N.jpeg at
     * startup by discover_logo_images, referenced from the Images
     * nav entries. Heap so the count can grow. Owned by the app —
     * freed at shutdown. */
    char **image_paths;
    int image_path_count;

    /* Video-path scratch: filled with `<data_dir>/yetty-unchained-2.mp4`
     * at startup by discover_video_files, referenced from the Video
     * nav entries. Same lifetime / shape as image_paths. */
    char **video_paths;
    int video_path_count;

    /* Extracted README.md absolute path inside data_dir, or NULL when
     * the YREADME tab should fall back to its inline placeholder. */
    char *readme_path;

    /* Client mode back-pointer for the key-handler's stop_cb path.
     * NULL in standalone mode. */
    struct client_state *client;

#ifdef YETTY_YGREETER_HAS_STANDALONE
    /* Standalone-mode resources, NULL in client mode. The headers that
     * define the by-value member types (memory_pty_pair, figure_args,
     * event_listener) pull in webgpu transitively, so the whole block
     * is gated. */
    struct yetty_yframework *yframework;
    struct yetty_yplatform_memory_pty_pair pty_pair;
    int has_pty_pair;
    struct yetty_yfigure_container *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_raw_figure_factory *figure_factory;
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct yetty_ydraw_font *font;
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_yevent_event_listener listener;
    struct yetty_ydraw_target *render_target;
#endif
};

/*=============================================================================
 * UI build + tab navigation.
 *===========================================================================*/

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

/*-----------------------------------------------------------------------------
 * Welcome tab spans — authored as a static table the build_welcome_rich
 * function walks. Same approach the original ygreeter used. Selecting a
 * different welcome row writes a different sub-table into the rich.
 *---------------------------------------------------------------------------*/

struct rich_span {
    const char *text;
    float fs;
    uint32_t color;
    int new_line_first;
};

static const struct rich_span welcome_intro_spans[] = {
    {"Welcome to yetty", 24.0f, BRAND_ACCENT, 1},
    {"", 0.0f, 0, 1},
    {"A GPU terminal that draws more than text.", 16.0f, BRAND_TEXT, 1},
    {"", 0.0f, 0, 1},
    {"Switch between Welcome / Plots / Images / Code to see the GPU layer.", 14.0f, BRAND_MUTED, 1},
    {"Use the navigation column on the left to jump between sub-topics.", 14.0f, BRAND_MUTED, 1},
};

static const struct rich_span welcome_what_spans[] = {
    {"What is yetty?", 22.0f, BRAND_ACCENT, 1},
    {"", 0.0f, 0, 1},
    {"yetty is a GPU-accelerated terminal emulator with native support for plots,", 14.0f,
     BRAND_TEXT, 1},
    {"images, rich text and arbitrary draw lists alongside conventional shell I/O.", 14.0f,
     BRAND_TEXT, 1},
};

static const struct rich_span welcome_quick_spans[] = {
    {"Quick start", 22.0f, BRAND_ACCENT, 1},
    {"", 0.0f, 0, 1},
    {"Keyboard:", 14.0f, BRAND_TEXT, 1},
    {"  q         — quit", 13.0f, BRAND_MUTED, 1},
    {"  ←/→       — switch tabs", 13.0f, BRAND_MUTED, 1},
    {"  click row — load that entry", 13.0f, BRAND_MUTED, 1},
};

static const struct rich_span welcome_caps_spans[] = {
    {"Capabilities", 22.0f, BRAND_ACCENT, 1},
    {"", 0.0f, 0, 1},
    {"  • yplot  — expression-driven 2D plotting (see Plots tab)", 14.0f, BRAND_TEXT, 1},
    {"  • yimage — decoded image surfaces (see Images tab)", 14.0f, BRAND_TEXT, 1},
    {"  • rich   — coloured text spans (this view)", 14.0f, BRAND_TEXT, 1},
    {"  • plus the regular shell underneath", 14.0f, BRAND_MUTED, 1},
};

/*-----------------------------------------------------------------------------
 * Code tab — multiple colour-coded snippets. Each entry's `payload` field
 * is just an identifier looked up in code_snippet_at().
 *---------------------------------------------------------------------------*/

struct code_line {
    struct code_span {
        const char *text;
        uint32_t color;
    } spans[6];
};

struct code_snippet {
    const char *id;
    const struct code_line *lines;
    size_t n_lines;
};

static const struct code_line code_minimal_lines[] = {
    {{{"/* Minimal ygui app — fits in main(). */", CODE_COMMENT}}},
    {{{"#include ", CODE_KEYWORD}, {"<yetty/ygui/ygui.h>", CODE_STRING}}},
    {{{"", 0}}},
    {{{"int", CODE_KEYWORD}, {" main", CODE_TYPE}, {"(", CODE_PUNCT}, {"void", CODE_KEYWORD},
      {") {", CODE_PUNCT}}},
    {{{"    framework_emit(engine);", BRAND_TEXT}}},
    {{{"    ", BRAND_TEXT}, {"return ", CODE_KEYWORD}, {"0", CODE_STRING}, {";", CODE_PUNCT}}},
    {{{"}", CODE_PUNCT}}},
};

static const struct code_line code_widget_lines[] = {
    {{{"/* Adding a button — single call site. */", CODE_COMMENT}}},
    {{{"struct ", CODE_KEYWORD}, {"yetty_ygui_object_ptr_result ", CODE_TYPE},
      {"br = yetty_ygui_add(", BRAND_TEXT}}},
    {{{"    yetty_ygui_button_class_get().value, parent);", BRAND_TEXT}}},
    {{{"yetty_ygui_button_set_label(br.value, ", BRAND_TEXT}, {"\"Apply\"", CODE_STRING},
      {");", CODE_PUNCT}}},
};

static const struct code_line code_subscribe_lines[] = {
    {{{"/* Subscribe to a value-changed event. */", CODE_COMMENT}}},
    {{{"yetty_ygui_object_subscribe(", BRAND_TEXT}}},
    {{{"    slider, ", BRAND_TEXT}, {"YETTY_YGUI_EVENT_VALUE_CHANGED", CODE_TYPE},
      {",", CODE_PUNCT}}},
    {{{"    on_changed, userdata);", BRAND_TEXT}}},
};

static const struct code_snippet *code_snippet_at(const char *id)
{
    static const struct code_snippet table[] = {
        {"minimal", code_minimal_lines, sizeof(code_minimal_lines) / sizeof(code_minimal_lines[0])},
        {"widget", code_widget_lines, sizeof(code_widget_lines) / sizeof(code_widget_lines[0])},
        {"subscribe", code_subscribe_lines,
         sizeof(code_subscribe_lines) / sizeof(code_subscribe_lines[0])},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (strcmp(table[i].id, id) == 0) return &table[i];
    }
    return NULL;
}

/*-----------------------------------------------------------------------------
 * Nav-entry tables, one per tab. The `payload` field is interpreted by
 * the tab's kind:
 *   Welcome  — pointer into welcome_*_spans (cast back via dispatch in
 *              load_entry)
 *   Plots    — yplot source expression
 *   Images   — index into g_image_paths (encoded as the path itself)
 *   Code     — snippet id resolved by code_snippet_at()
 *---------------------------------------------------------------------------*/

struct welcome_nav {
    const char *label;
    const struct rich_span *spans;
    size_t n_spans;
};

static const struct welcome_nav welcome_nav_entries[] = {
    {"Intro", welcome_intro_spans, sizeof(welcome_intro_spans) / sizeof(welcome_intro_spans[0])},
    {"What is yetty?", welcome_what_spans,
     sizeof(welcome_what_spans) / sizeof(welcome_what_spans[0])},
    {"Quick start", welcome_quick_spans,
     sizeof(welcome_quick_spans) / sizeof(welcome_quick_spans[0])},
    {"Capabilities", welcome_caps_spans,
     sizeof(welcome_caps_spans) / sizeof(welcome_caps_spans[0])},
};

static const struct nav_entry plot_nav_entries[] = {
    {"sin / cos",
     "f = sin(x); g = cos(x); @f.color = #6BA892; @g.color = #74C5A5",
     -3.14159f, 3.14159f, -1.5f, 1.5f},
    {"Polynomial",
     "f = x*x; g = 2*x + 1; @f.color = #FFD700; @g.color = #74C5A5",
     -5.0f, 5.0f, -2.0f, 12.0f},
    {"Damped wave",
     "f = exp(-x*x/4) * sin(3*x); @f.color = #74C0FC",
     -6.0f, 6.0f, -1.2f, 1.2f},
};

static const struct nav_entry code_nav_entries[] = {
    {"Minimal app", "minimal", 0, 0, 0, 0},
    {"Add a widget", "widget", 0, 0, 0, 0},
    {"Subscribe to events", "subscribe", 0, 0, 0, 0},
};

/*-----------------------------------------------------------------------------
 * Image discovery — populate g_image_paths[] with absolute paths to
 * logo-N.jpeg files under the platform's data dir, matching the
 * main-branch behaviour. */
static void discover_logo_images(struct app *app)
{
    if (app->image_paths) return;
    const char *data_dir = yetty_yplatform_get_data_dir();
    ydebug("ygreeter: discover_logo_images data_dir=%s", data_dir ? data_dir : "(null)");
    if (!data_dir || !*data_dir) return;
    char path_buf[1024];
    char **paths = NULL;
    int count = 0;
    int cap = 0;
    for (int i = 1; i <= 8; ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/logo-%d.jpeg", data_dir, i);
        int ok = access(path_buf, R_OK) == 0;
        ydebug("ygreeter: discover_logo_images probe=%s status=%s", path_buf,
               ok ? "found" : "missing");
        if (!ok) continue;
        if (count == cap) {
            int ncap = cap ? cap * 2 : 4;
            char **np = realloc(paths, (size_t)ncap * sizeof(*np));
            if (!np) break;
            paths = np;
            cap = ncap;
        }
        paths[count] = strdup(path_buf);
        if (!paths[count]) break;
        count++;
    }
    app->image_paths = paths;
    app->image_path_count = count;
    ydebug("ygreeter: discover_logo_images count=%d", count);
}

/* Same probe loop as discover_logo_images, but for the demo MP4 that
 * rides along with the logos in the same incbin "data/" section.
 * Single entry on disk today (yetty-unchained-2.mp4) — kept as a
 * `paths` + `count` pair so the Video tab can grow more clips later
 * without a callsite refactor. */
static void discover_video_files(struct app *app)
{
    if (app->video_paths) return;
    const char *data_dir = yetty_yplatform_get_data_dir();
    ydebug("ygreeter: discover_video_files data_dir=%s", data_dir ? data_dir : "(null)");
    if (!data_dir || !*data_dir) return;
    char path_buf[1024];
    char **paths = NULL;
    int count = 0;
    int cap = 0;
    static const char *const candidates[] = {"yetty-unchained-2.mp4"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", data_dir, candidates[i]);
        int ok = access(path_buf, R_OK) == 0;
        ydebug("ygreeter: discover_video_files probe=%s status=%s", path_buf,
               ok ? "found" : "missing");
        if (!ok) continue;
        if (count == cap) {
            int ncap = cap ? cap * 2 : 4;
            char **np = realloc(paths, (size_t)ncap * sizeof(*np));
            if (!np) break;
            paths = np;
            cap = ncap;
        }
        paths[count] = strdup(path_buf);
        if (!paths[count]) break;
        count++;
    }
    app->video_paths = paths;
    app->video_path_count = count;
    ydebug("ygreeter: discover_video_files count=%d", count);
}

/* Locate the extracted README.md (incbin manifest above ships
 * <data_dir>/README.md). Failure leaves app->readme_path NULL — the
 * YReadme tab then falls back to a small inline markdown blurb. */
static void discover_readme(struct app *app)
{
    if (app->readme_path) return;
    const char *data_dir = yetty_yplatform_get_data_dir();
    if (!data_dir || !*data_dir) return;
    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%s/README.md", data_dir);
    int ok = access(path_buf, R_OK) == 0;
    ydebug("ygreeter: discover_readme probe=%s status=%s", path_buf, ok ? "found" : "missing");
    if (!ok) return;
    app->readme_path = strdup(path_buf);
}

/*-----------------------------------------------------------------------------
 * Loaders — populate the per-tab content widget from a given entry.
 *---------------------------------------------------------------------------*/

/* Wipe a content widget's children and recreate it as the requested kind
 * — the new ygui rich/yplot/yimage widgets do not expose a clear API
 * comparable to ygui-old's set_yaml replacement. Recreating is the
 * simplest correct way to swap content. */
static struct yetty_ycore_void_result rebuild_tab_content(struct app *app, int tab_index);

static struct yetty_ycore_void_result load_plot_entry(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                      const struct nav_entry *entry)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *plot = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ygui_yplot_config cfg = {
        .x_min = entry->x_min,
        .x_max = entry->x_max,
        .y_min = entry->y_min,
        .y_max = entry->y_max,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    yetty_ycore_error_destroy_safe(yetty_ygui_yplot_set_config(plot, &cfg));
    return yetty_ygui_yplot_set_source(plot, entry->payload);
}

/* Read the entire file at `path` into a heap buffer. Caller owns the
 * returned pointer and must free it. On failure returns NULL and
 * leaves `*out_len` untouched. Shared by load_image_entry,
 * load_video_entry, load_readme_entry — same idiom three times wasn't
 * worth keeping. */
static uint8_t *slurp_file(const char *path, size_t *out_len)
{
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *out_len = got;
    return buf;
}

static struct yetty_ycore_void_result load_image_entry(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                       const char *path)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *image = (struct yetty_ygui_object *)_yc_obj;
    if (!path) {
        return yetty_ygui_yimage_set_bytes(image, NULL, 0);
    }
    size_t got = 0;
    uint8_t *buf = slurp_file(path, &got);
    if (!buf) return YETTY_ERR(yetty_ycore_void, "load_image_entry: slurp failed");
    struct yetty_ycore_void_result r = yetty_ygui_yimage_set_bytes(image, buf, got);
    free(buf);
    return r;
}

static struct yetty_ycore_void_result load_video_entry(struct yetty_yclass_ctx *_yc_ctx,
                                                       struct yetty_yclass_object *_yc_obj,
                                                       const char *path)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *video = (struct yetty_ygui_object *)_yc_obj;
    if (!path) {
        return yetty_ygui_yvideo_set_bytes(video, NULL, 0);
    }
    size_t got = 0;
    uint8_t *buf = slurp_file(path, &got);
    if (!buf) return YETTY_ERR(yetty_ycore_void, "load_video_entry: slurp failed");
    struct yetty_ycore_void_result r = yetty_ygui_yvideo_set_bytes(video, buf, got);
    free(buf);
    return r;
}

/* Inline placeholder used when no <data_dir>/README.md was extracted
 * (dev builds without YETTY_ENABLE_LIB_INCBIN, or a stale data dir
 * from before the assets manifest was wired up). Small enough to
 * answer "is this tab broken?" at a glance. */
static const char YREADME_FALLBACK_MD[] =
    "# YReadme\n"
    "\n"
    "This tab embeds **README.md** from the repo via incbin and renders\n"
    "it through the `ymarkdown` widget.\n"
    "\n"
    "No file was extracted into `<data_dir>/README.md` — the build was\n"
    "configured without `YETTY_ENABLE_LIB_INCBIN`, or the marker was\n"
    "left over from a previous version that didn't ship this asset.\n";

static struct yetty_ycore_void_result load_readme_entry(struct yetty_yclass_ctx *_yc_ctx,
                                                        struct yetty_yclass_object *_yc_obj,
                                                        const char *path)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *md = (struct yetty_ygui_object *)_yc_obj;
    if (path) {
        size_t got = 0;
        uint8_t *buf = slurp_file(path, &got);
        if (buf) {
            struct yetty_ycore_void_result r =
                yetty_ygui_ymarkdown_set_source(md, (const char *)buf, got);
            free(buf);
            return r;
        }
        /* Fall through to the fallback when the file slurp failed —
         * prefer rendering *something* over leaving the tab blank. */
    }
    return yetty_ygui_ymarkdown_set_source(md, YREADME_FALLBACK_MD,
                                           sizeof(YREADME_FALLBACK_MD) - 1);
}

/* Tiny self-contained HTML page exercising headings, lists, links,
 * inline styles, and a code block. Lives inline (no incbin) — the
 * point of the tab is to show ybrowser working, not to ship a real
 * website. */
static const char YBROWSER_SAMPLE_HTML[] =
    "<html>"
    "<body style=\"font-family: sans-serif; margin: 24px; color: #E4E5E0;\">"
    "<h1 style=\"color: #92A86B;\">yetty &mdash; YBrowser tab</h1>"
    "<p>Rendered by the <code>ybrowser</code> widget on top of "
    "<a href=\"https://lexbor.com\">lexbor</a>.</p>"
    "<h2>Features</h2>"
    "<ul>"
    "<li>HTML 5 parser via lexbor</li>"
    "<li>CSS via libcss</li>"
    "<li>Pixels via yetty's MSDF font + SDF primitives</li>"
    "</ul>"
    "<h2>Code sample</h2>"
    "<pre style=\"background:#1F1A14;color:#E4E5E0;padding:8px;border-radius:4px;\">"
    "yetty_ygui_add(\n"
    "    yetty_ygui_ybrowser_class_get().value,\n"
    "    parent);"
    "</pre>"
    "</body></html>";

static struct yetty_ycore_void_result load_browser_entry(struct yetty_yclass_ctx *_yc_ctx,
                                                         struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *browser = (struct yetty_ygui_object *)_yc_obj;
    return yetty_ygui_ybrowser_set_html(browser, YBROWSER_SAMPLE_HTML,
                                        sizeof(YBROWSER_SAMPLE_HTML) - 1);
}

static struct yetty_ycore_void_result write_code_snippet(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                         const char *snippet_id)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *rich = (struct yetty_ygui_object *)_yc_obj;
    const struct code_snippet *snip = code_snippet_at(snippet_id);
    if (!snip) return YETTY_OK_VOID();
    for (size_t li = 0; li < snip->n_lines; ++li) {
        struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(rich);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "code: rich_add_line");
        for (size_t si = 0;
             si < sizeof(snip->lines[li].spans) / sizeof(snip->lines[li].spans[0]); ++si) {
            const char *t = snip->lines[li].spans[si].text;
            if (!t) break;
            if (t[0] == '\0') continue;
            yetty_ycore_error_destroy_safe(yetty_ygui_rich_add_span(
                rich, t, 13.0f, snip->lines[li].spans[si].color));
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result write_welcome_spans(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                          const struct rich_span *spans,
                                                          size_t n_spans)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *rich = (struct yetty_ygui_object *)_yc_obj;
    for (size_t i = 0; i < n_spans; ++i) {
        if (spans[i].new_line_first) {
            struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(rich);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "welcome: rich_add_line");
        }
        if (spans[i].text[0]) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_rich_add_span(rich, spans[i].text, spans[i].fs, spans[i].color));
        }
    }
    return YETTY_OK_VOID();
}

/* Forward-decl click handler — used in build_tab_body, defined later. */
struct row_link {
    struct app *app;
    int tab;
    int entry;
};

static struct yetty_ycore_void_result on_row_clicked(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *btn,
                                                     void *userdata);

/*-----------------------------------------------------------------------------
 * build_tab_body — for one tab index, lay out hbox(nav, content) inside
 * `parent`. The nav is a vbox of buttons; the content widget's class is
 * dictated by `kind`.
 *---------------------------------------------------------------------------*/

struct row_link_arena {
    /* Per-tab arena of row_links so the click callbacks have a stable
     * back-pointer. The arena lives on the heap; freed at app teardown. */
    struct row_link *items;
    int count;
    int cap;
};

static struct row_link_arena g_row_links;

static struct row_link *new_row_link(struct app *app, int tab, int entry)
{
    if (g_row_links.count == g_row_links.cap) {
        int ncap = g_row_links.cap ? g_row_links.cap * 2 : 16;
        struct row_link *na = realloc(g_row_links.items, (size_t)ncap * sizeof(*na));
        if (!na) return NULL;
        g_row_links.items = na;
        g_row_links.cap = ncap;
    }
    struct row_link *rl = &g_row_links.items[g_row_links.count++];
    rl->app = app;
    rl->tab = tab;
    rl->entry = entry;
    return rl;
}

static int tab_entry_count(const struct app *app, int tab_index)
{
    switch (tab_index) {
    case 0: return (int)(sizeof(welcome_nav_entries) / sizeof(welcome_nav_entries[0]));
    case 1: return (int)(sizeof(plot_nav_entries) / sizeof(plot_nav_entries[0]));
    case 2: return app->image_path_count > 0 ? app->image_path_count : 1;
    case 3: return (int)(sizeof(code_nav_entries) / sizeof(code_nav_entries[0]));
    case 4: return app->video_path_count > 0 ? app->video_path_count : 1;
    case 5: /* Elements — chrome-only, the single nav row is a label hook. */
    case 6: /* YReadme  — single piece of content (README.md). */
    case 7: /* YBrowser — single inline HTML sample. */
        return 1;
    default: return 0;
    }
}

static const char *tab_entry_label(const struct app *app, int tab_index, int entry_index)
{
    switch (tab_index) {
    case 0: return welcome_nav_entries[entry_index].label;
    case 1: return plot_nav_entries[entry_index].label;
    case 2: {
        if (app->image_path_count <= 0) return "(no images found)";
        static char buf[64];
        snprintf(buf, sizeof(buf), "logo-%d", entry_index + 1);
        return buf;
    }
    case 3: return code_nav_entries[entry_index].label;
    case 4: {
        if (app->video_path_count <= 0) return "(no videos found)";
        static char buf[64];
        snprintf(buf, sizeof(buf), "clip-%d", entry_index + 1);
        return buf;
    }
    case 5: return "Showcase";
    case 6: return app->readme_path ? "README.md" : "(no README)";
    case 7: return "Sample";
    default: return "";
    }
}

static enum tab_kind tab_kind_for(int tab_index)
{
    switch (tab_index) {
    case 1: return TAB_KIND_PLOTS;
    case 2: return TAB_KIND_IMAGES;
    case 4: return TAB_KIND_VIDEO;
    case 5: return TAB_KIND_ELEMENTS;
    case 6: return TAB_KIND_YREADME;
    case 7: return TAB_KIND_YBROWSER;
    case 0:
    case 3:
    default:
        return TAB_KIND_RICH;
    }
}

/* Fill the Elements scrollarea with one collapsing-header per widget
 * family. The section list and the per-section widget mix mirror the
 * main-branch ygreeter's Elements tab exactly: Inputs, Selectors,
 * Display, Plot, Image, Lists & Trees, Layout & Containers, Overlays.
 * Each section is a collapsing_header that hosts a vbox of widgets;
 * the parent scrollarea wheel-scrolls the whole surface when sections
 * overflow.
 *
 * Per-widget calls swallow the Result error so a single misconfigured
 * widget can't take the entire tab build down. */
static struct yetty_ycore_void_result build_elements_content(struct app *app,
                                                              struct yetty_ygui_object *root)
{
    (void)app;

#define EL_OK(x) yetty_ycore_error_destroy_safe(x)
#define EL_ADD(cls, parent_obj, out_var)                                                           \
    struct yetty_ygui_object_ptr_result out_var##_r =                                              \
        yetty_ygui_add((cls), (parent_obj));                                                       \
    struct yetty_ygui_object *out_var = YETTY_IS_OK(out_var##_r) ? out_var##_r.value : NULL;       \
    if (YETTY_IS_ERR(out_var##_r)) yetty_ycore_error_destroy(out_var##_r.error)
#define EL_HEADER(title, parent_obj, out)                                                          \
    do {                                                                                           \
        struct yetty_ygui_object_ptr_result _hr_ =                                                 \
            yetty_ygui_add(yetty_ygui_collapsing_header_class_get().value, (parent_obj));          \
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _hr_, "elements: header " title);                    \
        EL_OK(yetty_ygui_collapsing_header_set_title(_hr_.value, title));                          \
        EL_OK(yetty_ygui_collapsing_header_set_open(_hr_.value, 0));                               \
        struct yetty_ygui_layout _l_ = *yetty_ygui_widget_layout_get(_hr_.value);                  \
        _l_.gap = 6.0f;                                                                            \
        _l_.padding_top = _l_.padding_bottom = 4.0f;                                               \
        EL_OK(yetty_ygui_widget_layout_set(_hr_.value, &_l_));                                     \
        (out) = _hr_.value;                                                                        \
    } while (0)

    struct yetty_ygui_object *sec = NULL;

    /* ---- Inputs ---- */
    EL_HEADER("Inputs", root, sec);
    {
        EL_ADD(yetty_ygui_button_class_get().value, sec, btn);
        if (btn) EL_OK(yetty_ygui_button_set_label(btn, "Button"));

        EL_ADD(yetty_ygui_textinput_class_get().value, sec, ti);
        if (ti) EL_OK(yetty_ygui_textinput_set_placeholder(ti, "type here…"));

        EL_ADD(yetty_ygui_slider_class_get().value, sec, sl);
        if (sl) {
            EL_OK(yetty_ygui_slider_set_range(sl, 0.0f, 1.0f));
            EL_OK(yetty_ygui_slider_set_value(sl, 0.4f));
        }

        EL_ADD(yetty_ygui_spinner_class_get().value, sec, spin_i);
        if (spin_i) {
            EL_OK(yetty_ygui_spinner_set_range(spin_i, 1.0f, 100.0f, 1.0f));
            EL_OK(yetty_ygui_spinner_set_value(spin_i, 42.0f));
        }

        EL_ADD(yetty_ygui_spinner_class_get().value, sec, spin_f);
        if (spin_f) {
            EL_OK(yetty_ygui_spinner_set_range(spin_f, 0.0f, 10.0f, 0.25f));
            EL_OK(yetty_ygui_spinner_set_value(spin_f, 2.5f));
        }

        EL_ADD(yetty_ygui_checkbox_class_get().value, sec, cb);
        if (cb) {
            EL_OK(yetty_ygui_checkbox_set_label(cb, "Enabled"));
            EL_OK(yetty_ygui_checkbox_set_checked(cb, 1));
        }

        EL_ADD(yetty_ygui_toggle_class_get().value, sec, tg);
        if (tg) {
            EL_OK(yetty_ygui_toggle_set_label(tg, "Notifications"));
            EL_OK(yetty_ygui_toggle_set_on(tg, 1));
        }

        EL_ADD(yetty_ygui_progress_class_get().value, sec, pg);
        if (pg) EL_OK(yetty_ygui_progress_set_value(pg, 0.65f));

        EL_ADD(yetty_ygui_textarea_class_get().value, sec, ta);
        if (ta)
            EL_OK(yetty_ygui_textarea_set_text(
                ta, "Multi-line text area.\nClick to focus, then type.\n"));
    }

    /* ---- Selectors ---- */
    EL_HEADER("Selectors", root, sec);
    {
        EL_ADD(yetty_ygui_radio_class_get().value, sec, r0);
        if (r0) {
            EL_OK(yetty_ygui_radio_set_label(r0, "Apple"));
            EL_OK(yetty_ygui_radio_set_selected(r0, 1));
        }
        EL_ADD(yetty_ygui_radio_class_get().value, sec, r1);
        if (r1) EL_OK(yetty_ygui_radio_set_label(r1, "Banana"));
        EL_ADD(yetty_ygui_radio_class_get().value, sec, r2);
        if (r2) EL_OK(yetty_ygui_radio_set_label(r2, "Cherry"));

        EL_ADD(yetty_ygui_dropdown_class_get().value, sec, dd);
        if (dd) {
            EL_OK(yetty_ygui_dropdown_add_option(dd, "Option A"));
            EL_OK(yetty_ygui_dropdown_add_option(dd, "Option B"));
            EL_OK(yetty_ygui_dropdown_add_option(dd, "Option C"));
            EL_OK(yetty_ygui_dropdown_set_selected(dd, 0));
        }

        EL_ADD(yetty_ygui_combobox_class_get().value, sec, cmb);
        if (cmb) {
            EL_OK(yetty_ygui_combobox_set_text(cmb, "red"));
            EL_OK(yetty_ygui_combobox_add_suggestion(cmb, "red"));
            EL_OK(yetty_ygui_combobox_add_suggestion(cmb, "green"));
            EL_OK(yetty_ygui_combobox_add_suggestion(cmb, "blue"));
            EL_OK(yetty_ygui_combobox_add_suggestion(cmb, "magenta"));
        }

        EL_ADD(yetty_ygui_choicebox_class_get().value, sec, ch);
        if (ch) {
            EL_OK(yetty_ygui_choicebox_add(ch, "Small"));
            EL_OK(yetty_ygui_choicebox_add(ch, "Medium"));
            EL_OK(yetty_ygui_choicebox_add(ch, "Large"));
            EL_OK(yetty_ygui_choicebox_add(ch, "Huge"));
        }

        EL_ADD(yetty_ygui_colorpicker_class_get().value, sec, cp);
        if (cp) EL_OK(yetty_ygui_colorpicker_set_color(cp, 0xFF6BA892));
    }

    /* ---- Display ---- */
    EL_HEADER("Display", root, sec);
    {
        EL_ADD(yetty_ygui_label_class_get().value, sec, lb);
        if (lb) EL_OK(yetty_ygui_label_set_text(lb, "Plain label"));

        EL_ADD(yetty_ygui_separator_class_get().value, sec, sp);
        (void)sp;

        EL_ADD(yetty_ygui_progress_class_get().value, sec, pg);
        if (pg) EL_OK(yetty_ygui_progress_set_value(pg, 0.25f));

        EL_ADD(yetty_ygui_table_class_get().value, sec, tbl);
        if (tbl) {
            static const char *cols[] = {"PID", "USER", "%CPU", "COMMAND"};
            EL_OK(yetty_ygui_table_set_columns(tbl, 4, cols));
            static const char *row1[] = {"1", "root", "0.0", "/sbin/init"};
            static const char *row2[] = {"42", "misi", "1.3", "/usr/bin/yetty"};
            static const char *row3[] = {"1337", "misi", "0.2", "/usr/bin/ygreeter"};
            EL_OK(yetty_ygui_table_add_row(tbl, row1, 4));
            EL_OK(yetty_ygui_table_add_row(tbl, row2, 4));
            EL_OK(yetty_ygui_table_add_row(tbl, row3, 4));
        }

        EL_ADD(yetty_ygui_breadcrumbs_class_get().value, sec, bc);
        if (bc) {
            EL_OK(yetty_ygui_breadcrumbs_add(bc, "Home"));
            EL_OK(yetty_ygui_breadcrumbs_add(bc, "Projects"));
            EL_OK(yetty_ygui_breadcrumbs_add(bc, "yetty"));
            EL_OK(yetty_ygui_breadcrumbs_add(bc, "ygui"));
        }

        EL_ADD(yetty_ygui_hbox_class_get().value, sec, chip_row);
        if (chip_row) {
            struct yetty_ygui_layout chl = *yetty_ygui_widget_layout_get(chip_row);
            chl.gap = 6.0f;
            EL_OK(yetty_ygui_widget_layout_set(chip_row, &chl));
            EL_ADD(yetty_ygui_chip_class_get().value, chip_row, c1);
            if (c1) {
                EL_OK(yetty_ygui_chip_set_label(c1, "linux"));
                EL_OK(yetty_ygui_chip_set_closable(c1, 1));
            }
            EL_ADD(yetty_ygui_chip_class_get().value, chip_row, c2);
            if (c2) {
                EL_OK(yetty_ygui_chip_set_label(c2, "gpu"));
                EL_OK(yetty_ygui_chip_set_closable(c2, 1));
            }
            EL_ADD(yetty_ygui_chip_class_get().value, chip_row, c3);
            if (c3) {
                EL_OK(yetty_ygui_chip_set_label(c3, "rust-free"));
                EL_OK(yetty_ygui_chip_set_closable(c3, 0));
            }
        }

        EL_ADD(yetty_ygui_stepper_class_get().value, sec, stp);
        if (stp) {
            EL_OK(yetty_ygui_stepper_add_step(stp, "Setup"));
            EL_OK(yetty_ygui_stepper_add_step(stp, "Install"));
            EL_OK(yetty_ygui_stepper_add_step(stp, "Done"));
            EL_OK(yetty_ygui_stepper_set_current(stp, 1));
        }
    }

    /* ---- Plot ---- */
    EL_HEADER("Plot", root, sec);
    {
        EL_ADD(yetty_ygui_yplot_class_get().value, sec, pl);
        if (pl) {
            struct yetty_ygui_yplot_config cfg = {
                .x_min = -6.2832f, .x_max = 6.2832f,
                .y_min = -1.5f,    .y_max = 1.5f,
                .flags = 0,
            };
            EL_OK(yetty_ygui_yplot_set_config(pl, &cfg));
            EL_OK(yetty_ygui_yplot_set_source(
                pl, "f=sin(x+t); g=cos(x+t); @f.color=#ff6b6b; @g.color=#4ecdc4"));
            struct yetty_ygui_layout pll = *yetty_ygui_widget_layout_get(pl);
            pll.width = 460.0f;
            pll.height = 200.0f;
            EL_OK(yetty_ygui_widget_layout_set(pl, &pll));
        }
    }

    /* ---- Image ---- */
    EL_HEADER("Image", root, sec);
    {
        const char *path = (app && app->image_path_count > 0 && app->image_paths)
                               ? app->image_paths[0]
                               : NULL;
        if (path) {
            EL_ADD(yetty_ygui_yimage_class_get().value, sec, img);
            if (img) {
                size_t got = 0;
                uint8_t *buf = slurp_file(path, &got);
                if (buf) {
                    EL_OK(yetty_ygui_yimage_set_bytes(img, buf, got));
                    free(buf);
                }
                struct yetty_ygui_layout il = *yetty_ygui_widget_layout_get(img);
                il.width = 320.0f;
                il.height = 320.0f;
                EL_OK(yetty_ygui_widget_layout_set(img, &il));
            }
        } else {
            EL_ADD(yetty_ygui_label_class_get().value, sec, ph);
            if (ph) EL_OK(yetty_ygui_label_set_text(ph, "(no logo image available)"));
        }
    }

    /* ---- Lists & Trees ---- */
    EL_HEADER("Lists & Trees", root, sec);
    {
        EL_ADD(yetty_ygui_list_class_get().value, sec, lst);
        if (lst) {
            EL_OK(yetty_ygui_list_add(lst, "Apple"));
            EL_OK(yetty_ygui_list_add(lst, "Banana"));
            EL_OK(yetty_ygui_list_add(lst, "Cherry"));
            EL_OK(yetty_ygui_list_add(lst, "Date"));
            EL_OK(yetty_ygui_list_set_selected(lst, 0));
        }

        EL_ADD(yetty_ygui_tree_node_class_get().value, sec, tn);
        if (tn) {
            EL_OK(yetty_ygui_tree_node_set_label(tn, "Tree root"));
            EL_OK(yetty_ygui_tree_node_set_open(tn, 1));
            EL_ADD(yetty_ygui_label_class_get().value, tn, k1);
            if (k1) EL_OK(yetty_ygui_label_set_text(k1, "  child 1"));
            EL_ADD(yetty_ygui_label_class_get().value, tn, k2);
            if (k2) EL_OK(yetty_ygui_label_set_text(k2, "  child 2"));
        }

        EL_ADD(yetty_ygui_selectable_class_get().value, sec, sl);
        if (sl) {
            EL_OK(yetty_ygui_selectable_set_text(sl, "Selectable row"));
            EL_OK(yetty_ygui_selectable_set_selected(sl, 0));
        }
    }

    /* ---- Layout & Containers ---- */
    EL_HEADER("Layout & Containers", root, sec);
    {
        EL_ADD(yetty_ygui_hbox_class_get().value, sec, split_row);
        if (split_row) {
            struct yetty_ygui_layout srl = *yetty_ygui_widget_layout_get(split_row);
            srl.height = 80.0f;
            EL_OK(yetty_ygui_widget_layout_set(split_row, &srl));

            EL_ADD(yetty_ygui_panel_class_get().value, split_row, lp);
            if (lp) {
                struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(lp);
                l.width = 220.0f;
                l.flex_grow = 0.0f;
                EL_OK(yetty_ygui_widget_layout_set(lp, &l));
                EL_OK(yetty_ygui_panel_set_bg(lp, (struct yetty_ycore_rgba){30, 38, 44, 255}));
            }
            EL_ADD(yetty_ygui_splitter_class_get().value, split_row, div);
            if (div) {
                struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(div);
                l.width = 6.0f;
                EL_OK(yetty_ygui_widget_layout_set(div, &l));
            }
            EL_ADD(yetty_ygui_panel_class_get().value, split_row, rp);
            if (rp) {
                struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(rp);
                l.flex_grow = 1.0f;
                EL_OK(yetty_ygui_widget_layout_set(rp, &l));
                EL_OK(yetty_ygui_panel_set_bg(rp, (struct yetty_ycore_rgba){20, 26, 31, 255}));
            }
        }

        EL_ADD(yetty_ygui_statusbar_class_get().value, sec, sb);
        (void)sb;
    }

    /* ---- Overlays ---- */
    EL_HEADER("Overlays", root, sec);
    {
        EL_ADD(yetty_ygui_tooltip_class_get().value, sec, tp);
        if (tp) EL_OK(yetty_ygui_tooltip_set_text(tp, "Tooltip example"));

        EL_ADD(yetty_ygui_selectable_class_get().value, sec, srow);
        if (srow) EL_OK(yetty_ygui_selectable_set_text(srow, "Selectable row"));

        EL_ADD(yetty_ygui_button_class_get().value, sec, open_dlg);
        if (open_dlg) EL_OK(yetty_ygui_button_set_label(open_dlg, "Open dialog…"));

        EL_ADD(yetty_ygui_button_class_get().value, sec, open_pop);
        if (open_pop) EL_OK(yetty_ygui_button_set_label(open_pop, "Open popup…"));

        EL_ADD(yetty_ygui_button_class_get().value, sec, open_menu);
        if (open_menu) EL_OK(yetty_ygui_button_set_label(open_menu, "Open menu…"));

        EL_ADD(yetty_ygui_popup_menu_class_get().value, sec, pmenu);
        if (pmenu) {
            EL_OK(yetty_ygui_popup_menu_add_item(pmenu, "First action", NULL, NULL));
            EL_OK(yetty_ygui_popup_menu_add_item(pmenu, "Second action", NULL, NULL));
            EL_OK(yetty_ygui_popup_menu_add_separator(pmenu));
            EL_OK(yetty_ygui_popup_menu_add_item(pmenu, "Third action", NULL, NULL));
        }
    }

#undef EL_HEADER
#undef EL_ADD
#undef EL_OK
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_tab_content(struct app *app, int tab_index)
{
    /* Wipe + reseed app->body_panel: nav on left + fresh content widget on right. */
    while (1) {
        struct yetty_ygui_object *c = yetty_ygui_object_first_child(app->body_panel);
        if (!c) break;
        yetty_ycore_error_destroy_safe(yetty_ygui_del(c));
    }

    struct tab_state *t = &app->tabs[tab_index];
    t->kind = tab_kind_for(tab_index);
    int n = tab_entry_count(app, tab_index);
    t->n_entries = n;
    if (t->active_entry < 0 || t->active_entry >= n) t->active_entry = 0;

    /* Tabs whose body is a single self-contained content widget skip
     * the nav vbox entirely — listing a single "Showcase" / "clip-1"
     * button on its own column is just visual noise. The content
     * widget then fills the whole body. */
    bool has_nav;
    switch (t->kind) {
    case TAB_KIND_VIDEO:
    case TAB_KIND_ELEMENTS:
    case TAB_KIND_YREADME:
    case TAB_KIND_YBROWSER:
        has_nav = false;
        break;
    default:
        has_nav = true;
        break;
    }

    /* Outer hbox: nav + content side-by-side (or just content). */
    struct yetty_ygui_object_ptr_result hr =
        yetty_ygui_add(yetty_ygui_hbox_class_get().value, app->body_panel);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "rebuild: hbox");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(hr.value);
        l.flex_grow = 1.0f;
        l.gap = has_nav ? 12.0f : 0.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(hr.value, &l));
    }

    if (has_nav) {
        /* Nav vbox — fixed 220-px wide column of clickable rows. */
        struct yetty_ygui_object_ptr_result nr =
            yetty_ygui_add(yetty_ygui_vbox_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, nr, "rebuild: nav vbox");
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(nr.value);
            l.width = 220.0f;
            l.gap = 4.0f;
            l.padding_top = l.padding_bottom = 4.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(nr.value, &l));
        }
        for (int i = 0; i < n; ++i) {
            struct yetty_ygui_object_ptr_result br =
                yetty_ygui_add(yetty_ygui_button_class_get().value, nr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "rebuild: nav button");
            yetty_ycore_error_destroy_safe(
                yetty_ygui_button_set_label(br.value, tab_entry_label(app, tab_index, i)));
            {
                struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(br.value);
                l.height = 28.0f;
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(br.value, &l));
            }
            struct row_link *rl = new_row_link(app, tab_index, i);
            if (rl) {
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_clickable_on_click_set(br.value, on_row_clicked, rl));
            }
        }
    }

    /* Content widget — class chosen by tab kind. */
    struct yetty_ygui_object *content = NULL;
    switch (t->kind) {
    case TAB_KIND_PLOTS: {
        struct yetty_ygui_object_ptr_result pr =
            yetty_ygui_add(yetty_ygui_yplot_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rebuild: yplot");
        content = pr.value;
        break;
    }
    case TAB_KIND_IMAGES: {
        struct yetty_ygui_object_ptr_result ir =
            yetty_ygui_add(yetty_ygui_yimage_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "rebuild: yimage");
        content = ir.value;
        break;
    }
    case TAB_KIND_VIDEO: {
        struct yetty_ygui_object_ptr_result vr =
            yetty_ygui_add(yetty_ygui_yvideo_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "rebuild: yvideo");
        content = vr.value;
        break;
    }
    case TAB_KIND_ELEMENTS: {
        struct yetty_ygui_object_ptr_result sr =
            yetty_ygui_add(yetty_ygui_scrollarea_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "rebuild: scrollarea");
        content = sr.value;
        break;
    }
    case TAB_KIND_YREADME: {
        struct yetty_ygui_object_ptr_result mr =
            yetty_ygui_add(yetty_ygui_ymarkdown_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "rebuild: ymarkdown");
        content = mr.value;
        break;
    }
    case TAB_KIND_YBROWSER: {
        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_ybrowser_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "rebuild: ybrowser");
        content = br.value;
        break;
    }
    case TAB_KIND_RICH:
    default: {
        struct yetty_ygui_object_ptr_result rr =
            yetty_ygui_add(yetty_ygui_rich_class_get().value, hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild: rich");
        content = rr.value;
        break;
    }
    }
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(content);
        l.flex_grow = 1.0f;
        l.min_height = 200.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(content, &l));
    }
    t->content = content;

    /* Seed the content with the active entry. */
    switch (t->kind) {
    case TAB_KIND_PLOTS: {
        const struct nav_entry *e = &plot_nav_entries[t->active_entry];
        yetty_ycore_error_destroy_safe(
            load_plot_entry(NULL, (struct yetty_yclass_object *)content, e));
        break;
    }
    case TAB_KIND_IMAGES: {
        const char *path = app->image_path_count > 0 && t->active_entry < app->image_path_count
                               ? app->image_paths[t->active_entry]
                               : NULL;
        yetty_ycore_error_destroy_safe(
            load_image_entry(NULL, (struct yetty_yclass_object *)content, path));
        break;
    }
    case TAB_KIND_VIDEO: {
        const char *path = app->video_path_count > 0 && t->active_entry < app->video_path_count
                               ? app->video_paths[t->active_entry]
                               : NULL;
        yetty_ycore_error_destroy_safe(
            load_video_entry(NULL, (struct yetty_yclass_object *)content, path));
        break;
    }
    case TAB_KIND_ELEMENTS:
        yetty_ycore_error_destroy_safe(build_elements_content(app, content));
        break;
    case TAB_KIND_YREADME:
        yetty_ycore_error_destroy_safe(
            load_readme_entry(NULL, (struct yetty_yclass_object *)content, app->readme_path));
        break;
    case TAB_KIND_YBROWSER:
        yetty_ycore_error_destroy_safe(
            load_browser_entry(NULL, (struct yetty_yclass_object *)content));
        break;
    case TAB_KIND_RICH:
    default: {
        if (tab_index == 0) {
            const struct welcome_nav *e = &welcome_nav_entries[t->active_entry];
            yetty_ycore_error_destroy_safe(write_welcome_spans(
                NULL, (struct yetty_yclass_object *)content, e->spans, e->n_spans));
        } else {
            yetty_ycore_error_destroy_safe(write_code_snippet(
                NULL, (struct yetty_yclass_object *)content,
                code_nav_entries[t->active_entry].payload));
        }
        break;
    }
    }

    yetty_ygui_framework_mark_dirty(app->engine);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_row_clicked(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj, void *userdata)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *btn = (struct yetty_ygui_object *)_yc_obj;
    (void)btn;
    struct row_link *rl = (struct row_link *)userdata;
    if (!rl) return YETTY_OK_VOID();
    if (rl->tab != yetty_ygui_tabbar_active(rl->app->tabbar)) {
        return YETTY_OK_VOID();
    }
    rl->app->tabs[rl->tab].active_entry = rl->entry;
    return rebuild_tab_content(rl->app, rl->tab);
}

static struct yetty_ycore_void_result on_tab_change(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *target = (struct yetty_ygui_object *)_yc_obj;
    (void)target;
    int idx = event->i0;
    if (idx < 0 || idx >= TAB_COUNT) return YETTY_OK_VOID();
    return rebuild_tab_content((struct app *)userdata, idx);
}

static struct yetty_ycore_void_result build_ui(struct app *app)
{
    /* yinit already ran ygreeter_extract_assets_cb early in startup, so
     * <data_dir>/logo-*.jpeg, yetty-unchained-2.mp4 and README.md are
     * on disk by the time we get here. The three discover_* probes
     * record absolute paths into the app struct for later use by the
     * Images / Video / YReadme tabs. */
    discover_logo_images(app);
    discover_video_files(app);
    discover_readme(app);

    struct yetty_ygui_object_ptr_result rr = yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "build_ui: root add");
    app->root = rr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->root);
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        l.gap = 0.0f;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->root, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: root layout");
    }
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->engine, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build_ui: set_root");

    /* Tabbar — Welcome / Plots / Images / Code. */
    struct yetty_ygui_object_ptr_result tbr =
        yetty_ygui_add(yetty_ygui_tabbar_class_get().value, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tbr, "build_ui: tabbar add");
    app->tabbar = tbr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->tabbar);
        l.height = 36;
        l.gap = 4;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->tabbar, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: tabbar layout");
    }
    for (int i = 0; i < TAB_COUNT; ++i) {
        struct yetty_ygui_object_ptr_result hr =
            yetty_ygui_tabbar_add_tab(app->tabbar, TAB_LABELS[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "build_ui: tabbar_add_tab");
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(hr.value);
        l.width = 130;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(hr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: header layout");
    }
    struct yetty_ycore_void_result subr = yetty_ygui_object_subscribe(
        app->tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_change, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, subr, "build_ui: subscribe");

    /* Body panel — vbox that the per-tab content rebuilds populate. */
    struct yetty_ygui_object_ptr_result bpr =
        yetty_ygui_add(yetty_ygui_vbox_class_get().value, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bpr, "build_ui: body panel add");
    app->body_panel = bpr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->body_panel);
        l.flex_grow = 1;
        l.padding_top = 8;
        l.padding_left = l.padding_right = 8;
        l.padding_bottom = 4;
        l.gap = 0;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->body_panel, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: body panel layout");
    }

    /* Statusbar — small bottom strip. */
    struct yetty_ygui_object_ptr_result sbr =
        yetty_ygui_add(yetty_ygui_statusbar_class_get().value, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sbr, "build_ui: statusbar add");
    app->statusbar = sbr.value;
    yetty_ycore_error_destroy_safe(
        yetty_ygui_statusbar_set_left(app->statusbar, "ygreeter — q to quit"));
    yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_right(app->statusbar, "v0.4"));

    for (int i = 0; i < TAB_COUNT; ++i) {
        app->tabs[i].active_entry = 0;
        app->tabs[i].kind = tab_kind_for(i);
    }
    /* Optional launch-tab override for screenshot scripting: YGREETER_TAB=N
     * selects the initial tab without needing keyboard or mouse input
     * before the screenshot is captured. */
    int start_tab = 0;
    const char *env_tab = getenv("YGREETER_TAB");
    if (env_tab && *env_tab) {
        int v = atoi(env_tab);
        if (v >= 0 && v < TAB_COUNT) start_tab = v;
    }
    if (start_tab != 0) {
        yetty_ycore_error_destroy_safe(yetty_ygui_tabbar_set_active(app->tabbar, start_tab));
    }
    return rebuild_tab_content(app, start_tab);
}

/* Common key handler — looks the same regardless of mode. The caller's
 * mode-specific shutdown lives on the stop_cb hook below. */
struct key_ctx {
    struct app *app;
    void (*stop_cb)(struct app *app);
};

static int on_key(struct yetty_ygui_runtime *engine, uint32_t key, int mods, void *userdata)
{
    (void)engine;
    (void)mods;
    struct key_ctx *kc = (struct key_ctx *)userdata;
    struct app *app = kc->app;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        if (kc->stop_cb) kc->stop_cb(app);
        return 1;
    }
    if (key == YETTY_YGUI_KEY_ARROW_LEFT) {
        int active = yetty_ygui_tabbar_active(app->tabbar);
        int next = active > 0 ? active - 1 : TAB_COUNT - 1;
        struct yetty_ycore_void_result r = yetty_ygui_tabbar_set_active(app->tabbar, next);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        return 1;
    }
    if (key == YETTY_YGUI_KEY_ARROW_RIGHT) {
        int active = yetty_ygui_tabbar_active(app->tabbar);
        int next = (active + 1) % TAB_COUNT;
        struct yetty_ycore_void_result r = yetty_ygui_tabbar_set_active(app->tabbar, next);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        return 1;
    }
    return 0;
}

/*=============================================================================
 * CLIENT MODE — STDOUT-wrapping pty + stdin poll + SIGWINCH + libuv loop.
 *
 * This is the boilerplate that bridges a real terminal to ygui. Lives
 * in ygreeter (not in ygui) because it's deployment glue.
 *===========================================================================*/

#ifdef YETTY_YGUI_HAS_UV

#include <yetty/ytrace/ytrace.h>

struct stdout_pty {
    struct yetty_platform_pty base;
    uv_pipe_t pipe;
};

struct stdout_write {
    uv_write_t req;
    char *data;
};

static void on_write_done(uv_write_t *req, int status)
{
    struct stdout_write *w = (struct stdout_write *)req;
    if (status != 0) {
        yerror("ygreeter client: uv_write status=%d (%s)", status, uv_strerror(status));
    }
    free(w->data);
    free(w);
}

static struct yetty_ycore_size_result stdout_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct stdout_pty *p = (struct stdout_pty *)self;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct stdout_write *w = (struct stdout_write *)calloc(1, sizeof(*w));
    if (!w) {
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: calloc req");
    }
    w->data = (char *)malloc(len);
    if (!w->data) {
        free(w);
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: malloc data");
    }
    memcpy(w->data, data, len);
    uv_buf_t buf = uv_buf_init(w->data, (unsigned)len);
    int rc = uv_write(&w->req, (uv_stream_t *)&p->pipe, &buf, 1, on_write_done);
    if (rc != 0) {
        free(w->data);
        free(w);
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: uv_write submit");
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result stdout_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    (void)self;
    (void)buf;
    (void)max_len;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_void_result stdout_pty_no_op(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stdout_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows, uint32_t pw,
                                                        uint32_t ph)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *stdout_pty_pipe_source(struct yetty_platform_pty *s)
{
    (void)s;
    return NULL;
}

static const struct yetty_platform_pty_ops *stdout_pty_ops_get(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = stdout_pty_no_op,
        .read = stdout_pty_read,
        .write = stdout_pty_write,
        .resize = stdout_pty_resize,
        .stop = stdout_pty_no_op,
        .pipe_source = stdout_pty_pipe_source,
    };
    return &ops;
}

struct client_state {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    struct stdout_pty out;
    /* Async-delivery PTY shim for stdin — bytes arrive via the libuv
     * poll callback and get fed into the wire SM with _feed; the SM
     * itself never reads through this PTY. */
    struct yetty_platform_pty stdin_pty;
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct app *app;
    int running;
};

/*-----------------------------------------------------------------------------
 * STDIN raw-mode — when the host yetty writes OSC 700000 mouse envelopes
 * to the PTY master, the slave's tty driver in cooked/ECHO mode echoes
 * those bytes back to the master. yetty then re-reads them, sees ESC
 * pretty-printed as "^[" (caret notation), can't parse them as envelopes,
 * and renders the garbage as visible text. cfmakeraw turns ECHO off.
 *---------------------------------------------------------------------------*/
static struct termios ygreeter_saved_tty;
static int ygreeter_tty_fd = -1;       /* fd whose termios we mutated */
static int ygreeter_tty_owned_fd = -1; /* fd we opened ourselves (close on restore) */

static void ygreeter_tty_restore(void)
{
    if (ygreeter_tty_fd >= 0) {
        tcsetattr(ygreeter_tty_fd, TCSANOW, &ygreeter_saved_tty);
        ygreeter_tty_fd = -1;
    }
    if (ygreeter_tty_owned_fd >= 0) {
        close(ygreeter_tty_owned_fd);
        ygreeter_tty_owned_fd = -1;
    }
}

/* Disable ECHO+ICANON on the controlling terminal. Critical for any
 * yetty-hosted client: the host writes OSC mouse envelopes to the PTY
 * master, and a cooked-mode slave tty echoes the ESC bytes back as
 * "^[" (ECHOCTL caret notation). The echoed bytes loop into yetty's
 * own master read, and libvterm displays the OSC payload as visible
 * "^[]700000;;<base64>^[\" garbage at the prompt.
 *
 * STDIN may not be the controlling tty in every launch path (login(1)
 * variants, su, redirected stdin). Fall back to /dev/tty so the raw-
 * mode setup still applies even when stdin is a pipe/socket. */
static int ygreeter_tty_raw(void)
{
    int fd = STDIN_FILENO;
    if (!isatty(fd)) {
        fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (fd < 0) return -1;
        ygreeter_tty_owned_fd = fd;
    }
    if (tcgetattr(fd, &ygreeter_saved_tty) < 0) goto fail;
    struct termios raw = ygreeter_saved_tty;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) < 0) goto fail;
    ygreeter_tty_fd = fd;
    atexit(ygreeter_tty_restore);
    return 0;
fail:
    if (ygreeter_tty_owned_fd >= 0) {
        close(ygreeter_tty_owned_fd);
        ygreeter_tty_owned_fd = -1;
    }
    return -1;
}

/*-----------------------------------------------------------------------------
 * Stdin PTY shim — placeholder ops the wire SM holds. All async-delivery:
 * bytes arrive via yetty_ywire_wire_statemachine_feed() from libuv's
 * stdin poll. The SM never calls .read.
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_size_result stdin_pty_read(struct yetty_platform_pty *self, char *buf,
                                                     size_t n)
{
    (void)self;
    (void)buf;
    (void)n;
    return YETTY_OK(yetty_ycore_size, 0);
}
static struct yetty_ycore_size_result stdin_pty_write(struct yetty_platform_pty *self,
                                                      const char *data, size_t n)
{
    (void)self;
    (void)data;
    return YETTY_OK(yetty_ycore_size, n);
}
static struct yetty_ycore_void_result stdin_pty_noop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result stdin_pty_resize(struct yetty_platform_pty *self,
                                                       uint32_t cols, uint32_t rows, uint32_t pw,
                                                       uint32_t ph)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}
static struct yetty_platform_pty_pipe_source *stdin_pty_pipe_source(struct yetty_platform_pty *s)
{
    (void)s;
    return NULL;
}
static const struct yetty_platform_pty_ops *stdin_pty_ops_get(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = stdin_pty_noop,
        .read = stdin_pty_read,
        .write = stdin_pty_write,
        .resize = stdin_pty_resize,
        .stop = stdin_pty_noop,
        .pipe_source = stdin_pty_pipe_source,
    };
    return &ops;
}

/*-----------------------------------------------------------------------------
 * Wire SM default sink — bytes outside any OSC envelope are real
 * keyboard input from the controlling terminal. Forward them to ygui's
 * input decoder verbatim (it understands CSI arrow sequences + ASCII).
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_default_sink(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct client_state *cs = (struct client_state *)userdata;
    uint8_t buf[256];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, buf, sizeof(buf));
        if (YETTY_IS_ERR(rr)) return YETTY_ERR(yetty_ycore_void, "default_sink: read", rr);
        if (rr.value == 0) {
            /* SM expects this coro to loop forever — never return. */
            continue;
        }
        struct yetty_ycore_void_result fr =
            yetty_ygui_framework_feed_input(cs->app->engine, (const char *)buf, rr.value);
        if (YETTY_IS_ERR(fr)) yetty_ycore_error_destroy(fr.error);
    }
}

/*-----------------------------------------------------------------------------
 * Mouse handler — yetty forwards pointer events to the inferior as
 * OSC carrying a yetty_client_input_mouse. Drain the envelope body and
 * dispatch to ygui's framework_feed_mouse_* entry points (same path
 * the standalone mode uses for synthetic events).
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_mouse_handler(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    /* userdata is cs.app (distinct from set_default's &cs — the SM
     * dedupes handler coros by userdata pointer, so reusing &cs would
     * make both registrations share a single coro running whichever fn
     * was registered first). */
    struct app *app = (struct app *)userdata;
    for (;;) {
        struct yetty_client_input_mouse msg;
        size_t got = 0;
        while (got < sizeof(msg)) {
            struct yetty_ycore_size_result rr = yetty_ywire_wire_statemachine_read(
                sm, ((uint8_t *)&msg) + got, sizeof(msg) - got);
            if (YETTY_IS_ERR(rr)) return YETTY_ERR(yetty_ycore_void, "mouse: read", rr);
            if (rr.value == 0) {
                /* Envelope ended — _read returns 0 immediately without
                 * yielding when terminator_seen + empty. Break out. */
                break;
            }
            got += rr.value;
        }
        if (got == sizeof(msg) && msg.magic == YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            switch (msg.kind) {
            case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
                struct yetty_ycore_void_result r = yetty_ygui_framework_feed_mouse_button(
                    app->engine, msg.x, msg.y, msg.button, msg.pressed, 0);
                if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
                break;
            }
            case YETTY_YMGUI_INPUT_MOUSE_POS: {
                struct yetty_ycore_void_result r =
                    yetty_ygui_framework_feed_mouse_motion(app->engine, msg.x, msg.y);
                if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
                break;
            }
            default:
                break;
            }
        }
        /* Yield AFTER each envelope (or partial-read failure) so the SM
         * scanner can transition out of SCAN_OSC_BODY and dispatch the
         * next envelope. Without this, _read keeps returning 0 immediately
         * (terminator_seen + empty out_carry) and the handler coro
         * burns CPU without ever letting the scanner re-run. */
        yetty_yplatform_coro_yield();
    }
}

/*-----------------------------------------------------------------------------
 * Tell the hosting yetty to forward mouse events to our stdin. yetty
 * watches for DEC private modes 1500 (button) and 1501 (move) via the
 * text-layer's libvterm hook — when both are set, mouse events on the
 * card under the cursor get re-emitted as YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE.
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_enable_mouse_forwarding(struct client_state *cs)
{
    static const char enable[] = "\033[?1500h\033[?1501h";
    struct yetty_ycore_size_result wr =
        cs->out.base.ops->write(&cs->out.base, enable, sizeof(enable) - 1);
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "client_enable_mouse_forwarding: write", wr);
    }
    return YETTY_OK_VOID();
}

static void client_stop(struct app *app)
{
    if (app && app->client) app->client->running = 0;
}

static void client_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[1024];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        /* Push bytes into the wire SM. Envelopes (mouse / key / resize
         * forwarded by the hosting yetty) dispatch to their registered
         * handlers; non-envelope bytes (real keystrokes typed at the
         * tty) flow through the default sink to framework_feed_input. */
        if (cs->wire_sm) {
            yetty_ywire_wire_statemachine_feed(cs->wire_sm, buf, (size_t)n);
            struct yetty_ycore_void_result pr =
                yetty_ywire_wire_statemachine_process(cs->wire_sm);
            if (YETTY_IS_ERR(pr)) yetty_ycore_error_destroy(pr.error);
        } else {
            struct yetty_ycore_void_result r =
                yetty_ygui_framework_feed_input(cs->app->engine, buf, (size_t)n);
            if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        }
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        /* Real EOF only when stdin is not a tty. With raw mode VMIN=0
         * VTIME=0, read() returns 0 routinely when no data is available. */
        cs->running = 0;
    }
}

static void client_pickup_winsz(struct client_state *cs)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_set_viewport(
            cs->app->engine, (float)ws.ws_xpixel, (float)ws.ws_ypixel);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
    }
}

static void client_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    client_pickup_winsz((struct client_state *)handle->data);
}

static void client_prep_cb(uv_prepare_t *handle)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (yetty_ygui_framework_is_dirty(cs->app->engine)) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_emit(cs->app->engine);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
    }
    if (!cs->running) {
        uv_stop(handle->loop);
    }
}

static void client_close_cb(uv_handle_t *h)
{
    (void)h;
}

static int run_client_mode(void)
{
    struct client_state cs = {0};
    /* Raw mode BEFORE any write — otherwise the first OSC envelope we
     * emit gets echoed back by the slave tty driver and ends up rendered
     * as visible "^[" text in the host yetty's display. */
    ygreeter_tty_raw();
    if (uv_loop_init(&cs.loop) != 0) {
        fprintf(stderr, "ygreeter client: uv_loop_init failed\n");
        return 1;
    }
    cs.out.base.ops = stdout_pty_ops_get();
    if (uv_pipe_init(&cs.loop, &cs.out.pipe, 0) != 0 ||
        uv_pipe_open(&cs.out.pipe, STDOUT_FILENO) != 0) {
        fprintf(stderr, "ygreeter client: stdout pipe init failed\n");
        return 1;
    }

    struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(&cs.out.base);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_print(stderr, "ygreeter client: framework_create", fr.error);
        yetty_ycore_error_destroy(fr.error);
        return 1;
    }
    struct app app = {0};
    app.engine = fr.value;
    app.client = &cs;

    struct key_ctx kc = {.app = &app, .stop_cb = client_stop};
    yetty_ygui_framework_set_key_cb(app.engine, on_key, &kc);
    cs.app = &app;
    cs.running = 1;

    struct yetty_ycore_void_result br = build_ui(&app);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_print(stderr, "ygreeter client: build_ui", br.error);
        yetty_ycore_error_destroy(br.error);
        return 1;
    }

    /* Wire SM on stdin — parses OSC envelopes the hosting yetty pushes
     * us (mouse/key/resize), with a default sink for verbatim keystrokes. */
    cs.stdin_pty.ops = stdin_pty_ops_get();
    struct yetty_ywire_wire_statemachine_ptr_result smr =
        yetty_ywire_wire_statemachine_create(&cs.stdin_pty);
    if (YETTY_IS_ERR(smr)) {
        yetty_ycore_error_print(stderr, "ygreeter client: wire_sm_create", smr.error);
        yetty_ycore_error_destroy(smr.error);
        return 1;
    }
    cs.wire_sm = smr.value;
    {
        struct yetty_ycore_void_result rd = yetty_ywire_wire_statemachine_set_default(
            cs.wire_sm, client_default_sink, &cs);
        if (YETTY_IS_ERR(rd)) yetty_ycore_error_destroy(rd.error);
    }
    {
        /* Distinct userdata from set_default — see comment in
         * client_mouse_handler above. cs.app is a different pointer than
         * &cs, so get_or_spawn_handler_coro creates a separate coro. */
        struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register(
            cs.wire_sm, YETTY_YWIRE_ENVELOPE_OSC, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE,
            /*has_args=*/1, client_mouse_handler, cs.app);
        if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);
    }
    /* Tell the host yetty to forward pointer events to our stdin. */
    {
        struct yetty_ycore_void_result sr = client_enable_mouse_forwarding(&cs);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_print(stderr, "ygreeter client: input subscribe", sr.error);
            yetty_ycore_error_destroy(sr.error);
        }
    }

    client_pickup_winsz(&cs);
    if (uv_poll_init(&cs.loop, &cs.stdin_poll, STDIN_FILENO) == 0) {
        cs.stdin_poll.data = &cs;
        uv_poll_start(&cs.stdin_poll, UV_READABLE, client_stdin_cb);
    }
    if (uv_signal_init(&cs.loop, &cs.sigwinch) == 0) {
        cs.sigwinch.data = &cs;
        uv_signal_start(&cs.sigwinch, client_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&cs.loop, &cs.prep) == 0) {
        cs.prep.data = &cs;
        uv_prepare_start(&cs.prep, client_prep_cb);
    }

    uv_run(&cs.loop, UV_RUN_DEFAULT);

    uv_poll_stop(&cs.stdin_poll);
    uv_signal_stop(&cs.sigwinch);
    uv_prepare_stop(&cs.prep);
    uv_close((uv_handle_t *)&cs.stdin_poll, client_close_cb);
    uv_close((uv_handle_t *)&cs.sigwinch, client_close_cb);
    uv_close((uv_handle_t *)&cs.prep, client_close_cb);
    uv_close((uv_handle_t *)&cs.out.pipe, client_close_cb);
    uv_run(&cs.loop, UV_RUN_NOWAIT);

    if (cs.wire_sm) {
        struct yetty_ycore_void_result wd =
            yetty_ywire_wire_statemachine_destroy(cs.wire_sm);
        if (YETTY_IS_ERR(wd)) yetty_ycore_error_destroy(wd.error);
        cs.wire_sm = NULL;
    }
    struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(app.engine);
    if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
    uv_loop_close(&cs.loop);
    ygreeter_tty_restore();
    return 0;
}

#endif /* YETTY_YGUI_HAS_UV */

#ifdef YETTY_YGREETER_HAS_STANDALONE
/*=============================================================================
 * STANDALONE MODE — yinit_run + yframework + local container + wire SM +
 * KEY→bytes encoder.
 *
 * The ygui framework's output_pty is the producer end of a memory pty
 * pair. The consumer end feeds a wire_statemachine that calls
 * yetty_yfigure_container_process_input, materialising the figure tree
 * locally. Render renders that tree onto yframework's render_target.
 *===========================================================================*/

/* Map yetty's KEY_DOWN keycodes to the CSI escape sequences a terminal
 * would emit. Used by the standalone event handler to push input into
 * ygui's framework_feed_input. */
static const char *standalone_encode_key(uint32_t key, char *scratch, size_t scratch_n,
                                         size_t *out_len)
{
    /* GLFW-style keycodes. The handful we care about: */
    if (key >= 32 && key < 127) {
        scratch[0] = (char)key;
        *out_len = 1;
        return scratch;
    }
    switch (key) {
    case 256: /* ESC */
        scratch[0] = 0x1B;
        *out_len = 1;
        return scratch;
    case 257: /* Enter */
        scratch[0] = '\r';
        *out_len = 1;
        return scratch;
    case 259: /* Backspace */
        scratch[0] = 0x7F;
        *out_len = 1;
        return scratch;
    case 263: /* Left */
        *out_len = snprintf(scratch, scratch_n, "\x1b[D");
        return scratch;
    case 262: /* Right */
        *out_len = snprintf(scratch, scratch_n, "\x1b[C");
        return scratch;
    case 265: /* Up */
        *out_len = snprintf(scratch, scratch_n, "\x1b[A");
        return scratch;
    case 264: /* Down */
        *out_len = snprintf(scratch, scratch_n, "\x1b[B");
        return scratch;
    default:
        *out_len = 0;
        return NULL;
    }
}

static struct yetty_ycore_int_result standalone_event_handler(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *ev)
{
    struct app *app = container_of(listener, struct app, listener);

    if (ev->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (app->render_target && app->render_target->ops->refresh_full) {
            app->render_target->ops->refresh_full(app->render_target);
        }
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return standalone_event_handler(listener, &re);
    }

    if (ev->type == YETTY_YCORE_RENDER) {
        if (!app->render_target) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        if (app->render_target->ops->is_busy &&
            app->render_target->ops->is_busy(app->render_target)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Produce a new frame's OSC envelope into the mem-pty if dirty. */
        if (yetty_ygui_framework_is_dirty(app->engine)) {
            struct yetty_ycore_void_result er = yetty_ygui_framework_emit(app->engine);
            if (YETTY_IS_ERR(er)) {
                yetty_ycore_error_destroy(er.error);
            }
        }
        /* Drain consumer-side bytes through the wire SM → container. */
        if (app->wire_sm) {
            struct yetty_ycore_void_result pr =
                yetty_ywire_wire_statemachine_process(app->wire_sm);
            if (YETTY_IS_ERR(pr)) yetty_ycore_error_destroy(pr.error);
        }
        /* Clear + paint container + present. */
        struct yetty_ycore_void_result cl =
            app->render_target->ops->clear(app->render_target);
        if (YETTY_IS_ERR(cl)) yetty_ycore_error_destroy(cl.error);
        if (app->root_container) {
            struct yetty_yfigure_figure *rf =
                yetty_yfigure_container_as_figure(app->root_container);
            struct yetty_ycore_void_result rr = rf->ops->render(rf, app->render_target);
            if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);
            rf->dirty = 0;
        }
        struct yetty_ycore_void_result pp =
            app->render_target->ops->present(app->render_target);
        if (YETTY_IS_ERR(pp)) yetty_ycore_error_destroy(pp.error);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (app->yframework && app->yframework->event_loop &&
            app->yframework->event_loop->ops->stop) {
            app->yframework->event_loop->ops->stop(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE:
        yetty_yframework_reconfigure_surface(app->yframework, (uint32_t)ev->resize.width,
                                            (uint32_t)ev->resize.height);
        if (app->render_target && app->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            app->render_target->ops->resize(app->render_target, vp);
        }
        {
            struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
                app->engine, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(vr)) yetty_ycore_error_destroy(vr.error);
        }
        if (app->root_container) {
            struct yetty_ycore_rectangle root_rect = {
                .min = {0, 0}, .max = {(float)ev->resize.width, (float)ev->resize.height}};
            struct yetty_yfigure_figure *rf =
                yetty_yfigure_container_as_figure(app->root_container);
            rf->rect = root_rect;
            rf->dirty = 1;
        }
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_KEY_DOWN: {
        char scratch[8];
        size_t n = 0;
        const char *bytes =
            standalone_encode_key(ev->key.key, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            struct yetty_ycore_void_result r =
                yetty_ygui_framework_feed_input(app->engine, bytes, n);
            if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
            if (app->yframework->event_loop->ops->request_render) {
                app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_void_result r = yetty_ygui_framework_feed_mouse_button(
            app->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button,
            ev->type == YETTY_YCORE_MOUSE_DOWN ? 1 : 0, ev->mouse.mods);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_void_result r =
            yetty_ygui_framework_feed_mouse_motion(app->engine, ev->mouse.x, ev->mouse.y);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        /* Hover state may have flipped — request a render so the new
         * hovered widget repaints before the next event. */
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    default:
        break;
    }
    if (app->yframework->event_loop->ops->request_render) {
        app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

static void standalone_stop(struct app *app)
{
    if (app->yframework && app->yframework->event_loop &&
        app->yframework->event_loop->ops->stop) {
        app->yframework->event_loop->ops->stop(app->yframework->event_loop);
    }
}

static struct yetty_ycore_void_result standalone_worker(struct yetty_yinit_runtime *rt,
                                                        void *user)
{
    struct app *app = (struct app *)user;

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "standalone: yframework_create");
    app->yframework = frr.value;
    app->render_target = app->yframework->render_target;

    /* MSDF font for the receiver-side ygrid (glyph expansion). */
    {
        const char *fonts_dir = app->yframework->config->ops->get_string(
            app->yframework->config, "paths/fonts", "");
        const char *shaders_dir = app->yframework->config->ops->get_string(
            app->yframework->config, "paths/shaders", "");
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 "DejaVuSansMNerdFontMono");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "ygreeter_default");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "standalone: msdf_font_create");
        app->font = fr.value;
        struct yetty_ycore_void_result load = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load, "standalone: load_basic_latin");
    }

    /* Raw figure factory — needed for the yplot / yimage producer
     * kinds. Same wiring yui.c uses (yui_create lines 506-571). */
    {
        struct yetty_ydraw_raw_figure_factory_ptr_result ffr =
            yetty_ydraw_raw_figure_factory_create(
                app->yframework->gpu.device, app->yframework->gpu.queue,
                app->yframework->gpu.surface_format, app->yframework->gpu.allocator,
                app->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "standalone: raw_figure_factory_create");
        app->figure_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_raw_figure_factory_register(app->figure_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_raw_figure_factory_register(app->figure_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);
        }
    }

    /* Figure registry — primitive widgets land in ygrid; producer
     * widgets (yimage, yplot) get their own kind→factory binding. */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "standalone: registry_create");
        app->figure_registry = reg.value;
        app->figure_args.default_font = app->font;
        app->figure_args.figure_factory = app->figure_factory;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(app->figure_registry, &app->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "standalone: ygrid_register_factory");
        static const uint32_t producer_kinds[] = {
            YETTY_YFIGURE_KIND_YPLOT, YETTY_YFIGURE_KIND_YIMAGE, YETTY_YFIGURE_KIND_YVIDEO,
        };
        for (size_t i = 0; i < sizeof(producer_kinds) / sizeof(producer_kinds[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                app->figure_registry, producer_kinds[i], &app->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr,
                                "standalone: ygrid_register_factory_for_kind");
        }
    }

    /* Local container. */
    struct yetty_context ctx = {.runtime = app->yframework, .event_loop = app->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)rt->surface_width, (float)rt->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res =
            yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "standalone: container_create");
        app->root_container = yetty_yfigure_container_from(obj_res.value);
        yetty_yfigure_container_set_context(app->root_container, &ctx);
        yetty_yfigure_container_set_registry(app->root_container, app->figure_registry);
        yetty_yfigure_container_set_rect(app->root_container, root_rect);
    }

    /* Memory pty pair: producer.a = ygui output, consumer.b = wire SM. */
    {
        struct yetty_yplatform_memory_pty_pair_result pr =
            yetty_yplatform_memory_pty_pair_create(0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "standalone: memory_pty_pair_create");
        app->pty_pair = pr.value;
        app->has_pty_pair = 1;
    }

    /* Wire state machine over the consumer end. */
    {
        struct yetty_ywire_wire_statemachine_ptr_result sr =
            yetty_ywire_wire_statemachine_create(app->pty_pair.b);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "standalone: wire_sm_create");
        app->wire_sm = sr.value;
        struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register(
            app->wire_sm, YETTY_YWIRE_ENVELOPE_OSC, YETTY_OSC_YCOMPOSITOR_BIN, /*has_args=*/1,
            yetty_yfigure_container_process_input, app->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "standalone: wire_sm register");
    }

    /* ygui framework — producer end of the pty pair. */
    {
        struct yetty_ygui_framework_ptr_result fr =
            yetty_ygui_framework_create(app->pty_pair.a);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "standalone: framework_create");
        app->engine = fr.value;
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            app->engine, (float)rt->surface_width, (float)rt->surface_height);
        if (YETTY_IS_ERR(vr)) yetty_ycore_error_destroy(vr.error);
    }

    struct key_ctx kc = {.app = app, .stop_cb = standalone_stop};
    yetty_ygui_framework_set_key_cb(app->engine, on_key, &kc);

    struct yetty_ycore_void_result br = build_ui(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "standalone: build_ui");

    /* Wire memory-pty wake → request_render so producer writes drive the
     * event loop. Without this, ygui_framework_emit appends bytes to the
     * mem-pty but the consumer side never schedules a render. */
    yetty_yplatform_memory_pty_set_wake(
        app->pty_pair.b,
        (yetty_yplatform_memory_pty_wake_fn)app->yframework->event_loop->ops->request_render,
        app->yframework->event_loop);

    app->listener.handler = standalone_event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(app->yframework->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "standalone: register_default_listeners");

    /* Kick first frame. */
    yetty_yevent_post_async(rt->platform_input_pipe,
                            &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        app->yframework->event_loop->ops->start(app->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    if (app->engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(app->engine);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        app->engine = NULL;
    }
    if (app->wire_sm) {
        struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(app->wire_sm);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        app->wire_sm = NULL;
    }
    if (app->has_pty_pair) {
        if (app->pty_pair.a && app->pty_pair.a->ops->destroy) {
            struct yetty_ycore_void_result dr = app->pty_pair.a->ops->destroy(app->pty_pair.a);
            if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        }
        if (app->pty_pair.b && app->pty_pair.b->ops->destroy) {
            struct yetty_ycore_void_result dr = app->pty_pair.b->ops->destroy(app->pty_pair.b);
            if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        }
        app->has_pty_pair = 0;
    }
    if (app->root_container) {
        struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(app->root_container);
        struct yetty_ycore_void_result dr = rf->ops->destroy(rf);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        app->root_container = NULL;
    }
    if (app->figure_registry) {
        yetty_yfigure_registry_destroy(app->figure_registry);
        app->figure_registry = NULL;
    }
    if (app->figure_factory) {
        yetty_ydraw_raw_figure_factory_destroy(app->figure_factory);
        app->figure_factory = NULL;
    }
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    if (app->yframework) {
        yetty_yframework_destroy(app->yframework);
        app->yframework = NULL;
    }
    /* Discovered-paths arrays — allocated by discover_logo_images /
     * discover_video_files / discover_readme at build_ui time. */
    if (app->image_paths) {
        for (int i = 0; i < app->image_path_count; ++i) free(app->image_paths[i]);
        free(app->image_paths);
        app->image_paths = NULL;
        app->image_path_count = 0;
    }
    if (app->video_paths) {
        for (int i = 0; i < app->video_path_count; ++i) free(app->video_paths[i]);
        free(app->video_paths);
        app->video_paths = NULL;
        app->video_path_count = 0;
    }
    free(app->readme_path);
    app->readme_path = NULL;
    return YETTY_OK_VOID();
}

/* yinit-shaped wrapper for ygreeter's own incbin extractor (logos +
 * demo video). We don't reuse yetty's larger
 * `yetty_platform_extract_assets` here because that one drives off the
 * `yetty_data_manifest.h` generated for the main yetty binary, which
 * doesn't exist when only ygreeter is being built. */
static struct yetty_ycore_void_result ygreeter_extract_assets_cb(void)
{
    const char *data_dir = yetty_yplatform_get_data_dir();
    if (data_dir && ygreeter_embedded_assets_extract(data_dir) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ygreeter: embedded asset extraction failed");
    }
    return YETTY_OK_VOID();
}

static int run_standalone_mode(int argc, char **argv)
{
    struct app app = {0};
    struct yetty_yinit_app_config cfg = {.extract_assets_fn = ygreeter_extract_assets_cb};
    return yetty_yinit_run(argc, argv, &cfg, standalone_worker, &app);
}

#endif /* YETTY_YGREETER_HAS_STANDALONE */

/*=============================================================================
 * Dispatcher.
 *===========================================================================*/

static int in_yetty_terminal(void)
{
    const char *tp = getenv("TERM_PROGRAM");
    return tp && strcmp(tp, "yetty") == 0;
}

int main(int argc, char **argv)
{
    ytrace_init();
    if (in_yetty_terminal()) {
#ifdef YETTY_YGUI_HAS_UV
        return run_client_mode();
#else
        fprintf(stderr, "ygreeter: TERM_PROGRAM=yetty but built without libuv\n");
        return 1;
#endif
    }
#ifdef YETTY_YGREETER_HAS_STANDALONE
    return run_standalone_mode(argc, argv);
#else
    (void)argc;
    (void)argv;
    fprintf(stderr,
            "ygreeter: standalone mode unavailable — built without webgpu. "
            "Run inside a yetty terminal (set TERM_PROGRAM=yetty).\n");
    return 1;
#endif
}
