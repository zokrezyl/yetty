/*
 * ygreeter — first-contact tool. IN-TERMINAL ONLY.
 *
 * Runs exclusively inside a hosting yetty (TERM_PROGRAM=yetty): the hosting
 * terminal consumes our OSC envelopes shipped over stdout; stdin delivers
 * real keystrokes. No standalone window mode, no WebGPU — outside yetty the
 * tool prints an error and exits.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <poll.h>
#include <unistd.h>
#endif

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/api/yterminal/terminal.h>
#include <yetty/yclass/rpc.h>
#include "yetty/gen/impl/ycircuit/circuit.h"
#include "yetty/gen/impl/ymusic/music.h"
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yshadertoy/demo-shaders.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yplot/yplot.h>

#ifdef YETTY_YGREETER_HAS_CHROME
/* ychrome headers are GPU-free — the wire/client chrome path emits figure
 * records the hosting yetty renders, so these compile on every target
 * (including the no-WebGPU riscv guest). Needed by both standalone and client
 * mode, hence gated on HAS_CHROME, not HAS_STANDALONE. */
#include "yetty/gen/impl/ychrome/chrome.h" /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#endif

/* Android standalone entry. ygreeter runs as a NativeActivity through the
 * shared NDK glue (src/yetty/yplatform/ymain/android-glue.c), which resolves the
 * yetty_android_program_init / _term pair defined at the foot of this file. */

#ifdef YETTY_YGUI_HAS_UV
#include <uv.h>
#endif

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
    TAB_KIND_RICH = 0,   /* per-row span list, rendered by `rich` widget */
    TAB_KIND_PLOTS,      /* per-row yplot source, rendered by `yplot` figure */
    TAB_KIND_IMAGES,     /* per-row logo path, rendered by `yimage` figure */
    TAB_KIND_VIDEO,      /* per-row mp4 path, rendered by `yvideo` figure */
    TAB_KIND_ELEMENTS,   /* showcase of ygui widgets in a scrollarea */
    TAB_KIND_YREADME,    /* extracted README.md, rendered by `ymarkdown` */
    TAB_KIND_YBROWSER,   /* inline HTML, rendered by `ybrowser` */
    TAB_KIND_DIAGRAMS,   /* Mermaid diagrams in collapsing headers (ydiagram) */
    TAB_KIND_YMAZE,      /* animated maze, rendered by the `ymaze` widget */
    TAB_KIND_YZOO,       /* animated zoo, rendered by the `yzoo` widget */
    TAB_KIND_YJUNGLE,    /* animated jungle, rendered by the `yjungle` widget */
    TAB_KIND_YSHADERTOY, /* Shadertoy-style WGSL, rendered by the `yshadertoy` widget */
    TAB_KIND_YNODES,     /* node-graph editor, rendered by the `ynodes` widget */
    TAB_KIND_YPDF,       /* PDF document, rendered by the `ypdf` widget */
    TAB_KIND_YCIRCUIT,   /* electric-circuit schematics (ycircuit → ydraw_embed) */
    TAB_KIND_YMUSIC,     /* musical score notation (ymusic → ydraw_embed) */
};

/* Scenes — the leaf content panels. `tab_kind_for` / `tab_entry_*` are
 * keyed by these indices. A scene is shown either directly (its own
 * top-level tab) or under a top tab's sub-tabbar. */
#define TAB_COUNT 17

static const char *SCENE_LABELS[TAB_COUNT] = {
    "Welcome",   "Plots",        "Images",   "Code",    "Video", "Elements",
    "Markdown",  "HTML/Browser", "Diagrams", "YMaze",   "YZoo",  "YJungle",
    "Shadertoy", "Node Editor",  "PDF",      "Circuit", "Music"};

/* Top-level tabs. A tab with a single scene shows it directly; a tab with
 * several shows a sub-tabbar (same widget the Shadertoy gallery uses) that
 * switches between its scenes. `subs` lists scene indices. */
struct top_tab {
    const char *label;
    int n_subs;
    int subs[8];
};

static const struct top_tab TOP_TABS[] = {
    {"Welcome", 1, {0}},
    {"Plots", 1, {1}},
    {"Media", 2, {2, 4}}, /* Images, Video */
    {"Rich content",
     7,
     {6, 3, 14, 7, 8, 15, 16}}, /* Markdown, Code, PDF, HTML/Browser, Diagrams, Circuit, Music */
    {"YGUI Widgets", 1, {5}},   /* former Elements */
    {"Shadertoy", 1, {12}},     /* own tab — it already carries a gallery sub-tabbar */
    {"Ymazing", 4, {9, 10, 11, 13}}, /* YMaze, YZoo, YJungle, Node Editor */
};

#define TOP_TAB_COUNT ((int)(sizeof(TOP_TABS) / sizeof(TOP_TABS[0])))

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
#define CODE_KEYWORD 0xFF4E8BECu /* warm orange — int / return */
#define CODE_TYPE 0xFFFFD8B9u    /* light blue — struct names */
#define CODE_STRING 0xFFA8E0A8u  /* mint — literals */
#define CODE_COMMENT 0xFF8B8B8Bu /* gray */
#define CODE_PUNCT 0xFFC0C0C0u   /* off-white */

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
    struct yetty_yclass_object *content;
    const struct nav_entry *entries;
    int n_entries;
    int active_entry;
};

struct app {
    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *tabbar;
    struct yetty_yclass_object *body_panel;
    struct yetty_yclass_object *statusbar;

    /* Elements-tab overlays — created once under `root` and reused
     * across tab rebuilds so they don't accumulate. NULL until the
     * Elements tab is first built. */
    struct yetty_yclass_object *el_dialog;
    struct yetty_yclass_object *el_popup_menu;

    struct tab_state tabs[TAB_COUNT];

    /* Two-level tab navigation. The top tabbar (app->tabbar) selects a
     * top tab; grouped top tabs add a sub-tabbar (subbar) that selects
     * among their scenes. The visible scene renders into scene_parent
     * (body_panel for a single-scene tab, subbody for a grouped one);
     * nav-row clicks rebuild into scene_parent. */
    int cur_top;
    int top_active_sub[TOP_TAB_COUNT];
    struct yetty_yclass_object *subbar;
    struct yetty_yclass_object *subbody;
    struct yetty_yclass_object *scene_parent;
    int cur_scene;

    /* Extracted sample PDF absolute path inside data_dir, or NULL. */
    char *pdf_path;

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

    /* Shutdown hook — same function the key handler's stop_cb uses,
     * stored on the app so the titlebar close button can quit too. */
    void (*stop_cb)(struct app *app);
};

/*=============================================================================
 * UI build + tab navigation.
 *===========================================================================*/

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/*-----------------------------------------------------------------------------
 * Shadertoy tab — a sub-tabbar swaps the WGSL source on the hosted
 * yshadertoy widget. The widget is rebuilt every time the Shadertoy tab
 * is re-entered (build_scene_body), so the handler reaches the live
 * widget through this single-instance pointer, refreshed at build time.
 * Shader gallery: <yetty/yshadertoy/demo-shaders.h>.
 *---------------------------------------------------------------------------*/
static struct yetty_yclass_object *g_shadertoy_widget;

static void shadertoy_apply(int idx)
{
    if (idx < 0 || idx >= yetty_yshadertoy_demo_shader_count || !g_shadertoy_widget) {
        return;
    }
    const char *wgsl = yetty_yshadertoy_demo_shaders[idx].wgsl;
    yetty_ycore_error_destroy_safe(
        yetty_ygui_yshadertoy_set_source(g_shadertoy_widget, wgsl, strlen(wgsl)));
}

static struct yetty_ycore_void_result on_shadertoy_subtab(struct yetty_yclass_object *yc_obj,
                                                          const struct yetty_ygui_event *event,
                                                          void *userdata)
{
    (void)yc_obj;
    (void)userdata;
    shadertoy_apply(event->i0);
    return YETTY_OK_VOID();
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
    {{{"int", CODE_KEYWORD},
      {" main", CODE_TYPE},
      {"(", CODE_PUNCT},
      {"void", CODE_KEYWORD},
      {") {", CODE_PUNCT}}},
    {{{"    framework_emit(engine);", BRAND_TEXT}}},
    {{{"    ", BRAND_TEXT}, {"return ", CODE_KEYWORD}, {"0", CODE_STRING}, {";", CODE_PUNCT}}},
    {{{"}", CODE_PUNCT}}},
};

static const struct code_line code_widget_lines[] = {
    {{{"/* Adding a button — single call site. */", CODE_COMMENT}}},
    {{{"struct ", CODE_KEYWORD},
      {"yetty_yclass_object_ptr_result ", CODE_TYPE},
      {"br = yetty_ygui_widget_add(", BRAND_TEXT}}},
    {{{"    parent, yetty_ygui_button_class_get().value);", BRAND_TEXT}}},
    {{{"yetty_ygui_button_set_label(br.value, ", BRAND_TEXT},
      {"\"Apply\"", CODE_STRING},
      {");", CODE_PUNCT}}},
};

