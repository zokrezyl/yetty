/*
 * yhello — ygreeter's UI running directly against yfigures.
 *
 * yhello is standalone-only: it opens its own window through the yplatform
 * bootstrap + yframework_create, creates a local yfigure_container, and wires ygui to it
 * with yetty_ygui_framework_set_container_obj. There is no terminal client
 * mode, no PTY, no OSC/DCS envelope, and no wire_statemachine in the render
 * path.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yfigure/container.h>
#include "yetty/gen/impl/ycircuit/circuit.h"
#include "yetty/gen/impl/ymusic/music.h"
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yshadertoy/demo-shaders.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yplot/yplot.h>

#include "embedded-assets.h"

/* yfigure producer kind IDs used by the registry (kind tokens). */
#define YHELLO_YFIGURE_KIND_YPLOT 5u
#define YHELLO_YFIGURE_KIND_YIMAGE 6u
#define YHELLO_YFIGURE_KIND_YVIDEO 7u

#ifdef YETTY_YHELLO_HAS_CHROME
#include "yetty/gen/impl/ychrome/chrome.h" /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#endif

#ifdef YETTY_YHELLO_HAS_STANDALONE
/* Headers below pull <yetty/yetty/yetty.h> (or <webgpu/webgpu.h> directly)
 * via their public API surface. */
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/api/yshadertoy/figure.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include "yetty/gen/impl/yapp/app.h"
#include <yetty/yclass/class.h>
#include <yetty/yrender/render-target.h>
#endif

/* Android standalone entry. yhello runs as a NativeActivity through the
 * shared NDK glue (src/yetty/yplatform/ymain/android-glue.c), which resolves the
 * yetty_android_program_init / _term pair defined at the foot of this file. */
#if defined(__ANDROID__) && defined(YETTY_YHELLO_HAS_STANDALONE)
#include <pthread.h>
#include <webgpu/webgpu.h>
#include <yetty/yplatform/android-glue.h>
#include <yetty/yplatform/platform-input-pipe.h>
#endif

/*=============================================================================
 * Tab descriptors — mirrors the main-branch yhello layout:
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

    /* Set when the visible scene self-animates (video, ymaze/zoo/jungle,
     * shadertoy, or an f(t) plot). The ~30 fps frame pump only ticks while
     * this is set — static scenes (Welcome, a static plot, Images, Code …)
     * render on demand instead of redrawing continuously. */
    int animating;

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

    /* Shutdown hook — same function the key handler's stop_cb uses,
     * stored on the app so the titlebar close button can quit too. */
    void (*stop_cb)(struct app *app);

#ifdef YETTY_YHELLO_HAS_STANDALONE
    /* Standalone-mode resources. The headers that define the by-value member
     * types pull in webgpu transitively, so the whole block is gated. */
    struct yetty_yframework *yframework;
    /* The yetty_context handed to the root container (set_context stores
     * the pointer, not a copy) and to the chrome host. It MUST outlive the
     * worker: on webasm standalone_worker returns immediately after the
     * emscripten main loop is registered, and the container mints its
     * ygrid figures lazily on the first render tick — long after the
     * worker's stack frame is gone. A stack-local context would dangle and
     * the lazy ygrid_create would read freed memory (OOB). Living on the
     * heap-allocated, program-lifetime `app` keeps it valid. */
    struct yetty_context ctx;
    struct yetty_yclass_object *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_yfont_font *font;
    struct yetty_ychrome_host *chrome; /* draggable/resizable titlebar + min/max/close */
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_yevent_event_listener listener;
    /* ~30 fps animation pump for self-animating widgets (ymaze, …). */
    struct yetty_yevent_event_listener frame_listener;
    yetty_yevent_timer_id frame_timer;
    struct yetty_ydraw_target *render_target;
#endif
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

/* Defined just above the frame pump; used earlier by build_scene_body and
 * on_row_clicked to decide whether the visible scene needs the pump. */
static bool scene_is_animating(int tab_index, int entry);

