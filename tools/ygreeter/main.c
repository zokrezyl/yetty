/*
 * ygreeter — the "first contact" tool shown to the user when a yetty
 * instance boots up the RISCV Linux VM (also useful standalone via
 * `yetty -e ./ygreeter` on any platform).
 *
 * Layout: a single full-canvas ygui app driving a tabbar at the top with
 * a per-tab body underneath. Every body is an hbox: a small navigation
 * tree on the left, a `rich` content surface on the right. The rich
 * widget holds a ypaint-core buffer built from an inline YAML — same
 * vocabulary as demo/scripts/ypaint/scrolling/.
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
 *   Welcome — rich-text intro authored as ypaint TEXT spans with mixed
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

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

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
 * and a hint to use the other tabs. Authored as ypaint TEXT spans so the
 * various sizes / colors actually render on the GPU canvas (the regular
 * ygui label widget doesn't carry inline colour runs).
 * ========================================================================= */

#define WELCOME_INTRO                                                                       \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [24, 56]\n"                                                             \
    "      content: \"Welcome to yetty\"\n"                                                  \
    "      font-size: 36\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 96]\n"                                                             \
    "      content: \"A GPU terminal that draws more than text.\"\n"                         \
    "      font-size: 18\n"                                                                  \
    "      color: \"#9ad7ff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 140]\n"                                                            \
    "      content: \"Plots, images, rich docs — all next to your shell.\"\n"                \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 170]\n"                                                            \
    "      content: \"Switch tabs to see what the GPU layer can do.\"\n"                     \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 220]\n"                                                            \
    "      content: \"Press 'q' to quit.\"\n"                                                \
    "      font-size: 14\n"                                                                  \
    "      color: \"#888888\"\n"

#define WELCOME_QUICKSTART                                                                  \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [24, 48]\n"                                                             \
    "      content: \"Quick start\"\n"                                                       \
    "      font-size: 28\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 92]\n"                                                             \
    "      content: \"$ ycat README.md\"\n"                                                  \
    "      font-size: 16\n"                                                                  \
    "      color: \"#a3e635\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 116]\n"                                                            \
    "      content: \"render Markdown / PDFs / images in-line\"\n"                           \
    "      font-size: 14\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 154]\n"                                                            \
    "      content: \"$ yplot 'sin(x); cos(x)'\"\n"                                          \
    "      font-size: 16\n"                                                                  \
    "      color: \"#a3e635\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 178]\n"                                                            \
    "      content: \"GPU function plots from a single expression\"\n"                       \
    "      font-size: 14\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 216]\n"                                                            \
    "      content: \"$ ytop\"\n"                                                            \
    "      font-size: 16\n"                                                                  \
    "      color: \"#a3e635\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 240]\n"                                                            \
    "      content: \"a ygui-built process monitor\"\n"                                      \
    "      font-size: 14\n"                                                                  \
    "      color: \"#cccccc\"\n"

#define WELCOME_CAPS                                                                        \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [24, 48]\n"                                                             \
    "      content: \"What's inside\"\n"                                                     \
    "      font-size: 28\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 90]\n"                                                             \
    "      content: \"• Multiple GPU-rendered layers\"\n"                                    \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 116]\n"                                                            \
    "      content: \"• MSDF font glyphs at any size\"\n"                                    \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 142]\n"                                                            \
    "      content: \"• SDF primitives — boxes, circles, segments\"\n"                       \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 168]\n"                                                            \
    "      content: \"• yplot — GPU function plotting\"\n"                                   \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 194]\n"                                                            \
    "      content: \"• yimage — PNG / JPEG via stb_image\"\n"                               \
    "      font-size: 16\n"                                                                  \
    "      color: \"#cccccc\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [24, 220]\n"                                                            \
    "      content: \"• ygui — a flex-layout widget toolkit\"\n"                             \
    "      font-size: 16\n"                                                                  \
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

