/*
 * ygreeter — the "first contact" tool shown to the user when a yetty
 * instance boots up the RISCV Linux VM (also useful standalone via
 * `yetty -e ./ygreeter` on any platform).
 *
 * Layout: a single full-canvas ygui app driving a tabbar at the top with
 * a per-tab body underneath. Every body is an hbox: a small navigation
 * tree on the left, a `rich` content surface on the right. The rich
 * widget holds a ydraw-core buffer built from an inline YAML — same
 * vocabulary as demo/scripts/ydraw/scrolling/.
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ [Welcome] [Plots] [Images] [Code]                            │
 *   ├──────────────────┬───────────────────────────────────────────┤
 *   │  ▾ Intro         │                                            │
 *   │     What is yetty│       <rich content for selected node>    │
 *   │     Quick start  │                                            │
 *   │     Capabilities │                                            │
 *   └──────────────────┴───────────────────────────────────────────┘
 *
 * Tabs:
 *   Welcome — rich-text intro authored as ydraw TEXT spans with mixed
 *             font sizes / colors. Short on purpose so it stays
 *             readable on phone-sized cards.
 *   Plots   — yplot demos (sin/cos, parabola, decay, ...).
 *   Images  — image rendering placeholders; nodes that have a path bound
 *             load via yimage YAML.
 *   Code    — colourised code snippets (text spans coloured by token
 *             class — mirrors what `ycat --ts` produces, just authored
 *             inline so we don't pull in tree-sitter for the v1 tool).
 *
 * Press 'q' to quit. Tree clicks update the right pane.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yplatform/fs.h>   /* path_dirname, path_realpath, file_is_regular */
#include <yetty/yplatform/term.h> /* term_get_size */
#include <yetty/yplatform/tty.h>  /* stderr-rerouting probe */
#include <yetty/ygui/ygui.h>
#ifdef YGREETER_HAS_YBROWSER
#include <yetty/ygui/ygui_ybrowser.h>
#endif
#ifdef YGREETER_HAS_YMARKDOWN
#include <yetty/ygui/ygui_ymarkdown.h>
#endif
#ifdef YGREETER_HAS_YPDF
#include <yetty/ygui/ygui_ypdf.h>
#endif
#ifdef YGREETER_HAS_YZOO
#include <yetty/ygui/ygui_yzoo.h>
#include <yetty/yzoo/yzoo.h>
#endif
#ifdef YGREETER_HAS_YJUNGLE
#include <yetty/ygui/ygui_yjungle.h>
#include <yetty/yjungle/yjungle.h>
#endif
#include <yetty/ytrace/ytrace.h>

/* Upper bound on the tab count. Built-ins (Welcome, Plots, Images,
 * Code) plus the widget-showcase tabs (Markdown, Browser, PDF) plus
 * a few free slots for runtime experimentation. */
#define YGREETER_TAB_MAX 12

/* =========================================================================
 * Tab-local navigation entry. Each entry binds a tree-row label to the
 * YAML payload that should be loaded into the tab's `rich` widget when the
 * row is selected. Some payloads share content across rows (e.g. multiple
 * "view the same image" rows); that's fine — they just point at the same
 * literal.
 * ========================================================================= */
struct nav_entry {
    const char *label;
    const char *yaml;
};

/* =========================================================================
 * Welcome tab content.
 *
 * Short blocks: a heading, a tagline, a two-line "what's special" pitch,
 * and a hint to use the other tabs. Authored as ydraw TEXT spans so the
 * various sizes / colors actually render on the GPU canvas (the regular
 * ygui label widget doesn't carry inline colour runs).
 * ========================================================================= */

#define WELCOME_INTRO                                                                              \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [24, 56]\n"                                                                   \
    "      content: \"Welcome to yetty\"\n"                                                        \
    "      font-size: 36\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 96]\n"                                                                   \
    "      content: \"A GPU terminal that draws more than text.\"\n"                               \
    "      font-size: 18\n"                                                                        \
    "      color: \"#9ad7ff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 140]\n"                                                                  \
    "      content: \"Plots, images, rich docs — all next to your shell.\"\n"                      \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 170]\n"                                                                  \
    "      content: \"Switch tabs to see what the GPU layer can do.\"\n"                           \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 220]\n"                                                                  \
    "      content: \"Press 'q' to quit.\"\n"                                                      \
    "      font-size: 14\n"                                                                        \
    "      color: \"#888888\"\n"

#define WELCOME_QUICKSTART                                                                         \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [24, 48]\n"                                                                   \
    "      content: \"Quick start\"\n"                                                             \
    "      font-size: 28\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 92]\n"                                                                   \
    "      content: \"$ ycat README.md\"\n"                                                        \
    "      font-size: 16\n"                                                                        \
    "      color: \"#a3e635\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 116]\n"                                                                  \
    "      content: \"render Markdown / PDFs / images in-line\"\n"                                 \
    "      font-size: 14\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 154]\n"                                                                  \
    "      content: \"$ yplot 'sin(x+t); cos(x+t)'\"\n"                                            \
    "      font-size: 16\n"                                                                        \
    "      color: \"#a3e635\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 178]\n"                                                                  \
    "      content: \"GPU function plots from a single expression\"\n"                             \
    "      font-size: 14\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 216]\n"                                                                  \
    "      content: \"$ ytop\"\n"                                                                  \
    "      font-size: 16\n"                                                                        \
    "      color: \"#a3e635\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 240]\n"                                                                  \
    "      content: \"a ygui-built process monitor\"\n"                                            \
    "      font-size: 14\n"                                                                        \
    "      color: \"#cccccc\"\n"

#define WELCOME_CAPS                                                                               \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [24, 48]\n"                                                                   \
    "      content: \"What's inside\"\n"                                                           \
    "      font-size: 28\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 90]\n"                                                                   \
    "      content: \"• Multiple GPU-rendered layers\"\n"                                          \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 116]\n"                                                                  \
    "      content: \"• MSDF font glyphs at any size\"\n"                                          \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 142]\n"                                                                  \
    "      content: \"• SDF primitives — boxes, circles, segments\"\n"                             \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 168]\n"                                                                  \
    "      content: \"• yplot — GPU function plotting\"\n"                                         \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 194]\n"                                                                  \
    "      content: \"• yimage — PNG / JPEG via stb_image\"\n"                                     \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [24, 220]\n"                                                                  \
    "      content: \"• ygui — a flex-layout widget toolkit\"\n"                                   \
    "      font-size: 16\n"                                                                        \
    "      color: \"#cccccc\"\n"

static const struct nav_entry WELCOME_NAV[] = {
    {"What is yetty", WELCOME_INTRO},
    {"Quick start", WELCOME_QUICKSTART},
    {"Capabilities", WELCOME_CAPS},
};

/* =========================================================================
 * Plots tab content.
 *
 * Several yplot demos showing increasingly busy compositions. The yplot
 * complex prim takes its bounds inside the buffer's coordinate system;
 * the rich widget translates those bounds by the widget's resolved
 * layout box, so authoring is purely widget-local.
 * ========================================================================= */

#define PLOT_TRIG                                                                                  \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"sin(x+t) and cos(x+t)\"\n"                                                   \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [-6.2832, 6.2832]\n"                                                           \
    "      y_range: [-1.5, 1.5]\n"                                                                 \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      functions:\n"                                                                           \
    "        - expr: \"sin(x+t)\"\n"                                                               \
    "          color: \"#ff6b6b\"\n"                                                               \
    "        - expr: \"cos(x+t)\"\n"                                                               \
    "          color: \"#4ecdc4\"\n"

#define PLOT_PARABOLA                                                                              \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"x*x  and  2*x + 1\"\n"                                                       \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [-5, 5]\n"                                                                     \
    "      y_range: [-2, 12]\n"                                                                    \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      functions:\n"                                                                           \
    "        - expr: \"x*x\"\n"                                                                    \
    "          color: \"#ffe66d\"\n"                                                               \
    "        - expr: \"2*x + 1\"\n"                                                                \
    "          color: \"#aa96da\"\n"

#define PLOT_DECAY                                                                                 \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"exp(-x*x/4) * sin(3*x)\"\n"                                                  \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [-6, 6]\n"                                                                     \
    "      y_range: [-1.2, 1.2]\n"                                                                 \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      functions:\n"                                                                           \
    "        - expr: \"exp(-x*x/4) * sin(3*x)\"\n"                                                 \
    "          color: \"#74c0fc\"\n"

/* The entries below use the new top-level `source:` YAML key, which feeds
 * the whole yexpr-plot text into the parser in one go — so `name=buffer`
 * declarations, inline `x=A..B` / `@view=` framing, and `@<name>.color=`
 * overrides all work. The legacy `functions:` list is bypassed. */

#define PLOT_INLINE_BUFFER                                                                         \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"inline buffer: 8 samples spread over x\"\n"                                  \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [0, 1]\n"                                                                      \
    "      y_range: [-1, 1]\n"                                                                     \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      source: \"data=buffer; @data.size=8; "                                                  \
    "@data.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2; @data.color=#74C5A5\"\n"

#define PLOT_BUFFER_MODULATED                                                                      \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"envelope(x) * sin(x*60)\"\n"                                                 \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [0, 1]\n"                                                                      \
    "      y_range: [-1.1, 1.1]\n"                                                                 \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      source: \"envelope=buffer; @envelope.size=8; "                                          \
    "@envelope.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2; "                                             \
    "pulse=envelope(x)*sin(x*60); "                                                                \
    "@envelope.color=#364A47; @pulse.color=#74C5A5\"\n"

#define PLOT_ADSR_HARMONICS                                                                        \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"ADSR envelope x three harmonics\"\n"                                         \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [0, 1]\n"                                                                      \
    "      y_range: [-1.1, 1.1]\n"                                                                 \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      source: \"env=buffer; @env.size=16; "                                                   \
    "@env.values=0,0.4,0.8,1,0.95,0.85,0.75,0.65,0.55,0.45,0.35,0.25,0.18,0.12,0.06,0; "           \
    "h1=env(x)*sin(x*6); h2=env(x)*sin(x*12); h3=env(x)*sin(x*24); "                               \
    "@env.color=#364A47; @h1.color=#FF6B6B; @h2.color=#FFE66D; @h3.color=#74C5A5\"\n"

#define PLOT_TRAVELLING_PHASE                                                                      \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"static envelope x travelling phase (time)\"\n"                               \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      x_range: [0, 1]\n"                                                                      \
    "      y_range: [-1.1, 1.1]\n"                                                                 \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      source: \"env=buffer; @env.size=12; "                                                   \
    "@env.values=0,0.3,0.7,1,0.95,0.85,0.7,0.5,0.3,0.15,0.05,0; "                                  \
    "travel=env(x)*sin(x*40 - time*4); "                                                           \
    "@env.color=#5A8979; @travel.color=#74C5A5\"\n"

#define PLOT_DOMAIN_VIEW                                                                           \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"inline x=-3pi..3pi, view zoomed to -pi..pi\"\n"                              \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - yplot:\n"                                                                                 \
    "      position: [16, 80]\n"                                                                   \
    "      size: [460, 280]\n"                                                                     \
    "      show_grid: true\n"                                                                      \
    "      show_axes: true\n"                                                                      \
    "      show_labels: true\n"                                                                    \
    "      source: \"x=-10..10; @view=-pi..pi,-0.5..1.5; "                                        \
    "signal=sin(x)/x; @signal.color=#74C5A5\"\n"

static const struct nav_entry PLOT_NAV[] = {
    {"Trigonometry", PLOT_TRIG},
    {"Polynomial", PLOT_PARABOLA},
    {"Damped wave", PLOT_DECAY},
    {"Inline buffer", PLOT_INLINE_BUFFER},
    {"Buffer x carrier", PLOT_BUFFER_MODULATED},
    {"ADSR harmonics", PLOT_ADSR_HARMONICS},
    {"Travelling phase", PLOT_TRAVELLING_PHASE},
    {"Domain & view", PLOT_DOMAIN_VIEW},
};

/* =========================================================================
 * Images tab content.
 *
 * yimage takes a path in the YAML. For the v1 tool we ship a simple
 * "what would render here" placeholder for each row — the user can swap
 * a real path in via env var (YGREETER_IMAGE=/path/to/foo.png) and the
 * placeholder gets replaced at startup. Keeping inline content static
 * so the file stays readable; a path-substitution helper builds the
 * real YAML when an env var is set.
 * ========================================================================= */