static const struct code_line code_subscribe_lines[] = {
    {{{"/* Subscribe to a value-changed event. */", CODE_COMMENT}}},
    {{{"yetty_ygui_widget_subscribe(", BRAND_TEXT}}},
    {{{"    slider, ", BRAND_TEXT},
      {"YETTY_YGUI_EVENT_VALUE_CHANGED", CODE_TYPE},
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
        if (strcmp(table[i].id, id) == 0) {
            return &table[i];
        }
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

/* yplot source grammar (see src/yetty/yplot/README.md):
 *   - `name = expr` defines a named series; `@name.color = #RRGGBB` styles it.
 *   - referencing `y` makes it a 2D field → rendered as a colormapped heatmap
 *     (the y_min/y_max below become the field's vertical domain).
 *   - referencing `time` (or `t`) makes it animate: a ~60 Hz timer feeds the
 *     elapsed seconds in and re-renders each frame — a true f(t). */
static const struct nav_entry plot_nav_entries[] = {
    {"sin / cos", "f = sin(x); g = cos(x); @f.color = #6BA892; @g.color = #74C5A5", -3.14159f,
     3.14159f, -1.5f, 1.5f},
    {"Polynomial", "f = x*x; g = 2*x + 1; @f.color = #FFD700; @g.color = #74C5A5", -5.0f, 5.0f,
     -2.0f, 12.0f},
    {"Damped wave", "f = exp(-x*x/4) * sin(3*x); @f.color = #74C0FC", -6.0f, 6.0f, -1.2f, 1.2f},
    /* Fourier synthesis: an 11-term odd-harmonic partial sum converging on a
     * square wave, drawn over its target. */
    {"Fourier square",
     "target = sign(sin(x)); "
     "sum = 4/pi*(sin(x) + sin(3*x)/3 + sin(5*x)/5 + sin(7*x)/7 + sin(9*x)/9 + sin(11*x)/11); "
     "@target.color = #556162; @sum.color = #FF6B6B",
     -6.28318f, 6.28318f, -1.5f, 1.5f},
    /* sinc / cardinal sine — a non-trivial single curve with a removable
     * singularity at the origin. */
    {"Cardinal sine", "f = sinc(x); @f.color = #74C5A5", -12.566f, 12.566f, -0.3f, 1.1f},
    /* Heatmap: standing-wave checkerboard, f(x,y) = sin(x)·cos(y). */
    {"Heatmap: sin·cos", "field = sin(x) * cos(y)", -6.28318f, 6.28318f, -6.28318f, 6.28318f},
    /* Heatmap: concentric ripples radiating from the origin. */
    {"Heatmap: ripples", "field = sin(3 * sqrt(x*x + y*y))", -6.0f, 6.0f, -6.0f, 6.0f},
    /* Dynamic f(t): a wave packet travelling left→right as time advances. */
    {"Traveling wave f(t)",
     "wave = sin(x - 2*time) * exp(-((x - 4*time - 6)^2)/8); "
     "@wave.color = #74C5A5",
     0.0f, 12.566f, -1.2f, 1.2f},
    /* Dynamic f(t): amplitude- and phase-modulated standing wave. */
    {"Pulsing sine f(t)", "f = sin(x) * cos(time); @f.color = #6BA892", -6.28318f, 6.28318f, -1.5f,
     1.5f},
    /* Dynamic 2D field f(x,y,t): the standing wave above, now animated. */
    {"Heatmap f(t)", "field = sin(x - time) * cos(y)", -6.28318f, 6.28318f, -6.28318f, 6.28318f},
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
/* Resolve the platform data dir for this run via the paths struct API (no
 * process-wide singleton getter). */
static void ygreeter_data_dir(char *out, size_t out_size)
{
    struct yetty_yplatform_paths_ptr_result paths_res = yetty_yplatform_paths_get_platform_paths();
    snprintf(out, out_size, "%s", YETTY_IS_OK(paths_res) ? paths_res.value->data_dir_buf : "");
    if (YETTY_IS_OK(paths_res)) {
        yetty_yplatform_paths_destroy(paths_res.value);
    } else {
        yetty_ycore_error_destroy(paths_res.error);
    }
}

static void discover_logo_images(struct app *app)
{
    if (app->image_paths) {
        return;
    }
    char data_dir_buf[512];
    ygreeter_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    ydebug("ygreeter: discover_logo_images data_dir=%s", data_dir ? data_dir : "(null)");
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    char **paths = NULL;
    int count = 0;
    int cap = 0;
    for (int i = 1; i <= 8; ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/logo-%d.jpeg", data_dir, i);
        int ok = yetty_yplatform_file_exists(path_buf);
        ydebug("ygreeter: discover_logo_images probe=%s status=%s", path_buf,
               ok ? "found" : "missing");
        if (!ok) {
            continue;
        }
        if (count == cap) {
            int ncap = cap ? cap * 2 : 4;
            char **np = realloc(paths, (size_t)ncap * sizeof(*np));
            if (!np) {
                break;
            }
            paths = np;
            cap = ncap;
        }
        paths[count] = strdup(path_buf);
        if (!paths[count]) {
            break;
        }
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
    if (app->video_paths) {
        return;
    }
    char data_dir_buf[512];
    ygreeter_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    ydebug("ygreeter: discover_video_files data_dir=%s", data_dir ? data_dir : "(null)");
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    char **paths = NULL;
    int count = 0;
    int cap = 0;
    static const char *const candidates[] = {"yetty-unchained-2.mp4"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", data_dir, candidates[i]);
        int ok = yetty_yplatform_file_exists(path_buf);
        ydebug("ygreeter: discover_video_files probe=%s status=%s", path_buf,
               ok ? "found" : "missing");
        if (!ok) {
            continue;
        }
        if (count == cap) {
            int ncap = cap ? cap * 2 : 4;
            char **np = realloc(paths, (size_t)ncap * sizeof(*np));
            if (!np) {
                break;
            }
            paths = np;
            cap = ncap;
        }
        paths[count] = strdup(path_buf);
        if (!paths[count]) {
            break;
        }
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
    if (app->readme_path) {
        return;
    }
    char data_dir_buf[512];
    ygreeter_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%s/README.md", data_dir);
    int ok = yetty_yplatform_file_exists(path_buf);
    ydebug("ygreeter: discover_readme probe=%s status=%s", path_buf, ok ? "found" : "missing");
    if (!ok) {
        return;
    }
    app->readme_path = strdup(path_buf);
}

/* Locate the extracted sample PDF (incbin manifest ships
 * <data_dir>/pdf-sample.pdf). Failure leaves app->pdf_path NULL — the PDF
 * subtab then renders an empty ypdf. */
static void discover_pdf(struct app *app)
{
    if (app->pdf_path) {
        return;
    }
    char data_dir_buf[512];
    ygreeter_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%s/pdf-sample.pdf", data_dir);
    int ok = yetty_yplatform_file_exists(path_buf);
    ydebug("ygreeter: discover_pdf probe=%s status=%s", path_buf, ok ? "found" : "missing");
    if (!ok) {
        return;
    }
    app->pdf_path = strdup(path_buf);
}

/*-----------------------------------------------------------------------------
 * Loaders — populate the per-tab content widget from a given entry.
 *---------------------------------------------------------------------------*/

/* Wipe a content widget's children and recreate it as the requested kind
 * — the ygui rich/yplot/yimage widgets do not expose a clear API, so
 * recreating is the simplest correct way to swap content. */
static struct yetty_ycore_void_result build_scene_body(struct app *app,
                                                       struct yetty_yclass_object *parent,
                                                       int scene_index);
static struct yetty_ycore_void_result rebuild_top(struct app *app, int top_index);

static struct yetty_ycore_void_result load_plot_entry(struct yetty_yclass_object *_yc_obj,
                                                      const struct nav_entry *entry)
{
    struct yetty_yclass_object *plot = (struct yetty_yclass_object *)_yc_obj;
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
 * leaves `*out_len` untouched. Shared by load_image_entry and
 * load_video_entry — the same idiom twice wasn't worth keeping. */
static uint8_t *slurp_file(const char *path, size_t *out_len)
{
    if (!path) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
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

static struct yetty_ycore_void_result load_image_entry(struct yetty_yclass_object *_yc_obj,
                                                       const char *path)
{
    struct yetty_yclass_object *image = (struct yetty_yclass_object *)_yc_obj;
    if (!path) {
        return yetty_ygui_yimage_set_bytes(image, NULL, 0);
    }
    size_t got = 0;
    uint8_t *buf = slurp_file(path, &got);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "load_image_entry: slurp failed");
    }
    struct yetty_ycore_void_result r = yetty_ygui_yimage_set_bytes(image, buf, got);
    free(buf);
    return r;
}

static struct yetty_ycore_void_result load_video_entry(struct yetty_yclass_object *_yc_obj,
                                                       const char *path)
{
    struct yetty_yclass_object *video = (struct yetty_yclass_object *)_yc_obj;
    if (!path) {
        return yetty_ygui_yvideo_set_bytes(video, NULL, 0);
    }
    size_t got = 0;
    uint8_t *buf = slurp_file(path, &got);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "load_video_entry: slurp failed");
    }
    struct yetty_ycore_void_result r = yetty_ygui_yvideo_set_bytes(video, buf, got);
    free(buf);
    return r;
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
    "yetty_ygui_widget_add(\n"
    "    parent, \n"
    "    yetty_ygui_ybrowser_class_get().value);"
    "</pre>"
    "</body></html>";

/* Seeding moved to build_ybrowser_content (below): the YBrowser tab now
 * renders a stack of collapsing-header sections, one ybrowser each, so
 * YBROWSER_SAMPLE_HTML above is reused as the "Overview" section. */

static struct yetty_ycore_void_result write_code_snippet(struct yetty_yclass_object *_yc_obj,
                                                         const char *snippet_id)
{
    struct yetty_yclass_object *rich = (struct yetty_yclass_object *)_yc_obj;
    const struct code_snippet *snip = code_snippet_at(snippet_id);
    if (!snip) {
        return YETTY_OK_VOID();
    }
    for (size_t li = 0; li < snip->n_lines; ++li) {
        struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(rich);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "code: rich_add_line");
        for (size_t si = 0; si < sizeof(snip->lines[li].spans) / sizeof(snip->lines[li].spans[0]);
             ++si) {
            const char *t = snip->lines[li].spans[si].text;
            if (!t) {
                break;
            }
            if (t[0] == '\0') {
                continue;
            }
            yetty_ycore_error_destroy_safe(
                yetty_ygui_rich_add_span(rich, t, 13.0f, snip->lines[li].spans[si].color));
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result write_welcome_spans(struct yetty_yclass_object *_yc_obj,
                                                          const struct rich_span *spans,
                                                          size_t n_spans)
{
    struct yetty_yclass_object *rich = (struct yetty_yclass_object *)_yc_obj;
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

static struct yetty_ycore_void_result on_row_clicked(struct yetty_yclass_object *btn,
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
        if (!na) {
            return NULL;
        }
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
    case 0:
        return (int)(sizeof(welcome_nav_entries) / sizeof(welcome_nav_entries[0]));
    case 1:
        return (int)(sizeof(plot_nav_entries) / sizeof(plot_nav_entries[0]));
    case 2:
        return app->image_path_count > 0 ? app->image_path_count : 1;
    case 3:
        return (int)(sizeof(code_nav_entries) / sizeof(code_nav_entries[0]));
    case 4:
        return app->video_path_count > 0 ? app->video_path_count : 1;
    case 5:  /* Elements — chrome-only, the single nav row is a label hook. */
    case 6:  /* YReadme  — single piece of content (README.md). */
    case 7:  /* YBrowser — single inline HTML sample. */
    case 8:  /* Diagrams — single self-contained scrollarea. */
    case 9:  /* YMaze     — single self-contained animated widget. */
    case 10: /* YZoo      — single self-contained animated widget. */
    case 11: /* YJungle   — single self-contained animated widget. */
    case 12: /* Shadertoy — single self-contained animated widget. */
        return 1;
    default:
        return 0;
    }
}

static const char *tab_entry_label(const struct app *app, int tab_index, int entry_index)
{
    switch (tab_index) {
    case 0:
        return welcome_nav_entries[entry_index].label;
    case 1:
        return plot_nav_entries[entry_index].label;
    case 2: {
        if (app->image_path_count <= 0) {
            return "(no images found)";
        }
        static char buf[64];
        snprintf(buf, sizeof(buf), "logo-%d", entry_index + 1);
        return buf;
    }
    case 3:
        return code_nav_entries[entry_index].label;
    case 4: {
        if (app->video_path_count <= 0) {
            return "(no videos found)";
        }
        static char buf[64];
        snprintf(buf, sizeof(buf), "clip-%d", entry_index + 1);
        return buf;
    }
    case 5:
        return "Showcase";
    case 6:
        return app->readme_path ? "README.md" : "(no README)";
    case 7:
        return "Sample";
    case 8:
        return "Showcase";
    default:
        return "";
    }
}

static enum tab_kind tab_kind_for(int tab_index)
{
    switch (tab_index) {
    case 1:
        return TAB_KIND_PLOTS;
    case 2:
        return TAB_KIND_IMAGES;
    case 4:
        return TAB_KIND_VIDEO;
    case 5:
        return TAB_KIND_ELEMENTS;
    case 6:
        return TAB_KIND_YREADME;
    case 7:
        return TAB_KIND_YBROWSER;
    case 8:
        return TAB_KIND_DIAGRAMS;
    case 9:
        return TAB_KIND_YMAZE;
    case 10:
        return TAB_KIND_YZOO;
    case 11:
        return TAB_KIND_YJUNGLE;
    case 12:
        return TAB_KIND_YSHADERTOY;
    case 13:
        return TAB_KIND_YNODES;
    case 14:
        return TAB_KIND_YPDF;
    case 15:
        return TAB_KIND_YCIRCUIT;
    case 16:
        return TAB_KIND_YMUSIC;
    case 0:
    case 3:
    default:
        return TAB_KIND_RICH;
    }
}

/* Elements scrollarea — minimal first cut mirroring
 * demo/ygui/35_collapsing_header_open: a small set of
 * collapsing_header sections each holding a handful of plain widgets.
 *
 * Sizing caveat documented in layout.c: the flex pass treats
 * `height = -1` as `main_pref = 0`, so children with no authored
 * height collapse to zero rect and stack at y=0. Every leaf widget
 * AND every section container therefore needs an explicit `height`.
 * yimage / yplot / chrome-heavy widgets are intentionally left out of
 * this initial shape — once headers + simple widgets render correctly
 * we add them back one section at a time. */

#define EL_ROW_H 28.0f
#define EL_HEADER_H 28.0f
#define EL_GAP 4.0f

static struct yetty_ycore_void_result el_set_height(struct yetty_yclass_object *w, float h)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "el_set_height: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.height = h;
    return yetty_ygui_widget_layout_set(w, &l);
}

static void el_set_width(struct yetty_yclass_object *w, float width)
{
    if (!w) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = width;
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(w, &l));
}

static void el_set_grow(struct yetty_yclass_object *w, float grow)
{
    if (!w) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = grow;
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(w, &l));
}

/* Add `cls` under `parent` and author its height. Returns NULL on
 * allocation failure (the showcase simply skips that widget). */
static struct yetty_yclass_object *el_w(struct yetty_yclass_object *parent,
                                        const struct yetty_yclass *cls, float h)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    yetty_ycore_error_destroy_safe(el_set_height(r.value, h));
    return r.value;
}

/* Titled collapsing_header section. Created expanded so el_finalize_section
 * can measure its open height; el_finalize_section then collapses it so
 * sections start folded (callers that want one open re-open it after). */
static struct yetty_yclass_object *el_section(struct yetty_yclass_object *parent, const char *title)
{
    struct yetty_yclass_object_ptr_result hr =
        yetty_ygui_widget_add(parent, yetty_ygui_collapsing_header_class_get().value);
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
        return NULL;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_collapsing_header_set_title(hr.value, title));
    yetty_ycore_error_destroy_safe(yetty_ygui_collapsing_header_set_open(hr.value, 1));
    return hr.value;
}

/* Derive the section's open height from its children: header strip +
 * paddings + sum of authored child heights + inter-row gaps. */
static void el_finalize_section(struct yetty_yclass_object *sec)
{
    if (!sec) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result section_layout_res =
        yetty_ygui_widget_layout_get(sec);
    if (YETTY_IS_ERR(section_layout_res)) {
        yetty_ycore_error_destroy(section_layout_res.error);
        return;
    }
    const struct yetty_ygui_layout *sl = section_layout_res.value;
    float total = sl->padding_top + sl->padding_bottom;
    int n = 0;
    struct yetty_yclass_object_ptr_result first_child_res = yetty_ygui_widget_first_child(sec);
    if (YETTY_IS_ERR(first_child_res)) {
        yetty_ycore_error_destroy(first_child_res.error);
        return;
    }
    for (struct yetty_yclass_object *c = first_child_res.value; c;) {
        struct yetty_ygui_layout_const_ptr_result child_layout_res =
            yetty_ygui_widget_layout_get(c);
        if (YETTY_IS_ERR(child_layout_res)) {
            yetty_ycore_error_destroy(child_layout_res.error);
            return;
        }
        const struct yetty_ygui_layout *cl = child_layout_res.value;
        total += cl->height > 0.0f ? cl->height : 0.0f;
        n++;
        struct yetty_yclass_object_ptr_result next_sibling_res = yetty_ygui_widget_next_sibling(c);
        if (YETTY_IS_ERR(next_sibling_res)) {
            yetty_ycore_error_destroy(next_sibling_res.error);
            return;
        }
        c = next_sibling_res.value;
    }
    if (n > 1) {
        total += sl->gap * (float)(n - 1);
    }
    yetty_ycore_error_destroy_safe(el_set_height(sec, total));
    /* Collapsed by default — the open height is already captured above, so
     * the section expands to the right size when the user clicks it. */
    yetty_ycore_error_destroy_safe(yetty_ygui_collapsing_header_set_open(sec, 0));
}

/* ---- Overlay trigger callbacks ---- */

static struct yetty_ycore_void_result el_menu_item(struct yetty_yclass_object *menu, int idx,
                                                   void *ud)
{
    (void)menu;
    (void)idx;
    (void)ud;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result el_open_dialog(struct yetty_yclass_object *obj, void *ud)
{
    (void)obj;
    return yetty_ygui_dialog_open_at((struct yetty_yclass_object *)ud, 260, 200, 380, 180);
}

static struct yetty_ycore_void_result el_open_menu(struct yetty_yclass_object *obj, void *ud)
{
    struct yetty_ycore_rectangle_result rect_res =
        yetty_ygui_widget_rect((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "el_open_menu: widget_rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    return yetty_ygui_popup_menu_toggle_at((struct yetty_yclass_object *)ud, r.min.x,
                                           r.max.y + 2.0f);
}

static struct yetty_ycore_void_result build_elements_content(struct app *app,
                                                             struct yetty_yclass_object *root)
{
    (void)app;

    /* ---- Inputs ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Inputs");

        struct yetty_yclass_object *btn = el_w(sec, yetty_ygui_button_class_get().value, 32);
        yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(btn, "Button"));

        struct yetty_yclass_object *ti =
            el_w(sec, yetty_ygui_textinput_class_get().value, EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_textinput_set_placeholder(ti, "type here…"));

        struct yetty_yclass_object *sl = el_w(sec, yetty_ygui_slider_class_get().value, EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_range(sl, 0.0f, 1.0f));
        yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_value(sl, 0.4f));

        struct yetty_yclass_object *spin_i = el_w(sec, yetty_ygui_spinner_class_get().value, 30);
        yetty_ycore_error_destroy_safe(yetty_ygui_spinner_set_range(spin_i, 1.0f, 100.0f, 1.0f));
        yetty_ycore_error_destroy_safe(yetty_ygui_spinner_set_value(spin_i, 42.0f));

        struct yetty_yclass_object *spin_f = el_w(sec, yetty_ygui_spinner_class_get().value, 30);
        yetty_ycore_error_destroy_safe(yetty_ygui_spinner_set_range(spin_f, 0.0f, 10.0f, 0.25f));
        yetty_ycore_error_destroy_safe(yetty_ygui_spinner_set_value(spin_f, 2.5f));

        struct yetty_yclass_object *cb = el_w(sec, yetty_ygui_checkbox_class_get().value, 24);
        yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_label(cb, "Enabled"));
        yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_checked(cb, 1));

        struct yetty_yclass_object *tg = el_w(sec, yetty_ygui_toggle_class_get().value, 26);
        yetty_ycore_error_destroy_safe(yetty_ygui_toggle_set_label(tg, "Notifications"));
        yetty_ycore_error_destroy_safe(yetty_ygui_toggle_set_on(tg, 1));

        struct yetty_yclass_object *pr = el_w(sec, yetty_ygui_progress_class_get().value, 16);
        yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(pr, 0.65f));

        struct yetty_yclass_object *ta = el_w(sec, yetty_ygui_textarea_class_get().value, 120);
        yetty_ycore_error_destroy_safe(yetty_ygui_textarea_set_text(
            ta, "Multi-line text area.\nClick to focus, then type.\n"));

        el_finalize_section(sec);
    }

    /* ---- Selectors ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Selectors");

        static const char *fruits[] = {"Apple", "Banana", "Cherry"};
        for (int i = 0; i < 3; i++) {
            struct yetty_yclass_object *rb = el_w(sec, yetty_ygui_radio_class_get().value, 24);
            yetty_ycore_error_destroy_safe(yetty_ygui_radio_set_label(rb, fruits[i]));
            yetty_ycore_error_destroy_safe(yetty_ygui_radio_set_selected(rb, i == 0 ? 1 : 0));
        }

        struct yetty_yclass_object *dd = el_w(sec, yetty_ygui_dropdown_class_get().value, EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(dd, "Option A"));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(dd, "Option B"));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(dd, "Option C"));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_set_selected(dd, 0));

        struct yetty_yclass_object *combo =
            el_w(sec, yetty_ygui_combobox_class_get().value, EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_set_text(combo, "red"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(combo, "green"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(combo, "blue"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(combo, "magenta"));

        struct yetty_yclass_object *ch =
            el_w(sec, yetty_ygui_choicebox_class_get().value, 4 * EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(ch, "Small"));
        yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(ch, "Medium"));
        yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(ch, "Large"));
        yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(ch, "Huge"));

        struct yetty_yclass_object *cp = el_w(sec, yetty_ygui_colorpicker_class_get().value, 32);
        yetty_ycore_error_destroy_safe(yetty_ygui_colorpicker_set_color(cp, 0xFF92A86Bu));

        el_finalize_section(sec);
    }

    /* ---- Display ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Display");

        struct yetty_yclass_object *lbl = el_w(sec, yetty_ygui_label_class_get().value, 24);
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(lbl, "Plain label"));

        el_w(sec, yetty_ygui_separator_class_get().value, 8);

        struct yetty_yclass_object *pr = el_w(sec, yetty_ygui_progress_class_get().value, 14);
        yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(pr, 0.25f));

        struct yetty_yclass_object *tbl = el_w(sec, yetty_ygui_table_class_get().value, 120);
        static const char *cols[] = {"PID", "USER", "%CPU", "COMMAND"};
        yetty_ycore_error_destroy_safe(yetty_ygui_table_set_columns(tbl, 4, cols));
        static const char *row1[] = {"1", "root", "0.0", "/sbin/init"};
        static const char *row2[] = {"42", "misi", "1.3", "/usr/bin/yetty"};
        static const char *row3[] = {"1337", "misi", "0.2", "/usr/bin/ygreeter"};
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(tbl, row1, 4));
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(tbl, row2, 4));
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(tbl, row3, 4));

        struct yetty_yclass_object *crumbs =
            el_w(sec, yetty_ygui_breadcrumbs_class_get().value, 24);
        yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(crumbs, "Home"));
        yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(crumbs, "Projects"));
        yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(crumbs, "yetty"));
        yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(crumbs, "ygui"));

        /* Closable chip / tag row. */
        struct yetty_yclass_object *chip_row = el_w(sec, yetty_ygui_hbox_class_get().value, 24);
        if (chip_row) {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(chip_row);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "elements: chip_row layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.gap = 6.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(chip_row, &l));
            static const char *tags[] = {"linux", "gpu", "rust-free"};
            for (int i = 0; i < 3; i++) {
                struct yetty_yclass_object *chip =
                    el_w(chip_row, yetty_ygui_chip_class_get().value, 24);
                el_set_width(chip, 90);
                yetty_ycore_error_destroy_safe(yetty_ygui_chip_set_label(chip, tags[i]));
                yetty_ycore_error_destroy_safe(yetty_ygui_chip_set_closable(chip, i < 2 ? 1 : 0));
            }
        }

        struct yetty_yclass_object *step = el_w(sec, yetty_ygui_stepper_class_get().value, 56);
        yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(step, "Setup"));
        yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(step, "Install"));
        yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(step, "Done"));
        yetty_ycore_error_destroy_safe(yetty_ygui_stepper_set_current(step, 1));

        el_finalize_section(sec);
    }

    /* ---- Lists & Trees ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Lists & Trees");

        struct yetty_yclass_object *lst = el_w(sec, yetty_ygui_list_class_get().value, 4 * 24);
        yetty_ycore_error_destroy_safe(yetty_ygui_list_add(lst, "Apple"));
        yetty_ycore_error_destroy_safe(yetty_ygui_list_add(lst, "Banana"));
        yetty_ycore_error_destroy_safe(yetty_ygui_list_add(lst, "Cherry"));
        yetty_ycore_error_destroy_safe(yetty_ygui_list_add(lst, "Date"));
        yetty_ycore_error_destroy_safe(yetty_ygui_list_set_selected(lst, 0));

        struct yetty_yclass_object *tn =
            el_w(sec, yetty_ygui_tree_node_class_get().value, 22 + 2 * 24 + 2);
        yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_set_label(tn, "Tree root"));
        yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_set_open(tn, 1));
        if (tn) {
            struct yetty_yclass_object *c1 = el_w(tn, yetty_ygui_label_class_get().value, 24);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(c1, "child 1"));
            struct yetty_yclass_object *c2 = el_w(tn, yetty_ygui_label_class_get().value, 24);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(c2, "child 2"));
        }

        /* Calendar. */
        struct yetty_yclass_object *dp = el_w(sec, yetty_ygui_datepicker_class_get().value, 220);
        yetty_ycore_error_destroy_safe(yetty_ygui_datepicker_set_date(dp, 2025, 4, 15));

        /* File browser, rooted at $HOME (or "/"). */
        struct yetty_yclass_object *fp = el_w(sec, yetty_ygui_filepicker_class_get().value, 240);
        const char *home = getenv("HOME");
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yetty_ygui_filepicker_set_dir(fp, home && *home ? home : "/"),
                            "elements: filepicker set_dir");

        el_finalize_section(sec);
    }

    /* ---- Layout & Containers ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Layout & Containers");

        /* [left panel | splitter | right panel] — drag the splitter. */
        struct yetty_yclass_object *row = el_w(sec, yetty_ygui_hbox_class_get().value, 80);
        if (row) {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(row);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "elements: row layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.gap = 0.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(row, &l));

            struct yetty_yclass_object *left = el_w(row, yetty_ygui_panel_class_get().value, 80);
            el_set_width(left, 220);
            yetty_ycore_error_destroy_safe(
                yetty_ygui_panel_set_bg(left, (struct yetty_ycore_rgba){30, 38, 44, 255}));

            struct yetty_yclass_object *div = el_w(row, yetty_ygui_splitter_class_get().value, 80);
            el_set_width(div, 6);

            struct yetty_yclass_object *right = el_w(row, yetty_ygui_panel_class_get().value, 80);
            el_set_grow(right, 1.0f);
            yetty_ycore_error_destroy_safe(
                yetty_ygui_panel_set_bg(right, (struct yetty_ycore_rgba){20, 26, 31, 255}));
        }

        el_finalize_section(sec);
    }

    /* ---- Overlays ---- */
    {
        struct yetty_yclass_object *sec = el_section(root, "Overlays");

        struct yetty_yclass_object *tip = el_w(sec, yetty_ygui_tooltip_class_get().value, EL_ROW_H);
        yetty_ycore_error_destroy_safe(yetty_ygui_tooltip_set_text(tip, "Tooltip example"));

        struct yetty_yclass_object *selr = el_w(sec, yetty_ygui_selectable_class_get().value, 26);
        yetty_ycore_error_destroy_safe(yetty_ygui_selectable_set_text(selr, "Selectable row"));

        struct yetty_yclass_object *open_dlg = el_w(sec, yetty_ygui_button_class_get().value, 30);
        yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(open_dlg, "Open dialog…"));

        struct yetty_yclass_object *open_menu = el_w(sec, yetty_ygui_button_class_get().value, 30);
        yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(open_menu, "Open menu…"));

        el_finalize_section(sec);

        /* The dialog + popup_menu float above everything, so they live on
         * the engine root rather than inside the scrollarea — and are
         * created once, then reused across rebuilds (this function runs on
         * every tab switch). */
        struct yetty_yclass_object *host = app->root ? app->root : root;

        if (!app->el_dialog) {
            struct yetty_yclass_object_ptr_result dr =
                yetty_ygui_widget_add(host, yetty_ygui_dialog_class_get().value);
            if (YETTY_IS_OK(dr)) {
                app->el_dialog = dr.value;
                yetty_ycore_error_destroy_safe(yetty_ygui_dialog_set_title(dr.value, "Dialog"));
                struct yetty_yclass_object_ptr_result body =
                    yetty_ygui_widget_add(dr.value, yetty_ygui_label_class_get().value);
                if (YETTY_IS_OK(body)) {
                    yetty_ycore_error_destroy_safe(
                        yetty_ygui_label_set_text(body.value, "This is a modal dialog."));
                }
            } else {
                yetty_ycore_error_destroy(dr.error);
            }
        }
        if (app->el_dialog) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(open_dlg, el_open_dialog, app->el_dialog));
        }

        if (!app->el_popup_menu) {
            struct yetty_yclass_object_ptr_result pm =
                yetty_ygui_widget_add(host, yetty_ygui_popup_menu_class_get().value);
            if (YETTY_IS_OK(pm)) {
                app->el_popup_menu = pm.value;
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_popup_menu_add_item(pm.value, "First action", el_menu_item, NULL));
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_popup_menu_add_item(pm.value, "Second action", el_menu_item, NULL));
                yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_add_separator(pm.value));
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_popup_menu_add_item(pm.value, "Third action", el_menu_item, NULL));
            } else {
                yetty_ycore_error_destroy(pm.error);
            }
        }
        if (app->el_popup_menu) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(open_menu, el_open_menu, app->el_popup_menu));
        }
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * YBrowser tab — feature scenarios as collapsing sections.
 *
 * Each scenario is a complete HTML document handed to its own ybrowser
 * widget inside a collapsing_header, stacked in the same Elements-style
 * scrollarea. Mirrors demo/ygui/26_ybrowser — the same Typography /
 * Forms / Grid / CSS-cards / JavaScript pages — so the YBrowser tab
 * shows the engine's breadth (HTML parse, CSS cascade incl. var(),
 * flexbox, and live JavaScript) at a glance. The brand palette is baked
 * into each page so they blend with the dark canvas.
 *===========================================================================*/