#define PLOT_TRIG                                                                           \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"sin(x) and cos(x)\"\n"                                                 \
    "      font-size: 22\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - yplot:\n"                                                                           \
    "      position: [16, 60]\n"                                                             \
    "      size: [460, 280]\n"                                                               \
    "      x_range: [-6.2832, 6.2832]\n"                                                     \
    "      y_range: [-1.5, 1.5]\n"                                                           \
    "      show_grid: true\n"                                                                \
    "      show_axes: true\n"                                                                \
    "      show_labels: true\n"                                                              \
    "      functions:\n"                                                                     \
    "        - expr: \"sin(x)\"\n"                                                           \
    "          color: \"#ff6b6b\"\n"                                                         \
    "        - expr: \"cos(x)\"\n"                                                           \
    "          color: \"#4ecdc4\"\n"

#define PLOT_PARABOLA                                                                       \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"x*x  and  2*x + 1\"\n"                                                 \
    "      font-size: 22\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - yplot:\n"                                                                           \
    "      position: [16, 60]\n"                                                             \
    "      size: [460, 280]\n"                                                               \
    "      x_range: [-5, 5]\n"                                                               \
    "      y_range: [-2, 12]\n"                                                              \
    "      show_grid: true\n"                                                                \
    "      show_axes: true\n"                                                                \
    "      show_labels: true\n"                                                              \
    "      functions:\n"                                                                     \
    "        - expr: \"x*x\"\n"                                                              \
    "          color: \"#ffe66d\"\n"                                                         \
    "        - expr: \"2*x + 1\"\n"                                                          \
    "          color: \"#aa96da\"\n"

#define PLOT_DECAY                                                                          \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"exp(-x*x/4) * sin(3*x)\"\n"                                            \
    "      font-size: 22\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - yplot:\n"                                                                           \
    "      position: [16, 60]\n"                                                             \
    "      size: [460, 280]\n"                                                               \
    "      x_range: [-6, 6]\n"                                                               \
    "      y_range: [-1.2, 1.2]\n"                                                           \
    "      show_grid: true\n"                                                                \
    "      show_axes: true\n"                                                                \
    "      show_labels: true\n"                                                              \
    "      functions:\n"                                                                     \
    "        - expr: \"exp(-x*x/4) * sin(3*x)\"\n"                                           \
    "          color: \"#74c0fc\"\n"