#define IMAGE_PLACEHOLDER_FOR(_title, _hint)                                                       \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"" _title "\"\n"                                                              \
    "      font-size: 22\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 84]\n"                                                                   \
    "      content: \"" _hint "\"\n"                                                               \
    "      font-size: 14\n"                                                                        \
    "      color: \"#bbbbbb\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 112]\n"                                                                  \
    "      content: \"Set YGREETER_IMAGE=/path/to/image.png to load.\"\n"                          \
    "      font-size: 14\n"                                                                        \
    "      color: \"#888888\"\n"                                                                   \
    "  - box:\n"                                                                                   \
    "      position: [16, 150]\n"                                                                  \
    "      size: [460, 240]\n"                                                                     \
    "      fill: \"#1f2330\"\n"                                                                    \
    "      stroke: \"#2c3447\"\n"                                                                  \
    "      stroke-width: 2\n"                                                                      \
    "      round: 8\n"                                                                             \
    "  - text:\n"                                                                                  \
    "      position: [180, 280]\n"                                                                 \
    "      content: \"[ image goes here ]\"\n"                                                     \
    "      font-size: 16\n"                                                                        \
    "      color: \"#666e85\"\n"

/* Placeholder YAML for an image row when its file isn't on disk. The
 * Images tab's nav entries are built at startup from whatever
 * docs/logo-*.jpeg files exist, so the placeholder is only seen when
 * a probe failed mid-way through. */
#define IMAGE_FALLBACK_YAML                                                                        \
    IMAGE_PLACEHOLDER_FOR("Image not found",                                                       \
                          "ygreeter probes docs/logo-*.jpeg relative to the executable.")

/* Heap-owned: built at startup from discover_logo_images(), freed
 * before exit. Parallel arrays — entry N's label is g_image_nav[N],
 * the file it loads is g_image_paths[N]. */
static struct nav_entry *g_image_nav = NULL;
static char **g_image_paths = NULL;
static int g_image_path_count = 0;

/* Build a ydraw buffer holding ONE yimage prim plus a TEXT_SPAN title.
 * The ydraw-yaml parser doesn't support `yimage:` blocks today
 * (yaml_factory: none in yimage.yaml), so we go through yimage's C API
 * directly and append the title via the buffer's add_text helper.
 *
 * Caller owns the returned buffer; ownership is transferred to the rich
 * widget when handed off via set_buffer. */
#include <yetty/yimage/yimage.h>

static struct yetty_ydraw_draw_list *build_image_buffer(const char *title, const char *path)
{
    ydebug("build_image_buffer: ENTER title='%s' path='%s'", title ? title : "(null)",
           path ? path : "(null)");
    /* Image bounds: position 80 (same y-clearance the plot tab uses
     * over its 22pt title so the heading isn't covered) and 2× the
     * previous draw area so the bundled logos render at a respectable
     * size on a typical card. */
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 16.0f,
        .bounds_y = 80.0f,
        .bounds_w = 920.0f,
        .bounds_h = 640.0f,
    };
    struct yetty_ydraw_draw_list_result r = yetty_yimage_render_path(path, &cfg);
    if (YETTY_IS_ERR(r)) {
        ydebug("build_image_buffer: yimage_render_path FAILED: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    ydebug("build_image_buffer: yimage_render_path OK, buf=%p", (void *)r.value);
    /* Title TEXT_SPAN drawn above the image. The buffer's add_text takes
     * a yetty_ycore_buffer view so we wrap the literal in place. */
    if (title && *title) {
        struct yetty_ycore_buffer text_view = {
            .data = (uint8_t *)title,
            .size = strlen(title),
            .capacity = strlen(title),
        };
        struct yetty_ycore_void_result tr =
            yetty_ydraw_draw_list_add_text(r.value, 16.0f, 36.0f, &text_view, 22.0f,
                                           /*color (ABGR)=*/0xFFFFFFFFu, /*layer=*/0,
                                           /*font_id=*/-1, /*rotation=*/0.0f);
        if (YETTY_IS_ERR(tr)) {
            yetty_ycore_error_destroy(tr.error);
            /* keep the image even if the title failed */
        }
    }
    return r.value;
}

static int path_exists(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    return yetty_yplatform_file_is_regular(path);
}

/* Resolve a path relative to argv[0]'s directory walking up `levels`
 * times. Used to locate bundled assets (assets/logo.jpeg, docs/logo.jpeg)
 * regardless of where the user invokes the binary from. Returns a
 * malloc'd path or NULL. */
static char *resolve_relative_to_exe(const char *argv0, int up_levels, const char *suffix)
{
    if (!argv0) {
        return NULL;
    }
    char *cur = yetty_yplatform_path_realpath(argv0);
    if (!cur) {
        return NULL;
    }
    for (int i = 0; i < up_levels; i++) {
        /* PATH_MAX is the conservative upper bound for filesystem paths
         * on every platform we target; ygreeter walks at most a handful
         * of dirs up from argv[0], so this stays comfortably below. */
        char parent[4096];
        if (yetty_yplatform_path_dirname(cur, parent, sizeof(parent)) != 0) {
            free(cur);
            return NULL;
        }
        char *next = strdup(parent);
        free(cur);
        cur = next;
        if (!cur) {
            return NULL;
        }
    }
    size_t need = strlen(cur) + 1 + strlen(suffix) + 1;
    char *out = (char *)malloc(need);
    if (!out) {
        free(cur);
        return NULL;
    }
    snprintf(out, need, "%s/%s", cur, suffix);
    free(cur);
    return out;
}

/* Try to find a bundled image to use for the Images tab. Probe order:
 *   1. $YGREETER_IMAGE (caller override)
 *   2. <repo>/assets/logo.jpeg
 *   3. <repo>/docs/logo-1.jpeg ... logo-4.jpeg
 *   4. <repo>/assets/apple-touch-icon.jpg
 *
 * The "repo root" is derived from argv[0]. With a CMake build the
 * binary sits at <repo>/build-X/tools/ygreeter/ygreeter — that's 4
 * dirname() calls away from the assets/. An installed layout may be
 * shallower; we try a couple of plausible levels until one resolves. */
static char *find_repo_image(const char *argv0, const char *relative)
{
    static const int candidate_levels[] = {4, 3, 2, 1};
    for (size_t i = 0; i < sizeof(candidate_levels) / sizeof(candidate_levels[0]); i++) {
        char *p = resolve_relative_to_exe(argv0, candidate_levels[i], relative);
        if (p && path_exists(p)) {
            return p;
        }
        free(p);
    }
    return NULL;
}

/* Discover every docs/logo-N.jpeg that exists relative to the binary
 * and populate g_image_paths / g_image_nav. One nav entry per file so
 * the Images tab's left rail flexes from 1 to N rows depending on how
 * many bundled logos are shipped. The yaml field is the fallback
 * placeholder — load_entry routes through build_image_buffer when the
 * file path resolves, so the YAML is only seen on decode failure. */
static void discover_logo_images(const char *argv0)
{
    /* Probe a generous upper bound on logo numbers. The repo currently
     * ships logo-1..logo-4 in docs/, but a future build might add
     * more — we stop at the first miss after at least one hit, with
     * a hard ceiling so a broken numbering scheme can't loop. */
    enum { MAX_LOGOS = 32 };
    char *paths_tmp[MAX_LOGOS];
    int count = 0;
    /* Optional user override comes first so YGREETER_IMAGE still shows
     * up as the leading entry. */
    const char *env = getenv("YGREETER_IMAGE");
    if (env && *env && path_exists(env)) {
        paths_tmp[count++] = strdup(env);
    }
    for (int i = 1; i <= MAX_LOGOS && count < MAX_LOGOS; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "docs/logo-%d.jpeg", i);
        char *p = find_repo_image(argv0, rel);
        if (!p) {
            /* allow gaps in numbering up to logo-8 before giving up */
            if (i > 8) {
                break;
            }
            continue;
        }
        paths_tmp[count++] = p;
    }
    /* assets/logo.jpeg as a final fallback when nothing else resolved */
    if (count == 0) {
        char *p = find_repo_image(argv0, "assets/logo.jpeg");
        if (p) {
            paths_tmp[count++] = p;
        }
    }
    if (count == 0) {
        return;
    }
    g_image_paths = (char **)calloc((size_t)count, sizeof(char *));
    g_image_nav = (struct nav_entry *)calloc((size_t)count, sizeof(struct nav_entry));
    if (!g_image_paths || !g_image_nav) {
        for (int i = 0; i < count; i++) {
            free(paths_tmp[i]);
        }
        free(g_image_paths);
        g_image_paths = NULL;
        free(g_image_nav);
        g_image_nav = NULL;
        return;
    }
    /* The label memory has to outlive build_tab_body — heap-allocated
     * strings owned by ygreeter; freed in free_image_nav() at exit. */
    for (int i = 0; i < count; i++) {
        g_image_paths[i] = paths_tmp[i];
        char label[64];
        const char *base = strrchr(paths_tmp[i], '/');
        base = base ? base + 1 : paths_tmp[i];
        snprintf(label, sizeof(label), "%.60s", base);
        g_image_nav[i].label = strdup(label);
        g_image_nav[i].yaml = IMAGE_FALLBACK_YAML;
    }
    g_image_path_count = count;
}

static void free_image_nav(void)
{
    for (int i = 0; i < g_image_path_count; i++) {
        free(g_image_paths[i]);
        free((char *)g_image_nav[i].label);
    }
    free(g_image_paths);
    g_image_paths = NULL;
    free(g_image_nav);
    g_image_nav = NULL;
    g_image_path_count = 0;
}

/* =========================================================================
 * Code tab content.
 *
 * The user asked for "code viewing using the ycat feature". ycat builds a
 * ydraw buffer of coloured TEXT_SPAN prims via its tree-sitter backend;
 * linking yetty_ycat pulls in libmagic + tree-sitter grammars (~few MB).
 * For the v1 tool we author the same shape inline — TEXT spans with
 * token-class colors — so the demo runs anywhere ygui runs (including
 * the RISCV browser build, which doesn't link tree-sitter). When the
 * RISCV path gets ycat support, swap these literals for
 * yetty_ycat_ts_render() output + yetty_ygui_widget_rich_set_buffer().
 * ========================================================================= */

#define CODE_HELLO                                                                                 \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"hello.c\"\n"                                                                 \
    "      font-size: 18\n"                                                                        \
    "      color: \"#9ad7ff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 80]\n"                                                                   \
    "      content: \"#include\"\n"                                                                \
    "      font-size: 15\n"                                                                        \
    "      color: \"#c586c0\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [100, 80]\n"                                                                  \
    "      content: \"<stdio.h>\"\n"                                                               \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 120]\n"                                                                  \
    "      content: \"int\"\n"                                                                     \
    "      font-size: 15\n"                                                                        \
    "      color: \"#569cd6\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [56, 120]\n"                                                                  \
    "      content: \"main(void) {\"\n"                                                            \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 148]\n"                                                                  \
    "      content: \"printf(\"\n"                                                                 \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [104, 148]\n"                                                                 \
    "      content: \"\\\"hello, yetty\\\\n\\\"\"\n"                                               \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [240, 148]\n"                                                                 \
    "      content: \");\"\n"                                                                      \
    "      font-size: 15\n"                                                                        \
    "      color: \"#d4d4d4\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 176]\n"                                                                  \
    "      content: \"return\"\n"                                                                  \
    "      font-size: 15\n"                                                                        \
    "      color: \"#c586c0\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [104, 176]\n"                                                                 \
    "      content: \"0\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#b5cea8\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [120, 176]\n"                                                                 \
    "      content: \";\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#d4d4d4\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 204]\n"                                                                  \
    "      content: \"}\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#d4d4d4\"\n"

#define CODE_PYTHON                                                                                \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"hello.py\"\n"                                                                \
    "      font-size: 18\n"                                                                        \
    "      color: \"#9ad7ff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 80]\n"                                                                   \
    "      content: \"def\"\n"                                                                     \
    "      font-size: 15\n"                                                                        \
    "      color: \"#569cd6\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [56, 80]\n"                                                                   \
    "      content: \"greet(name):\"\n"                                                            \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [40, 108]\n"                                                                  \
    "      content: \"print(\"\n"                                                                  \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [104, 108]\n"                                                                 \
    "      content: \"f\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#c586c0\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [116, 108]\n"                                                                 \
    "      content: \"\\\"hello, {name}\\\"\"\n"                                                   \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [280, 108]\n"                                                                 \
    "      content: \")\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#d4d4d4\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 148]\n"                                                                  \
    "      content: \"greet(\"\n"                                                                  \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [76, 148]\n"                                                                  \
    "      content: \"\\\"yetty\\\"\"\n"                                                           \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [136, 148]\n"                                                                 \
    "      content: \")\"\n"                                                                       \
    "      font-size: 15\n"                                                                        \
    "      color: \"#d4d4d4\"\n"