#define YG_BROWSER_CSS                                                                             \
    "html,body{margin:0;padding:0;}"                                                               \
    "body{background:#0B1014;color:#E0E5E4;font-size:15px;padding:14px 18px;}"                     \
    "h1{color:#74C5A5;}h2{color:#6BA892;}h3{color:#9FA7A8;}"                                       \
    "p{margin:0 0 10px;}"                                                                          \
    "a{color:#6BA892;}"                                                                            \
    ".muted{color:#9FA7A8;}"                                                                       \
    ".accent{color:#74C5A5;}"                                                                      \
    "code{color:#74C5A5;}"                                                                         \
    "hr{border:0;border-top:1px solid #364A47;margin:14px 0;}"                                     \
    ".card{background:#141A1F;border:1px solid #364A47;border-radius:10px;"                        \
    "padding:14px 16px;margin:0 0 12px;}"

#define YG_DOC(EXTRA_CSS, BODY)                                                                    \
    "<!doctype html><html lang=en><head><meta charset=utf-8><style>" YG_BROWSER_CSS EXTRA_CSS      \
    "</style></head><body>" BODY "</body></html>"

struct ybrowser_scenario {
    const char *title;
    const char *html;
    float height; /* authored open-height of the section's ybrowser */
};

/* Static-const local table (no file-scope data symbol). Index-aligned
 * sections; the first opens, the rest start collapsed. */
static const struct ybrowser_scenario *ybrowser_scenarios(int *count)
{
    static const struct ybrowser_scenario table[] = {
        {"Overview", YBROWSER_SAMPLE_HTML, 230.0f},

        {"Typography",
         YG_DOC("blockquote{border-left:3px solid #6BA892;background:#141A1F;"
                "padding:10px 16px;margin:0 0 14px;color:#9FA7A8;border-radius:0 8px 8px 0;}"
                "pre{background:#0B1014;border:1px solid #364A47;border-radius:8px;"
                "padding:12px 14px;color:#74C5A5;margin:0 0 14px;}"
                "ul,ol{padding-left:22px;margin:0 0 12px;}"
                "li{padding:3px 0;}",
                "<div class=card>"
                "<h1>Heading 1</h1><h2>Heading 2</h2><h3>Heading 3</h3><h4>Heading 4</h4>"
                "<p>Body text flows as wrapped inline runs. The cascade resolves "
                "color, size and spacing per block element.</p>"
                "</div>"
                "<div class=card>"
                "<h3>Blockquote</h3>"
                "<blockquote>Terminals were never meant to stay monochrome rectangles. "
                "ybrowser proves a terminal can host the actual web platform.</blockquote>"
                "<h3>Preformatted &amp; code</h3>"
                "<pre>struct yetty_ycore_void_result\n"
                "yetty_ygui_ybrowser_set_html(obj, html, len);</pre>"
                "<h3>Lists</h3>"
                "<ul><li>Block-flow vertical stacking</li>"
                "<li>Inline text wrapping</li>"
                "<li>Flex-row even split</li></ul>"
                "</div>"),
         470.0f},

        {"Forms",
         YG_DOC("form{margin:0;}"
                "label{display:block;color:#9FA7A8;font-size:13px;margin:12px 0 5px;}"
                "input,select,textarea{display:block;background:#0B1014;color:#E0E5E4;"
                "border:1px solid #364A47;border-radius:7px;padding:9px 11px;margin:0;}"
                "textarea{height:60px;}"
                ".chkrow{display:flex;flex-direction:row;align-items:center;margin:14px 0 0;}"
                ".box{width:18px;height:18px;border:1px solid #364A47;border-radius:5px;"
                "background:#0B1014;}"
                ".box.on{background:#6BA892;border-color:#6BA892;}"
                ".chklbl{flex-grow:1;color:#E0E5E4;padding-left:10px;}"
                ".btns{display:flex;flex-direction:row;margin:20px 0 0;}"
                "button{display:block;flex-grow:1;border:0;border-radius:7px;padding:11px 0;"
                "text-align:center;background:#1E262C;color:#E0E5E4;margin-right:10px;}"
                "button.primary{background:#6BA892;color:#0B1014;}",
                "<div class=card>"
                "<h2>Create account</h2>"
                "<form>"
                "<label>Full name</label><input type=text>"
                "<label>Email</label><input type=email>"
                "<label>Password</label><input type=password>"
                "<label>Plan</label>"
                "<select><option>Free &mdash; community</option>"
                "<option>Pro</option><option>Team</option></select>"
                "<label>Notes</label>"
                "<textarea>Ships GPU-rendered HTML inside the terminal.</textarea>"
                "<div class=chkrow><div class=\"box on\"></div>"
                "<div class=chklbl>Email me product updates</div></div>"
                "<div class=chkrow><div class=box></div>"
                "<div class=chklbl>Enable experimental features</div></div>"
                "<div class=btns><button class=primary>Create account</button>"
                "<button>Cancel</button></div>"
                "</form>"
                "</div>"),
         540.0f},

        {"Grid",
         YG_DOC(".grid{border:1px solid #364A47;border-radius:9px;background:#141A1F;"
                "padding:0;margin:0;}"
                ".tr{display:flex;flex-direction:row;}"
                ".tr .c{flex-grow:1;padding:10px 14px;border-top:1px solid #1E262C;color:#E0E5E4;}"
                ".tr .name{flex-grow:2;}"
                ".head{background:#1E262C;border-radius:9px 9px 0 0;}"
                ".head .c{border-top:0;color:#74C5A5;}"
                ".odd{background:#0F151A;}"
                ".num{color:#9FA7A8;}",
                "<div class=card>"
                "<h2>Flexbox grid</h2>"
                "<p class=muted>Each row is <code>display:flex</code>; cells share width via "
                "<code>flex-grow</code>. The name column grows twice as fast.</p>"
                "<div class=grid>"
                "<div class=\"tr head\"><div class=\"c name\">Component</div>"
                "<div class=c>Backend</div><div class=\"c num\">KLOC</div></div>"
                "<div class=tr><div class=\"c name\">Parser</div>"
                "<div class=c>lexbor</div><div class=\"c num\">1.4</div></div>"
                "<div class=\"tr odd\"><div class=\"c name\">Cascade</div>"
                "<div class=c>libcss</div><div class=\"c num\">0.9</div></div>"
                "<div class=tr><div class=\"c name\">Scripting</div>"
                "<div class=c>QuickJS-NG</div><div class=\"c num\">2.1</div></div>"
                "<div class=\"tr odd\"><div class=\"c name\">Layout</div>"
                "<div class=c>block + flex</div><div class=\"c num\">1.5</div></div>"
                "</div>"
                "</div>"),
         330.0f},

        {"CSS cards",
         YG_DOC(":root{--lift:#141A1F;--row:#1E262C;--accent:#6BA892;--bright:#74C5A5;"
                "--border:#364A47;}"
                ".deck{display:flex;flex-direction:row;}"
                ".col{flex-grow:1;background:var(--lift);border:1px solid var(--border);"
                "border-radius:12px;padding:16px;margin-right:12px;}"
                ".col.two{background:var(--row);}"
                ".col.three{background:var(--accent);}"
                ".col h3{color:var(--bright);margin:0 0 8px;}"
                ".col.three h3{color:#0B1014;}"
                ".col p{color:#9FA7A8;margin:0;}"
                ".col.three p{color:#0B1014;}"
                ".swatch{height:34px;border-radius:8px;margin:0 0 10px;background:var(--accent);}"
                ".col.two .swatch{background:var(--bright);}"
                ".col.three .swatch{background:#0B1014;}",
                "<div class=card>"
                "<h2>CSS custom properties</h2>"
                "<p class=muted>Colors below come from <code>var(--accent)</code> &amp; friends, "
                "declared once on <code>:root</code> and resolved during the box pass.</p>"
                "<div class=deck>"
                "<div class=col><div class=swatch></div><h3>Surface</h3>"
                "<p>Raised panel on the brand background ladder.</p></div>"
                "<div class=\"col two\"><div class=swatch></div><h3>Row</h3>"
                "<p>One step brighter for hover / selection.</p></div>"
                "<div class=\"col three\"><div class=swatch></div><h3>Accent</h3>"
                "<p>The brand mint, driving every focus highlight.</p></div>"
                "</div>"
                "</div>"),
         260.0f},

        {"JavaScript",
         YG_DOC(
             "pre{background:#0B1014;border:1px solid #364A47;border-radius:8px;"
             "padding:12px 14px;color:#74C5A5;margin:0 0 14px;}"
             ".jrow{display:flex;flex-direction:row;}"
             ".jrow .jc{flex-grow:1;padding:8px 12px;border-top:1px solid #1E262C;color:#E0E5E4;}"
             ".jrow.jhead{background:#1E262C;border-radius:8px 8px 0 0;}"
             ".jrow.jhead .jc{border-top:0;color:#74C5A5;}"
             "#out{border:1px solid #364A47;border-radius:8px;background:#141A1F;margin:0 0 12px;}"
             ".note{padding:9px 12px;color:#9FA7A8;border-top:1px solid #1E262C;}",
             "<div class=card>"
             "<h2>JavaScript at load</h2>"
             "<p class=muted>QuickJS runs inline scripts while the page loads and mutates "
             "the DOM before paint. Everything in the box below was produced by this script:</p>"
             "<pre>var out = document.querySelector('#out');\n"
             "for (var n = 1; n &lt;= 6; n++)\n"
             "  out.innerHTML += row(n, n*n, Math.pow(2,n));\n"
             "out.appendChild(stamp(new Date()));</pre>"
             "<div id=out><div class=note>This panel is generated by JavaScript "
             "at page load. Seeing this line means the build was configured "
             "without QuickJS (the lib-quickjs prebuilt wasn't available).</div></div>"
             "</div>"
             "<script>"
             "function cell(t){return '<div class=\"jc\">'+t+'</div>';}"
             "var out = document.querySelector('#out');"
             "var html = '<div class=\"jrow jhead\">'+cell('n')+cell('n "
             "squared')+cell('2^n')+'</div>';"
             "for (var n = 1; n <= 6; n++) {"
             "  html += '<div class=\"jrow\">'+cell(n)+cell(n*n)+cell(Math.pow(2,n))+'</div>';"
             "}"
             "out.innerHTML = html;"
             "var note = document.createElement('div');"
             "note.className = 'note';"
             "note.textContent = 'document.createElement + new Date(): ' + new Date().toString();"
             "out.appendChild(note);"
             "</script>"),
         400.0f},
    };
    *count = (int)(sizeof(table) / sizeof(table[0]));
    return table;
}

/* Build the YBrowser tab body: one collapsing_header section per
 * scenario, each wrapping an ybrowser. Mirrors build_elements_content;
 * `root` is the tab's scrollarea. */
static struct yetty_ycore_void_result build_ybrowser_content(struct app *app,
                                                             struct yetty_yclass_object *root)
{
    (void)app;
    int count = 0;
    const struct ybrowser_scenario *scen = ybrowser_scenarios(&count);
    for (int i = 0; i < count; ++i) {
        struct yetty_yclass_object *sec = el_section(root, scen[i].title);
        if (!sec) {
            continue;
        }
        struct yetty_yclass_object *br =
            el_w(sec, yetty_ygui_ybrowser_class_get().value, scen[i].height);
        if (br) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_ybrowser_set_html(br, scen[i].html, strlen(scen[i].html)));
        }
        el_finalize_section(sec);
        /* el_finalize_section collapses every section by default; the
         * YBrowser tab keeps its first scenario expanded so the tab isn't
         * empty on open. */
        if (i == 0) {
            yetty_ycore_error_destroy_safe(yetty_ygui_collapsing_header_set_open(sec, 1));
        }
    }
    return YETTY_OK_VOID();
}

/* One YReadme section: a titled collapsing_header whose whole body is a
 * single ymarkdown widget rendering `src`. Body height is derived from the
 * source line count — the renderer advances ~22.4px per line at the 16px
 * cell, and tables / code fences render fewer lines than they span in
 * source, so line_count * 24 plus a small pad never clips. Mirrors the
 * add_md_section helper in demo/ygui/24_ymarkdown. */