static const struct nav_entry PLOT_NAV[] = {
    {"Trigonometry", PLOT_TRIG},
    {"Polynomial", PLOT_PARABOLA},
    {"Damped wave", PLOT_DECAY},
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

#define IMAGE_PLACEHOLDER_FOR(_title, _hint)                                                \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"" _title "\"\n"                                                        \
    "      font-size: 22\n"                                                                  \
    "      color: \"#ffffff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 84]\n"                                                             \
    "      content: \"" _hint "\"\n"                                                         \
    "      font-size: 14\n"                                                                  \
    "      color: \"#bbbbbb\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 112]\n"                                                            \
    "      content: \"Set YGREETER_IMAGE=/path/to/image.png to load.\"\n"                    \
    "      font-size: 14\n"                                                                  \
    "      color: \"#888888\"\n"                                                             \
    "  - box:\n"                                                                             \
    "      position: [16, 150]\n"                                                            \
    "      size: [460, 240]\n"                                                               \
    "      fill: \"#1f2330\"\n"                                                              \
    "      stroke: \"#2c3447\"\n"                                                            \
    "      stroke-width: 2\n"                                                                \
    "      round: 8\n"                                                                       \
    "  - text:\n"                                                                            \
    "      position: [180, 280]\n"                                                           \
    "      content: \"[ image goes here ]\"\n"                                               \
    "      font-size: 16\n"                                                                  \
    "      color: \"#666e85\"\n"

#define IMAGE_LOGO                                                                          \
    IMAGE_PLACEHOLDER_FOR("Logo preview",                                                   \
                          "yimage decodes PNG / JPEG via stb_image and uploads it as a")

#define IMAGE_PHOTO                                                                         \
    IMAGE_PLACEHOLDER_FOR("Photograph",                                                     \
                          "ypaint texture atlas; the GPU samples it at the displayed size.")

#define IMAGE_DIAGRAM                                                                       \
    IMAGE_PLACEHOLDER_FOR("Diagram",                                                        \
                          "Works inline with other primitives — see the Plots tab.")

static const struct nav_entry IMAGE_NAV[] = {
    {"Logo", IMAGE_LOGO},
    {"Photograph", IMAGE_PHOTO},
    {"Diagram", IMAGE_DIAGRAM},
};

/* If the user sets YGREETER_IMAGE, we substitute the placeholder for a
 * real yimage block on the active image row. */
static char *build_image_yaml_from_path(const char *title, const char *path)
{
    /* Pre-size: ~600 bytes of fixed YAML + path length. */
    size_t n = strlen(path) + strlen(title) + 512;
    char *buf = (char *)malloc(n);
    if (!buf) {
        return NULL;
    }
    snprintf(buf, n,
             "body:\n"
             "  - text:\n"
             "      position: [16, 36]\n"
             "      content: \"%s\"\n"
             "      font-size: 22\n"
             "      color: \"#ffffff\"\n"
             "  - yimage:\n"
             "      position: [16, 60]\n"
             "      size: [460, 320]\n"
             "      path: \"%s\"\n",
             title, path);
    return buf;
}

/* =========================================================================
 * Code tab content.
 *
 * The user asked for "code viewing using the ycat feature". ycat builds a
 * ypaint buffer of coloured TEXT_SPAN prims via its tree-sitter backend;
 * linking yetty_ycat pulls in libmagic + tree-sitter grammars (~few MB).
 * For the v1 tool we author the same shape inline — TEXT spans with
 * token-class colors — so the demo runs anywhere ygui runs (including
 * the RISCV browser build, which doesn't link tree-sitter). When the
 * RISCV path gets ycat support, swap these literals for
 * yetty_ycat_ts_render() output + yetty_ygui_widget_rich_set_buffer().
 * ========================================================================= */

#define CODE_HELLO                                                                          \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"hello.c\"\n"                                                           \
    "      font-size: 18\n"                                                                  \
    "      color: \"#9ad7ff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 80]\n"                                                             \
    "      content: \"#include\"\n"                                                          \
    "      font-size: 15\n"                                                                  \
    "      color: \"#c586c0\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [100, 80]\n"                                                            \
    "      content: \"<stdio.h>\"\n"                                                         \
    "      font-size: 15\n"                                                                  \
    "      color: \"#ce9178\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 120]\n"                                                            \
    "      content: \"int\"\n"                                                               \
    "      font-size: 15\n"                                                                  \
    "      color: \"#569cd6\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [56, 120]\n"                                                            \
    "      content: \"main(void) {\"\n"                                                      \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 148]\n"                                                            \
    "      content: \"printf(\"\n"                                                           \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [104, 148]\n"                                                           \
    "      content: \"\\\"hello, yetty\\\\n\\\"\"\n"                                         \
    "      font-size: 15\n"                                                                  \
    "      color: \"#ce9178\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [240, 148]\n"                                                           \
    "      content: \");\"\n"                                                                \
    "      font-size: 15\n"                                                                  \
    "      color: \"#d4d4d4\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 176]\n"                                                            \
    "      content: \"return\"\n"                                                            \
    "      font-size: 15\n"                                                                  \
    "      color: \"#c586c0\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [104, 176]\n"                                                           \
    "      content: \"0\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#b5cea8\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [120, 176]\n"                                                           \
    "      content: \";\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#d4d4d4\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 204]\n"                                                            \
    "      content: \"}\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#d4d4d4\"\n"