#define CODE_SHELL                                                                                 \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [16, 36]\n"                                                                   \
    "      content: \"hello.sh\"\n"                                                                \
    "      font-size: 18\n"                                                                        \
    "      color: \"#9ad7ff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 80]\n"                                                                   \
    "      content: \"#!/usr/bin/env bash\"\n"                                                     \
    "      font-size: 15\n"                                                                        \
    "      color: \"#6a9955\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 120]\n"                                                                  \
    "      content: \"name=\"\n"                                                                   \
    "      font-size: 15\n"                                                                        \
    "      color: \"#9cdcfe\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [72, 120]\n"                                                                  \
    "      content: \"\\\"yetty\\\"\"\n"                                                           \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [16, 160]\n"                                                                  \
    "      content: \"echo\"\n"                                                                    \
    "      font-size: 15\n"                                                                        \
    "      color: \"#dcdcaa\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [60, 160]\n"                                                                  \
    "      content: \"\\\"hello, $name\\\"\"\n"                                                    \
    "      font-size: 15\n"                                                                        \
    "      color: \"#ce9178\"\n"

static const struct nav_entry CODE_NAV[] = {
    {"hello.c", CODE_HELLO},
    {"hello.py", CODE_PYTHON},
    {"hello.sh", CODE_SHELL},
};

/* =========================================================================
 * Application state.
 *
 * Per tab we own the left-nav list, the right-side rich widget, and the
 * array of nav entries currently bound to that nav. A click on a tree
 * row resolves to its tab + index via `rebind_*` userdata so we can
 * swap the rich content.
 * ========================================================================= */
struct tab_state {
    struct yetty_ygui_widget *nav_list;
    struct yetty_ygui_widget *rich;
    const struct nav_entry *entries;
    int n_entries;
};

/* The Images tab uses g_image_paths[entry_index] (built at startup
 * by discover_logo_images) instead of a single resolved image. Kept
 * as a local in main() rather than a separate global, so the
 * pre-discovery placeholder behaviour for $YGREETER_IMAGE still
 * works as a fallback when no logo-* files were located. */

/* Index of the Images tab. Tracked as a runtime variable so that
 * closing earlier tabs (which shifts subsequent indices down) keeps
 * the image-tab dispatch in sync. -1 = the Images tab has been
 * closed and there is no image-bearing tab to short-circuit for. */
static int g_images_tab_index = 2;

/* Indices of the Yzoo / Yjungle tabs — same rationale as
 * g_images_tab_index. -1 = the tab was either never created
 * (feature disabled) or was closed; the anim_set_running probe must
 * see -1 to know the tab is gone. The values are kept in sync by
 * on_tab_close. */
#ifdef YGREETER_HAS_YZOO
static int g_yzoo_tab_index = -1;
#endif
#ifdef YGREETER_HAS_YJUNGLE
static int g_yjungle_tab_index = -1;
#endif

#if defined(YGREETER_HAS_YZOO) || defined(YGREETER_HAS_YJUNGLE)
#include <time.h>
#include <uv.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>

/* Linked from libygui_yzoo / libygui_yjungle (same symbol, exported by
 * both since ygui_flatten.c is compiled into each). Walks a draw_list,
 * recurses into CMD_GROUP payloads, drops CMD_ZERO / CMD_DELETE, copies
 * paint primitives into `dst`. The internal header is in src/ygui/
 * which isn't on ygreeter's public include path — forward-declare
 * locally. */
struct yetty_ydraw_draw_list;
struct yetty_ycore_void_result yetty_ygui_flatten_draw_list(
    struct yetty_ydraw_draw_list *dst, const struct yetty_ydraw_draw_list *src);

static uint64_t ygreeter_mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}
#endif

#ifdef YGREETER_HAS_YZOO
/* yzoo is a per-frame-FULL-state producer: yzoo_render() clears the
 * supplied buffer and writes CMD_ZERO + all current primitives. So
 * one yzoo_render → flatten → rich_set_buffer cycle per tick. */
struct yzoo_anim {
    struct yetty_ygui_engine     *engine;
    struct yetty_yzoo            *zoo;
    struct yetty_ydraw_draw_list *raw;   /* what yzoo_render writes into */
    struct yetty_ydraw_draw_list *flat;  /* owned by rich widget once attached */
    struct yetty_ygui_widget     *rich;
    struct yetty_ygui_widget     *tab;
    uv_timer_t                    timer;
    bool                          timer_inited;
    bool                          running;
    uint32_t                      seed;
    float                         scene_w;
    float                         scene_h;
    uint64_t                      start_ms;
};
#endif

#ifdef YGREETER_HAS_YJUNGLE
/* yjungle is an INCREMENTAL producer: each tick emits CMD_ZERO (only on
 * the first tick), CMD_DELETE(old_id) and CMD_GROUP(new_id) for the
 * segments that changed this event. We maintain a parallel `live[]`
 * array of top-level segment records (id + raw GROUP bytes) so we can
 * reconstruct the current scene on demand without re-implementing
 * scene-canvas state. */
struct yj_live_seg {
    uint32_t id;
    uint8_t *bytes; /* full CMD_GROUP record (12-byte header + payload) */
    size_t   size;
};

struct yjungle_anim {
    struct yetty_ygui_engine     *engine;
    struct yetty_yjungle         *jungle;
    struct yetty_ydraw_draw_list *delta; /* what yjungle_tick writes into */
    struct yetty_ydraw_draw_list *acc;   /* rebuilt each tick from live[] */
    struct yetty_ydraw_draw_list *flat;  /* owned by rich widget once attached */
    struct yetty_ygui_widget     *rich;
    struct yetty_ygui_widget     *tab;
    uv_timer_t                    timer;
    bool                          timer_inited;
    bool                          running;
    uint32_t                      seed;
    float                         scene_w;
    float                         scene_h;
    uint64_t                      start_ms;
    struct yj_live_seg           *live;
    size_t                        n_live;
    size_t                        cap_live;
};
#endif

/* Forward decls — bodies live just before on_resize, but on_tab_close
 * / on_tab_change (defined earlier in the file) need to call them. */
#ifdef YGREETER_HAS_YZOO
static void yzoo_anim_set_running(struct yzoo_anim *a, bool run);
static void yzoo_anim_on_tab_closed(struct yzoo_anim *a);
#endif
#ifdef YGREETER_HAS_YJUNGLE
static void yj_anim_set_running(struct yjungle_anim *a, bool run);
static void yj_anim_on_tab_closed(struct yjungle_anim *a);
#endif

struct app {
    struct yetty_ygui_engine *engine;
    struct yetty_ygui_widget *outer;
    struct yetty_ygui_widget *tabbar;
    struct tab_state tabs[YGREETER_TAB_MAX];
    /* About dialog. Three top-level widgets hidden until the menu's
     * About entry opens them. They're re-centred on engine resize. The
     * popup carries the modal overlay + drop shadow; the rich widget
     * holds the body content; the button closes the dialog. */
    struct yetty_ygui_widget *about_popup;
    struct yetty_ygui_widget *about_rich;
    struct yetty_ygui_widget *about_close;
    /* Animated yzoo / yjungle tabs. Each anim owns its producer,
     * the (raw) producer buffer, the (flat) rich-widget buffer, a
     * uv_timer driving ticks, and any live-state bookkeeping the
     * producer's wire shape needs. Defined just before on_resize. */
#ifdef YGREETER_HAS_YZOO
    struct yzoo_anim yzoo;
#endif
#ifdef YGREETER_HAS_YJUNGLE
    struct yjungle_anim yjungle;
#endif
};

/* Per-nav-row binding. One of these is allocated for every row at
 * build_tab_body() and handed to the row button as click_userdata. Each
 * row_link is its own heap allocation so the address is stable even as
 * the bookkeeping array grows; the array only stores pointers for
 * shutdown cleanup. */
struct row_link {
    struct app *app;
    int tab_index;
    int entry_index;
};

static struct row_link **g_row_links = NULL;
static int g_row_link_count = 0;
static int g_row_link_cap = 0;

static struct row_link *new_row_link(struct app *app, int tab, int entry)
{
    if (g_row_link_count >= g_row_link_cap) {
        int nc = g_row_link_cap ? g_row_link_cap * 2 : 32;
        struct row_link **n =
            (struct row_link **)realloc(g_row_links, (size_t)nc * sizeof(struct row_link *));
        if (!n) {
            return NULL;
        }
        g_row_links = n;
        g_row_link_cap = nc;
    }
    struct row_link *rl = (struct row_link *)calloc(1, sizeof(*rl));
    if (!rl) {
        return NULL;
    }
    rl->app = app;
    rl->tab_index = tab;
    rl->entry_index = entry;
    g_row_links[g_row_link_count++] = rl;
    return rl;
}

static void free_row_links(void)
{
    for (int i = 0; i < g_row_link_count; i++) {
        free(g_row_links[i]);
    }
    free(g_row_links);
    g_row_links = NULL;
    g_row_link_count = 0;
    g_row_link_cap = 0;
}

static const char *yaml_for(const struct tab_state *t, int entry_index)
{
    return t->entries[entry_index].yaml;
}

static void load_entry(struct app *app, int tab_index, int entry_index)
{
    if (tab_index < 0 || tab_index >= YGREETER_TAB_MAX) {
        return;
    }
    struct tab_state *t = &app->tabs[tab_index];
    if (entry_index < 0 || entry_index >= t->n_entries) {
        return;
    }
    /* Images tab: pick the per-entry file from g_image_paths and build
     * a fresh ydraw buffer (yimage prim + title TEXT_SPAN). Falls
     * through to the placeholder YAML when nothing was discovered or
     * the file decode failed. */
    if (tab_index == g_images_tab_index && entry_index < g_image_path_count && g_image_paths &&
        g_image_paths[entry_index]) {
        struct yetty_ydraw_draw_list *buf =
            build_image_buffer(t->entries[entry_index].label, g_image_paths[entry_index]);
        if (buf) {
            yetty_ygui_widget_rich_set_buffer(t->rich, buf);
            return;
        }
    }
    const char *yaml = yaml_for(t, entry_index);
    struct yetty_ycore_void_result r = yetty_ygui_widget_rich_set_yaml(t->rich, yaml, strlen(yaml));
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Click handler bound per-button. Each nav-row button carries its own
 * row_link (allocated in register_row_link) as click_userdata, so we
 * don't have to look up the click target after dispatch.
 *
 * row_link->app is set to NULL by on_tab_close when the row's tab gets
 * removed — the button is already freed by then, so the callback
 * should never fire, but the guard makes the path explicit. */
static void on_row_clicked(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    const struct row_link *rl = (const struct row_link *)userdata;
    if (!rl || !rl->app) {
        return;
    }
    if (yetty_ygui_widget_tabbar_get_active(rl->app->tabbar) != rl->tab_index) {
        yetty_ygui_widget_tabbar_set_active(rl->app->tabbar, rl->tab_index);
    }
    load_entry(rl->app, rl->tab_index, rl->entry_index);
}

/* Fired when the user clicks the close 'x' on a tab. We own the
 * decision here: ygreeter keeps a parallel app.tabs[] array indexed
 * by the tabbar's tab index, plus a side table of row_links carrying
 * tab indices. When a tab is closed those indices shift down — fix
 * the bookkeeping BEFORE handing off to tabbar_remove_tab, otherwise
 * the change_callback fires for the new active tab while ygreeter is
 * still pointing at the closed tab's freed widgets (= dangling
 * pointer -> heap corruption -> "app freezes"). */
static void on_tab_close(struct yetty_ygui_widget *tabbar, float value, void *userdata)
{
    struct app *app = (struct app *)userdata;
    int idx = (int)value;
    int n = yetty_ygui_widget_tabbar_count(tabbar);
    if (!app || idx < 0 || idx >= n) {
        return;
    }

    /* Shift app->tabs[] left starting from idx. The trailing slot
     * is zeroed so any stale access bails on the n_entries==0
     * check inside load_entry. */
    int max = (int)(sizeof(app->tabs) / sizeof(app->tabs[0]));
    for (int i = idx; i < n - 1 && i + 1 < max; i++) {
        app->tabs[i] = app->tabs[i + 1];
    }
    if (n - 1 < max) {
        memset(&app->tabs[n - 1], 0, sizeof(app->tabs[n - 1]));
    }

    /* Fix up the per-row userdata: any row_link pointing at the
     * closed tab gets its app pointer NUL'd so the now-unreachable
     * row-click handler (its button is freed below) is a no-op even
     * if the engine somehow still routes a phantom event to it. Row
     * links for tabs AFTER the closed one shift their tab_index
     * down by one to stay aligned with the new compact tabs[]. */
    for (int i = 0; i < g_row_link_count; i++) {
        struct row_link *rl = g_row_links[i];
        if (!rl) {
            continue;
        }
        if (rl->tab_index == idx) {
            rl->app = NULL;
        } else if (rl->tab_index > idx) {
            rl->tab_index--;
        }
    }

    /* Keep the images-tab dispatch in sync. */
    if (g_images_tab_index >= 0) {
        if (idx == g_images_tab_index) {
            g_images_tab_index = -1;
        } else if (idx < g_images_tab_index) {
            g_images_tab_index--;
        }
    }

    /* Same bookkeeping for the yzoo / yjungle showcase tabs — drop our
     * dangling widget/tab pointers when the tab itself goes away,
     * otherwise the next resize-driven rebuild would chase a freed
     * tabbar child. */
#ifdef YGREETER_HAS_YZOO
    if (g_yzoo_tab_index >= 0) {
        if (idx == g_yzoo_tab_index) {
            g_yzoo_tab_index = -1;
            yzoo_anim_on_tab_closed(&app->yzoo);
        } else if (idx < g_yzoo_tab_index) {
            g_yzoo_tab_index--;
        }
    }
#endif
#ifdef YGREETER_HAS_YJUNGLE
    if (g_yjungle_tab_index >= 0) {
        if (idx == g_yjungle_tab_index) {
            g_yjungle_tab_index = -1;
            yj_anim_on_tab_closed(&app->yjungle);
        } else if (idx < g_yjungle_tab_index) {
            g_yjungle_tab_index--;
        }
    }
#endif

    /* Now safe to actually drop the tab — widgets get freed but
     * nothing left in app->tabs[] / g_row_links[] still references
     * them. */
    yetty_ygui_widget_tabbar_remove_tab(tabbar, idx);
}

static void on_tab_change(struct yetty_ygui_widget *tabbar, float value, void *userdata)
{
    (void)tabbar;
    struct app *app = (struct app *)userdata;
    int idx = (int)value;
    /* Default: load the first entry of the newly-active tab — only
     * applies to the first 4 tabs (Welcome / Plots / Images / Code),
     * which are the ones that use the nav+rich pattern. */
    if (idx >= 0 && idx < 4) {
        load_entry(app, idx, 0);
    }
    /* Drive the yzoo / yjungle animation timers based on which tab is
     * now active. Pausing on tab-switch keeps idle CPU low; resuming
     * picks the producer's internal clock back up where it left off. */
#ifdef YGREETER_HAS_YZOO
    yzoo_anim_set_running(&app->yzoo, idx == g_yzoo_tab_index);
#endif
#ifdef YGREETER_HAS_YJUNGLE
    yj_anim_set_running(&app->yjungle, idx == g_yjungle_tab_index);
#endif
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
    }
}