static void yr_md_section(struct yetty_yclass_object *area, const char *title, const char *src)
{
    struct yetty_yclass_object *sec = el_section(area, title);
    if (!sec) {
        return;
    }
    size_t len = strlen(src);
    int lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\n') {
            lines++;
        }
    }
    float h = (float)lines * 24.0f + 12.0f;
    struct yetty_yclass_object *md = el_w(sec, yetty_ygui_ymarkdown_class_get().value, h);
    if (md) {
        yetty_ycore_error_destroy_safe(yetty_ygui_ymarkdown_set_source(md, src, len));
    }
    el_finalize_section(sec);
}

/* Build the YReadme tab body: one collapsing_header per markdown feature,
 * each wrapping a ymarkdown widget — the exact accordion from
 * demo/ygui/24_ymarkdown. `root` is the tab's scrollarea. */
static struct yetty_ycore_void_result build_yreadme_content(struct app *app,
                                                            struct yetty_yclass_object *root)
{
    (void)app;

    yr_md_section(root, "Overview",
                  "# ymarkdown\n"
                  "\n"
                  "Each section below renders one **markdown** feature.\n"
                  "Click a section header to *fold* it away, then click\n"
                  "again to bring it back.\n");

    yr_md_section(root, "Headings",
                  "# Heading 1\n"
                  "## Heading 2\n"
                  "### Heading 3\n"
                  "#### Heading 4\n"
                  "##### Heading 5\n"
                  "###### Heading 6\n"
                  "\n"
                  "Paragraph text sits between headings at the base size.\n");

    yr_md_section(root, "Inline styles",
                  "Markdown runs can be **bold**, *italic*, or\n"
                  "***bold and italic*** at once.\n"
                  "\n"
                  "Inline `code` sits on its own background box, text can\n"
                  "be ~~struck through~~, and a [link](https://example.com)\n"
                  "is drawn in the accent colour.\n");

    yr_md_section(root, "Lists",
                  "Bulleted list:\n"
                  "\n"
                  "- first bullet\n"
                  "- second bullet with **emphasis**\n"
                  "- third bullet\n"
                  "\n"
                  "Ordered list:\n"
                  "\n"
                  "1. step one\n"
                  "2. step two\n"
                  "3. step three\n");

    yr_md_section(root, "Blockquotes",
                  "> Terminals can show rich content, not just text.\n"
                  "> > Nested quotes get their own accent gutter bar.\n"
                  "\n"
                  "Body text resumes after the quote.\n");

    yr_md_section(root, "Tables",
                  "Per-column alignment with a drawn grid:\n"
                  "\n"
                  "| Feature     | Status | Notes                |\n"
                  "|:------------|:------:|---------------------:|\n"
                  "| Headings    |   ok   |          six levels  |\n"
                  "| Tables      |   ok   |   aligned + bordered |\n"
                  "| Code blocks |   ok   |       shared panel   |\n");

    yr_md_section(root, "Code blocks",
                  "Fenced blocks render verbatim on a shared panel:\n"
                  "\n"
                  "```\n"
                  "fn main() {\n"
                  "    println!(\"hello, ymarkdown\");\n"
                  "}\n"
                  "```\n");

    yr_md_section(root, "Horizontal rules",
                  "Text above the rule.\n"
                  "\n"
                  "---\n"
                  "\n"
                  "Text below the rule.\n");

    return YETTY_OK_VOID();
}

/* Add one ydiagram leaf under `sec`, fed `mermaid`. The widget self-sizes
 * its layout box to the diagram's intrinsic extent in set_source, so no
 * height is authored here. */
static void diag_add(struct yetty_yclass_object *sec, const char *mermaid)
{
    if (!sec) {
        return;
    }
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(sec, yetty_ygui_ydiagram_class_get().value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_ydiagram_set_source(r.value, mermaid));
}

/* Diagrams scrollarea — mirrors demo/ygui/36_ydiagram: one collapsing_header
 * per diagram kind, each holding a single ydiagram fed a different bit of
 * Mermaid source. el_finalize_section reserves the section height from the
 * widget's self-negotiated extent, so each diagram fits its header. */
static struct yetty_ycore_void_result build_diagrams_content(struct app *app,
                                                             struct yetty_yclass_object *root)
{
    (void)app;
    const struct {
        const char *title;
        const char *src;
    } sections[] = {
        {"Flowchart (top-down)", "graph TD\n"
                                 "  A[Start] --> B{Decision}\n"
                                 "  B -->|Yes| C(Process)\n"
                                 "  B -->|No|  D((Done))\n"
                                 "  C --> D\n"},
        {"Pipeline (left-to-right)", "graph LR\n"
                                     "  A[node a] --> B[node b]\n"
                                     "  B --> C[node c]\n"
                                     "  C --> D[node d]\n"
                                     "  A --> D\n"},
        {"Node shapes", "graph TD\n"
                        "  R[rectangle]\n"
                        "  RR(rounded)\n"
                        "  C((circle))\n"
                        "  D{diamond}\n"
                        "  H{{hexagon}}\n"
                        "  CY[(cylinder)]\n"
                        "  S([stadium])\n"
                        "  PR[/parallelogram/]\n"
                        "  R  --> RR\n"
                        "  RR --> C\n"
                        "  C  --> D\n"
                        "  D  --> H\n"
                        "  H  --> CY\n"
                        "  CY --> S\n"
                        "  S  --> PR\n"},
        {"Subgraphs / clusters", "graph TD\n"
                                 "  subgraph frontend [Frontend]\n"
                                 "    UI[UI layer]\n"
                                 "    API[API client]\n"
                                 "    UI --> API\n"
                                 "  end\n"
                                 "  subgraph backend [Backend services]\n"
                                 "    Gateway[Gateway]\n"
                                 "    Auth[Auth]\n"
                                 "    Store[(Store)]\n"
                                 "    Gateway --> Auth\n"
                                 "    Auth    --> Store\n"
                                 "  end\n"
                                 "  API --> Gateway\n"},
        {"State machine", "flowchart TD\n"
                          "  Start((start)) --> Idle[Idle]\n"
                          "  Idle -->|connect|     Connecting{{Connecting}}\n"
                          "  Connecting -->|ok|    Ready(Ready)\n"
                          "  Connecting -->|fail|  Failed[/Failed/]\n"
                          "  Ready  -->|disconnect| Idle\n"
                          "  Ready  -->|crash|      Failed\n"
                          "  Failed -->|retry|      Connecting\n"
                          "  Failed -->|abort|      Done((done))\n"},
        {"Sequence diagram", "sequenceDiagram\n"
                             "  participant C as Client\n"
                             "  participant S as Server\n"
                             "  participant D as Database\n"
                             "  C->>S: GET /profile\n"
                             "  S->>D: SELECT user\n"
                             "  D-->>S: row\n"
                             "  S-->>C: 200 OK\n"
                             "  Note over C,S: cached for 60 s\n"
                             "  C->>C: render page\n"},
        {"Class diagram", "classDiagram\n"
                          "  class Shape {\n"
                          "    <<interface>>\n"
                          "    +area() float\n"
                          "    +draw()\n"
                          "  }\n"
                          "  class Circle {\n"
                          "    +float radius\n"
                          "    +area() float\n"
                          "  }\n"
                          "  class Rectangle {\n"
                          "    +float width\n"
                          "    +float height\n"
                          "    +area() float\n"
                          "  }\n"
                          "  class Canvas {\n"
                          "    +add(Shape)\n"
                          "    +render()\n"
                          "  }\n"
                          "  Shape <|-- Circle\n"
                          "  Shape <|-- Rectangle\n"
                          "  Canvas o-- Shape : holds\n"
                          "  Canvas ..> Renderer : uses\n"},
        {"Entity-relationship diagram", "erDiagram\n"
                                        "  CUSTOMER ||--o{ ORDER : places\n"
                                        "  ORDER ||--|{ LINE-ITEM : contains\n"
                                        "  CUSTOMER }o..o| ADDRESS : ships-to\n"
                                        "  CUSTOMER {\n"
                                        "    string name\n"
                                        "    string id PK\n"
                                        "  }\n"
                                        "  ORDER {\n"
                                        "    int number PK\n"
                                        "    date created\n"
                                        "  }\n"
                                        "  LINE-ITEM {\n"
                                        "    string sku\n"
                                        "    int quantity\n"
                                        "  }\n"},
        {"State diagram", "stateDiagram-v2\n"
                          "  state \"Waiting to retry\" as Backoff\n"
                          "  [*] --> Idle\n"
                          "  Idle --> Connecting : connect\n"
                          "  Connecting --> Ready : ok\n"
                          "  Connecting --> Backoff : fail\n"
                          "  Backoff --> Connecting : retry\n"
                          "  Backoff --> [*] : abort\n"
                          "  Ready --> Idle : disconnect\n"
                          "  Ready --> [*] : quit\n"},
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        struct yetty_yclass_object *sec = el_section(root, sections[i].title);
        diag_add(sec, sections[i].src);
        el_finalize_section(sec);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Circuit tab — electric-circuit schematics. ycircuit parses a line-based
 * netlist DSL into a ydraw drawable list (GPU-free SDF prims + MSDF text),
 * exactly like ydiagram does for Mermaid, so the generic `ydraw_embed`
 * widget paints it 1:1. One collapsing_header per example. DSL reference:
 * src/yetty/ycircuit/README.md.
 *===========================================================================*/

/* Grid pitch (px per grid unit) for the showcase schematics. The DSL coords
 * are in grid units; ~20 px keeps the examples readable without overflowing
 * a collapsing section. */
#define YGREETER_CIRCUIT_GRID_PX 20.0f

static void circuit_add(struct yetty_yclass_object *sec, const char *dsl)
{
    if (!sec) {
        return;
    }
    struct yetty_yclass_object_ptr_result wr =
        yetty_ygui_widget_add(sec, yetty_ygui_ydraw_embed_class_get().value);
    if (YETTY_IS_ERR(wr)) {
        yetty_ycore_error_destroy(wr.error);
        return;
    }
    struct yetty_yclass_object *widget = wr.value;

    /* create → configure(grid pitch) → parse(DSL) → render → drop model.
     * The rendered list owns its own bytes, so the circuit model is freed
     * immediately and ydraw_embed takes ownership of the list below. */
    struct yetty_yclass_object_ptr_result cr = yetty_ycircuit_circuit_create(NULL);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return;
    }
    struct yetty_yclass_object *circuit = cr.value;
    yetty_ycore_error_destroy_safe(
        yetty_ycircuit_configure(circuit, YGREETER_CIRCUIT_GRID_PX, YETTY_YCIRCUIT_FLAG_NONE));
    yetty_ycore_error_destroy_safe(yetty_ycircuit_parse(circuit, dsl, strlen(dsl)));
    struct yetty_ydraw_drawable_list_result lr = yetty_ycircuit_render(circuit);
    yetty_ycore_error_destroy_safe(yetty_ycircuit_destroy(circuit));
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }

    /* Size the widget to the schematic's intrinsic extent — ydraw_embed
     * paints 1:1 with no scale-to-fit, so the layout must reserve exactly
     * the drawable list's scene bounds (same negotiation ydiagram does). */
    float w = yetty_ydraw_drawable_list_scene_max_x(lr.value) -
              yetty_ydraw_drawable_list_scene_min_x(lr.value);
    float h = yetty_ydraw_drawable_list_scene_max_y(lr.value) -
              yetty_ydraw_drawable_list_scene_min_y(lr.value);
    if (w > 0.0f && h > 0.0f) {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(widget);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return;
        }
        struct yetty_ygui_layout layout = *layout_res.value;
        layout.width = w;
        layout.height = h;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(widget, &layout));
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_ydraw_embed_set_buffer(widget, lr.value));
}

static struct yetty_ycore_void_result build_circuit_content(struct app *app,
                                                            struct yetty_yclass_object *root)
{
    (void)app;
    /* Idempotent: registers the ycircuit:circuit class on first call. */
    yetty_ycore_error_destroy_safe(yetty_ycircuit_register());

    static const struct {
        const char *title;
        const char *src;
    } sections[] = {
        {"Resistive voltage divider", "circuit Voltage divider\n"
                                      "battery   2  7  r270  V1  9V\n"
                                      "wire 2 4  8 4\n"
                                      "resistor  8  7  v     R1  10k\n"
                                      "dot 8 10\n"
                                      "wire 8 10  11 10\n"
                                      "label 11.5 10.3 Vout\n"
                                      "resistor  8 13  v     R2  4.7k\n"
                                      "wire 2 10  2 16  8 16\n"
                                      "gnd 5 16\n"
                                      "dot 5 16\n"},
        {"RC low-pass filter", "circuit RC low-pass filter\n"
                               "acsource  2  8  v  AC1\n"
                               "label 3 2.3 Vin\n"
                               "wire 2 5  2 3  5 3\n"
                               "resistor  8  3  h  R1  1k\n"
                               "wire 11 3  14 3\n"
                               "dot 14 3\n"
                               "capacitor 14  6  v  C1  100n\n"
                               "wire 14 3  17 3\n"
                               "label 17.5 3.3 Vout\n"
                               "wire 2 11  2 12  14 12\n"
                               "wire 14 9  14 12\n"
                               "gnd 8 12\n"
                               "dot 8 12\n"},
        {"Half-wave rectifier", "circuit Half-wave rectifier\n"
                                "acsource  2  8  v  AC1  50Hz\n"
                                "wire 2 5  2 3  5 3\n"
                                "diode     8  3  h  D1  1N4007\n"
                                "wire 11 3  17 3\n"
                                "dot 14 3\n"
                                "capacitor 14  6  v  C1  470u\n"
                                "resistor  17  6  v  R1  2.2k\n"
                                "label 18 2.4 Vdc\n"
                                "wire 2 11  2 12  17 12\n"
                                "wire 14 9  14 12\n"
                                "dot 14 12\n"
                                "wire 17 9  17 12\n"
                                "gnd 8 12\n"
                                "dot 8 12\n"},
        {"Common-emitter amplifier", "circuit Common-emitter amplifier\n"
                                     "vcc 10 0\n"
                                     "wire 4 0  16 0\n"
                                     "dot 10 0\n"
                                     "resistor  4  3  v  R1  47k\n"
                                     "resistor  4  9  v  R2  10k\n"
                                     "dot 4 6\n"
                                     "wire 4 6  8 6  8 9  12 9\n"
                                     "label 9 8.3 Vin\n"
                                     "resistor 16  3  v  RC  2.2k\n"
                                     "npn      15  9  h  Q1  BC547\n"
                                     "dot 16 6\n"
                                     "wire 16 6  19 6\n"
                                     "label 19.5 6.3 Vout\n"
                                     "resistor 16 15  v  RE  1k\n"
                                     "wire 4 12  4 18  16 18\n"
                                     "gnd 10 18\n"
                                     "dot 10 18\n"},
        {"Inverting op-amp", "circuit Inverting amplifier\n"
                             "label 1 4.3 Vin\n"
                             "wire 1.5 5  3 5\n"
                             "resistor 6 5 h R1 10k\n"
                             "wire 9 5  11 5\n"
                             "dot 10 5\n"
                             "wire 10 5  10 1  11 1\n"
                             "resistor 14 1 h R2 100k\n"
                             "wire 17 1  18 1  18 6\n"
                             "dot 18 6\n"
                             "opamp 14 6 h U1\n"
                             "wire 11 7  10 7  10 9\n"
                             "gnd 10 9\n"
                             "wire 17 6  21 6\n"
                             "label 21.5 6.3 Vout\n"},
        {"NE555 astable blinker", "circuit 555 astable blinker\n"
                                  "ic 14 8 h U1 NE555 l:GND,TRIG,OUT,RESET r:VCC,DIS,THR,CV\n"
                                  "wire 8 2  25 2\n"
                                  "vcc 13 2\n"
                                  "dot 13 2\n"
                                  "wire 4 21  25 21\n"
                                  "gnd 14 21\n"
                                  "dot 14 21\n"
                                  "wire 17.8 5.6  22 5.6  22 2\n"
                                  "dot 22 2\n"
                                  "wire 10.2 10.4  8 10.4  8 2\n"
                                  "wire 10.2 5.6  6 5.6  6 21\n"
                                  "dot 6 21\n"
                                  "resistor 25 5 v R1 10k\n"
                                  "wire 17.8 7.2  21 7.2  21 8  25 8\n"
                                  "dot 25 8\n"
                                  "resistor 25 11 v R2 47k\n"
                                  "wire 17.8 8.8  23 8.8  23 14\n"
                                  "wire 10.2 7.2  9 7.2  9 12.5  23 12.5  23 14\n"
                                  "dot 23 14\n"
                                  "wire 23 14  25 14\n"
                                  "dot 25 14\n"
                                  "wire 25 14  25 15\n"
                                  "capacitor 25 18 v C1 10u\n"
                                  "wire 10.2 8.8  4 8.8  4 9\n"
                                  "resistor 4 12 v R3 330\n"
                                  "led 4 18 r90 D1 red\n"},
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        struct yetty_yclass_object *sec = el_section(root, sections[i].title);
        circuit_add(sec, sections[i].src);
        el_finalize_section(sec);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Music tab — engraved musical scores. ymusic (the module ycircuit was
 * modelled on) parses a LilyPond-subset score into a ydraw drawable list of
 * staff lines / note heads / stems / beams plus Emmentaler glyph spans, which
 * the generic `ydraw_embed` widget paints 1:1 — same hosting pattern as the
 * Circuit tab. The Emmentaler music font ships as /data/msdf-fonts/
 * Emmentaler.cdb (staged for webasm, installed on desktop). DSL reference:
 * src/yetty/ymusic/README.md.
 *===========================================================================*/

/* System width (px) the engraver wraps measures into, and the staff-line gap
 * (em = 4 × staff_space). Sized for the showcase's scrollarea — readable but
 * compact enough that a few measures fit before wrapping. */
#define YGREETER_MUSIC_WIDTH 760.0f
#define YGREETER_MUSIC_STAFF 13.0f

static void music_add(struct yetty_yclass_object *sec, const char *score)
{
    if (!sec) {
        return;
    }
    struct yetty_yclass_object_ptr_result wr =
        yetty_ygui_widget_add(sec, yetty_ygui_ydraw_embed_class_get().value);
    if (YETTY_IS_ERR(wr)) {
        yetty_ycore_error_destroy(wr.error);
        return;
    }
    struct yetty_yclass_object *widget = wr.value;

    /* create → configure(width, staff space) → parse(score) → render → drop
     * model. The rendered list owns its bytes, so the model is freed at once
     * and ydraw_embed takes ownership of the list. */
    struct yetty_yclass_object_ptr_result mr = yetty_ymusic_music_create(NULL);
    if (YETTY_IS_ERR(mr)) {
        yetty_ycore_error_destroy(mr.error);
        return;
    }
    struct yetty_yclass_object *music = mr.value;
    yetty_ycore_error_destroy_safe(yetty_ymusic_configure(
        music, YGREETER_MUSIC_WIDTH, YGREETER_MUSIC_STAFF, YETTY_YMUSIC_FLAG_NONE));
    yetty_ycore_error_destroy_safe(yetty_ymusic_parse(music, score, strlen(score)));
    struct yetty_ydraw_drawable_list_result lr = yetty_ymusic_render(music);
    yetty_ycore_error_destroy_safe(yetty_ymusic_destroy(music));
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }

    float w = yetty_ydraw_drawable_list_scene_max_x(lr.value) -
              yetty_ydraw_drawable_list_scene_min_x(lr.value);
    float h = yetty_ydraw_drawable_list_scene_max_y(lr.value) -
              yetty_ydraw_drawable_list_scene_min_y(lr.value);
    if (w > 0.0f && h > 0.0f) {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(widget);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return;
        }
        struct yetty_ygui_layout layout = *layout_res.value;
        layout.width = w;
        layout.height = h;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(widget, &layout));
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_ydraw_embed_set_buffer(widget, lr.value));
}