/* TEMP latency instrumentation. */
#include <yetty/yplatform/time.h> /* portable monotonic clock (POSIX + Windows) */
#if defined(__ANDROID__)
#include <android/log.h>
#define YPERF(...) __android_log_print(4, "yhello", __VA_ARGS__)
#else
#define YPERF(...) yinfo(__VA_ARGS__)
#endif
static double yperf_ms(void)
{
    return yetty_yplatform_ytime_monotonic_sec() * 1000.0;
}
static double g_last_present_ms;

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
 * function walks. Same approach the original yhello used. Selecting a
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
static void yhello_data_dir(char *out, size_t out_size)
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
    yhello_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    ydebug("yhello: discover_logo_images data_dir=%s", data_dir ? data_dir : "(null)");
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
        ydebug("yhello: discover_logo_images probe=%s status=%s", path_buf,
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
    ydebug("yhello: discover_logo_images count=%d", count);
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
    yhello_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    ydebug("yhello: discover_video_files data_dir=%s", data_dir ? data_dir : "(null)");
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
        ydebug("yhello: discover_video_files probe=%s status=%s", path_buf,
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
    ydebug("yhello: discover_video_files count=%d", count);
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
    yhello_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%s/README.md", data_dir);
    int ok = yetty_yplatform_file_exists(path_buf);
    ydebug("yhello: discover_readme probe=%s status=%s", path_buf, ok ? "found" : "missing");
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
    yhello_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    if (!data_dir || !*data_dir) {
        return;
    }
    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%s/pdf-sample.pdf", data_dir);
    int ok = yetty_yplatform_file_exists(path_buf);
    ydebug("yhello: discover_pdf probe=%s status=%s", path_buf, ok ? "found" : "missing");
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
            struct yetty_ygui_layout_const_ptr_result chip_row_layout_res =
                yetty_ygui_widget_layout_get(chip_row);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, chip_row_layout_res,
                                "build_elements_content: chip_row layout_get");
            struct yetty_ygui_layout l = *chip_row_layout_res.value;
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
            struct yetty_ygui_layout_const_ptr_result row_layout_res =
                yetty_ygui_widget_layout_get(row);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, row_layout_res,
                                "build_elements_content: row layout_get");
            struct yetty_ygui_layout l = *row_layout_res.value;
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
#define YHELLO_CIRCUIT_GRID_PX 20.0f

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
        yetty_ycircuit_configure(circuit, YHELLO_CIRCUIT_GRID_PX, YETTY_YCIRCUIT_FLAG_NONE));
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
#define YHELLO_MUSIC_WIDTH 760.0f
#define YHELLO_MUSIC_STAFF 13.0f

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
        music, YHELLO_MUSIC_WIDTH, YHELLO_MUSIC_STAFF, YETTY_YMUSIC_FLAG_NONE));
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
        struct yetty_ygui_layout_const_ptr_result editor_layout_res =
            yetty_ygui_widget_layout_get(editor);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, editor_layout_res,
                            "build_ynodes_content: editor layout_get");
        struct yetty_ygui_layout l = *editor_layout_res.value;
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
    app->animating = scene_is_animating(tab_index, t->active_entry);

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
        struct yetty_ygui_layout_const_ptr_result hbox_layout_res =
            yetty_ygui_widget_layout_get(hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hbox_layout_res, "build_scene_body: hbox layout_get");
        struct yetty_ygui_layout l = *hbox_layout_res.value;
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
            struct yetty_ygui_layout_const_ptr_result nav_layout_res =
                yetty_ygui_widget_layout_get(nr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, nav_layout_res,
                                "build_scene_body: nav layout_get");
            struct yetty_ygui_layout l = *nav_layout_res.value;
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
                struct yetty_ygui_layout_const_ptr_result button_layout_res =
                    yetty_ygui_widget_layout_get(br.value);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, button_layout_res,
                                    "build_scene_body: nav button layout_get");
                struct yetty_ygui_layout l = *button_layout_res.value;
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
            struct yetty_ygui_layout_const_ptr_result shadertoy_vbox_layout_res =
                yetty_ygui_widget_layout_get(vr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shadertoy_vbox_layout_res,
                                "build_scene_body: shadertoy vbox layout_get");
            struct yetty_ygui_layout l = *shadertoy_vbox_layout_res.value;
            l.flex_grow = 1.0f;
            l.gap = 6.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(vr.value, &l));
        }
        struct yetty_yclass_object_ptr_result str =
            yetty_ygui_widget_add(vr.value, yetty_ygui_tabbar_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, str, "rebuild: shadertoy sub-tabbar");
        {
            struct yetty_ygui_layout_const_ptr_result shadertoy_subtab_layout_res =
                yetty_ygui_widget_layout_get(str.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shadertoy_subtab_layout_res,
                                "build_scene_body: shadertoy sub-tabbar layout_get");
            struct yetty_ygui_layout l = *shadertoy_subtab_layout_res.value;
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
            struct yetty_ygui_layout_const_ptr_result shadertoy_widget_layout_res =
                yetty_ygui_widget_layout_get(zr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shadertoy_widget_layout_res,
                                "build_scene_body: yshadertoy layout_get");
            struct yetty_ygui_layout l = *shadertoy_widget_layout_res.value;
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
            struct yetty_ygui_layout_const_ptr_result ynodes_vbox_layout_res =
                yetty_ygui_widget_layout_get(vr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ynodes_vbox_layout_res,
                                "build_scene_body: ynodes vbox layout_get");
            struct yetty_ygui_layout l = *ynodes_vbox_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result content_layout_res =
            yetty_ygui_widget_layout_get(content);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, content_layout_res,
                            "build_scene_body: content layout_get");
        struct yetty_ygui_layout l = *content_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result group_vbox_layout_res =
            yetty_ygui_widget_layout_get(vr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, group_vbox_layout_res,
                            "rebuild_top: group vbox layout_get");
        struct yetty_ygui_layout l = *group_vbox_layout_res.value;
        l.flex_grow = 1.0f;
        l.gap = 6.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(vr.value, &l));
    }
    struct yetty_yclass_object_ptr_result str =
        yetty_ygui_widget_add(vr.value, yetty_ygui_tabbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, str, "rebuild_top: sub-tabbar");
    {
        struct yetty_ygui_layout_const_ptr_result subtab_layout_res =
            yetty_ygui_widget_layout_get(str.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, subtab_layout_res,
                            "rebuild_top: sub-tabbar layout_get");
        struct yetty_ygui_layout l = *subtab_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result subbody_layout_res =
            yetty_ygui_widget_layout_get(sb.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, subbody_layout_res,
                            "rebuild_top: subbody layout_get");
        struct yetty_ygui_layout l = *subbody_layout_res.value;
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
    struct app *app = rl->app;
    struct tab_state *t = &app->tabs[rl->tab];
    t->active_entry = rl->entry;

    /* In-place re-source for figure-backed tabs. Rebuilding the whole scene
     * (build_scene_body) destroys and re-mints the yplot / yimage figure,
     * which makes the GPU resource binder see a structural change and
     * recompile its shader pipeline from scratch — ~0.5 s on mobile, paid on
     * every click. Keeping the SAME content widget and only swapping its
     * source preserves the figure (and its compiled pipeline), so the switch
     * is a cheap buffer re-upload. Other tab kinds (rich text, …) have no
     * persistent figure to recompile, so they take the cheap rebuild path. */
    bool can_inplace = t->content && (t->kind == TAB_KIND_PLOTS || t->kind == TAB_KIND_IMAGES);
    if (can_inplace) {
        struct yetty_ycore_void_result loaded;
        if (t->kind == TAB_KIND_PLOTS) {
            loaded = load_plot_entry(t->content, &plot_nav_entries[rl->entry]);
        } else {
            const char *path = app->image_path_count > 0 && rl->entry < app->image_path_count
                                   ? app->image_paths[rl->entry]
                                   : NULL;
            loaded = load_image_entry(t->content, path);
        }
        yetty_ycore_error_destroy_safe(loaded);
        if (app->engine) {
            yetty_ygui_framework_mark_dirty(app->engine);
        }
        if (app->yframework && app->yframework->event_loop &&
            app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK_VOID();
    }
    return build_scene_body(app, app->scene_parent, rl->tab);
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
    /* yhello's assets are placed early in startup (installer/bundle on
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
        struct yetty_ygui_layout_const_ptr_result root_layout_res =
            yetty_ygui_widget_layout_get(content);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, root_layout_res, "build_ui: root layout_get");
        struct yetty_ygui_layout l = *root_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result tabbar_layout_res =
            yetty_ygui_widget_layout_get(app->tabbar);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tabbar_layout_res, "build_ui: tabbar layout_get");
        struct yetty_ygui_layout l = *tabbar_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result header_layout_res =
            yetty_ygui_widget_layout_get(hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, header_layout_res, "build_ui: header layout_get");
        struct yetty_ygui_layout l = *header_layout_res.value;
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
        struct yetty_ygui_layout_const_ptr_result body_panel_layout_res =
            yetty_ygui_widget_layout_get(app->body_panel);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, body_panel_layout_res,
                            "build_ui: body panel layout_get");
        struct yetty_ygui_layout l = *body_panel_layout_res.value;
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

    /* Launch-view overrides for screenshot scripting: YHELLO_TAB=N picks
     * the top tab, YHELLO_SUBTAB=M the sub-tab within a grouped one — no
     * keyboard or mouse needed. Set the sub-tab before the build so
     * rebuild_top picks it up. */
    int start_tab = 0;
    const char *env_tab = getenv("YHELLO_TAB");
    if (env_tab && *env_tab) {
        int v = atoi(env_tab);
        if (v >= 0 && v < TOP_TAB_COUNT) {
            start_tab = v;
        }
    }
    const char *env_sub = getenv("YHELLO_SUBTAB");
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

#ifdef YETTY_YHELLO_HAS_STANDALONE
/*=============================================================================
 * STANDALONE MODE — yplatform bootstrap + yframework + local container + direct yfigures
 * dispatch.
 *
 * ygui emits figure records directly into root_container through the yclass slot
 * path. The render handler paints that container onto yframework's render_target.
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

/* Whether the scene at `tab_index` needs the continuous frame pump — i.e.
 * it animates by re-running its widget's emit_body each tick (ymaze, zoo,
 * jungle, shadertoy, video). Static scenes return false so the pump stays
 * idle and they repaint only on input / explicit state changes.
 *
 * Note: f(t) plots are deliberately NOT here. yplot animates its `time`
 * uniform from its own shared timer (yplot-time.c), independent of this
 * pump, so a plot scene never needs it. */
static bool scene_is_animating(int tab_index, int entry)
{
    (void)entry;
    switch (tab_kind_for(tab_index)) {
    case TAB_KIND_VIDEO:
    case TAB_KIND_YMAZE:
    case TAB_KIND_YZOO:
    case TAB_KIND_YJUNGLE:
    case TAB_KIND_YSHADERTOY:
        return true;
    default:
        return false;
    }
}

/* Animation pump — force a full re-emit each tick so self-animating
 * widgets (ymaze, video, f(t) plots) re-run their emit_body and advance.
 * Skipped entirely on static scenes: redrawing a still frame ~30×/s wastes
 * the GPU, heats the device, and saturates the event loop so real input
 * waits behind a present(). */
static struct yetty_ycore_int_result standalone_frame_tick(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *ev)
{
    (void)ev;
    struct app *app = container_of(listener, struct app, frame_listener);
    if (!app->animating) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (app->engine) {
        yetty_ygui_framework_mark_dirty(app->engine);
    }
    if (app->yframework && app->yframework->event_loop &&
        app->yframework->event_loop->ops->request_render) {
        app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/* HiDPI scale (framebuffer px / logical px) of the local display, 1.0 if
 * unset. The ygui chrome is authored in logical pixels and the ygrid
 * receiver scales it back up, so the standalone platform path divides
 * framebuffer-pixel viewport/pointer values by this on the way into ygui. */
static float app_content_scale(const struct app *app)
{
    float s = app->yframework->gpu.app_gpu_context.content_scale;
    return s > 0.0f ? s : 1.0f;
}

static void standalone_resize_container(struct app *app, float pixel_w, float pixel_h)
{
    if (!app->root_container) {
        return;
    }
    struct yetty_ycore_rectangle root_rect = {.min = {0, 0}, .max = {pixel_w, pixel_h}};
    yetty_yfigure_figure_rect_set(app->root_container, root_rect);
    yetty_yfigure_figure_dirty_set(app->root_container, 1);
}

/* Client-first / chrome-fallback: hand a pointer event the greeter UI didn't
 * consume to the window chrome (drag / edge-resize / maximize / window
 * controls). chrome works in raw framebuffer px, so the unscaled event is
 * passed straight through. */
static void yhello_chrome_fallback(struct app *app, const struct yetty_yui_event *ev)
{
    if (!app->chrome) {
        return;
    }
    struct yetty_ycore_int_result cr = yetty_ychrome_host_handle_event(app->chrome, ev);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
    }
}

static struct yetty_ycore_int_result standalone_event_handler(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *ev)
{
    struct app *app = container_of(listener, struct app, listener);
    const float scale = app_content_scale(app);

    if (ev->type != YETTY_YCORE_RENDER) {
        YPERF("YPERF input event type=%d @ %.0f", (int)ev->type, yperf_ms());
    }

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
            YPERF("YPERF render SKIPPED (present in flight / busy)");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        double t0 = yperf_ms();
        /* Produce a new frame directly into root_container if dirty. */
        if (yetty_ygui_framework_is_dirty(app->engine)) {
            struct yetty_ycore_void_result er = yetty_ygui_framework_emit(app->engine);
            if (YETTY_IS_ERR(er)) {
                yetty_ycore_error_destroy(er.error);
            }
        }
        double t1 = yperf_ms();
        /* Clear + paint container + present. */
        struct yetty_ycore_void_result cl = app->render_target->ops->clear(app->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (app->root_container) {
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(app->root_container, app->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set(app->root_container, 0);
        }
        double t2 = yperf_ms();
        struct yetty_ycore_void_result pp = app->render_target->ops->present(app->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        double t3 = yperf_ms();
        YPERF("YPERF render: since_last=%.0f emit=%.1f paint=%.1f present_call=%.1f total=%.1f ms",
              g_last_present_ms > 0 ? t3 - g_last_present_ms : 0.0, t1 - t0, t2 - t1, t3 - t2,
              t3 - t0);
        g_last_present_ms = t3;
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
            /* Logical viewport; container rect below stays framebuffer px. */
            struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
                app->engine, (float)ev->resize.width / scale, (float)ev->resize.height / scale);
            if (YETTY_IS_ERR(vr)) {
                yetty_ycore_error_destroy(vr.error);
            }
        }
        standalone_resize_container(app, ev->resize.width, ev->resize.height);
        if (app->chrome) {
            struct yetty_ycore_void_result chrome_rz = yetty_ychrome_host_resized(
                app->chrome, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(chrome_rz)) {
                yetty_ycore_error_destroy(chrome_rz.error);
            }
        }
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_KEY_DOWN: {
        char scratch[8];
        size_t n = 0;
        const char *bytes = standalone_encode_key(ev->key.key, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            struct yetty_ycore_void_result r =
                yetty_ygui_framework_feed_input(app->engine, bytes, n);
            if (YETTY_IS_ERR(r)) {
                yetty_ycore_error_destroy(r.error);
            }
            if (app->yframework->event_loop->ops->request_render) {
                app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_button(
            app->engine, ev->mouse.x / scale, ev->mouse.y / scale, ev->mouse.button,
            ev->type == YETTY_YCORE_MOUSE_DOWN ? 1 : 0, ev->mouse.mods);
        int consumed = YETTY_IS_OK(r) && r.value;
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        /* Client-first: only if the greeter UI (tabs/buttons) didn't take it
         * does the window chrome get a shot (empty title-bar drag, edges). */
        if (!consumed) {
            yhello_chrome_fallback(app, ev);
        }
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        /* ygui has no double-click concept here; it's the chrome's
         * title-bar maximize gesture. */
        yhello_chrome_fallback(app, ev);
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_SCROLL: {
        /* Positions are scaled like the others; the wheel deltas are not. */
        struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_scroll(
            app->engine, ev->mouse_scroll.x / scale, ev->mouse_scroll.y / scale,
            ev->mouse_scroll.dx, ev->mouse_scroll.dy);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        if (app->yframework->event_loop->ops->request_render) {
            app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_motion(
            app->engine, ev->mouse.x / scale, ev->mouse.y / scale);
        int consumed = YETTY_IS_OK(r) && r.value;
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        /* Client-first fallback: chrome gets the move (resize-edge cursor /
         * in-progress drag) only if no widget claimed it. */
        if (!consumed) {
            yhello_chrome_fallback(app, ev);
        }
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
    if (app->yframework && app->yframework->event_loop && app->yframework->event_loop->ops->stop) {
        app->yframework->event_loop->ops->stop(app->yframework->event_loop);
    }
}

/*
 * yclass app wrapper. The standalone window host is a yapp:app subclass; the
 * heavy per-run state stays in `struct app` (shared with client mode), which the
 * data block just embeds. codegen sees this class because the Makefile passes
 * YETTY_YHELLO_HAS_STANDALONE via YCLASS_DEFINES; main.gen.c is #included at the
 * foot, inside the same guard, so reduced builds never compile it.
 */
struct YETTY_ANNOTATE("class@yhello:app") YETTY_ANNOTATE("parent@yapp:app") yetty_yhello_app {
    struct app app;
};

YETTY_YRESULT_DECLARE(yetty_yhello_app_ptr, struct yetty_yhello_app *);
struct yetty_yclass_ptr_result yetty_yhello_app_class_get(void);
struct yetty_yhello_app_ptr_result yetty_yhello_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yhello_app_create(struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols. yhello has its own dual-mode main(), so it
 * drives this sequence directly rather than via the shared ymain/glfw.c. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_default_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result yhello_app_init(struct yetty_yclass_object *obj,
                                                      struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result standalone_worker(struct yetty_yclass_object *obj,
                                                        struct yetty_yclass_object *platform)
{
    struct yetty_yhello_app_ptr_result app_res = yetty_yhello_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yhello:app:run: app_from");
    struct app *app = &app_res.value->app;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "yhello:app:run: gpu_context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "yhello:app:run: input_pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;
    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yhello:app:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "standalone: yframework_create");
    app->yframework = frr.value;
    app->render_target = app->yframework->render_target;

    /* MSDF font for the receiver-side ygrid (glyph expansion). */
    {
        const char *fonts_dir =
            app->yframework->config->ops->get_string(app->yframework->config, "paths/fonts", "");
        const char *shaders_dir =
            app->yframework->config->ops->get_string(app->yframework->config, "paths/shaders", "");
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 "DejaVuSansMNerdFontMono");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yhello_default");
        if (YETTY_IS_ERR(fr)) {
            /* The MSDF glyph cdb + shader are NOT embedded in yhello — they
             * come from the main yetty install's asset extraction into the
             * shared data dir. Name the exact paths on stderr (the Result msg
             * can only carry a static literal, and the trace log is off by
             * default) so the failure is actionable, then propagate. */
            fprintf(stderr,
                    "yhello: cannot load MSDF font — required runtime assets are missing.\n"
                    "  glyph cdb: %s\n"
                    "  shader:    %s\n"
                    "  (provided by the main yetty install; run yetty once to extract fonts)\n",
                    cdb_path, shader_path);
            return YETTY_ERR(yetty_ycore_void, "standalone: msdf_font_create", fr);
        }
        app->font = fr.value;
        struct yetty_ycore_void_result load = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load, "standalone: load_basic_latin");
    }

    /* Raw figure factory — needed for the yplot / yimage producer
     * kinds. Same wiring yui.c uses (yui_create lines 506-571). */
    {
        struct yetty_ydraw_composite_factory_ptr_result ffr = yetty_ydraw_composite_factory_create(
            app->yframework->gpu.device, app->yframework->gpu.queue,
            app->yframework->gpu.surface_format, app->yframework->gpu.allocator,
            app->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "standalone: raw_composite_factory_create");
        app->composite_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            yplot_f->destroy = yetty_yplot_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(app->composite_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yplot_factory_destroy(yplot_f);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            yimage_f->destroy = yetty_yimage_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(app->composite_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yimage_factory_destroy(yimage_f);
            }
        }
    }

    /* Figure registry — primitive widgets land in ygrid; producer
     * widgets (yimage, yplot) get their own kind→factory binding. */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "standalone: registry_create");
        app->figure_registry = reg.value;
        app->figure_args.default_font = app->font;
        app->figure_args.composite_factory = app->composite_factory;
        /* ygui chrome: producer figures (yplot/yimage/yvideo) are laid out in
         * logical pixels and must be scaled to framebuffer by content_scale
         * (HiDPI). Their widgets emit at the absolute widget rect to match. */
        app->figure_args.absolute_coords = 1;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(app->figure_registry, &app->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "standalone: ygrid_register_factory");
        static const uint32_t producer_kinds[] = {
            YHELLO_YFIGURE_KIND_YPLOT,
            YHELLO_YFIGURE_KIND_YIMAGE,
            YHELLO_YFIGURE_KIND_YVIDEO,
        };
        for (size_t i = 0; i < sizeof(producer_kinds) / sizeof(producer_kinds[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                app->figure_registry, producer_kinds[i], &app->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr,
                                "standalone: ygrid_register_factory_for_kind");
        }
        /* yshadertoy has its own factory + renderer (not the ygrid path). */
        struct yetty_ycore_void_result sfr =
            yetty_yshadertoy_register_factory(app->figure_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sfr, "standalone: yshadertoy_register_factory");
    }

    /* Local container. The context lives on `app` (program-lifetime) so it
     * outlives this worker — the container stores the pointer and mints its
     * ygrid figures lazily, after the worker has returned on webasm. */
    app->ctx = (struct yetty_context){.runtime = app->yframework,
                                      .event_loop = app->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)gpu->surface_width, (float)gpu->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "standalone: container_create");
        app->root_container = obj_res.value;
        yetty_yfigure_container_set_context(app->root_container, &app->ctx);
        yetty_yfigure_container_set_registry(app->root_container, app->figure_registry);
        yetty_yfigure_container_set_rect(app->root_container, root_rect);
    }

    /* In-process DIRECT dispatch (same path yui.c uses): the framework ships
     * its records straight into root_container via the yclass slot path. */
    {
        struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "standalone: framework_create");
        app->engine = fr.value;
        struct yetty_ycore_void_result scr =
            yetty_ygui_framework_set_container_obj(app->engine, app->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "standalone: set_container_obj");
        float cs = app_content_scale(app);
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            app->engine, (float)gpu->surface_width / cs, (float)gpu->surface_height / cs);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
    }

    struct key_ctx kc = {.app = app, .stop_cb = standalone_stop};
    app->stop_cb = standalone_stop;
    yetty_ygui_framework_set_key_cb(app->engine, on_key, &kc);

    struct yetty_ycore_void_result br = build_ui(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "standalone: build_ui");

    /* Window chrome: draggable/resizable titlebar + min/max/close (SDF, no
     * font), composited as a pinned figure over the greeter UI. */
    {
        struct yetty_ychrome_host_ptr_result chrome_r = yetty_ychrome_host_create(
            app->root_container, app->font, &app->ctx, app->yframework->window_chrome,
            (float)gpu->surface_width, (float)gpu->surface_height, 36.0f, 8.0f,
            YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_r)) {
            app->chrome = chrome_r.value;
        } else {
            ywarn("yhello standalone: chrome host create failed: %s", chrome_r.error.msg);
            yetty_ycore_error_destroy(chrome_r.error);
        }
    }

    app->listener.handler = standalone_event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(app->yframework->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "standalone: register_default_listeners");

    /* ~30 fps animation pump for self-animating widgets (ymaze tab). */
    {
        struct yetty_yevent_event_loop *loop = app->yframework->event_loop;
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        if (YETTY_IS_OK(tr)) {
            app->frame_timer = tr.value;
            app->frame_listener.handler = standalone_frame_tick;
            struct yetty_ycore_void_result cr = loop->ops->config_timer(loop, app->frame_timer, 33);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
            struct yetty_ycore_void_result lr =
                loop->ops->register_timer_listener(loop, app->frame_timer, &app->frame_listener);
            if (YETTY_IS_ERR(lr)) {
                yetty_ycore_error_destroy(lr.error);
            }
            struct yetty_ycore_void_result st = loop->ops->start_timer(loop, app->frame_timer);
            if (YETTY_IS_ERR(st)) {
                yetty_ycore_error_destroy(st.error);
            }
        } else {
            yetty_ycore_error_destroy(tr.error);
        }
    }

    /* Kick first frame. */
    yetty_yevent_post_async(input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        app->yframework->event_loop->ops->start(app->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

#ifdef __EMSCRIPTEN__
    /* CRITICAL webasm difference: event_loop->start() registers the
     * emscripten main loop (emscripten_set_main_loop_arg) and returns
     * IMMEDIATELY — it does not block like the desktop/libuv loop. The
     * teardown below is desktop shutdown code; running it now would
     * destroy the ygui engine, render target and GPU device microseconds
     * after creation, so the browser-driven frames render nothing. The
     * app must stay alive for program lifetime (the browser owns the
     * loop after we return), so bail out before teardown — same as
     * yetty's own webasm worker, which intentionally leaks. */
    yinfo("yhello: standalone running on webasm — leaving app alive, "
          "browser drives frames (engine=%p render_target=%p)",
          (void *)app->engine, (void *)app->render_target);
    return YETTY_OK_VOID();
#endif

    if (app->chrome) {
        struct yetty_ycore_void_result dc = yetty_ychrome_host_destroy(app->chrome);
        if (YETTY_IS_ERR(dc)) {
            yetty_ycore_error_destroy(dc.error);
        }
        app->chrome = NULL;
    }
    if (app->engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(app->engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        app->engine = NULL;
    }
    if (app->root_container) {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(app->root_container);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        app->root_container = NULL;
    }
    if (app->figure_registry) {
        yetty_yfigure_registry_destroy(app->figure_registry);
        app->figure_registry = NULL;
    }
    if (app->composite_factory) {
        yetty_ydraw_composite_factory_destroy(app->composite_factory);
        app->composite_factory = NULL;
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
        for (int i = 0; i < app->image_path_count; ++i) {
            free(app->image_paths[i]);
        }
        free(app->image_paths);
        app->image_paths = NULL;
        app->image_path_count = 0;
    }
    if (app->video_paths) {
        for (int i = 0; i < app->video_path_count; ++i) {
            free(app->video_paths[i]);
        }
        free(app->video_paths);
        app->video_paths = NULL;
        app->video_path_count = 0;
    }
    free(app->readme_path);
    app->readme_path = NULL;
    free(app->pdf_path);
    app->pdf_path = NULL;
    return YETTY_OK_VOID();
}

/* Confirm the embedded assets the rich-content tabs depend on actually
 * landed on disk. Extraction can report "success" while leaving nothing
 * behind — a build without incbin (the no-op extractor stub), a stale
 * skip-marker sitting over a wiped data dir, or a partial brotli decode
 * all do it. Without this check the missing files are swallowed twice
 * over (discover_* returns silently, rebuild_top discards the load
 * error), so the tabs render blank instead of failing. Surface it as a
 * hard error so startup aborts with a clear message — the same way yetty
 * itself dies when its own asset extraction fails. */
static struct yetty_ycore_void_result yhello_verify_assets(const char *data_dir)
{
    static const char *const required[] = {
        "logo-1.jpeg",           "logo-2.jpeg",    "logo-3.jpeg", "logo-4.jpeg",
        "yetty-unchained-2.mp4", "pdf-sample.pdf", "README.md",
    };
    int missing = 0;
    char path_buf[1024];
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", data_dir, required[i]);
        if (yetty_yplatform_file_exists(path_buf)) {
            continue;
        }
        yerror("yhello: required runtime asset missing: %s", path_buf);
        missing++;
    }
    if (missing > 0) {
#ifdef __EMSCRIPTEN__
        /* On the web build the logos / intro video / pdf samples aren't
         * bundled yet (yhello's incbin path is compiled out; only the
         * shared font/shader assets are preloaded). Those drive the
         * Images / Video / PDF tabs, which already degrade to blank
         * rather than crash — so warn and continue instead of aborting
         * the whole showcase. The rest of the tabs render normally. */
        ywarn("yhello: %d showcase asset(s) missing on web — Images/Video/PDF "
              "tabs will be blank; the rest of the showcase still renders",
              missing);
        return YETTY_OK_VOID();
#else
        return YETTY_ERR(yetty_ycore_void,
                         "yhello: required runtime assets are missing from the data dir "
                         "(embedded-asset extraction produced none) — see log for the list");
#endif
    }
    return YETTY_OK_VOID();
}

/* yhello's own incbin extractor (logos + demo video), used by the Android
 * program-init below. Desktop/web place assets via the installer / bundle. */
YETTY_MAYBE_UNUSED static struct yetty_ycore_void_result yhello_extract_assets_cb(void)
{
    char data_dir_buf[512];
    yhello_data_dir(data_dir_buf, sizeof(data_dir_buf));
    const char *data_dir = data_dir_buf;
    if (!data_dir || !*data_dir) {
        return YETTY_ERR(yetty_ycore_void,
                         "yhello: could not resolve a data dir for asset extraction");
    }
    if (yhello_embedded_assets_extract(data_dir) != 0) {
        return YETTY_ERR(yetty_ycore_void, "yhello: embedded asset extraction failed");
    }
    return yhello_verify_assets(data_dir);
}

#if defined(__ANDROID__)
/* Render-thread payload: the heap app object + the synthetic runtime that
 * standalone_worker reads during setup. Both must outlive program_init's
 * stack — standalone_worker runs the event loop to completion on this thread,
 * so the thread owns them and frees them when it returns. */
struct yhello_android_thread_args {
    struct yetty_yclass_object *app_obj;  /* yhello:app object (owns struct app) */
    struct yetty_yclass_object *platform; /* bring-up-state carrier (NDK owns the loop) */
};

YETTY_EXTERNAL_CALLBACK
static void *yhello_android_render_thread(void *arg)
{
    struct yhello_android_thread_args *targs = arg;
    /* Blocks: builds the framework, the UI and the chrome, runs the event
     * loop, then tears the whole lot down when the loop is stopped (the
     * non-emscripten tail of standalone_worker). The Android standalone path
     * uses the yplatform Android entry (content-scale + NDK glue now in
     * src/yetty/yplatform/ymain/android-glue.c). The call below tracks the
     * migrated yapp:app:run signature. */
    struct yetty_ycore_void_result run_res = standalone_worker(targs->app_obj, targs->platform);
    if (YETTY_IS_ERR(run_res)) {
        LOGE("yhello standalone worker: %s",
             run_res.error.msg ? run_res.error.msg : "(no message)");
        yetty_ycore_error_destroy(run_res.error);
    }
    if (targs->platform) {
        (void)yetty_yclass_object_free(targs->platform);
    }
    free(targs);
    return NULL;
}

/* Android program entry — resolved at link time by android-glue.c. Builds the
 * standalone showcase from the live surface and runs it on a render thread,
 * mirroring the terminal's yetty_android_program_init in src/yetty/yplatform/ymain/android.c. */
YETTY_EXTERNAL_CALLBACK
void yetty_android_program_init(struct yetty_yplatform_app_state *state)
{
    if (state->initialized || !state->window) {
        return;
    }

    LOGI("Initializing yhello...");

    /* Extract yhello's embedded assets (shaders, MSDF cdb font, logos, intro
     * video, sample pdf, README); the extractor creates its own target dirs.
     * The showcase reads its font + shaders from config (paths/fonts,
     * paths/shaders), which yconfig_create resolves via the platform paths
     * abstraction — the tool never touches the path getters directly. */
    {
        struct yetty_ycore_void_result extract_res = yhello_extract_assets_cb();
        if (YETTY_IS_ERR(extract_res)) {
            LOGE("yhello: asset extraction failed: %s",
                 extract_res.error.msg ? extract_res.error.msg : "(no message)");
            yetty_ycore_error_destroy(extract_res.error);
            return;
        }
    }

    /* No --qemu: yhello standalone renders its own figure tree, no VM. */
    {
        char *fake_argv[] = {(char *)"yhello", NULL};
        struct yetty_yconfig_result config_result = yetty_yconfig_create(1, fake_argv);
        if (!YETTY_IS_OK(config_result)) {
            LOGE("yhello: config create failed");
            return;
        }
        state->config = config_result.value;
    }

    struct yetty_yplatform_input_pipe_result pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        LOGE("yhello: input pipe create failed");
        return;
    }
    state->pipe = pipe_result.value;

    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    state->instance = wgpuCreateInstance(&instance_desc);
    if (!state->instance) {
        LOGE("yhello: WebGPU instance create failed");
        return;
    }

    state->surface = yetty_yplatform_create_surface_from_window(state->instance, state->window);
    if (!state->surface) {
        LOGE("yhello: surface create failed");
        return;
    }

    int32_t width = ANativeWindow_getWidth(state->window);
    int32_t height = ANativeWindow_getHeight(state->window);

    /* The yapp:app object owns the embedded struct app; create it through the
     * class so run() can resolve it. (Registration is idempotent.) */
    (void)yetty_yplatform_register();
    (void)yetty_yapp_register();
    struct yetty_yclass_object_ptr_result app_res = yetty_yhello_app_create(NULL);
    struct yhello_android_thread_args *targs = calloc(1, sizeof(*targs));
    if (YETTY_IS_ERR(app_res) || !targs) {
        LOGE("yhello: out of memory / app create failed");
        if (YETTY_IS_ERR(app_res)) {
            yetty_ycore_error_destroy(app_res.error);
        }
        free(targs);
        return;
    }
    struct yetty_yhello_app_ptr_result app_data = yetty_yhello_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        yetty_ycore_error_destroy(app_data.error);
        free(targs);
        return;
    }
    struct app *app = &app_data.value->app;
    targs->app_obj = app_res.value;

    /* Register the platform classes and create a platform object to carry the
     * bring-up state by hand (Android doesn't go through the GLFW platform run).
     * FIXME: the new yplatform Android entry must supply the content-scale path. */
    struct yetty_ycore_void_result plat_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(plat_reg)) {
        LOGE("yhello: platform register failed: %s",
             plat_reg.error.msg ? plat_reg.error.msg : "(no message)");
        yetty_ycore_error_destroy(plat_reg.error);
        free(targs);
        return;
    }
    struct yetty_yclass_object_ptr_result plat_res = yetty_yplatform_platform_create(NULL);
    if (!YETTY_IS_OK(plat_res)) {
        LOGE("yhello: platform create failed: %s",
             plat_res.error.msg ? plat_res.error.msg : "(no message)");
        yetty_ycore_error_destroy(plat_res.error);
        free(targs);
        return;
    }
    targs->platform = plat_res.value;

    struct yetty_yplatform_gpu_context gpu = {
        .instance = state->instance,
        .surface = state->surface,
        .surface_width = (uint32_t)width,
        .surface_height = (uint32_t)height,
        .content_scale = yetty_yplatform_android_content_scale(state->app),
    };
    struct yetty_ycore_void_result populate =
        yetty_yplatform_platform_set_gpu_context(targs->platform, &gpu);
    if (YETTY_IS_OK(populate)) {
        populate = yetty_yplatform_platform_set_services(targs->platform, state->config,
                                                         state->pipe, NULL, NULL);
    }
    if (YETTY_IS_ERR(populate)) {
        LOGE("yhello: platform populate failed: %s",
             populate.error.msg ? populate.error.msg : "(no message)");
        yetty_ycore_error_destroy(populate.error);
        (void)yetty_yclass_object_free(targs->platform);
        free(targs);
        return;
    }

    state->program_state = app;
    /* The showcase is pointer-driven; never auto-pop the soft IME. */
    state->suppress_soft_keyboard = 1;
    state->initialized = 1;
    state->running = 1;
    pthread_create(&state->render_thread, NULL, yhello_android_render_thread, targs);

    /* Initial resize so the container + chrome get the real surface size. */
    {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_RESIZE;
        ev.resize.width = (float)width;
        ev.resize.height = (float)height;
        state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
    }

    LOGI("yhello initialized successfully");
}

struct yetty_ycore_void_result yetty_android_program_term(struct yetty_yplatform_app_state *state)
{
    if (!state->initialized) {
        return YETTY_OK_VOID();
    }

    LOGI("Terminating yhello...");

    /* Stop the event loop; standalone_worker then unwinds its own teardown
     * (framework, engine, container, surface + instance) on the render thread
     * before returning. */
    struct app *app = state->program_state;
    state->running = 0;
    if (app) {
        standalone_stop(app);
    }
    if (state->render_thread) {
        pthread_join(state->render_thread, NULL);
        state->render_thread = 0;
    }
    /* app + the thread args were freed by the render thread; the surface and
     * instance were released by standalone_worker's yframework teardown. */
    state->program_state = NULL;
    state->surface = NULL;
    state->instance = NULL;

    if (state->pipe) {
        state->pipe->ops->destroy(state->pipe);
        state->pipe = NULL;
    }
    if (state->config) {
        state->config->ops->destroy(state->config);
        state->config = NULL;
    }
    state->initialized = 0;
    return YETTY_OK_VOID();
}
#endif /* __ANDROID__ */

#ifndef __ANDROID__
static int run_standalone_mode(int argc, char **argv)
{
    /* The app object's data block (struct yetty_yhello_app embedding struct app)
     * is heap-allocated by yetty_yhello_app_create and never freed before exit —
     * which also covers the webasm case where standalone_worker returns
     * immediately but the browser keeps driving frames through callbacks that
     * dereference `app` (a stack app would be a use-after-free there). */
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "yhello: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "yhello: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_yhello_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yhello: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res =
        yetty_yplatform_default_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yhello: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yhello: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}
#endif /* !__ANDROID__ — run_standalone_mode (drives the yplatform sequence) */

/* yclass glue for yhello:app — compiled in every standalone build (desktop, web,
 * Android), outside the __ANDROID__ split above. */
#include "main.gen.c"

#endif /* YETTY_YHELLO_HAS_STANDALONE */

#ifndef __ANDROID__
int main(int argc, char **argv)
{
    ytrace_init();
#ifdef YETTY_YHELLO_HAS_STANDALONE
    return run_standalone_mode(argc, argv);
#else
    (void)argc;
    (void)argv;
    fprintf(stderr, "yhello: standalone mode unavailable — built without webgpu.\n");
    return 1;
#endif
}
#endif /* !__ANDROID__ — desktop/web dispatcher */