/* About dialog content. Body of the popup; rendered as a rich widget
 * inside the dialog. Authored in widget-local pixel coordinates so the
 * rich widget translates everything to the dialog's resolved layout
 * box automatically. */
#define ABOUT_BODY_YAML                                                                            \
    "body:\n"                                                                                      \
    "  - text:\n"                                                                                  \
    "      position: [0, 20]\n"                                                                    \
    "      content: \"yetty\"\n"                                                                   \
    "      font-size: 28\n"                                                                        \
    "      color: \"#ffffff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [0, 60]\n"                                                                    \
    "      content: \"A GPU-accelerated terminal.\"\n"                                             \
    "      font-size: 16\n"                                                                        \
    "      color: \"#9ad7ff\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [0, 96]\n"                                                                    \
    "      content: \"Renders text, plots, images and rich docs\"\n"                               \
    "      font-size: 14\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [0, 116]\n"                                                                   \
    "      content: \"side by side with your shell.\"\n"                                           \
    "      font-size: 14\n"                                                                        \
    "      color: \"#cccccc\"\n"                                                                   \
    "  - text:\n"                                                                                  \
    "      position: [0, 156]\n"                                                                   \
    "      content: \"ygreeter — first-contact tool\"\n"                                           \
    "      font-size: 13\n"                                                                        \
    "      color: \"#888888\"\n"

/* Dialog geometry. Kept here so reposition_about_dialog and the
 * constructor share constants without spelling them out twice. */
#define ABOUT_W 460.0f
#define ABOUT_H 280.0f
#define ABOUT_BODY_PAD_X 24.0f
#define ABOUT_BODY_PAD_TOP 52.0f /* leaves room for the popup's header band */
#define ABOUT_BUTTON_W 90.0f
#define ABOUT_BUTTON_H 28.0f
#define ABOUT_BUTTON_PAD 14.0f

/* Re-centre the About dialog and reposition its body / close-button
 * children to match. Called when the dialog is opened and on every
 * engine resize so the dialog stays anchored to the middle of the
 * canvas. Children of a popup are positioned in ABSOLUTE canvas
 * coordinates (popup_render_all doesn't push ctx->offset for its
 * children — see demo/ygui/16_new_widgets/main.c for the pattern). */
static void reposition_about_dialog(struct app *app)
{
    if (!app || !app->about_popup) {
        return;
    }
    float ew = 0.0f, eh = 0.0f;
    yetty_ygui_engine_get_size(app->engine, &ew, &eh);
    if (ew <= 0.0f || eh <= 0.0f) {
        return;
    }
    float x = (ew - ABOUT_W) * 0.5f;
    float y = (eh - ABOUT_H) * 0.5f;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    yetty_ygui_widget_set_position(app->about_popup, x, y);
    yetty_ygui_widget_popup_set_scene_size(app->about_popup, ew, eh);

    if (app->about_rich) {
        yetty_ygui_widget_set_position(app->about_rich, x + ABOUT_BODY_PAD_X,
                                       y + ABOUT_BODY_PAD_TOP);
    }
    if (app->about_close) {
        yetty_ygui_widget_set_position(app->about_close,
                                       x + ABOUT_W - ABOUT_BUTTON_W - ABOUT_BUTTON_PAD,
                                       y + ABOUT_H - ABOUT_BUTTON_H - ABOUT_BUTTON_PAD);
    }
}

static void about_dialog_set_open(struct app *app, int open)
{
    if (!app || !app->about_popup) {
        return;
    }
    if (open) {
        reposition_about_dialog(app);
    }
    yetty_ygui_widget_popup_set_open(app->about_popup, open);
    /* The rich + close button live as top-level siblings (not popup
     * children of the rendering machinery — see popup_render_all
     * comment). Toggle their visibility in lockstep with the popup's
     * OPEN flag. */
    if (app->about_rich) {
        yetty_ygui_widget_set_visible(app->about_rich, open);
    }
    if (app->about_close) {
        yetty_ygui_widget_set_visible(app->about_close, open);
    }
}

static void on_about_close_click(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    about_dialog_set_open((struct app *)userdata, 0);
}

/* Menu item handlers. Each takes the standard click signature; the
 * widget arg is the menu itself (the engine fires the callback as if
 * the menu had been clicked). We route via the `app` pointer stashed
 * in userdata. */
static void on_menu_close(struct yetty_ygui_widget *widget, void *userdata)
{
    (void)widget;
    struct app *app = (struct app *)userdata;
    if (app && app->engine) {
        yetty_ygui_engine_close_preserve(app->engine);
    }
}

static void on_menu_about(struct yetty_ygui_widget *widget, void *userdata)
{
    (void)widget;
    about_dialog_set_open((struct app *)userdata, 1);
}

static void build_about_dialog(struct app *app)
{
    /* Popup carries the header label, modal overlay, drop shadow and
     * the rounded body. The two extra widgets ride on top as
     * top-level siblings (a popup's children render at absolute
     * coordinates anyway — see reposition_about_dialog comment). */
    app->about_popup =
        yetty_ygui_engine_popup(app->engine, "about_popup", 0, 0, ABOUT_W, ABOUT_H, "About");
    yetty_ygui_widget_popup_set_modal(app->about_popup, 1);
    yetty_ygui_widget_popup_set_open(app->about_popup, 0);

    app->about_rich = yetty_ygui_engine_rich_from_yaml(
        app->engine, "about_rich", 0, 0, ABOUT_W - 2 * ABOUT_BODY_PAD_X,
        ABOUT_H - ABOUT_BODY_PAD_TOP - ABOUT_BUTTON_H - 2 * ABOUT_BUTTON_PAD, ABOUT_BODY_YAML,
        strlen(ABOUT_BODY_YAML));
    yetty_ygui_widget_set_visible(app->about_rich, 0);

    app->about_close = yetty_ygui_engine_button(app->engine, "about_close", 0, 0, ABOUT_BUTTON_W,
                                                ABOUT_BUTTON_H, "Close");
    yetty_ygui_widget_button_on_click(app->about_close, on_about_close_click, app);
    yetty_ygui_widget_set_visible(app->about_close, 0);
}

/* =========================================================================
 * yzoo / yjungle animated tabs.
 *
 * Both libraries are pure producers of ydraw primitives — they need a
 * driver. The standalone ./tools/yzoo and ./tools/yjungle tools drive
 * them with their own libuv loops and ship envelopes to a host yetty.
 * Inside ygreeter we drive them directly: a uv_timer fires while the
 * tab is open, the producer's output is flattened to paint-only
 * primitives, and the rich widget repaints.
 *
 * The animation pauses when the tab loses focus (the timer is stopped
 * in on_tab_change) and resumes when it gains focus again.
 * ========================================================================= */

#ifdef YGREETER_HAS_YZOO
static void yzoo_anim_tick(struct yzoo_anim *a)
{
    if (!a || !a->zoo || !a->raw || !a->flat || !a->rich) {
        return;
    }
    float t = (float)((double)(ygreeter_mono_ms() - a->start_ms) / 1000.0);
    struct yetty_ycore_void_result rr = yetty_yzoo_render(a->zoo, a->raw, t);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return;
    }
    /* The flat buffer is owned by the rich widget; clear + refill, then
     * re-set so the widget's dirty bit flips. */
    yetty_ydraw_draw_list_clear(a->flat);
    yetty_ydraw_draw_list_set_scene_bounds(a->flat, 0.0f, 0.0f, a->scene_w, a->scene_h);
    struct yetty_ycore_void_result fr = yetty_ygui_flatten_draw_list(a->flat, a->raw);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return;
    }
    yetty_ygui_widget_rich_set_buffer(a->rich, a->flat);
}

YETTY_EXTERNAL_CALLBACK
static void on_yzoo_timer(uv_timer_t *t) { yzoo_anim_tick((struct yzoo_anim *)t->data); }

static void yzoo_anim_drop_producer(struct yzoo_anim *a)
{
    if (a->zoo) {
        yetty_yzoo_destroy(a->zoo);
        a->zoo = NULL;
    }
    if (a->raw) {
        yetty_ydraw_draw_list_destroy(a->raw);
        a->raw = NULL;
    }
}

static void yzoo_anim_drop_widgets(struct yzoo_anim *a)
{
    if (a->rich) {
        /* Removing the rich widget also frees the flat buffer it owns
         * via the engine's widget teardown. Null both so a re-attach
         * starts from a clean slate. */
        yetty_ygui_widget_remove(a->rich);
        a->rich = NULL;
        a->flat = NULL;
    }
}

/* Look up the tab's resolved content area. The tabbar puts its tabs
 * inside the window body (between the menubar + statusbar chrome and
 * the tab strip itself), so the engine canvas size is NOT the right
 * scene size for yzoo / yjungle — using it would generate primitives
 * for a scene larger than the actual viewport, with most of the content
 * spilling off-pane.
 *
 * The resolved layout box is only valid after engine_layout (or
 * engine_render) has computed it, so we trigger a layout pass first.
 * Fallback to engine size if the tab is somehow zero-sized (e.g. the
 * tab hasn't been activated yet on backends that defer panel layout). */
static void ygreeter_tab_viewport(struct yetty_ygui_engine *engine,
                                  struct yetty_ygui_widget *tab,
                                  float *out_w, float *out_h)
{
    struct yetty_ycore_void_result lr = yetty_ygui_engine_layout(engine);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
    }
    float w = 0.0f, h = 0.0f;
    yetty_ygui_widget_get_layout_box(tab, NULL, NULL, &w, &h);
    if (w < 1.0f || h < 1.0f) {
        yetty_ygui_engine_get_size(engine, &w, &h);
    }
    if (w < 1.0f) w = 800.0f;
    if (h < 1.0f) h = 600.0f;
    *out_w = w;
    *out_h = h;
}

static void yzoo_anim_init(struct yzoo_anim *a, struct yetty_ygui_engine *engine,
                           struct yetty_ygui_widget *tab, uint32_t seed)
{
    /* Stash the addressables. Producer + widget creation is deferred
     * to yzoo_anim_ensure_attached so the scene can be built at the
     * tab's actual resolved size (which isn't known until the engine
     * has run at least one layout pass). */
    a->engine = engine;
    a->tab = tab;
    a->seed = seed;
}