static struct yetty_ycore_void_result build_music_content(struct app *app,
                                                          struct yetty_yclass_object *root)
{
    (void)app;
    /* Idempotent: registers the ymusic:music class on first call. */
    yetty_ycore_error_destroy_safe(yetty_ymusic_register());

    static const struct {
        const char *title;
        const char *src;
    } sections[] = {
        /* The canonical example shipped with the repo — treble clef, D major
         * key signature, common time, octave changes and dotted rhythms. */
        {"Ode to Joy — D major",
         "\\relative c'' {\n"
         "  \\clef treble\n"
         "  \\key d \\major\n"
         "  \\time 4/4\n"
         "  fis4 fis g a    | a g fis e      | d d e fis      | fis4. e8 e2    |\n"
         "  fis4 fis g a    | a g fis e      | d d e fis      | e4. d8 d2      |\n"
         "  e4 e fis d      | e fis8 g fis4 d | e fis8 g fis4 e | d e a,2       |\n"
         "}\n"},
        /* Two-octave C-major scale up and down — the plainest staff, no
         * accidentals, eighth-note runs that inherit their duration. */
        {"C major scale (treble)", "\\relative c' {\n"
                                   "  \\clef treble \\key c \\major \\time 4/4\n"
                                   "  c8 d e f g a b c | c b a g f e d c |\n"
                                   "}\n"},
        /* Bass clef — a descending line; exercises the second clef glyph and
         * ledger-free notes below the treble range. */
        {"Bass clef — descending line", "\\relative c {\n"
                                        "  \\clef bass \\key c \\major \\time 4/4\n"
                                        "  c4 b a g | f e d c | g g c2 |\n"
                                        "}\n"},
        /* A flat key in compound time — B-flat major (two flats) in 6/8,
         * arpeggiated, to show the key-signature accidentals and a non-4/4
         * meter. */
        {"B-flat major, 6/8",
         "\\relative c' {\n"
         "  \\clef treble \\key bes \\major \\time 6/8\n"
         "  bes8 d f bes f d | c es g c g es | d f bes d bes f | bes4. bes,4. |\n"
         "}\n"},
        /* Stacked pitches — triads and a seventh built as chords inside angle
         * brackets, then held as half notes. */
        {"Chords & triads", "\\relative c' {\n"
                            "  \\clef treble \\key c \\major \\time 4/4\n"
                            "  <c e g>4 <d f a> <e g b> <f a c> | <g b d f>2 <c, e g>2 |\n"
                            "}\n"},
        /* Every accidental the engraver draws — single sharp/flat, double
         * sharp (isis) and double flat (eses), against the natural key. */
        {"Accidentals — sharps, flats, doubles", "\\relative c' {\n"
                                                 "  \\clef treble \\time 4/4\n"
                                                 "  cis4 des e f | fis ges aisis beses | c1 |\n"
                                                 "}\n"},
        /* The rhythmic vocabulary — whole through sixteenth, a dotted figure
         * and a rest, so every note-head/flag/dot/rest glyph appears. */
        {"Rhythms & rests", "\\relative c' {\n"
                            "  \\clef treble \\key c \\major \\time 4/4\n"
                            "  c1 | c2 c2 | c4 c c c | c8 c c c c c c c | r4 c8. c16 c4 r4 |\n"
                            "}\n"},
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        struct yetty_yclass_object *sec = el_section(root, sections[i].title);
        music_add(sec, sections[i].src);
        el_finalize_section(sec);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Node-editor tab — the exact scene from demo/ygui/38_ynodes: a ynodes
 * editor filling the body, three nodes holding ordinary widgets, two
 * pre-wired links, and a palette the node context menu can insert.
 *===========================================================================*/
static struct yetty_yclass_object *yng_make_node(struct yetty_yclass_object *editor, float gx,
                                                 float gy, float gw, float gh, const char *title)
{
    struct yetty_yclass_object_ptr_result nr = yetty_ygui_ynodes_add_node(editor, gx, gy);
    if (YETTY_IS_ERR(nr)) {
        yetty_ycore_error_destroy(nr.error);
        return NULL;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_ynode_set_graph_size(nr.value, gw, gh));
    yetty_ycore_error_destroy_safe(yetty_ygui_ynode_set_title(nr.value, title));
    return nr.value;
}

static void yng_reg(struct yetty_yclass_object *editor, const char *name,
                    struct yetty_yclass_ptr_result cls)
{
    if (YETTY_IS_ERR(cls)) {
        yetty_ycore_error_destroy(cls.error);
        return;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_ynodes_register_widget(editor, name, cls.value));
}

static void yng_u32(struct uint32_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Add a child widget of `cls` to `node` and set its row height. */
static struct yetty_yclass_object *yng_child(struct yetty_yclass_object *node,
                                             struct yetty_yclass_ptr_result cls, float h)
{
    if (YETTY_IS_ERR(cls)) {
        yetty_ycore_error_destroy(cls.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(node, cls.value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    yetty_ycore_error_destroy_safe(el_set_height(r.value, h));
    return r.value;
}

static struct yetty_ycore_void_result build_ynodes_content(struct app *app,
                                                           struct yetty_yclass_object *parent)
{
    (void)app;
    const struct yetty_ycore_rgba text = {224, 229, 228, 255};
    const struct yetty_ycore_rgba muted = {168, 167, 159, 255};

    /* Instruction strip. */
    struct yetty_yclass_object_ptr_result hint =
        yetty_ygui_widget_add(parent, yetty_ygui_label_class_get().value);
    if (YETTY_IS_OK(hint)) {
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(
            hint.value,
            "drag nodes  \xe2\x80\xa2  drag pin\xe2\x86\x92pin to connect  \xe2\x80\xa2  "
            "right-click for menu  \xe2\x80\xa2  pan: drag canvas  \xe2\x80\xa2  wheel: zoom"));
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_color(hint.value, muted));
        yetty_ycore_error_destroy_safe(el_set_height(hint.value, 24.0f));
    } else {
        yetty_ycore_error_destroy(hint.error);
    }

    /* Editor fills the rest of the tab body. */
    struct yetty_yclass_object_ptr_result er =
        yetty_ygui_widget_add(parent, yetty_ygui_ynodes_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "build_ynodes_content: ynodes");
    struct yetty_yclass_object *editor = er.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(editor);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res,
                            "build_ynodes_content: editor layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.min_height = 200.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(editor, &l));
    }

    /* Palette the node context menu can insert ("Add <name>"). */
    yng_reg(editor, "Label", yetty_ygui_label_class_get());
    yng_reg(editor, "Button", yetty_ygui_button_class_get());
    yng_reg(editor, "Slider", yetty_ygui_slider_class_get());
    yng_reg(editor, "Checkbox", yetty_ygui_checkbox_class_get());
    yng_reg(editor, "Toggle", yetty_ygui_toggle_class_get());
    yng_reg(editor, "Progress", yetty_ygui_progress_class_get());

    /* Node A — source with one output. */
    struct yetty_yclass_object *a = yng_make_node(editor, 60.0f, 70.0f, 190.0f, 130.0f, "Source");
    if (a) {
        yng_u32(yetty_ygui_ynode_add_output(a, "value"));
        struct yetty_yclass_object *lbl = yng_child(a, yetty_ygui_label_class_get(), 18.0f);
        if (lbl) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(lbl, "amplitude"));
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_color(lbl, text));
        }
        struct yetty_yclass_object *sld = yng_child(a, yetty_ygui_slider_class_get(), 26.0f);
        if (sld) {
            yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_range(sld, 0.0f, 1.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_value(sld, 0.6f));
        }
    }

    /* Node B — mixer: two inputs, one output, a couple of controls. */
    struct yetty_yclass_object *b = yng_make_node(editor, 350.0f, 130.0f, 200.0f, 160.0f, "Mixer");
    if (b) {
        yng_u32(yetty_ygui_ynode_add_input(b, "a"));
        yng_u32(yetty_ygui_ynode_add_input(b, "b"));
        yng_u32(yetty_ygui_ynode_add_output(b, "out"));
        struct yetty_yclass_object *chk = yng_child(b, yetty_ygui_checkbox_class_get(), 24.0f);
        if (chk) {
            yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_label(chk, "enabled"));
            yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_checked(chk, 1));
        }
        struct yetty_yclass_object *btn = yng_child(b, yetty_ygui_button_class_get(), 32.0f);
        if (btn) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(btn, "Apply"));
        }
    }

    /* Node C — sink with one input. */
    struct yetty_yclass_object *c = yng_make_node(editor, 680.0f, 90.0f, 180.0f, 120.0f, "Output");
    if (c) {
        yng_u32(yetty_ygui_ynode_add_input(c, "result"));
        struct yetty_yclass_object *lbl = yng_child(c, yetty_ygui_label_class_get(), 18.0f);
        if (lbl) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(lbl, "preview"));
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_color(lbl, muted));
        }
        struct yetty_yclass_object *btn = yng_child(c, yetty_ygui_button_class_get(), 32.0f);
        if (btn) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(btn, "Save"));
        }
    }

    /* Pre-wire: Source.value → Mixer.a, Mixer.out → Output.result. */
    if (a && b) {
        yetty_ycore_error_destroy_safe(yetty_ygui_ynodes_link(editor, a, 0, b, 0));
    }
    if (b && c) {
        yetty_ycore_error_destroy_safe(yetty_ygui_ynodes_link(editor, b, 0, c, 0));
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_scene_body(struct app *app,
                                                       struct yetty_yclass_object *parent,
                                                       int tab_index)
{
    /* Wipe + reseed `parent`: nav on left + fresh content widget on right.
     * `parent` is body_panel for a single-scene tab, or a group's subbody. */
    app->scene_parent = parent;
    app->cur_scene = tab_index;
    while (1) {
        struct yetty_yclass_object_ptr_result first_child_res =
            yetty_ygui_widget_first_child(parent);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, first_child_res, "build_scene_body: first_child");
        struct yetty_yclass_object *c = first_child_res.value;
        if (!c) {
            break;
        }
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_destroy(c));
    }

    /* Recycle the row_link arena. Every row_link belongs to a nav button
     * that was just destroyed above (widget_destroy is synchronous), so no
     * live click handler still points into the arena. Resetting here stops
     * it growing without bound across scene switches AND keeps it from ever
     * reallocating (a scene has only a handful of nav rows), which would
     * otherwise dangle the interior pointers handed to live buttons. */
    g_row_links.count = 0;

    struct tab_state *t = &app->tabs[tab_index];
    t->kind = tab_kind_for(tab_index);
    int n = tab_entry_count(app, tab_index);
    t->n_entries = n;
    if (t->active_entry < 0 || t->active_entry >= n) {
        t->active_entry = 0;
    }

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
    case TAB_KIND_DIAGRAMS:
    case TAB_KIND_YMAZE:
    case TAB_KIND_YZOO:
    case TAB_KIND_YJUNGLE:
    case TAB_KIND_YSHADERTOY:
    case TAB_KIND_YNODES:
    case TAB_KIND_YPDF:
    case TAB_KIND_YCIRCUIT:
    case TAB_KIND_YMUSIC:
        has_nav = false;
        break;
    default:
        has_nav = true;
        break;
    }

    /* Outer hbox: nav + content side-by-side (or just content). */
    struct yetty_yclass_object_ptr_result hr =
        yetty_ygui_widget_add(parent, yetty_ygui_hbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "rebuild: hbox");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: hbox layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.gap = has_nav ? 12.0f : 0.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(hr.value, &l));
    }

    if (has_nav) {
        /* Nav vbox — fixed 220-px wide column of clickable rows. */
        struct yetty_yclass_object_ptr_result nr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_vbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, nr, "rebuild: nav vbox");
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(nr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: nav vbox layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.width = 220.0f;
            l.gap = 4.0f;
            l.padding_top = l.padding_bottom = 4.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(nr.value, &l));
        }
        for (int i = 0; i < n; ++i) {
            struct yetty_yclass_object_ptr_result br =
                yetty_ygui_widget_add(nr.value, yetty_ygui_button_class_get().value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "rebuild: nav button");
            yetty_ycore_error_destroy_safe(
                yetty_ygui_button_set_label(br.value, tab_entry_label(app, tab_index, i)));
            {
                struct yetty_ygui_layout_const_ptr_result layout_res =
                    yetty_ygui_widget_layout_get(br.value);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: nav button layout_get");
                struct yetty_ygui_layout l = *layout_res.value;
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
    struct yetty_yclass_object *content = NULL;
    switch (t->kind) {
    case TAB_KIND_PLOTS: {
        struct yetty_yclass_object_ptr_result pr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_yplot_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rebuild: yplot");
        content = pr.value;
        break;
    }
    case TAB_KIND_IMAGES: {
        struct yetty_yclass_object_ptr_result ir =
            yetty_ygui_widget_add(hr.value, yetty_ygui_yimage_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "rebuild: yimage");
        content = ir.value;
        break;
    }
    case TAB_KIND_VIDEO: {
        struct yetty_yclass_object_ptr_result vr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_yvideo_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "rebuild: yvideo");
        content = vr.value;
        break;
    }
    case TAB_KIND_ELEMENTS:
    case TAB_KIND_DIAGRAMS:
    case TAB_KIND_YCIRCUIT:
    case TAB_KIND_YMUSIC: {
        struct yetty_yclass_object_ptr_result sr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_scrollarea_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "rebuild: scrollarea");
        content = sr.value;
        break;
    }
    case TAB_KIND_YMAZE: {
        struct yetty_yclass_object_ptr_result zr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_ymaze_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "rebuild: ymaze");
        content = zr.value;
        break;
    }
    case TAB_KIND_YZOO: {
        struct yetty_yclass_object_ptr_result zr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_yzoo_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "rebuild: yzoo");
        content = zr.value;
        break;
    }
    case TAB_KIND_YJUNGLE: {
        struct yetty_yclass_object_ptr_result zr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_yjungle_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "rebuild: yjungle");
        content = zr.value;
        break;
    }
    case TAB_KIND_YSHADERTOY: {
        /* vbox(sub-tabbar, yshadertoy): same gallery as demo 41. The
         * sub-tabbar swaps the WGSL source on the hosted yshadertoy
         * widget; the host-side figure injects iResolution/iTime/iMouse
         * and runs mainImage() on a full-rect quad, animating itself.
         * Shaders: <yetty/yshadertoy/demo-shaders.h>. */
        struct yetty_yclass_object_ptr_result vr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_vbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "rebuild: shadertoy vbox");
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(vr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: shadertoy vbox layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.flex_grow = 1.0f;
            l.gap = 6.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(vr.value, &l));
        }
        struct yetty_yclass_object_ptr_result str =
            yetty_ygui_widget_add(vr.value, yetty_ygui_tabbar_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, str, "rebuild: shadertoy sub-tabbar");
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(str.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res,
                                "rebuild: shadertoy sub-tabbar layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 32.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(str.value, &l));
        }
        for (int i = 0; i < yetty_yshadertoy_demo_shader_count; i++) {
            struct yetty_yclass_object_ptr_result hh =
                yetty_ygui_tabbar_add_tab(str.value, yetty_yshadertoy_demo_shaders[i].label);
            if (YETTY_IS_ERR(hh)) {
                yetty_ycore_error_destroy(hh.error);
            }
        }
        struct yetty_yclass_object_ptr_result zr =
            yetty_ygui_widget_add(vr.value, yetty_ygui_yshadertoy_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "rebuild: yshadertoy");
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(zr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: yshadertoy layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.flex_grow = 1.0f;
            l.min_height = 200.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(zr.value, &l));
        }
        g_shadertoy_widget = zr.value;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_subscribe(
            str.value, YETTY_YGUI_EVENT_VALUE_CHANGED, on_shadertoy_subtab, app));
        shadertoy_apply(0);
        content = vr.value;
        break;
    }
    case TAB_KIND_YNODES: {
        /* vbox(hint, ynodes-editor) — the editor scene is seeded below by
         * build_ynodes_content (mirrors demo/ygui/38_ynodes). */
        struct yetty_yclass_object_ptr_result vr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_vbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "rebuild: ynodes vbox");
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(vr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: ynodes vbox layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.flex_grow = 1.0f;
            l.gap = 6.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(vr.value, &l));
        }
        content = vr.value;
        break;
    }
    case TAB_KIND_YPDF: {
        struct yetty_yclass_object_ptr_result pr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_ypdf_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rebuild: ypdf");
        if (app->pdf_path) {
            yetty_ycore_error_destroy_safe(yetty_ygui_ypdf_set_file(pr.value, app->pdf_path));
        }
        content = pr.value;
        break;
    }
    case TAB_KIND_YREADME: {
        /* Scrollarea hosts the per-feature collapsing sections, same
         * shape as the Elements and YBrowser tabs. */
        struct yetty_yclass_object_ptr_result sr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_scrollarea_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "rebuild: yreadme scrollarea");
        content = sr.value;
        break;
    }
    case TAB_KIND_YBROWSER: {
        /* Scrollarea hosts the per-scenario collapsing sections, same
         * shape as the Elements tab. */
        struct yetty_yclass_object_ptr_result sr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_scrollarea_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "rebuild: ybrowser scrollarea");
        content = sr.value;
        break;
    }
    case TAB_KIND_RICH:
    default: {
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_add(hr.value, yetty_ygui_rich_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild: rich");
        content = rr.value;
        break;
    }
    }
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(content);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild: content layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.min_height = 200.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(content, &l));
    }
    t->content = content;

    /* Seed the content with the active entry. */
    switch (t->kind) {
    case TAB_KIND_PLOTS: {
        const struct nav_entry *e = &plot_nav_entries[t->active_entry];
        yetty_ycore_error_destroy_safe(load_plot_entry((struct yetty_yclass_object *)content, e));
        break;
    }
    case TAB_KIND_IMAGES: {
        const char *path = app->image_path_count > 0 && t->active_entry < app->image_path_count
                               ? app->image_paths[t->active_entry]
                               : NULL;
        yetty_ycore_error_destroy_safe(
            load_image_entry((struct yetty_yclass_object *)content, path));
        break;
    }
    case TAB_KIND_VIDEO: {
        const char *path = app->video_path_count > 0 && t->active_entry < app->video_path_count
                               ? app->video_paths[t->active_entry]
                               : NULL;
        yetty_ycore_error_destroy_safe(
            load_video_entry((struct yetty_yclass_object *)content, path));
        break;
    }
    case TAB_KIND_ELEMENTS:
        yetty_ycore_error_destroy_safe(build_elements_content(app, content));
        break;
    case TAB_KIND_DIAGRAMS:
        yetty_ycore_error_destroy_safe(build_diagrams_content(app, content));
        break;
    case TAB_KIND_YCIRCUIT:
        yetty_ycore_error_destroy_safe(build_circuit_content(app, content));
        break;
    case TAB_KIND_YMUSIC:
        yetty_ycore_error_destroy_safe(build_music_content(app, content));
        break;
    case TAB_KIND_YNODES:
        yetty_ycore_error_destroy_safe(build_ynodes_content(app, content));
        break;
    case TAB_KIND_YMAZE:
    case TAB_KIND_YZOO:
    case TAB_KIND_YJUNGLE:
    case TAB_KIND_YSHADERTOY:
    case TAB_KIND_YPDF:
        /* Self-contained: these widgets create + animate (or load) their
         * own scene when the content widget was built above (yshadertoy's
         * source / ypdf's file were set there), so there is nothing to
         * seed. */
        break;
    case TAB_KIND_YREADME:
        yetty_ycore_error_destroy_safe(build_yreadme_content(app, content));
        break;
    case TAB_KIND_YBROWSER:
        yetty_ycore_error_destroy_safe(build_ybrowser_content(app, content));
        break;
    case TAB_KIND_RICH:
    default: {
        if (tab_index == 0) {
            const struct welcome_nav *e = &welcome_nav_entries[t->active_entry];
            yetty_ycore_error_destroy_safe(
                write_welcome_spans((struct yetty_yclass_object *)content, e->spans, e->n_spans));
        } else {
            yetty_ycore_error_destroy_safe(write_code_snippet(
                (struct yetty_yclass_object *)content, code_nav_entries[t->active_entry].payload));
        }
        break;
    }
    }

    yetty_ygui_framework_mark_dirty(app->engine);
    return YETTY_OK_VOID();
}