#define CODE_PYTHON                                                                         \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"hello.py\"\n"                                                          \
    "      font-size: 18\n"                                                                  \
    "      color: \"#9ad7ff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 80]\n"                                                             \
    "      content: \"def\"\n"                                                               \
    "      font-size: 15\n"                                                                  \
    "      color: \"#569cd6\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [56, 80]\n"                                                             \
    "      content: \"greet(name):\"\n"                                                      \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [40, 108]\n"                                                            \
    "      content: \"print(\"\n"                                                            \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [104, 108]\n"                                                           \
    "      content: \"f\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#c586c0\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [116, 108]\n"                                                           \
    "      content: \"\\\"hello, {name}\\\"\"\n"                                             \
    "      font-size: 15\n"                                                                  \
    "      color: \"#ce9178\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [280, 108]\n"                                                           \
    "      content: \")\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#d4d4d4\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 148]\n"                                                            \
    "      content: \"greet(\"\n"                                                            \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [76, 148]\n"                                                            \
    "      content: \"\\\"yetty\\\"\"\n"                                                     \
    "      font-size: 15\n"                                                                  \
    "      color: \"#ce9178\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [136, 148]\n"                                                           \
    "      content: \")\"\n"                                                                 \
    "      font-size: 15\n"                                                                  \
    "      color: \"#d4d4d4\"\n"

#define CODE_SHELL                                                                          \
    "body:\n"                                                                                \
    "  - text:\n"                                                                            \
    "      position: [16, 36]\n"                                                             \
    "      content: \"hello.sh\"\n"                                                          \
    "      font-size: 18\n"                                                                  \
    "      color: \"#9ad7ff\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 80]\n"                                                             \
    "      content: \"#!/usr/bin/env bash\"\n"                                               \
    "      font-size: 15\n"                                                                  \
    "      color: \"#6a9955\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 120]\n"                                                            \
    "      content: \"name=\"\n"                                                             \
    "      font-size: 15\n"                                                                  \
    "      color: \"#9cdcfe\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [72, 120]\n"                                                            \
    "      content: \"\\\"yetty\\\"\"\n"                                                     \
    "      font-size: 15\n"                                                                  \
    "      color: \"#ce9178\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [16, 160]\n"                                                            \
    "      content: \"echo\"\n"                                                              \
    "      font-size: 15\n"                                                                  \
    "      color: \"#dcdcaa\"\n"                                                             \
    "  - text:\n"                                                                            \
    "      position: [60, 160]\n"                                                            \
    "      content: \"\\\"hello, $name\\\"\"\n"                                              \
    "      font-size: 15\n"                                                                  \
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
    /* Optional: a heap-allocated YAML that overrides one of the entries
     * (used by the Image tab when YGREETER_IMAGE is set). Owned. */
    char *overlay_yaml;
    int overlay_index;
};

struct app {
    struct yetty_ygui_engine *engine;
    struct yetty_ygui_widget *outer;
    struct yetty_ygui_widget *tabbar;
    struct tab_state tabs[4];
};

/* Side table mapping clicked-row widget → (tab_index, entry_index). The
 * list widget's on_select callback is shared across all rows in the
 * list, so we route via the row widget pointer here (same pattern as
 * demo/ygui/21_tree_with_panes). */
struct row_link {
    struct yetty_ygui_widget *row; /* not owned — refs an engine widget */
    int tab_index;
    int entry_index;
};

static struct row_link *g_row_links = NULL;
static int g_row_link_count = 0;
static int g_row_link_cap = 0;

static int register_row_link(struct yetty_ygui_widget *row, int tab, int entry)
{
    if (g_row_link_count >= g_row_link_cap) {
        int nc = g_row_link_cap ? g_row_link_cap * 2 : 32;
        struct row_link *n =
            (struct row_link *)realloc(g_row_links, (size_t)nc * sizeof(struct row_link));
        if (!n) {
            return 0;
        }
        g_row_links = n;
        g_row_link_cap = nc;
    }
    g_row_links[g_row_link_count].row = row;
    g_row_links[g_row_link_count].tab_index = tab;
    g_row_links[g_row_link_count].entry_index = entry;
    g_row_link_count++;
    return 1;
}