static void yzoo_anim_attach(struct yzoo_anim *a, struct yetty_ygui_engine *engine,
                             struct yetty_ygui_widget *tab, float w, float h)
{
    yzoo_anim_drop_widgets(a);
    yzoo_anim_drop_producer(a);
    a->engine = engine;
    a->tab = tab;
    a->scene_w = w;
    a->scene_h = h;

    struct yetty_yzoo_config cfg = yetty_yzoo_config_default();
    cfg.scene_width = w;
    cfg.scene_height = h;
    struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, a->seed);
    if (YETTY_IS_ERR(zr)) {
        yetty_ycore_error_destroy(zr.error);
        return;
    }
    a->zoo = zr.value;
    struct yetty_ydraw_draw_list_config bc = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = w, .scene_max_y = h,
    };
    struct yetty_ydraw_draw_list_result rr = yetty_ydraw_draw_list_config_buffer_create(&bc);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        yzoo_anim_drop_producer(a);
        return;
    }
    a->raw = rr.value;
    struct yetty_ydraw_draw_list_result fr = yetty_ydraw_draw_list_config_buffer_create(&bc);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        yzoo_anim_drop_producer(a);
        return;
    }
    a->flat = fr.value;
    a->rich = yetty_ygui_engine_rich(engine, "yzoo_view", 0, 0, w, h);
    if (!a->rich) {
        yetty_ydraw_draw_list_destroy(a->flat);
        a->flat = NULL;
        yzoo_anim_drop_producer(a);
        return;
    }
    yetty_ygui_widget_apply_css(a->rich, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(tab, a->rich);
    yetty_ygui_widget_rich_set_buffer(a->rich, a->flat); /* widget owns flat */

    a->start_ms = ygreeter_mono_ms();
    if (!a->timer_inited) {
        uv_loop_t *loop = yetty_ygui_engine_get_loop(engine);
        uv_timer_init(loop, &a->timer);
        a->timer.data = a;
        a->timer_inited = true;
    }
    yzoo_anim_tick(a); /* initial frame so the tab isn't blank */
}

/* Make sure the producer + rich widget exist at the tab's CURRENT
 * resolved size. No-op when already attached at the same size; rebuilds
 * when the size has changed (e.g. after a window resize) or when
 * nothing has been attached yet. Safe to call from on_tab_change and
 * on_resize. */
static void yzoo_anim_ensure_attached(struct yzoo_anim *a)
{
    if (!a || !a->engine || !a->tab) {
        return;
    }
    float w = 0.0f, h = 0.0f;
    ygreeter_tab_viewport(a->engine, a->tab, &w, &h);
    if (a->rich && a->scene_w == w && a->scene_h == h) {
        return;
    }
    yzoo_anim_attach(a, a->engine, a->tab, w, h);
}

static void yzoo_anim_set_running(struct yzoo_anim *a, bool run)
{
    if (!a) {
        return;
    }
    if (run) {
        yzoo_anim_ensure_attached(a);
        if (!a->timer_inited || a->running) {
            return;
        }
        /* ~30 fps — same cadence the standalone yzoo tool runs at. */
        uv_timer_start(&a->timer, on_yzoo_timer, /*initial=*/0, /*repeat=*/33);
        a->running = true;
    } else if (a->timer_inited && a->running) {
        uv_timer_stop(&a->timer);
        a->running = false;
    }
}

static void yzoo_anim_on_tab_closed(struct yzoo_anim *a)
{
    yzoo_anim_set_running(a, false);
    /* The tab tree owns the rich widget (and through it the flat
     * buffer) — they're already freed by tabbar_remove_tab when we
     * get here. Just clear our pointers so the next attach starts
     * fresh, and drop the producer-side state we still own. */
    a->rich = NULL;
    a->flat = NULL;
    a->tab = NULL;
    yzoo_anim_drop_producer(a);
}

static void yzoo_anim_shutdown(struct yzoo_anim *a)
{
    if (!a) {
        return;
    }
    if (a->timer_inited) {
        if (a->running) {
            uv_timer_stop(&a->timer);
        }
        uv_close((uv_handle_t *)&a->timer, NULL);
        a->timer_inited = false;
        a->running = false;
    }
    /* rich + flat are torn down with the engine; producer / raw we own. */
    yzoo_anim_drop_producer(a);
    a->rich = NULL;
    a->flat = NULL;
}
#endif /* YGREETER_HAS_YZOO */

#ifdef YGREETER_HAS_YJUNGLE
static void yj_live_clear(struct yjungle_anim *a)
{
    for (size_t i = 0; i < a->n_live; i++) {
        free(a->live[i].bytes);
    }
    a->n_live = 0;
}

static void yj_live_remove(struct yjungle_anim *a, uint32_t id)
{
    for (size_t i = 0; i < a->n_live; i++) {
        if (a->live[i].id == id) {
            free(a->live[i].bytes);
            a->live[i] = a->live[a->n_live - 1];
            a->n_live--;
            return;
        }
    }
}

static int yj_live_append(struct yjungle_anim *a, uint32_t id, const uint8_t *src, size_t size)
{
    if (a->n_live == a->cap_live) {
        size_t nc = a->cap_live ? a->cap_live * 2 : 64;
        struct yj_live_seg *nl = (struct yj_live_seg *)realloc(a->live, nc * sizeof(*nl));
        if (!nl) {
            return -1;
        }
        a->live = nl;
        a->cap_live = nc;
    }
    uint8_t *copy = (uint8_t *)malloc(size);
    if (!copy) {
        return -1;
    }
    memcpy(copy, src, size);
    a->live[a->n_live].id = id;
    a->live[a->n_live].bytes = copy;
    a->live[a->n_live].size = size;
    a->n_live++;
    return 0;
}

/* Parse one tick's delta records, mutating the live[] mirror. The wire
 * layouts are the same the receiver's scene-canvas processes, so this
 * is the minimum subset of process_group_body needed to keep a flat
 * view of the chain alive across deltas. */
static void yj_apply_delta(struct yjungle_anim *a)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(a->delta);
    size_t len = yetty_ydraw_draw_list_size(a->delta);
    if (!bytes || len == 0) {
        return;
    }
    size_t off = 0;
    while (off + sizeof(uint32_t) <= len) {
        uint32_t type = *(const uint32_t *)(bytes + off);
        if (type == YETTY_YDRAW_CMD_ZERO) {
            yj_live_clear(a);
            if (off + 8 > len) break;
            off += 8;
            continue;
        }
        if (type == YETTY_YDRAW_CMD_DELETE) {
            if (off + 12 > len) break;
            uint32_t id = ((const uint32_t *)(bytes + off))[1];
            yj_live_remove(a, id);
            off += 12;
            continue;
        }
        if (type == YETTY_YDRAW_CMD_GROUP) {
            if (off + 12 > len) break;
            uint32_t id = ((const uint32_t *)(bytes + off))[1];
            uint32_t payload_size = ((const uint32_t *)(bytes + off))[2];
            size_t total = (size_t)12 + payload_size;
            if (off + total > len) break;
            if (yj_live_append(a, id, bytes + off, total) != 0) {
                return;
            }
            off += total;
            continue;
        }
        break; /* unknown record — bail rather than skid through */
    }
}

static void yj_anim_tick(struct yjungle_anim *a)
{
    if (!a || !a->jungle || !a->delta || !a->acc || !a->flat || !a->rich) {
        return;
    }
    uint64_t now = ygreeter_mono_ms() - a->start_ms;
    struct yetty_ycore_void_result tr = yetty_yjungle_tick(a->jungle, a->delta, now);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
        return;
    }
    /* Quiet tick — yjungle had no event scheduled this poll. The wire
     * buffer is empty; nothing to repaint. */
    if (yetty_ydraw_draw_list_size(a->delta) == 0u) {
        return;
    }
    yj_apply_delta(a);

    /* Pack live segments back into an accumulator buffer the flatten
     * helper can chew on, then flatten paint-only into flat for the
     * rich widget. */
    yetty_ydraw_draw_list_clear(a->acc);
    yetty_ydraw_draw_list_set_scene_bounds(a->acc, 0.0f, 0.0f, a->scene_w, a->scene_h);
    for (size_t i = 0; i < a->n_live; i++) {
        struct yetty_ydraw_id_result ar =
            yetty_ydraw_draw_list_add_prim(a->acc, a->live[i].bytes, a->live[i].size);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
            return;
        }
    }
    yetty_ydraw_draw_list_clear(a->flat);
    yetty_ydraw_draw_list_set_scene_bounds(a->flat, 0.0f, 0.0f, a->scene_w, a->scene_h);
    struct yetty_ycore_void_result fr = yetty_ygui_flatten_draw_list(a->flat, a->acc);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return;
    }
    yetty_ygui_widget_rich_set_buffer(a->rich, a->flat);
}

YETTY_EXTERNAL_CALLBACK
static void on_yj_timer(uv_timer_t *t) { yj_anim_tick((struct yjungle_anim *)t->data); }

static void yj_anim_drop_producer(struct yjungle_anim *a)
{
    if (a->jungle) {
        yetty_yjungle_destroy(a->jungle);
        a->jungle = NULL;
    }
    if (a->delta) {
        yetty_ydraw_draw_list_destroy(a->delta);
        a->delta = NULL;
    }
    if (a->acc) {
        yetty_ydraw_draw_list_destroy(a->acc);
        a->acc = NULL;
    }
    yj_live_clear(a);
}

static void yj_anim_drop_widgets(struct yjungle_anim *a)
{
    if (a->rich) {
        yetty_ygui_widget_remove(a->rich);
        a->rich = NULL;
        a->flat = NULL;
    }
}

/* Deferred-attach counterpart to yzoo_anim_init — see the equivalent
 * yzoo_anim_init comment. */
static void yj_anim_init(struct yjungle_anim *a, struct yetty_ygui_engine *engine,
                         struct yetty_ygui_widget *tab, uint32_t seed)
{
    a->engine = engine;
    a->tab = tab;
    a->seed = seed;
}

static void yj_anim_attach(struct yjungle_anim *a, struct yetty_ygui_engine *engine,
                           struct yetty_ygui_widget *tab, float w, float h)
{
    yj_anim_drop_widgets(a);
    yj_anim_drop_producer(a);
    a->engine = engine;
    a->tab = tab;
    a->scene_w = w;
    a->scene_h = h;

    struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
    /* Match demo/ygui/27_yjungle's bigger initial chain so the tab isn't
     * sparse on first paint. Event cadence stays at the library default
     * so growth / replacement happens at a watchable rate. */
    cfg.scene_width = w;
    cfg.scene_height = h;
    cfg.initial_chain_length = 20;
    cfg.max_depth = 2;
    cfg.group_prob_depth0 = 0.4f;
    struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, a->seed);
    if (YETTY_IS_ERR(jr)) {
        yetty_ycore_error_destroy(jr.error);
        return;
    }
    a->jungle = jr.value;
    struct yetty_ydraw_draw_list_config bc = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = w, .scene_max_y = h,
    };
    struct yetty_ydraw_draw_list_result dr = yetty_ydraw_draw_list_config_buffer_create(&bc);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
        yj_anim_drop_producer(a);
        return;
    }
    a->delta = dr.value;
    struct yetty_ydraw_draw_list_result acr = yetty_ydraw_draw_list_config_buffer_create(&bc);
    if (YETTY_IS_ERR(acr)) {
        yetty_ycore_error_destroy(acr.error);
        yj_anim_drop_producer(a);
        return;
    }
    a->acc = acr.value;
    struct yetty_ydraw_draw_list_result fr = yetty_ydraw_draw_list_config_buffer_create(&bc);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        yj_anim_drop_producer(a);
        return;
    }
    a->flat = fr.value;
    a->rich = yetty_ygui_engine_rich(engine, "yjungle_view", 0, 0, w, h);
    if (!a->rich) {
        yetty_ydraw_draw_list_destroy(a->flat);
        a->flat = NULL;
        yj_anim_drop_producer(a);
        return;
    }
    yetty_ygui_widget_apply_css(a->rich, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(tab, a->rich);
    yetty_ygui_widget_rich_set_buffer(a->rich, a->flat); /* widget owns flat */

    a->start_ms = ygreeter_mono_ms();
    if (!a->timer_inited) {
        uv_loop_t *loop = yetty_ygui_engine_get_loop(engine);
        uv_timer_init(loop, &a->timer);
        a->timer.data = a;
        a->timer_inited = true;
    }
    yj_anim_tick(a); /* first-ever tick — emits CMD_ZERO + initial chain */
}