/* Sub-tabbar change inside a grouped top tab → rebuild the selected scene
 * into the group's subbody (the sub-tabbar itself is left intact). */
static struct yetty_ycore_void_result on_subtab_change(struct yetty_yclass_object *_yc_obj,
                                                       const struct yetty_ygui_event *event,
                                                       void *userdata)
{
    struct app *app = (struct app *)userdata;
    /* Ignore events from a sub-tabbar that is not the active group's — e.g.
     * one being torn down while switching top tabs. */
    if ((struct yetty_yclass_object *)_yc_obj != app->subbar) {
        return YETTY_OK_VOID();
    }
    const struct top_tab *tt = &TOP_TABS[app->cur_top];
    int sub = event->i0;
    if (sub < 0 || sub >= tt->n_subs || !app->subbody) {
        return YETTY_OK_VOID();
    }
    app->top_active_sub[app->cur_top] = sub;
    return build_scene_body(app, app->subbody, tt->subs[sub]);
}

/* Build the body for top tab `top_index`: a single-scene tab fills the
 * body directly; a grouped tab lays out vbox(sub-tabbar, subbody) and
 * builds its active sub-scene into subbody. */
static struct yetty_ycore_void_result rebuild_top(struct app *app, int top_index)
{
    if (top_index < 0 || top_index >= TOP_TAB_COUNT) {
        return YETTY_OK_VOID();
    }
    app->cur_top = top_index;
    app->subbar = NULL;
    app->subbody = NULL;
    const struct top_tab *tt = &TOP_TABS[top_index];

    if (tt->n_subs <= 1) {
        return build_scene_body(app, app->body_panel, tt->subs[0]);
    }

    /* Grouped tab — wipe the body and lay out vbox(sub-tabbar, subbody). */
    while (1) {
        struct yetty_yclass_object_ptr_result first_child_res =
            yetty_ygui_widget_first_child(app->body_panel);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, first_child_res, "rebuild_top: first_child");
        struct yetty_yclass_object *c = first_child_res.value;
        if (!c) {
            break;
        }
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_destroy(c));
    }
    struct yetty_yclass_object_ptr_result vr =
        yetty_ygui_widget_add(app->body_panel, yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "rebuild_top: group vbox");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(vr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild_top: group vbox layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.gap = 6.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(vr.value, &l));
    }
    struct yetty_yclass_object_ptr_result str =
        yetty_ygui_widget_add(vr.value, yetty_ygui_tabbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, str, "rebuild_top: sub-tabbar");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(str.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild_top: sub-tabbar layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 30.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(str.value, &l));
    }
    for (int k = 0; k < tt->n_subs; k++) {
        struct yetty_yclass_object_ptr_result hh =
            yetty_ygui_tabbar_add_tab(str.value, SCENE_LABELS[tt->subs[k]]);
        if (YETTY_IS_ERR(hh)) {
            yetty_ycore_error_destroy(hh.error);
        }
    }
    struct yetty_yclass_object_ptr_result sb =
        yetty_ygui_widget_add(vr.value, yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "rebuild_top: subbody");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(sb.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "rebuild_top: subbody layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(sb.value, &l));
    }
    app->subbar = str.value;
    app->subbody = sb.value;

    int sub = app->top_active_sub[top_index];
    if (sub < 0 || sub >= tt->n_subs) {
        sub = 0;
        app->top_active_sub[top_index] = 0;
    }
    if (sub != 0) {
        yetty_ycore_error_destroy_safe(yetty_ygui_tabbar_set_active(str.value, sub));
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_subscribe(
        str.value, YETTY_YGUI_EVENT_VALUE_CHANGED, on_subtab_change, app));
    return build_scene_body(app, sb.value, tt->subs[sub]);
}

static struct yetty_ycore_void_result on_row_clicked(struct yetty_yclass_object *_yc_obj,
                                                     void *userdata)
{
    struct yetty_yclass_object *btn = (struct yetty_yclass_object *)_yc_obj;
    (void)btn;
    struct row_link *rl = (struct row_link *)userdata;
    if (!rl) {
        return YETTY_OK_VOID();
    }
    /* Ignore stale row-links from a scene that's no longer the one on screen. */
    if (rl->tab != rl->app->cur_scene) {
        return YETTY_OK_VOID();
    }
    rl->app->tabs[rl->tab].active_entry = rl->entry;
    return build_scene_body(rl->app, rl->app->scene_parent, rl->tab);
}

static struct yetty_ycore_void_result on_tab_change(struct yetty_yclass_object *_yc_obj,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    struct yetty_yclass_object *target = (struct yetty_yclass_object *)_yc_obj;
    (void)target;
    int idx = event->i0;
    if (idx < 0 || idx >= TOP_TAB_COUNT) {
        return YETTY_OK_VOID();
    }
    return rebuild_top((struct app *)userdata, idx);
}

/* Title-bar close-x handler — the window widget emits EVENT_CLOSE; we
 * react by running the app's mode-specific stop hook. */
static struct yetty_ycore_void_result build_ui(struct app *app)
{
    /* ygreeter's assets are placed early in startup (installer/bundle on
     * desktop/web; the Android program-init's extractor), so
     * <data_dir>/logo-*.jpeg, yetty-unchained-2.mp4 and README.md are
     * on disk by the time we get here. The three discover_* probes
     * record absolute paths into the app struct for later use by the
     * Images / Video / YReadme tabs. */
    discover_logo_images(app);
    discover_video_files(app);
    discover_readme(app);
    discover_pdf(app);

    /* Root is a plain vbox — the window title bar + close are provided by the
     * real OS-window chrome (ychrome), so no in-canvas window widget. The
     * greeter's own content (its tabbar / body / statusbar) stacks directly in
     * here. Standalone mode insets the top by the chrome caption height (see
     * run_standalone_mode); close arrives via ychrome → window_chrome →
     * WINDOW_CLOSE, handled in the event loop. */
    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "build_ui: root add");
    app->root = rr.value;
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->engine, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build_ui: set_root");

    struct yetty_yclass_object *content = app->root;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(content);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "build_ui: root layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        l.gap = 0.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(content, &l));
    }

    /* Tabbar — Welcome / Plots / Images / Code. */
    struct yetty_yclass_object_ptr_result tbr =
        yetty_ygui_widget_add(content, yetty_ygui_tabbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tbr, "build_ui: tabbar add");
    app->tabbar = tbr.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(app->tabbar);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "build_ui: tabbar layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 36;
        l.gap = 4;
        /* Keep the tabs clear of the window controls (3 × 46 px) that chrome
         * paints flush-right on top of this strip. */
        l.padding_right = 3.0f * 46.0f;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->tabbar, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: tabbar layout");
    }
    for (int i = 0; i < TOP_TAB_COUNT; ++i) {
        struct yetty_yclass_object_ptr_result hr =
            yetty_ygui_tabbar_add_tab(app->tabbar, TOP_TABS[i].label);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "build_ui: tabbar_add_tab");
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "build_ui: header layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.width = 130;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(hr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: header layout");
    }
    /* Body panel — vbox that the per-tab content rebuilds populate. */
    struct yetty_yclass_object_ptr_result bpr =
        yetty_ygui_widget_add(content, yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bpr, "build_ui: body panel add");
    app->body_panel = bpr.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(app->body_panel);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "build_ui: body panel layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1;
        l.padding_top = 8;
        l.padding_left = l.padding_right = 8;
        l.padding_bottom = 4;
        l.gap = 0;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->body_panel, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: body panel layout");
    }

    /* Statusbar — small bottom strip. */
    struct yetty_yclass_object_ptr_result sbr =
        yetty_ygui_widget_add(content, yetty_ygui_statusbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sbr, "build_ui: statusbar add");
    app->statusbar = sbr.value;
    yetty_ycore_error_destroy_safe(
        yetty_ygui_statusbar_set_left(app->statusbar, "ygreeter — q to quit"));
    yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_right(app->statusbar, "v0.4"));

    for (int i = 0; i < TAB_COUNT; ++i) {
        app->tabs[i].active_entry = 0;
        app->tabs[i].kind = tab_kind_for(i);
    }
    /* Subscribe before selecting the start tab: a programmatic set_active
     * emits VALUE_CHANGED, so on_tab_change drives the initial build. That
     * keeps exactly one rebuild path (no second, racing rebuild). */
    struct yetty_ycore_void_result subr = yetty_ygui_widget_subscribe(
        app->tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_change, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, subr, "build_ui: subscribe");

    /* Launch-view overrides for screenshot scripting: YGREETER_TAB=N picks
     * the top tab, YGREETER_SUBTAB=M the sub-tab within a grouped one — no
     * keyboard or mouse needed. Set the sub-tab before the build so
     * rebuild_top picks it up. */
    int start_tab = 0;
    const char *env_tab = getenv("YGREETER_TAB");
    if (env_tab && *env_tab) {
        int v = atoi(env_tab);
        if (v >= 0 && v < TOP_TAB_COUNT) {
            start_tab = v;
        }
    }
    const char *env_sub = getenv("YGREETER_SUBTAB");
    if (env_sub && *env_sub) {
        int sv = atoi(env_sub);
        if (sv >= 0 && sv < TOP_TABS[start_tab].n_subs) {
            app->top_active_sub[start_tab] = sv;
        }
    }
    if (start_tab != 0) {
        /* set_active emits VALUE_CHANGED → on_tab_change → rebuild_top. */
        return yetty_ygui_tabbar_set_active(app->tabbar, start_tab);
    }
    return rebuild_top(app, 0);
}

/* Common key handler — looks the same regardless of mode. The caller's
 * mode-specific shutdown lives on the stop_cb hook below. */
struct key_ctx {
    struct app *app;
    void (*stop_cb)(struct app *app);
};

static int on_key(struct yetty_yclass_object *engine, uint32_t key, int mods, void *userdata)
{
    (void)engine;
    (void)mods;
    struct key_ctx *kc = (struct key_ctx *)userdata;
    struct app *app = kc->app;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        if (kc->stop_cb) {
            kc->stop_cb(app);
        }
        return 1;
    }
    if (key == YETTY_YGUI_KEY_ARROW_LEFT) {
        struct yetty_ycore_int_result active_r = yetty_ygui_tabbar_active(app->tabbar);
        int active = YETTY_IS_OK(active_r) ? active_r.value : 0;
        if (YETTY_IS_ERR(active_r)) {
            yetty_ycore_error_destroy(active_r.error);
        }
        int next = active > 0 ? active - 1 : TOP_TAB_COUNT - 1;
        struct yetty_ycore_void_result r = yetty_ygui_tabbar_set_active(app->tabbar, next);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        return 1;
    }
    if (key == YETTY_YGUI_KEY_ARROW_RIGHT) {
        struct yetty_ycore_int_result active_r = yetty_ygui_tabbar_active(app->tabbar);
        int active = YETTY_IS_OK(active_r) ? active_r.value : 0;
        if (YETTY_IS_ERR(active_r)) {
            yetty_ycore_error_destroy(active_r.error);
        }
        int next = (active + 1) % TOP_TAB_COUNT;
        struct yetty_ycore_void_result r = yetty_ygui_tabbar_set_active(app->tabbar, next);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        return 1;
    }
    return 0;
}

/*=============================================================================
 * CLIENT MODE — a yetty_ywire_connection over the terminal fd pair.
 *
 * The connection's transport (yetty_yclass_transport_pty; the side-channel fd
 * pair instead when YETTY_YWIRE_SIDE_CHANNEL is set) is the sole owner of
 * STDIN/STDOUT: it owns terminal raw-mode (the echo-loop fix), the ordered
 * non-blocking writer, and the fd()/pump() reactor seam the libuv loop
 * drives. Figure/chrome RPC rides the rpc channel; forwarded mouse + pane
 * resize arrive on the input channel; raw keystrokes on the raw channel.
 * Lives in ygreeter (not in ygui) because it's deployment glue.
 *===========================================================================*/

#ifdef YETTY_YGUI_HAS_UV

#include <yetty/yclass/transport-pty.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