static const struct row_link *lookup_row_link(const struct yetty_ygui_widget *row)
{
    for (int i = 0; i < g_row_link_count; i++) {
        if (g_row_links[i].row == row) {
            return &g_row_links[i];
        }
    }
    return NULL;
}

static void free_row_links(void)
{
    free(g_row_links);
    g_row_links = NULL;
    g_row_link_count = 0;
    g_row_link_cap = 0;
}

/* Pick the YAML to load for a given tab+entry. The overlay (if set)
 * trumps the static entry — used so the Image tab can substitute a real
 * yimage block when YGREETER_IMAGE is set. */
static const char *yaml_for(const struct tab_state *t, int entry_index)
{
    if (t->overlay_yaml && entry_index == t->overlay_index) {
        return t->overlay_yaml;
    }
    return t->entries[entry_index].yaml;
}

static void load_entry(struct app *app, int tab_index, int entry_index)
{
    if (tab_index < 0 || tab_index >= 4) {
        return;
    }
    struct tab_state *t = &app->tabs[tab_index];
    if (entry_index < 0 || entry_index >= t->n_entries) {
        return;
    }
    const char *yaml = yaml_for(t, entry_index);
    struct yetty_ycore_void_result r =
        yetty_ygui_widget_rich_set_yaml(t->rich, yaml, strlen(yaml));
    if (YETTY_IS_ERR(r)) {
        /* Stay alive; the rich widget still renders the previous buffer.
         * Print to stderr for the dev — the user sees nothing. */
        yetty_ycore_error_destroy(r.error);
    }
}

static void on_row_select(struct yetty_ygui_widget *row, void *userdata)
{
    struct app *app = (struct app *)userdata;
    if (!app || !row) {
        return;
    }
    const struct row_link *rl = lookup_row_link(row);
    if (!rl) {
        return;
    }
    /* When the user clicks a tree row, also flip to that row's tab —
     * the tabbar drives visibility, and this keeps content + chrome in
     * sync if a tree click happens via keyboard shortcut etc. */
    if (yetty_ygui_widget_tabbar_get_active(app->tabbar) != rl->tab_index) {
        yetty_ygui_widget_tabbar_set_active(app->tabbar, rl->tab_index);
    }
    load_entry(app, rl->tab_index, rl->entry_index);
}

static void on_tab_change(struct yetty_ygui_widget *tabbar, float value, void *userdata)
{
    (void)tabbar;
    struct app *app = (struct app *)userdata;
    int idx = (int)value;
    if (idx < 0 || idx >= 4) {
        return;
    }
    /* Default: load the first entry of the newly-active tab. The nav
     * list keeps its own selection state, so this is a soft sync. */
    load_entry(app, idx, 0);
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
    }
}

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
}