static void yj_anim_ensure_attached(struct yjungle_anim *a)
{
    if (!a || !a->engine || !a->tab) {
        return;
    }
    float w = 0.0f, h = 0.0f;
    ygreeter_tab_viewport(a->engine, a->tab, &w, &h);
    if (a->rich && a->scene_w == w && a->scene_h == h) {
        return;
    }
    yj_anim_attach(a, a->engine, a->tab, w, h);
}

static void yj_anim_set_running(struct yjungle_anim *a, bool run)
{
    if (!a) {
        return;
    }
    if (run) {
        yj_anim_ensure_attached(a);
        if (!a->timer_inited || a->running) {
            return;
        }
        /* Poll at 100 ms. yjungle's events are scheduled in 500–2000 ms
         * windows so most polls are quiet (no-op); the timer just gives
         * the producer a chance to fire on time. */
        uv_timer_start(&a->timer, on_yj_timer, /*initial=*/0, /*repeat=*/100);
        a->running = true;
    } else if (a->timer_inited && a->running) {
        uv_timer_stop(&a->timer);
        a->running = false;
    }
}

static void yj_anim_on_tab_closed(struct yjungle_anim *a)
{
    yj_anim_set_running(a, false);
    a->rich = NULL;
    a->flat = NULL;
    a->tab = NULL;
    yj_anim_drop_producer(a);
}

static void yj_anim_shutdown(struct yjungle_anim *a)
{
    if (!a) {
        return;
    }
    if (a->timer_inited) {
        if (a->running) {
            uv_timer_stop(&a->timer);
        }
        uv_close((uv_handle_t *)&a->timer, NULL);
        a->timer_inited = false;
        a->running = false;
    }
    yj_anim_drop_producer(a);
    free(a->live);
    a->live = NULL;
    a->n_live = 0;
    a->cap_live = 0;
    a->rich = NULL;
    a->flat = NULL;
}
#endif /* YGREETER_HAS_YJUNGLE */

static void on_resize(struct yetty_ygui_engine *e, float new_w, float new_h, float pw, float ph,
                      void *u)
{
    (void)e;
    (void)pw;
    (void)ph;
    struct app *app = (struct app *)u;
    if (app->outer) {
        yetty_ygui_widget_set_size(app->outer, new_w, new_h);
    }
    /* Keep the About dialog centred when the user resizes the
     * terminal / card. Cheap — only updates positions, not contents. */
    reposition_about_dialog(app);
    /* yzoo / yjungle bake their scene bounds into the producer + buffer
     * at create time. ensure_attached compares against the tab panel's
     * resolved layout box and re-creates only when the size changed
     * (cheap no-op for inactive tabs whose size happened to stay the
     * same). Only the currently-running anim needs a refresh — others
     * lazily re-attach the next time their tab is activated. */
#ifdef YGREETER_HAS_YZOO
    if (app->yzoo.running) {
        yzoo_anim_ensure_attached(&app->yzoo);
    } else if (app->yzoo.rich) {
        /* Tab not currently visible — drop the now-stale widget so the
         * next activation builds fresh against the new viewport. */
        yzoo_anim_drop_widgets(&app->yzoo);
        yzoo_anim_drop_producer(&app->yzoo);
    }
#endif
#ifdef YGREETER_HAS_YJUNGLE
    if (app->yjungle.running) {
        yj_anim_ensure_attached(&app->yjungle);
    } else if (app->yjungle.rich) {
        yj_anim_drop_widgets(&app->yjungle);
        yj_anim_drop_producer(&app->yjungle);
    }
#endif
}

/* =========================================================================
 * UI construction.
 *
 * Each tab body is an hbox: nav_list (fixed 220px on the left) + rich
 * surface (flex: 1) on the right. The nav list contains one leaf row per
 * entry; the on_select callback flips the tab and loads the entry into
 * the rich widget. Mobile-friendliness: when the canvas is narrow
 * (<560px) we collapse the hbox to a column so the nav stacks above the
 * content. v1: we just use stretch alignment and let flex wrap; a
 * proper media-query-style switch comes later.
 * ========================================================================= */