struct client_state {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    /* Writable-interest poll on the connection's out fd — armed only while
     * connection_want_write() reports queued outbound bytes. Without it a
     * multi-MB figure body drains 64 KB per INBOUND loop wakeup (the loop
     * sleeps between host events), which is what stretched one image upload
     * into minutes (#457). */
    uv_poll_t out_poll;
    int out_poll_inited;
    int out_poll_armed;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    struct yetty_yclass_transport_pty *transport; /* sole owner of the fd pair */
    struct yetty_ywire_connection *conn;          /* multiplexed link */
    struct app *app;
    int running;
    /* Host display's HiDPI factor (framebuffer px / logical px), learned from
     * yetty_client_input_resize.content_scale on the RESIZE envelope. ygui and
     * ychrome author in LOGICAL px and the host's yscene multiplies
     * absolute-coords figures back up by this, so every framebuffer-px input
     * from the host (pane size, forwarded pointer coords) is divided by it on
     * the way in. 1.0 until the first RESIZE envelope arrives, and for a host
     * too old to populate the field. */
    float content_scale;
#ifdef YETTY_YGREETER_HAS_CHROME
    /* Window chrome over the wire — the same ychrome the standalone window
     * gets, but driven onto the hosting yetty's root figure container proxy via
     * the typed yclass-RPC stubs. The opaque backdrop it pins hides the pane's
     * terminal text beneath us; the caption gives the in-terminal app a
     * titlebar. The producer session owns the RPC transport; the container is a
     * borrowed proxy obtained from it. */
    struct yetty_yclass_object *chrome_root;      /* terminal (session root) */
    struct yetty_yclass_object *chrome_container; /* navigated container proxy */
    struct yetty_ychrome_host *chrome_host;
    /* content_scale the live chrome_host was created with — the host captures
     * it once, so a change means rebuild (see client_chrome_sync). */
    float chrome_scale;
    int chrome_width;
    int chrome_height;
#endif
};

/* Host HiDPI factor with the 1.0 guard — see client_state::content_scale. The
 * standalone counterpart is app_content_scale(); this is the client-mode one,
 * fed by the host over the wire instead of read from a local GPU context. */
static float client_content_scale(const struct client_state *cs)
{
    return (cs && cs->content_scale > 0.0f) ? cs->content_scale : 1.0f;
}

/*-----------------------------------------------------------------------------
 * Raw channel sink — bytes outside any envelope are real keyboard input from
 * the controlling terminal. Forward them to ygui's input decoder verbatim (it
 * understands CSI arrow sequences + ASCII). Raw-mode ownership (the cooked-tty
 * echo-loop fix) lives in the connection's transport now.
 *---------------------------------------------------------------------------*/
static void client_raw_sink(void *userdata, const uint8_t *bytes, size_t n)
{
    struct client_state *cs = (struct client_state *)userdata;
    struct yetty_ycore_void_result fr =
        yetty_ygui_framework_feed_input(cs->app->engine, (const char *)bytes, n);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
}

/*-----------------------------------------------------------------------------
 * Mouse handler — yetty forwards pointer events to the inferior as
 * OSC carrying a yetty_client_input_mouse. Drain the envelope body and
 * dispatch to ygui's framework_feed_mouse_* entry points (same path
 * the standalone mode uses for synthetic events).
 *---------------------------------------------------------------------------*/
#ifdef YETTY_YGREETER_HAS_CHROME
/* Create the wire chrome host on the first known pane size, and keep it sized
 * to the pane thereafter. The host drives its opaque backdrop (hides the pane's
 * terminal text) + caption onto the hosting yetty's root container proxy
 * (cs->chrome_container) via the typed yclass-RPC stubs.
 * The caption height matches the standalone window (run_standalone_mode) and
 * the space ygreeter's tabbar already reserves for window controls.
 *
 * Returns a Result so the coroutine / callback caller can chain the failure
 * rather than swallow it. */
static struct yetty_ycore_void_result client_chrome_sync(struct client_state *cs, float width,
                                                         float height)
{
    if (!cs || !cs->chrome_container || width <= 0.0f || height <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float scale = client_content_scale(cs);
    /* The host captures content_scale at create time, but the first sync can
     * fire from TIOCGWINSZ before the RESIZE envelope has told us the real
     * factor (and the factor changes for real when the window moves to a
     * display of a different density). Rebuild rather than render the caption
     * at a stale scale. */
    if (cs->chrome_host && scale != cs->chrome_scale) {
        struct yetty_ycore_void_result clear_result = yetty_ychrome_host_clear(cs->chrome_host);
        if (YETTY_IS_ERR(clear_result)) {
            yetty_ycore_error_destroy(clear_result.error);
        }
        struct yetty_ycore_void_result destroy_result = yetty_ychrome_host_destroy(cs->chrome_host);
        if (YETTY_IS_ERR(destroy_result)) {
            yetty_ycore_error_destroy(destroy_result.error);
        }
        cs->chrome_host = NULL;
    }
    if (!cs->chrome_host) {
        /* Wire mode has no local GPU context; the host's HiDPI factor arrives
         * on the resize envelope (yetty_client_input_resize.content_scale), so
         * chrome authors LOGICAL px and the host's receiving yscene scales it
         * back up for display. */
        struct yetty_ychrome_host_ptr_result host_result =
            yetty_ychrome_host_create_wire(cs->chrome_container, /*window_chrome=*/NULL, width,
                                           height, scale, 34.0f, 8.0f, YETTY_YCHROME_FLAG_ALL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, host_result, "client_chrome_sync: create wire host");
        cs->chrome_host = host_result.value;
        cs->chrome_scale = scale;
        cs->chrome_width = (int)width;
        cs->chrome_height = (int)height;
        return YETTY_OK_VOID();
    }
    if ((int)width != cs->chrome_width || (int)height != cs->chrome_height) {
        cs->chrome_width = (int)width;
        cs->chrome_height = (int)height;
        struct yetty_ycore_void_result resized_result =
            yetty_ychrome_host_resized(cs->chrome_host, width, height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resized_result, "client_chrome_sync: resize host");
    }
    return YETTY_OK_VOID();
}

static void client_stop(struct app *app);

/* Feed a forwarded pointer event to the wire chrome host before ygui sees it.
 * Returns 1 if chrome claimed the event (caption strip / window controls), so
 * ygui must not also process it. A release on the close control exits the app;
 * minimize/maximize of an in-terminal pane have no host-side semantics yet, so
 * they are claimed but inert (a host pane-control protocol would make them
 * act). */
static int client_chrome_consume_mouse(struct client_state *cs,
                                       const struct yetty_client_input_mouse *msg)
{
    if (!cs || !cs->chrome_host) {
        return 0;
    }
    struct yetty_yui_event event = {0};
    if (msg->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
        event.type = msg->pressed ? YETTY_YCORE_MOUSE_DOWN : YETTY_YCORE_MOUSE_UP;
        event.mouse.button = msg->button;
    } else if (msg->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
        event.type = YETTY_YCORE_MOUSE_MOVE;
    } else {
        return 0;
    }
    event.mouse.x = msg->x;
    event.mouse.y = msg->y;

    struct yetty_ycore_int_result handle_result =
        yetty_ychrome_host_handle_event(cs->chrome_host, &event);
    if (YETTY_IS_ERR(handle_result)) {
        ywarn("ygreeter client: chrome handle_event failed: %s", handle_result.error.msg);
        yetty_ycore_error_destroy(handle_result.error);
        return 0;
    }
    if (!handle_result.value) {
        return 0;
    }
    /* Chrome claimed it. Act on the release of a window control. */
    if (event.type == YETTY_YCORE_MOUSE_UP) {
        struct yetty_ycore_int_result hover_result =
            yetty_ychrome_hover_button(yetty_ychrome_host_chrome(cs->chrome_host));
        if (YETTY_IS_OK(hover_result)) {
            if (hover_result.value == 3) { /* 3 = close */
                client_stop(cs->app);
            }
        } else {
            yetty_ycore_error_destroy(hover_result.error);
        }
    }
    return 1;
}
#endif

/*-----------------------------------------------------------------------------
 * Input channel sink — the hosting yetty forwards pointer events and the pane
 * pixel size as client-input OSC envelopes; the connection demuxes them onto
 * the input channel and fires this sink once per decoded envelope.
 *
 * Resize matters beyond SIGWINCH: over the telnet/guest transport
 * (--temu / --qemu) the RESIZE envelope is the ONLY size signal — TIOCGWINSZ
 * carries no pixels there — so without applying it the framework stays at its
 * 800x600 default and renders in a small corner.
 *---------------------------------------------------------------------------*/
static void client_input_sink(void *userdata, int wire_code, const uint8_t *args, size_t args_len,
                              const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct client_state *cs = (struct client_state *)userdata;
    struct app *app = cs->app;

    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE ||
        wire_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE) {
        if (payload_len < sizeof(struct yetty_client_input_mouse)) {
            return;
        }
        const struct yetty_client_input_mouse *msg =
            (const struct yetty_client_input_mouse *)payload;
        if (msg->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            return;
        }
        /* The host forwards pointer coords in FRAMEBUFFER px; ygui hit-tests in
         * logical px (same divide the viewport gets), so scale once here or
         * every click lands content_scale× too far right and down. */
        float scale = client_content_scale(cs);
        float mx = msg->x / scale;
        float my = msg->y / scale;
        ydebug("ygreeter client: mouse envelope kind=%u fig=%u dev=(%.1f,%.1f) logical=(%.1f,%.1f)",
               (unsigned)msg->kind, msg->figure_id, msg->x, msg->y, mx, my);
        /* ygreeter's UI (tabs/buttons) gets first refusal; only events it
         * does not consume fall through to the window chrome — mirrors the
         * standalone client-first / chrome-fallback ordering, so the chrome
         * caption never steals a tab click. */
        int ygui_consumed = 0;
        switch (msg->kind) {
        case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
            struct yetty_ycore_int_result feed_result = yetty_ygui_framework_feed_mouse_button(
                app->engine, mx, my, msg->button, msg->pressed, 0);
            ygui_consumed = YETTY_IS_OK(feed_result) && feed_result.value;
            if (YETTY_IS_ERR(feed_result)) {
                yetty_ycore_error_destroy(feed_result.error);
            }
            ydebug("ygreeter client: feed_mouse_button consumed=%d", ygui_consumed);
            break;
        }
        case YETTY_YMGUI_INPUT_MOUSE_POS: {
            struct yetty_ycore_int_result feed_result =
                yetty_ygui_framework_feed_mouse_motion(app->engine, mx, my);
            ygui_consumed = YETTY_IS_OK(feed_result) && feed_result.value;
            if (YETTY_IS_ERR(feed_result)) {
                yetty_ycore_error_destroy(feed_result.error);
            }
            break;
        }
        default:
            break;
        }
#ifdef YETTY_YGREETER_HAS_CHROME
        if (!ygui_consumed) {
            client_chrome_consume_mouse(app->client, msg);
        }
#else
        (void)ygui_consumed;
#endif
        return;
    }

    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE ||
        wire_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE) {
        if (payload_len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *msg =
            (const struct yetty_client_input_resize *)payload;
        if (msg->magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC || msg->width <= 0.0f ||
            msg->height <= 0.0f) {
            return;
        }
        /* Learn the host's HiDPI factor before anything consumes the size: the
         * envelope carries framebuffer px, ygui and ychrome author in logical
         * px, and the host's yscene scales absolute-coords figures back up by
         * exactly this factor. Divide once here and the whole client-mode UI
         * lands 1:1 on the pane; skip it and every coordinate is multiplied by
         * content_scale a second time (plots pushed right and off the pane on
         * any HiDPI display). A host too old to populate the field sends 0 —
         * fall back to 1.0 and keep the pre-field behaviour. */
        if (msg->content_scale > 0.0f) {
            app->client->content_scale = msg->content_scale;
        }
        float cs = app->client->content_scale > 0.0f ? app->client->content_scale : 1.0f;
        float logical_w = msg->width / cs;
        float logical_h = msg->height / cs;
        struct yetty_ycore_void_result r =
            yetty_ygui_framework_set_viewport(app->engine, logical_w, logical_h);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        yetty_ygui_framework_mark_dirty(app->engine);
#ifdef YETTY_YGREETER_HAS_CHROME
        /* Event-loop callback boundary: a transient chrome-sync failure must
         * not kill input handling — surface it to the trace log and move on.
         * chrome_sync takes FRAMEBUFFER px; the host wrapper divides by the
         * content_scale it was created with. */
        struct yetty_ycore_void_result chrome_sync_result =
            client_chrome_sync(app->client, msg->width, msg->height);
        if (YETTY_IS_ERR(chrome_sync_result)) {
            yetty_ycore_error_print(stderr, "ygreeter client: chrome sync (resize) failed",
                                    chrome_sync_result.error);
            yetty_ycore_error_destroy(chrome_sync_result.error);
        }
#endif
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
    /* The raw channel tmux-wraps verbatim control bytes on flush, so the
     * card-mouse enable survives a multiplexer. */
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "client_enable_mouse_forwarding: no raw channel");
    }
    static const char enable[] = "\033[?1500h\033[?1501h";
    struct yetty_ycore_size_result wr = yetty_ywire_channel_write(raw, enable, sizeof(enable) - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "client_enable_mouse_forwarding: write");
    return yetty_ywire_channel_flush(raw);
}

static void client_stop(struct app *app)
{
    if (app && app->client) {
        app->client->running = 0;
    }
}

static void client_out_poll_cb(uv_poll_t *handle, int status, int events);

/* Drain any queued outbound bytes (non-blocking), then keep the loop's
 * writable interest in sync with the queue: armed while bytes remain (so the
 * next drain happens the moment the PTY can take more — full wire speed),
 * disarmed once empty (so an idle app doesn't spin on a writable tty). */
static void client_pump_out(struct client_state *cs)
{
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_writable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    if (!cs->out_poll_inited) {
        return;
    }
    int want_write = yetty_ywire_connection_want_write(cs->conn);
    if (want_write && !cs->out_poll_armed) {
        if (uv_poll_start(&cs->out_poll, UV_WRITABLE, client_out_poll_cb) == 0) {
            cs->out_poll_armed = 1;
        }
    } else if (!want_write && cs->out_poll_armed) {
        uv_poll_stop(&cs->out_poll);
        cs->out_poll_armed = 0;
    }
}

static void client_out_poll_cb(uv_poll_t *handle, int status, int events)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (status < 0 || !(events & UV_WRITABLE)) {
        return;
    }
    client_pump_out(cs);
}

static void client_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    /* The connection is the single reader: it reads the fd, demuxes, and
     * routes each lane to its channel — rpc bytes buffer for the transport
     * adapter, forwarded mouse/resize fires the input sink, raw keystrokes
     * fire the raw sink. */
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_readable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    client_pump_out(cs);
    if (yetty_ywire_connection_is_eof(cs->conn)) {
        cs->running = 0;
    }
}

/* Resize callback — the connection reads TIOCGWINSZ and hands us the geometry;
 * inject it as the framework viewport (pixel fields are 0 over transports that
 * report no pixel size — the RESIZE envelope covers those). */
static void client_resize_cb(void *user, int width_px, int height_px, int cols, int rows)
{
    (void)cols;
    (void)rows;
    struct client_state *cs = (struct client_state *)user;
    if (width_px <= 0 || height_px <= 0) {
        return;
    }
    /* TIOCGWINSZ pixels are FRAMEBUFFER px (cols × cell, and the cell stride
     * already carries content_scale). Same divide as the RESIZE envelope so
     * this fallback can't clobber the logical viewport with device px. */
    float scale = client_content_scale(cs);
    struct yetty_ycore_void_result r = yetty_ygui_framework_set_viewport(
        cs->app->engine, (float)width_px / scale, (float)height_px / scale);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
#ifdef YETTY_YGREETER_HAS_CHROME
    /* uv signal/callback boundary — surface + free, don't swallow. */
    struct yetty_ycore_void_result chrome_sync_result =
        client_chrome_sync(cs, (float)width_px, (float)height_px);
    if (YETTY_IS_ERR(chrome_sync_result)) {
        yetty_ycore_error_print(stderr, "ygreeter client: chrome sync (winsz)",
                                chrome_sync_result.error);
        yetty_ycore_error_destroy(chrome_sync_result.error);
    }
#endif
}