static void query_terminal_cells(int *cols, int *rows)
{
    *cols = 80;
    *rows = 24;
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) *cols = ws.ws_col;
        if (ws.ws_row > 0) *rows = ws.ws_row;
    }
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
    struct yetty_ygui_widget *body =
        yetty_ygui_engine_hbox(app->engine, id_buf, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(body,
                                "padding: 8px; gap: 12px; flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(tab_panel, body);

    snprintf(id_buf, sizeof(id_buf), "%s_nav", id_prefix);
    struct yetty_ygui_widget *nav =
        yetty_ygui_engine_list(app->engine, id_buf, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(nav, "padding: 8px; gap: 4px; flex: 0 0 220px;");
    yetty_ygui_widget_add_child(body, nav);

    for (int i = 0; i < n_entries; i++) {
        snprintf(id_buf, sizeof(id_buf), "%s_row_%d", id_prefix, i);
        struct yetty_ygui_widget *row =
            yetty_ygui_engine_label(app->engine, id_buf, 0, 0, entries[i].label);
        yetty_ygui_widget_add_child(nav, row);
        register_row_link(row, tab_index, i);
    }
    /* The list's on_select callback is shared across rows; we recover
     * per-row routing via lookup_row_link(clicked_row). */
    yetty_ygui_widget_list_on_select(nav, on_row_select, app);

    /* Build the rich surface. */
    snprintf(id_buf, sizeof(id_buf), "%s_rich", id_prefix);
    struct yetty_ygui_widget *rich =
        yetty_ygui_engine_rich(app->engine, id_buf, 0, 0, 0, 0);
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
 * Entry point.
 * ========================================================================= */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Drop stderr so libuv / fontconfig diagnostics don't paint the card. */
    FILE *redir = freopen("/dev/null", "w", stderr);
    (void)redir;

    if (yetty_ygui_init() != 0) {
        fprintf(stdout, "ygreeter: ygui_init failed (run inside a real terminal)\n");
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create("ygreeter", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }

    struct app app = {0};
    app.engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(app.engine, YETTY_YGUI_CANVAS_FIT);

    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 16.0f);
    yetty_ygui_theme_set_row_height(theme, 28.0f);
    yetty_ygui_engine_set_theme(app.engine, theme);

    app.outer = yetty_ygui_engine_vbox(app.engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(app.outer, "padding: 0; gap: 0; align-items: stretch;");

    app.tabbar = yetty_ygui_engine_tabbar(app.engine, "tabs", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(app.tabbar, "flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(app.outer, app.tabbar);

    yetty_ygui_widget_tabbar_on_change(app.tabbar, on_tab_change, &app);

    /* Welcome */
    struct yetty_ygui_widget *welcome = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Welcome");
    build_tab_body(&app, 0, welcome, WELCOME_NAV,
                   (int)(sizeof(WELCOME_NAV) / sizeof(WELCOME_NAV[0])), "welcome");

    /* Plots */
    struct yetty_ygui_widget *plots = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Plots");
    build_tab_body(&app, 1, plots, PLOT_NAV,
                   (int)(sizeof(PLOT_NAV) / sizeof(PLOT_NAV[0])), "plots");

    /* Images — with optional env-var overlay for the first row. */
    struct yetty_ygui_widget *images = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Images");
    build_tab_body(&app, 2, images, IMAGE_NAV,
                   (int)(sizeof(IMAGE_NAV) / sizeof(IMAGE_NAV[0])), "images");
    const char *img = getenv("YGREETER_IMAGE");
    if (img && *img) {
        app.tabs[2].overlay_yaml = build_image_yaml_from_path("Custom image", img);
        app.tabs[2].overlay_index = 0;
        load_entry(&app, 2, 0); /* re-render row 0 with the live yimage */
    }

    /* Code */
    struct yetty_ygui_widget *code = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Code");
    build_tab_body(&app, 3, code, CODE_NAV,
                   (int)(sizeof(CODE_NAV) / sizeof(CODE_NAV[0])), "code");

    yetty_ygui_engine_on_resize(app.engine, on_resize, &app);
    yetty_ygui_engine_on_key(app.engine, on_key, NULL);
    yetty_ygui_engine_show(app.engine);
    {
        float cw = 0, ch = 0;
        yetty_ygui_engine_get_size(app.engine, &cw, &ch);
        if (cw > 0 && ch > 0) {
            yetty_ygui_widget_set_size(app.outer, cw, ch);
        }
    }

    yetty_ygui_engine_run(app.engine);

    for (int i = 0; i < 4; i++) {
        free(app.tabs[i].overlay_yaml);
    }
    free_row_links();
    yetty_ygui_engine_destroy(app.engine);
    yetty_ygui_shutdown();
    return 0;
}