static struct yetty_ygui_widget *build_tab_body(struct app *app, int tab_index,
                                                struct yetty_ygui_widget *tab_panel,
                                                const struct nav_entry *entries, int n_entries,
                                                const char *id_prefix)
{
    char id_buf[128];
    snprintf(id_buf, sizeof(id_buf), "%s_body", id_prefix);
    struct yetty_ygui_widget *body = yetty_ygui_engine_hbox(app->engine, id_buf, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(body,
                                "padding: 8px; gap: 12px; flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(tab_panel, body);

    /* Nav: a flex-column vbox of button rows. We tried using a `list`
     * with label children first — but ygui's grid_query routes clicks
     * to the deepest hit, and labels have no on_press, so the list's
     * on_select never fired. Buttons have on_press AND the engine
     * fires their click_callback on mouse-up out of the box. */
    snprintf(id_buf, sizeof(id_buf), "%s_nav", id_prefix);
    struct yetty_ygui_widget *nav = yetty_ygui_engine_vbox(app->engine, id_buf, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(nav, "padding: 8px; gap: 4px; flex: 0 0 220px;");
    yetty_ygui_widget_add_child(body, nav);

    for (int i = 0; i < n_entries; i++) {
        snprintf(id_buf, sizeof(id_buf), "%s_row_%d", id_prefix, i);
        struct yetty_ygui_widget *row =
            yetty_ygui_engine_button(app->engine, id_buf, 0, 0, 200, 28, entries[i].label);
        yetty_ygui_widget_apply_css(row, "align-self: stretch;");
        yetty_ygui_widget_add_child(nav, row);
        struct row_link *rl = new_row_link(app, tab_index, i);
        if (rl) {
            yetty_ygui_widget_button_on_click(row, on_row_clicked, rl);
        }
    }

    /* Build the rich surface. */
    snprintf(id_buf, sizeof(id_buf), "%s_rich", id_prefix);
    struct yetty_ygui_widget *rich = yetty_ygui_engine_rich(app->engine, id_buf, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(rich, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(body, rich);

    /* Wire tab_state. */
    app->tabs[tab_index].nav_list = nav;
    app->tabs[tab_index].rich = rich;
    app->tabs[tab_index].entries = entries;
    app->tabs[tab_index].n_entries = n_entries;

    /* Load entry 0 by default so the right pane isn't empty at startup. */
    if (n_entries > 0) {
        load_entry(app, tab_index, 0);
    }
    return body;
}

/* =========================================================================
 * Elements tab — a single tab gathering one sample of every ygui widget,
 * grouped under vertical organizers (collapsing_header) so the tabbar
 * isn't drowned by one tab per widget. Each section toggles open / closed
 * with a click on its header strip.
 * ========================================================================= */

/* No-op click handler — the showcase widgets exist for visual demo, not
 * to drive app state. The presence of the callback keeps the cursor in
 * "interactive" mode so hover / press states still render. */
static void on_demo_click(struct yetty_ygui_widget *w, void *u)
{
    (void)w;
    (void)u;
}

/* Trigger handlers for the Overlays section — open a popup-like widget
 * (dialog / popup / popup_menu) whose pointer is passed in userdata. */
static void on_demo_open_popup_like(struct yetty_ygui_widget *btn, void *u)
{
    (void)btn;
    struct yetty_ygui_widget *target = (struct yetty_ygui_widget *)u;
    if (target) {
        yetty_ygui_widget_popup_set_open(target, 1);
    }
}

static void on_demo_open_menu(struct yetty_ygui_widget *btn, void *u)
{
    struct yetty_ygui_widget *menu = (struct yetty_ygui_widget *)u;
    if (!btn || !menu) {
        return;
    }
    /* Anchor under the trigger button so the menu is visible next to
     * the click point. */
    float x = 0, y = 0, w = 0, h = 0;
    yetty_ygui_widget_get_layout_box(btn, &x, &y, &w, &h);
    yetty_ygui_widget_popup_menu_open_at(menu, x, y + h + 4);
}

/* Build one collapsing_header section + add it as a child of `parent`.
 * Returns the header widget so the caller can keep adding children
 * (each child becomes one row inside the section, stacked vertically
 * by collapsing_header_render_all). */
static struct yetty_ygui_widget *make_section(struct app *app, struct yetty_ygui_widget *parent,
                                              const char *id, const char *label, int initially_open)
{
    /* Width 0 here is a placeholder — the parent vbox's flex stretch
     * resolves the real width at layout time. Height 28 is what 16_new
     * uses for the header bar. */
    struct yetty_ygui_widget *sec =
        yetty_ygui_engine_collapsing_header(app->engine, id, 0, 0, 600, 28, label);
    if (!sec) {
        return NULL;
    }
    yetty_ygui_widget_collapsing_header_set_open(sec, initially_open);
    yetty_ygui_widget_apply_css(sec, "align-self: stretch;");
    yetty_ygui_widget_add_child(parent, sec);
    return sec;
}

static void build_elements_tab(struct app *app, struct yetty_ygui_widget *tab_panel)
{
    /* Menubar at the top — a hbox of menu buttons each opening a
     * popup_menu beneath. */
    struct yetty_ygui_widget *mb = yetty_ygui_engine_menubar(app->engine, "el_mb", 0, 0, 600, 28);
    yetty_ygui_widget_apply_css(mb, "align-self: stretch;");
    struct yetty_ygui_widget *m_file =
        yetty_ygui_engine_popup_menu(app->engine, "el_m_file", 0, 0, 160);
    yetty_ygui_widget_popup_menu_add_item(m_file, "New", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_file, "Open…", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_file, "Save", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_separator(m_file);
    yetty_ygui_widget_popup_menu_add_item(m_file, "Quit", on_demo_click, NULL);
    struct yetty_ygui_widget *m_edit =
        yetty_ygui_engine_popup_menu(app->engine, "el_m_edit", 0, 0, 160);
    yetty_ygui_widget_popup_menu_add_item(m_edit, "Undo", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_edit, "Redo", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_edit, "Cut", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_edit, "Copy", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_edit, "Paste", on_demo_click, NULL);
    struct yetty_ygui_widget *m_view =
        yetty_ygui_engine_popup_menu(app->engine, "el_m_view", 0, 0, 160);
    yetty_ygui_widget_popup_menu_add_item(m_view, "Zoom In", on_demo_click, NULL);
    yetty_ygui_widget_popup_menu_add_item(m_view, "Zoom Out", on_demo_click, NULL);
    yetty_ygui_widget_menubar_add(mb, "File", m_file);
    yetty_ygui_widget_menubar_add(mb, "Edit", m_edit);
    yetty_ygui_widget_menubar_add(mb, "View", m_view);
    yetty_ygui_widget_add_child(tab_panel, mb);

    /* Scrollable container — flex-column scrollarea that takes care
     * of layout, hit-test, and wheel scrolling so the user can reach
     * every section even with all of them open. */
    struct yetty_ygui_widget *root =
        yetty_ygui_engine_scrollarea(app->engine, "el_root", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(root, "padding: 12px; gap: 4px; flex: 1 0 0; "
                                      "align-self: stretch; align-items: stretch;");
    yetty_ygui_widget_add_child(tab_panel, root);

    /* ---- Inputs ---- */
    {
        struct yetty_ygui_widget *sec = make_section(app, root, "el_inputs", "Inputs", 0);
        if (!sec) {
            return;
        }
        struct yetty_ygui_widget *btn =
            yetty_ygui_engine_button(app->engine, "el_btn", 24, 0, 160, 32, "Button");
        yetty_ygui_widget_button_on_click(btn, on_demo_click, NULL);
        yetty_ygui_widget_add_child(sec, btn);
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_textinput(app->engine, "el_input", 24, 0,
                                                                     320, 28, "type here…"));
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_slider(app->engine, "el_slider", 24, 0,
                                                                  320, 28, 0.0f, 1.0f, 0.4f));
        /* Integer + float spinner side-by-side. */
        yetty_ygui_widget_add_child(sec,
                                    yetty_ygui_engine_spinner(app->engine, "el_spin_i", 24, 0, 160,
                                                              30, 1.0f, 100.0f, 1.0f, 42.0f));
        struct yetty_ygui_widget *spin_f = yetty_ygui_engine_spinner(
            app->engine, "el_spin_f", 24, 0, 160, 30, 0.0f, 10.0f, 0.25f, 2.5f);
        yetty_ygui_widget_spinner_set_precision(spin_f, 2);
        yetty_ygui_widget_add_child(sec, spin_f);
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_checkbox(app->engine, "el_check", 24, 0, 220, 24, "Enabled", 1));
        /* Toggle switch — pill-shaped on/off. */
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_toggle(app->engine, "el_toggle", 24, 0,
                                                                  240, 26, "Notifications", 1));
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_progress(app->engine, "el_prog", 24, 0, 320, 16, 0.65f));
        /* Indeterminate progress — sliding slug, no value. */
        struct yetty_ygui_widget *prog_indet =
            yetty_ygui_engine_progress(app->engine, "el_prog_indet", 24, 0, 320, 16, 0.0f);
        yetty_ygui_widget_progress_set_indeterminate(prog_indet, 1);
        yetty_ygui_widget_add_child(sec, prog_indet);
        /* Multi-line text area — initial text + line-aware nav. */
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_textarea(app->engine, "el_ta", 24, 0, 420, 120,
                                            "Multi-line text area.\nClick to focus, then type.\n"));
    }

    /* ---- Selectors ---- */
    {
        struct yetty_ygui_widget *sec = make_section(app, root, "el_select", "Selectors", 0);
        if (!sec) {
            return;
        }
        /* Radio group — single-select, vertical by default. */
        struct yetty_ygui_widget *rg =
            yetty_ygui_engine_radio_group(app->engine, "el_radio", 24, 0, 220, 0);
        yetty_ygui_widget_radio_group_add(rg, "el_r_apple", "Apple");
        yetty_ygui_widget_radio_group_add(rg, "el_r_banana", "Banana");
        yetty_ygui_widget_radio_group_add(rg, "el_r_cherry", "Cherry");
        yetty_ygui_widget_radio_group_set_selected_index(rg, 0);
        yetty_ygui_widget_add_child(sec, rg);
        static const char *dd_items[] = {"Option A", "Option B", "Option C"};
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_dropdown(app->engine, "el_dd", 24, 0, 220, 28, dd_items, 3));
        /* Combo box — editable textinput with a dropdown of suggestions. */
        static const char *combo_items[] = {"red", "green", "blue", "magenta"};
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_combo(app->engine, "el_combo", 24, 0,
                                                                 220, 28, "red", combo_items, 4));
        static const char *ch_items[] = {"Small", "Medium", "Large", "Huge"};
        /* Choicebox row count × theme row_height (28) — must match the
         * actual rendered height, otherwise the widget paints below its
         * authored box and overlaps siblings inside the section. */
        struct yetty_ygui_widget *ch =
            yetty_ygui_engine_choicebox(app->engine, "el_choice", 24, 0, 220, 28 * 4, ch_items, 4);
        yetty_ygui_widget_choicebox_set_selected(ch, 1);
        yetty_ygui_widget_add_child(sec, ch);
        /* colorpicker takes only geometry — no initial-color arg. */
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_colorpicker(app->engine, "el_color", 24, 0, 240, 160));
    }

    /* ---- Display / static ---- */
    {
        struct yetty_ygui_widget *sec = make_section(app, root, "el_display", "Display", 0);
        if (!sec) {
            return;
        }
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_label(app->engine, "el_lbl", 24, 0, "Plain label"));
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_separator(app->engine, "el_sep", 24, 0, 400, 8));
        yetty_ygui_widget_add_child(
            sec, yetty_ygui_engine_progress(app->engine, "el_prog2", 24, 0, 400, 14, 0.25f));
        /* Table — 4 columns × 3 rows. Last column stretches. */
        struct yetty_ygui_widget *tbl =
            yetty_ygui_engine_table(app->engine, "el_table", 24, 0, 600, 120);
        static const char *col_names[] = {"PID", "USER", "%CPU", "COMMAND"};
        static const float col_widths[] = {60.0f, 100.0f, 60.0f, 0.0f /* stretch */};
        yetty_ygui_widget_table_set_columns(tbl, col_names, col_widths, 4);
        static const char *row1[] = {"1", "root", "0.0", "/sbin/init"};
        static const char *row2[] = {"42", "misi", "1.3", "/usr/bin/yetty"};
        static const char *row3[] = {"1337", "misi", "0.2", "/usr/bin/ygreeter"};
        yetty_ygui_widget_table_add_row(tbl, row1, 4);
        yetty_ygui_widget_table_add_row(tbl, row2, 4);
        yetty_ygui_widget_table_add_row(tbl, row3, 4);
        /* Sortable + resizable headers — click a header to sort, drag
         * its right edge (~6 px grip) to resize. */
        yetty_ygui_widget_table_set_sortable(tbl, 1);
        yetty_ygui_widget_add_child(sec, tbl);
        /* Breadcrumbs — last segment is the "current" location and
         * paints in fg color, others in muted. */
        static const char *crumbs[] = {"Home", "Projects", "yetty", "ygui"};
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_breadcrumbs(app->engine, "el_crumbs", 24,
                                                                       0, 400, 24, crumbs, 4));
        /* Chip / tag row — closable. */
        struct yetty_ygui_widget *chip_row =
            yetty_ygui_engine_hbox(app->engine, "el_chip_row", 24, 0, 480, 28);
        yetty_ygui_widget_apply_css(chip_row, "padding: 0; gap: 6; align-items: stretch;");
        yetty_ygui_widget_add_child(
            chip_row, yetty_ygui_engine_chip(app->engine, "el_chip_a", 0, 0, 80, 24, "linux", 1));
        yetty_ygui_widget_add_child(
            chip_row, yetty_ygui_engine_chip(app->engine, "el_chip_b", 0, 0, 70, 24, "gpu", 1));
        yetty_ygui_widget_add_child(chip_row, yetty_ygui_engine_chip(app->engine, "el_chip_c", 0, 0,
                                                                     90, 24, "rust-free", 0));
        yetty_ygui_widget_add_child(sec, chip_row);
        /* Stepper — three named steps with the middle one active. */
        static const char *steps[] = {"Setup", "Install", "Done"};
        struct yetty_ygui_widget *step =
            yetty_ygui_engine_stepper(app->engine, "el_steps", 24, 0, 360, 56, steps, 3);
        yetty_ygui_widget_stepper_set_current(step, 1);
        yetty_ygui_widget_add_child(sec, step);
    }

    /* ---- Lists & trees ---- */
    {
        struct yetty_ygui_widget *sec = make_section(app, root, "el_lists", "Lists & Trees", 0);
        if (!sec) {
            return;
        }
        /* list is a generic row container — items are arbitrary widgets
         * added with widget_add_child. */
        struct yetty_ygui_widget *lst =
            yetty_ygui_engine_list(app->engine, "el_list", 24, 0, 220, 24 * 4);
        static const char *fruits[] = {"Apple", "Banana", "Cherry", "Date"};
        for (int i = 0; i < 4; i++) {
            char id[32];
            snprintf(id, sizeof(id), "el_list_row_%d", i);
            yetty_ygui_widget_add_child(
                lst, yetty_ygui_engine_selectable(app->engine, id, 0, 0, 200, 24, fruits[i]));
        }
        yetty_ygui_widget_add_child(sec, lst);
        /* tree_node has the (engine, id, label) ctor; geometry comes
         * from layout. Children go into the auto-allocated children
         * container accessed via tree_node_children(). */
        struct yetty_ygui_widget *tn =
            yetty_ygui_engine_tree_node(app->engine, "el_tree", "Tree root");
        yetty_ygui_widget_tree_node_set_expanded(tn, 1);
        struct yetty_ygui_widget *tn_kids = yetty_ygui_widget_tree_node_children(tn);
        if (tn_kids) {
            yetty_ygui_widget_add_child(
                tn_kids, yetty_ygui_engine_label(app->engine, "el_tn1", 0, 0, "  child 1"));
            yetty_ygui_widget_add_child(
                tn_kids, yetty_ygui_engine_label(app->engine, "el_tn2", 0, 0, "  child 2"));
        }
        yetty_ygui_widget_add_child(sec, tn);
        /* Date picker — compact month calendar. */
        struct yetty_ygui_widget *dp =
            yetty_ygui_engine_datepicker(app->engine, "el_date", 24, 0, 240, 220);
        yetty_ygui_widget_datepicker_set_date(dp, 2025, 4, 15); /* May 15, 2025 */
        yetty_ygui_widget_add_child(sec, dp);
        /* File picker — starts in user's home or "/" if none. */
        const char *home = getenv("HOME");
        struct yetty_ygui_widget *fp = yetty_ygui_engine_filepicker(
            app->engine, "el_fp", 24, 0, 480, 240, home && *home ? home : "/");
        yetty_ygui_widget_add_child(sec, fp);
    }

    /* ---- Layout & Containers ---- */
    {
        struct yetty_ygui_widget *sec =
            make_section(app, root, "el_layout", "Layout & Containers", 0);
        if (!sec) {
            return;
        }

        /* Splitter sample — mini hbox with [left | splitter | right]
         * panels. Drag the splitter to resize. The siblings carry
         * authored widths (no flex:1 0 0) so the splitter can move
         * them. */
        struct yetty_ygui_widget *split_row =
            yetty_ygui_engine_hbox(app->engine, "el_split_row", 24, 0, 600, 80);
        yetty_ygui_widget_apply_css(split_row, "padding: 0; gap: 0; align-items: stretch;");
        struct yetty_ygui_widget *l =
            yetty_ygui_engine_panel(app->engine, "el_split_left", 0, 0, 220, 80);
        yetty_ygui_widget_set_bg_color(l, 0xFF1E262C);
        yetty_ygui_widget_apply_css(l, "align-self: stretch;");
        yetty_ygui_widget_add_child(split_row, l);
        struct yetty_ygui_widget *div =
            yetty_ygui_engine_splitter(app->engine, "el_split", 0, 0, 6, 80);
        yetty_ygui_widget_apply_css(div, "align-self: stretch;");
        yetty_ygui_widget_add_child(split_row, div);
        struct yetty_ygui_widget *r =
            yetty_ygui_engine_panel(app->engine, "el_split_right", 0, 0, 374, 80);
        yetty_ygui_widget_set_bg_color(r, 0xFF141A1F);
        yetty_ygui_widget_apply_css(r, "align-self: stretch;");
        yetty_ygui_widget_add_child(split_row, r);
        yetty_ygui_widget_add_child(sec, split_row);
        /* Standalone scrollbars deliberately omitted — out of context
         * they look like an unattached pill. The scrollbar widget is
         * exercised in the PDF tab where it's bound to the ypdf
         * widget via yetty_ygui_widget_scrollbar_bind. */
    }

    /* ---- Overlays ---- */
    {
        struct yetty_ygui_widget *sec = make_section(app, root, "el_over", "Overlays", 0);
        if (!sec) {
            return;
        }
        yetty_ygui_widget_add_child(sec, yetty_ygui_engine_tooltip(app->engine, "el_tip", 24, 0,
                                                                   240, 28, "Tooltip example"));
        struct yetty_ygui_widget *sel = yetty_ygui_engine_selectable(app->engine, "el_selable", 24,
                                                                     0, 240, 26, "Selectable row");
        yetty_ygui_widget_add_child(sec, sel);

        /* Modal dialog — assembled once; the button toggles its OPEN
         * flag. The dialog widget lives at the top of the engine's
         * widget list so the popup overlay renders above sections. */
        const char *btns[] = {"Cancel", "OK"};
        struct yetty_ygui_dialog_args dargs = {
            .id = "el_dlg",
            .title = "Dialog",
            .message = "This is a modal dialog. Pick a button.",
            .buttons = btns,
            .button_count = 2,
            .on_button = NULL,
            .userdata = NULL,
            .modal = 1,
        };
        struct yetty_ygui_widget *dlg = yetty_ygui_engine_dialog(app->engine, &dargs);
        struct yetty_ygui_widget *open_dlg =
            yetty_ygui_engine_button(app->engine, "el_open_dlg", 24, 0, 220, 30, "Open dialog…");
        yetty_ygui_widget_button_on_click(open_dlg, on_demo_open_popup_like, dlg);
        yetty_ygui_widget_add_child(sec, open_dlg);

        /* Popup — a labelled overlay you toggle. Identical activation
         * pattern as the dialog. */
        struct yetty_ygui_widget *pop =
            yetty_ygui_engine_popup(app->engine, "el_popup", 200, 200, 280, 120, "Popup title");
        struct yetty_ygui_widget *open_pop =
            yetty_ygui_engine_button(app->engine, "el_open_pop", 24, 0, 220, 30, "Open popup…");
        yetty_ygui_widget_button_on_click(open_pop, on_demo_open_popup_like, pop);
        yetty_ygui_widget_add_child(sec, open_pop);

        /* Popup menu — anchored to the trigger button's position. */
        struct yetty_ygui_widget *pmenu =
            yetty_ygui_engine_popup_menu(app->engine, "el_pmenu", 0, 0, 220);
        yetty_ygui_widget_popup_menu_add_item(pmenu, "First action", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(pmenu, "Second action", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_separator(pmenu);
        yetty_ygui_widget_popup_menu_add_item(pmenu, "Third action", on_demo_click, NULL);
        struct yetty_ygui_widget *open_menu =
            yetty_ygui_engine_button(app->engine, "el_open_menu", 24, 0, 220, 30, "Open menu…");
        yetty_ygui_widget_button_on_click(open_menu, on_demo_open_menu, pmenu);
        yetty_ygui_widget_add_child(sec, open_menu);

        /* Right-click context menu — same popup_menu type, attached to
         * a target widget. Engine intercepts right-click and opens at
         * the cursor. */
        struct yetty_ygui_widget *ctx_target = yetty_ygui_engine_button(
            app->engine, "el_ctx_target", 24, 0, 280, 30, "Right-click me for a context menu");
        struct yetty_ygui_widget *cmenu =
            yetty_ygui_engine_popup_menu(app->engine, "el_ctxmenu", 0, 0, 200);
        yetty_ygui_widget_popup_menu_add_item(cmenu, "Cut", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(cmenu, "Copy", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(cmenu, "Paste", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_separator(cmenu);
        yetty_ygui_widget_popup_menu_add_item(cmenu, "Delete", on_demo_click, NULL);
        yetty_ygui_widget_set_context_menu(ctx_target, cmenu);
        yetty_ygui_widget_add_child(sec, ctx_target);
    }
}

/* =========================================================================
 * Entry point.
 * ========================================================================= */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Force stderr to line-buffered. Callers redirect it via the
     * shell (e.g. `ygreeter 2>/tmp/ygreeter.log`); without this nudge
     * libc switches to block buffering once the fd points at a file,
     * and any diagnostic emitted just before a crash / hang sits in
     * the buffer instead of reaching disk. */
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* When the caller didn't redirect stderr, it shares the PTY slave
     * with stdout under `yetty -e`. ygreeter's stdout carries the OSC
     * envelopes to the parent yetty; mixing diagnostic text in there
     * corrupts every OSC frame the parent tries to parse, manifesting
     * as a frozen UI as soon as something noisy like the Images tab
     * activates the full render path. The yplatform tty helper detects
     * that case and reroutes stderr to <runtime>/ygreeter-<pid>.log
     * so the OSC stream stays clean and the trace is recoverable.
     *
     * Windows impl is a no-op (no PTY-share scenario there). */
    yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout("ygreeter");

    if (yetty_ygui_init() != 0) {
        fprintf(stdout, "ygreeter: ygui_init failed (run inside a real terminal)\n");
        return 1;
    }

    /* Theme up front so engine_create can install it during construction;
     * the engine takes ownership when passed in `args.theme`. */
    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 16.0f);
    yetty_ygui_theme_set_row_height(theme, 28.0f);

    struct yetty_ygui_engine_args args = {
        .name = "ygreeter",
        .theme = theme,
    };
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(args);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }

    struct app app = {0};
    app.engine = eng_r.value;

    /* Outer frame: a window widget supplies the title bar and the
     * hamburger menu button. The menu we attach below carries the
     * Close entry — clicking it calls engine_close_preserve() so the
     * last frame stays painted on the canvas after we exit. */
    app.outer = yetty_ygui_engine_window(app.engine, "outer", 0, 0, 100, 100, "ygreeter");
    struct yetty_ygui_widget *body = yetty_ygui_widget_window_body(app.outer);

    /* App menu attached to the hamburger button. */
    struct yetty_ygui_widget *app_menu =
        yetty_ygui_engine_popup_menu(app.engine, "app_menu", 0, 0, 200.0f);
    yetty_ygui_widget_popup_menu_add_item(app_menu, "About", on_menu_about, &app);
    yetty_ygui_widget_popup_menu_add_separator(app_menu);
    yetty_ygui_widget_popup_menu_add_item(app_menu, "Close", on_menu_close, &app);
    yetty_ygui_widget_window_set_menu(app.outer, app_menu);

    /* About dialog (popup + rich content + close button). Hidden until
     * the menu's About item opens it. */
    build_about_dialog(&app);

    /* Window-level menubar — File / Edit / Help. Sits between the
     * window's title strip and the body (the tabbar). */
    {
        struct yetty_ygui_widget *mb =
            yetty_ygui_engine_menubar(app.engine, "win_mb", 0, 0, 100, 26);
        struct yetty_ygui_widget *mf =
            yetty_ygui_engine_popup_menu(app.engine, "win_mf", 0, 0, 180);
        yetty_ygui_widget_popup_menu_add_item(mf, "New tab", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(mf, "Reload", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_separator(mf);
        yetty_ygui_widget_popup_menu_add_item(mf, "Quit", on_menu_close, &app);
        struct yetty_ygui_widget *me =
            yetty_ygui_engine_popup_menu(app.engine, "win_me", 0, 0, 180);
        yetty_ygui_widget_popup_menu_add_item(me, "Cut", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(me, "Copy", on_demo_click, NULL);
        yetty_ygui_widget_popup_menu_add_item(me, "Paste", on_demo_click, NULL);
        struct yetty_ygui_widget *mh =
            yetty_ygui_engine_popup_menu(app.engine, "win_mh", 0, 0, 180);
        yetty_ygui_widget_popup_menu_add_item(mh, "About", on_menu_about, &app);
        yetty_ygui_widget_menubar_add(mb, "File", mf);
        yetty_ygui_widget_menubar_add(mb, "Edit", me);
        yetty_ygui_widget_menubar_add(mb, "Help", mh);
        yetty_ygui_widget_window_set_menubar(app.outer, mb);
    }

    /* Window-level statusbar. */
    {
        struct yetty_ygui_widget *sb = yetty_ygui_engine_statusbar(app.engine, "win_sb", 0, 0, 100,
                                                                   22, "Ready — ygui showcase");
        yetty_ygui_widget_statusbar_set_right(sb, "v0.2");
        yetty_ygui_widget_window_set_statusbar(app.outer, sb);
    }

    app.tabbar = yetty_ygui_engine_tabbar(app.engine, "tabs", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(app.tabbar, "flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(body, app.tabbar);

    yetty_ygui_widget_tabbar_on_change(app.tabbar, on_tab_change, &app);
    yetty_ygui_widget_tabbar_on_tab_close(app.tabbar, on_tab_close, &app);

    /* Welcome */
    struct yetty_ygui_widget *welcome = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Welcome");
    build_tab_body(&app, 0, welcome, WELCOME_NAV,
                   (int)(sizeof(WELCOME_NAV) / sizeof(WELCOME_NAV[0])), "welcome");

    /* Plots */
    struct yetty_ygui_widget *plots = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Plots");
    build_tab_body(&app, 1, plots, PLOT_NAV, (int)(sizeof(PLOT_NAV) / sizeof(PLOT_NAV[0])),
                   "plots");

    /* Images tab — nav rows built dynamically from docs/logo-*.jpeg
     * so the rail grows with the bundled asset count. Falls back to
     * the placeholder YAML row when no logo files are found. */
    discover_logo_images(argv[0]);
    struct yetty_ygui_widget *images = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Images");
    if (g_image_path_count > 0) {
        build_tab_body(&app, g_images_tab_index, images, g_image_nav, g_image_path_count, "images");
    } else {
        /* No bundled images located — single placeholder row so the
         * tab isn't empty and the hint text is visible. */
        static const struct nav_entry fallback[] = {
            {"Logo", IMAGE_FALLBACK_YAML},
        };
        build_tab_body(&app, g_images_tab_index, images, fallback, 1, "images");
    }

    /* Code */
    struct yetty_ygui_widget *code = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Code");
    build_tab_body(&app, 3, code, CODE_NAV, (int)(sizeof(CODE_NAV) / sizeof(CODE_NAV[0])), "code");

    /* Elements — single tab gathering every ygui widget under
     * collapsing-header sections, so the tabbar doesn't grow one tab
     * per widget. Tab index 4 (after Welcome/Plots/Images/Code) — no
     * tab_state entry registered because Elements doesn't use the
     * nav+rich pattern. */
    {
        struct yetty_ygui_widget *el_tab = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Elements");
        build_elements_tab(&app, el_tab);
    }

    /* Yzoo / Yjungle — primitive-emitting demo modules driven LIVE
     * (not snapshot). Each tab is filled by a single rich widget; a
     * uv_timer attached to the engine's loop fires while the tab is
     * active and updates the widget's buffer from the producer's
     * output. on_tab_change starts / stops the timers so the work
     * only happens while the tab is visible.
     *
     * Init only stashes the (engine, tab, seed) handles — the producer
     * and rich widget are created on first activation, so they're
     * sized against the tab panel's resolved layout box rather than
     * the engine canvas (which would over-shoot the viewport by the
     * menubar / statusbar / tabstrip chrome height). */
#ifdef YGREETER_HAS_YZOO
    {
        struct yetty_ygui_widget *yzoo_tab =
            yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Yzoo");
        g_yzoo_tab_index = yetty_ygui_widget_tabbar_count(app.tabbar) - 1;
        yzoo_anim_init(&app.yzoo, app.engine, yzoo_tab, /*seed=*/0);
    }
#endif
#ifdef YGREETER_HAS_YJUNGLE
    {
        struct yetty_ygui_widget *yjungle_tab =
            yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Yjungle");
        g_yjungle_tab_index = yetty_ygui_widget_tabbar_count(app.tabbar) - 1;
        yj_anim_init(&app.yjungle, app.engine, yjungle_tab, /*seed=*/0);
    }
#endif

    /* Markdown / HTML / PDF widget showcase tabs — each shows one
     * sample document via the corresponding ygui widget. The widget
     * fills its tab via flex: 1; if the bundled sample isn't found
     * the tab is silently skipped (the feature was probably built
     * without the dependency). */
#ifdef YGREETER_HAS_YMARKDOWN
    {
        char *md_path = find_repo_image(argv[0], "README.md");
        if (md_path) {
            struct yetty_ygui_widget *tab =
                yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Markdown");
            struct yetty_ygui_widget *w = yetty_ygui_engine_ymarkdown_from_file(
                app.engine, "md_view", 0, 0, 100, 100, md_path);
            if (w) {
                yetty_ygui_widget_apply_css(w, "flex: 1 0 0; align-self: stretch;");
                yetty_ygui_widget_add_child(tab, w);
            }
            free(md_path);
        }
    }
#endif
#ifdef YGREETER_HAS_YBROWSER
    {
        char *html_path = find_repo_image(argv[0], "demo/ygui/26_ybrowser/sample.html");
        if (html_path) {
            struct yetty_ygui_widget *tab = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Browser");
            struct yetty_ygui_widget *w = yetty_ygui_engine_ybrowser_from_file(
                app.engine, "html_view", 0, 0, 100, 100, html_path);
            if (w) {
                yetty_ygui_widget_apply_css(w, "flex: 1 0 0; align-self: stretch;");
                yetty_ygui_widget_add_child(tab, w);
            }
            free(html_path);
        }
    }
#endif
#ifdef YGREETER_HAS_YPDF
    {
        /* Prefer the comprehensive PDF — falls back to pdf-sample if the
         * larger one isn't shipped. */
        char *pdf_path = find_repo_image(argv[0], "test/ut/ypdf/test-comprehensive.pdf");
        if (!pdf_path) {
            pdf_path = find_repo_image(argv[0], "test/ut/ypdf/pdf-sample.pdf");
        }
        if (pdf_path) {
            struct yetty_ygui_widget *tab = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "PDF");
            struct yetty_ygui_widget *w =
                yetty_ygui_engine_ypdf_from_file(app.engine, "pdf_view", 0, 0, 100, 100, pdf_path);
            if (w) {
                yetty_ygui_widget_apply_css(w, "flex: 1 0 0; align-self: stretch;");
                yetty_ygui_widget_add_child(tab, w);
            }
            free(pdf_path);
        }
    }
#endif

    yetty_ygui_engine_on_resize(app.engine, on_resize, &app);
    yetty_ygui_engine_on_key(app.engine, on_key, NULL);
    /* DEBUG: start with Images tab active so we can repro the freeze
     * without external mouse input. Revert before commit. */
    if (getenv("YGREETER_START_IMAGES")) {
        yetty_ygui_widget_tabbar_set_active(app.tabbar, g_images_tab_index);
    }
    /* engine_create already sent the init handshake; the real pixel size
     * arrives via SC_RESIZE on the loop. The window stays at its authored
     * 100x100 until then — on_resize installs the real size as soon as
     * the host replies. */

    yetty_ygui_engine_run(app.engine);

    free_image_nav();
    free_row_links();
    /* Stop timers and free producer state BEFORE the engine teardown —
     * the engine teardown closes the loop the timers are attached to. */
#ifdef YGREETER_HAS_YZOO
    yzoo_anim_shutdown(&app.yzoo);
#endif
#ifdef YGREETER_HAS_YJUNGLE
    yj_anim_shutdown(&app.yjungle);
#endif
    yetty_ygui_engine_destroy(app.engine);
    yetty_ygui_shutdown();
    return 0;
}