static void client_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    struct client_state *cs = (struct client_state *)handle->data;
    struct yetty_ycore_void_result r = yetty_ywire_connection_pickup_winsize(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static void client_prep_cb(uv_prepare_t *handle)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (yetty_ygui_framework_is_dirty(cs->app->engine)) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_emit(cs->app->engine);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
#ifdef YETTY_YGREETER_HAS_CHROME
        /* Co-emit the chrome on EVERY frame the framework draws, not just the
         * first few. The chrome figures (opaque backdrop + caption) must reach
         * the host pane with the same reliability as the widgets, and the only
         * thing that gives the widgets that reliability is being re-emitted on
         * each dirty frame. Over a slow guest transport (--temu/--qemu) the
         * pane's container isn't ready for the first ~boot's worth of frames,
         * so an emit bounded to the first N frames is spent before the link is
         * live — the widgets then arrive (they keep re-emitting) but a one-shot
         * backdrop does not, leaving the host's console text visible underneath.
         * The framework only marks itself dirty on real UI changes, so this is
         * naturally throttled to the widget-emit cadence, not every vsync. */
        if (cs->chrome_host) {
            struct yetty_ycore_void_result resync_result =
                yetty_ychrome_host_resync(cs->chrome_host);
            if (YETTY_IS_ERR(resync_result)) {
                ywarn("ygreeter client: chrome resync failed: %s", resync_result.error.msg);
                yetty_ycore_error_destroy(resync_result.error);
            }
        }
#endif
    }
    client_pump_out(cs);
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

    /* The connection's transport is the sole owner of STDIN/STDOUT (or of the
     * side-channel fd pair when YETTY_YWIRE_SIDE_CHANNEL is set). Raw mode runs
     * BEFORE any write — otherwise the first DCS envelope we emit gets echoed
     * back by the slave tty driver and ends up rendered as visible "^[" text
     * in the host yetty's display. destroy restores the termios. */
    struct yetty_yclass_transport_pty_ptr_result tr =
        yetty_yclass_transport_pty_create_from_env(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_print(stderr, "ygreeter client: transport create", tr.error);
        yetty_ycore_error_destroy(tr.error);
        return 1;
    }
    cs.transport = tr.value;
    {
        struct yetty_ycore_void_result raw_res =
            yetty_yclass_transport_pty_enable_raw_mode(cs.transport);
        if (YETTY_IS_ERR(raw_res)) {
            ywarn("ygreeter client: enable_raw_mode failed: %s", raw_res.error.msg);
            yetty_ycore_error_destroy(raw_res.error);
        }
    }
    {
        struct yetty_ywire_connection_ptr_result cr = yetty_ywire_connection_create(
            yetty_yclass_transport_pty_reactor(cs.transport), /*compressed=*/1);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_print(stderr, "ygreeter client: connection_create", cr.error);
            yetty_ycore_error_destroy(cr.error);
            struct yetty_ycore_void_result td = yetty_yclass_transport_pty_destroy(cs.transport);
            if (YETTY_IS_ERR(td)) {
                yetty_ycore_error_destroy(td.error);
            }
            return 1;
        }
        cs.conn = cr.value;
    }
    if (uv_loop_init(&cs.loop) != 0) {
        fprintf(stderr, "ygreeter client: uv_loop_init failed\n");
        return 1;
    }

    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_print(stderr, "ygreeter client: framework_create", fr.error);
        yetty_ycore_error_destroy(fr.error);
        return 1;
    }
    struct app app = {0};
    app.engine = fr.value;
    app.client = &cs;

    struct key_ctx kc = {.app = &app, .stop_cb = client_stop};
    app.stop_cb = client_stop;
    yetty_ygui_framework_set_key_cb(app.engine, on_key, &kc);
    cs.app = &app;
    cs.running = 1;

#ifdef YETTY_YGREETER_HAS_CHROME
    /* Attach to the hosting yetty's root figure container over the connection's
     * rpc channel (the yclass RPC session rides a channel-backed transport
     * adapter — nothing but the connection ever touches the fds), so the window
     * chrome (backdrop + caption) can be driven onto the host's pane via the
     * typed stubs. The attach handshake reads the fd synchronously through the
     * connection's blocking pump — it MUST run here, before libuv's poll takes
     * the fd over below. The chrome host itself is created lazily once we know
     * the pane pixel size (client_chrome_sync, driven from the resize path). */
    {
        /* Open our OWN dynamic RPC channel on the connection (the SSH model)
         * — NOT the shared well-known lane — so multiple in-pane clients on
         * one PTY never tear each other's frames. connect_channel returns the
         * session root (the terminal); navigate to its figure container. */
        struct yetty_yclass_object_ptr_result terminal_result =
            yetty_yclass_rpc_connect_channel(cs.conn);
        if (YETTY_IS_OK(terminal_result)) {
            cs.chrome_root = terminal_result.value;
            struct yetty_yclass_object_ptr_result container_result =
                yetty_yterminal_figure_root_container(cs.chrome_root);
            if (YETTY_IS_OK(container_result)) {
                cs.chrome_container = container_result.value;
                /* Prime the container's slots now (attach window, pipeline
                 * empty): steady-state pipelined mutations never mid-stream
                 * RESOLVE_SLOT (which async mode forbids). */
                struct yetty_ycore_void_result prime_result =
                    yetty_yclass_rpc_session_translate_class(cs.chrome_root->session,
                                                             "yetty_yfigure_container");
                if (YETTY_IS_ERR(prime_result)) {
                    yetty_ycore_error_destroy(prime_result.error);
                }
                /* Drive the ygui widget tree into the SAME host container over
                 * the SAME session as the chrome — one channel, one reader. */
                struct yetty_ycore_void_result set_container_result =
                    yetty_ygui_framework_set_container_obj(app.engine, cs.chrome_container);
                if (YETTY_IS_ERR(set_container_result)) {
                    yetty_ycore_error_print(stderr, "ygreeter client: set_container_obj",
                                            set_container_result.error);
                    yetty_ycore_error_destroy(set_container_result.error);
                }
            } else {
                ywarn("ygreeter client: figure_root_container failed: %s",
                      container_result.error.msg);
                yetty_ycore_error_destroy(container_result.error);
            }
        } else {
            ywarn("ygreeter client: connect_channel failed: %s", terminal_result.error.msg);
            yetty_ycore_error_destroy(terminal_result.error);
        }
    }
#endif

    struct yetty_ycore_void_result br = build_ui(&app);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_print(stderr, "ygreeter client: build_ui", br.error);
        yetty_ycore_error_destroy(br.error);
        return 1;
    }

    /* Route inbound lanes: raw keystrokes → input decoder; forwarded
     * mouse/resize envelopes → the input sink; TIOCGWINSZ geometry → the
     * resize callback. The connection's demux replaces the hand-rolled wire
     * SM + per-envelope coroutine handlers this file used to carry. */
    {
        struct yetty_ywire_channel *raw =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ywire_channel *input =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_INPUT);
        struct yetty_ycore_void_result rs =
            yetty_ywire_channel_set_raw_sink(raw, client_raw_sink, &cs);
        if (YETTY_IS_ERR(rs)) {
            yetty_ycore_error_destroy(rs.error);
        }
        struct yetty_ycore_void_result is =
            yetty_ywire_channel_set_envelope_sink(input, client_input_sink, &cs);
        if (YETTY_IS_ERR(is)) {
            yetty_ycore_error_destroy(is.error);
        }
        struct yetty_ycore_void_result rr =
            yetty_ywire_connection_set_resize_cb(cs.conn, client_resize_cb, &cs);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    /* Tell the host yetty to forward pointer events to our stdin. */
    {
        struct yetty_ycore_void_result sr = client_enable_mouse_forwarding(&cs);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_print(stderr, "ygreeter client: input subscribe", sr.error);
            yetty_ycore_error_destroy(sr.error);
        }
    }

    {
        struct yetty_ycore_void_result wr = yetty_ywire_connection_pickup_winsize(cs.conn);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_error_destroy(wr.error);
        }
    }
    if (uv_poll_init(&cs.loop, &cs.stdin_poll, yetty_ywire_connection_fd(cs.conn)) == 0) {
        cs.stdin_poll.data = &cs;
        uv_poll_start(&cs.stdin_poll, UV_READABLE, client_stdin_cb);
    }
    /* Writable-interest poll (see client_state) — armed on demand by
     * client_pump_out. Non-fatal if the fd can't be polled: output then
     * degrades to draining on inbound wakeups. */
    if (uv_poll_init(&cs.loop, &cs.out_poll, yetty_ywire_connection_out_fd(cs.conn)) == 0) {
        cs.out_poll.data = &cs;
        cs.out_poll_inited = 1;
    } else {
        ywarn("ygreeter client: out-fd poll init failed — writes drain on inbound wakeups only");
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

    /* Teardown runs every step best-effort and folds each failure into one
     * chain (nothing swallowed); the root surfaces + frees the chain below. */
    struct yetty_ycore_void_result teardown_result = YETTY_OK_VOID();

#ifdef YETTY_YGREETER_HAS_CHROME
    /* Explicitly remove our backdrop + caption from the host pane FIRST, via the
     * typed delete_child stubs — each is one-way and flushes its request with a
     * synchronous blocking write (safe now that the loop has stopped). Then free
     * our side and detach the RPC session (which owns the transport). */
    if (cs.chrome_host) {
        teardown_result =
            yetty_ycore_void_chain(teardown_result, yetty_ychrome_host_clear(cs.chrome_host));
        teardown_result =
            yetty_ycore_void_chain(teardown_result, yetty_ychrome_host_destroy(cs.chrome_host));
        cs.chrome_host = NULL;
    }
    /* Disconnecting cs.chrome_root is DEFERRED to the very end of teardown: the
     * ygui framework was wired to this same session's root-container proxy
     * (set_container_obj), so framework_clear / framework_destroy below still
     * use it. Detaching here would free the proxy out from under them, skipping
     * the widget-figure clear and leaking every figure on the host pane. */
#endif

    /* Undo client_enable_mouse_forwarding before we exit. The DEC private
     * modes 1500/1501 live in the host yetty's terminal state, not ours —
     * if we leave them set, the shell that reclaims this pane keeps
     * receiving OSC mouse envelopes, and its cooked-mode tty echoes the
     * ESC bytes back as visible "^[" garbage. The raw-channel flush queues
     * the (tmux-wrapped) bytes; the loop has stopped, so force them onto the
     * wire with the transport's blocking flush. */
    {
        static const char disable_fwd[] = "\033[?1500l\033[?1501l";
        struct yetty_ywire_channel *raw =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ycore_size_result fwd_write =
            yetty_ywire_channel_write(raw, disable_fwd, sizeof(disable_fwd) - 1);
        if (YETTY_IS_ERR(fwd_write)) {
            teardown_result = yetty_ycore_void_chain(
                teardown_result,
                (struct yetty_ycore_void_result){.ok = 0, .error = fwd_write.error});
        } else {
            teardown_result =
                yetty_ycore_void_chain(teardown_result, yetty_ywire_channel_flush(raw));
        }
        teardown_result = yetty_ycore_void_chain(
            teardown_result, yetty_yclass_transport_pty_flush_blocking(cs.transport));
    }

    /* Tell the host to destroy our remote figure containers, otherwise it
     * keeps our last frame frozen on the pane after we exit (the shell that
     * reclaims the pane would render under a stale ygreeter image). Drives
     * yetty_yfigure_clear_all on the wired host container through the typed
     * yclass stub. */
    teardown_result =
        yetty_ycore_void_chain(teardown_result, yetty_ygui_framework_clear(app.engine));

    uv_poll_stop(&cs.stdin_poll);
    if (cs.out_poll_inited) {
        uv_poll_stop(&cs.out_poll);
        uv_close((uv_handle_t *)&cs.out_poll, client_close_cb);
    }
    uv_signal_stop(&cs.sigwinch);
    uv_prepare_stop(&cs.prep);
    uv_close((uv_handle_t *)&cs.stdin_poll, client_close_cb);
    uv_close((uv_handle_t *)&cs.sigwinch, client_close_cb);
    uv_close((uv_handle_t *)&cs.prep, client_close_cb);
    uv_run(&cs.loop, UV_RUN_NOWAIT);

    /* FIRST — before closing any channel — arm the host's input barrier
     * (no-payload INPUT_HOLD): the host holds the user's keystrokes host-side
     * for the rest of teardown and releases them to the pane once we are gone,
     * so exit-window keys reach the resumed shell instead of being consumed by
     * our close drain. Carries no bytes → cannot inject. */
    {
        struct yetty_ywire_channel *hold_lane =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ycore_buffer hold_env = {0};
        struct yetty_ycore_void_result hold_emit =
            yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_HOLD,
                             /*has_args=*/1, /*compressed=*/0, NULL, 0, NULL, 0, &hold_env);
        if (YETTY_IS_OK(hold_emit) && hold_lane) {
            struct yetty_ycore_size_result hw =
                yetty_ywire_channel_write(hold_lane, hold_env.data, hold_env.size);
            if (YETTY_IS_ERR(hw)) {
                yetty_ycore_error_destroy(hw.error);
            } else {
                struct yetty_ycore_void_result hf = yetty_ywire_channel_flush(hold_lane);
                if (YETTY_IS_ERR(hf)) {
                    yetty_ycore_error_destroy(hf.error);
                }
                hf = yetty_yclass_transport_pty_flush_blocking(cs.transport);
                if (YETTY_IS_ERR(hf)) {
                    yetty_ycore_error_destroy(hf.error);
                }
            }
        } else if (YETTY_IS_ERR(hold_emit)) {
            yetty_ycore_error_destroy(hold_emit.error);
        }
        yetty_ycore_buffer_destroy(&hold_env);
    }

    /* WAIT for the host's HOLD-ACK before detaching sinks — its arrival proves
     * the barrier is armed. Parser + sinks stay alive across this wait: a key
     * the host forwarded before it armed comes back as an echo the still-live
     * client consumes here, not the close drain; every key after the ACK is
     * held host-side. Wall-clock bounded (500 ms) so a dead host cannot hang
     * exit. The result GATES the close drain below (see there). */
    int hold_ack_confirmed = yetty_ywire_connection_drain_until_hold_ack(cs.conn, 500);

    /* SINKS first: the raw/input sinks dispatch into the framework being
     * destroyed next — anything the close drain below still parses must never
     * reach a freed engine (frame-wrapped input lands in the channel inbuf
     * instead). */
    {
        struct yetty_ywire_channel *raw_lane =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ywire_channel *input_lane =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_INPUT);
        struct yetty_ycore_void_result sink_res =
            yetty_ywire_channel_set_raw_sink(raw_lane, NULL, NULL);
        if (YETTY_IS_ERR(sink_res)) {
            yetty_ycore_error_destroy(sink_res.error);
        }
        sink_res = yetty_ywire_channel_set_envelope_sink(input_lane, NULL, NULL);
        if (YETTY_IS_ERR(sink_res)) {
            yetty_ycore_error_destroy(sink_res.error);
        }
    }
    teardown_result =
        yetty_ycore_void_chain(teardown_result, yetty_ygui_framework_destroy(app.engine));
#ifdef YETTY_YGREETER_HAS_CHROME
    /* Every figure clear (chrome delete_child + framework clear_all) has now
     * been emitted onto the host, and the framework is torn down — so it is
     * finally safe to detach the shared RPC session, which frees the root-
     * container proxy and destroys the channel-backed transport adapter. Must
     * be the LAST use of the session, and must precede connection_destroy
     * (the adapter borrows the rpc channel). */
    if (cs.chrome_root) {
        /* The container proxy is a plain calloc'd carrier — free it, then
         * disconnect (session_destroy CLOSEs our dynamic channel; the
         * connection stays ours to destroy after). */
        free(cs.chrome_container);
        cs.chrome_container = NULL;
        teardown_result =
            yetty_ycore_void_chain(teardown_result, yetty_yclass_rpc_disconnect(cs.chrome_root));
        cs.chrome_root = NULL;
    }
#endif
    /* The figure clears above were queued through the non-blocking writer —
     * force the tail onto the wire before the transport goes away. */
    teardown_result = yetty_ycore_void_chain(
        teardown_result, yetty_yclass_transport_pty_flush_blocking(cs.transport));
    /* COMPLETION-AWARE close drain, parser ALIVE, BYTE-WISE (exit hygiene
     * without input loss): the flush above put the chrome clears / figure
     * deletes / channel CLOSE frames on the wire; the host answers each
     * CLOSE with a framed CLOSE echo. drain_closes feeds the parser one
     * byte at a time until the last echo is parsed (a WALL-CLOCK 500 ms
     * bound covers a dead or babbling host) and stops reading EXACTLY at
     * that framed boundary — a user key queued right after the echo, even
     * in the same kernel-readable window, is never consumed and stays
     * queued for the resumed shell. The sinks were detached above, so
     * nothing the drain parses can touch the destroyed framework.
     *
     * ONLY when the HOLD was ACKed AND the lease is still fresh: a confirmed arm
     * means every post-arm keystroke is held host-side, so the inbound stream
     * carries only CLOSE echoes — safe to drain. WITHOUT the ACK the barrier may
     * be unarmed; and the ACK is only a LEASE — the host's barrier expires on
     * its own 3 s deadline and resumes forwarding, so if teardown to this point
     * took longer than the lease the host may already have un-armed. In either
     * case a forwarded key could interleave with the echoes, so we SKIP the
     * drain (matching origin/main, which drains nothing). Lease 2000 < host 3000. */
    if (hold_ack_confirmed && yetty_ywire_connection_hold_ack_lease_valid(cs.conn, 2000)) {
        (void)yetty_ywire_connection_drain_closes(cs.conn, 500);
    }
    /* Connection before transport: the connection borrows the transport's
     * reactor view. Destroying the transport restores the terminal raw mode. */
    teardown_result =
        yetty_ycore_void_chain(teardown_result, yetty_ywire_connection_destroy(cs.conn));
    teardown_result =
        yetty_ycore_void_chain(teardown_result, yetty_yclass_transport_pty_destroy(cs.transport));
    uv_loop_close(&cs.loop);

    /* Root of the client path: surface the whole teardown chain to the trace
     * log (stderr would pollute the host PTY) and free it. */
    if (YETTY_IS_ERR(teardown_result)) {
        char teardown_message[512];
        yetty_ycore_error_snprint(teardown_message, sizeof(teardown_message),
                                  teardown_result.error);
        ywarn("ygreeter client: teardown errors: %s", teardown_message);
        yetty_ycore_error_destroy(teardown_result.error);
    }
    return 0;
}

#endif /* YETTY_YGUI_HAS_UV */

/*=============================================================================
 * Entry — IN-TERMINAL ONLY: inside a hosting yetty run the client mode;
 * anywhere else refuse. There is no standalone window mode.
 *===========================================================================*/
static int in_yetty_terminal(void)
{
    return yetty_running_under_yetty();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ytrace_init();
    if (in_yetty_terminal()) {
#ifdef YETTY_YGUI_HAS_UV
        return run_client_mode();
#else
        fprintf(stderr, "ygreeter: TERM_PROGRAM=yetty but built without libuv\n");
        return 1;
#endif
    }
    fprintf(stderr, "ygreeter renders into a hosting yetty terminal — run it inside yetty "
                    "(TERM_PROGRAM=yetty)\n");
    return 1;
}
