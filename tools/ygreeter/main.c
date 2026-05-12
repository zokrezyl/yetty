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

/* Build a ypaint buffer holding ONE yimage prim plus a TEXT_SPAN title.
 * The ypaint-yaml parser doesn't support `yimage:` blocks today
 * (yaml_factory: none in yimage.yaml), so we go through yimage's C API
 * directly and append the title via the buffer's add_text helper.
 *
 * Caller owns the returned buffer; ownership is transferred to the rich
 * widget when handed off via set_buffer. */
#include <yetty/yimage/yimage.h>

static struct yetty_ypaint_core_buffer *build_image_buffer(const char *title, const char *path)
{
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 16.0f,
        .bounds_y = 60.0f,
        .bounds_w = 460.0f,
        .bounds_h = 320.0f,
    };
    struct yetty_ypaint_core_buffer_result r = yetty_yimage_render_path(path, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    /* Title TEXT_SPAN drawn above the image. The buffer's add_text takes
     * a yetty_ycore_buffer view so we wrap the literal in place. */
    if (title && *title) {
        struct yetty_ycore_buffer text_view = {
            .data = (uint8_t *)title,
            .size = strlen(title),
            .capacity = strlen(title),
        };
        struct yetty_ycore_void_result tr =
            yetty_ypaint_core_buffer_add_text(r.value, 16.0f, 36.0f, &text_view, 22.0f,
                                              /*color (ABGR)=*/0xFFFFFFFFu, /*layer=*/0,
                                              /*font_id=*/-1, /*rotation=*/0.0f);
        if (YETTY_IS_ERR(tr)) {
            yetty_ycore_error_destroy(tr.error);
            /* keep the image even if the title failed */
        }
    }
    return r.value;
}

#include <libgen.h> /* dirname */
#include <sys/stat.h>

static int path_exists(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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
    char *real = realpath(argv0, NULL);
    if (!real) {
        return NULL;
    }
    char *cur = real;
    for (int i = 0; i < up_levels; i++) {
        /* dirname() may mutate its input; replace cur in-place. */
        char *parent = strdup(cur);
        if (!parent) {
            free(real);
            return NULL;
        }
        char *d = dirname(parent);
        char *next = strdup(d);
        free(parent);
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

static char *probe_default_image(const char *argv0)
{
    const char *env = getenv("YGREETER_IMAGE");
    if (env && *env && path_exists(env)) {
        return strdup(env);
    }
    static const char *candidates[] = {
        "assets/logo.jpeg",
        "docs/logo-1.jpeg",
        "docs/logo-2.jpeg",
        "docs/logo-3.jpeg",
        "docs/logo-4.jpeg",
        "assets/apple-touch-icon.jpg",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char *p = find_repo_image(argv0, candidates[i]);
        if (p) {
            return p;
        }
    }
    return NULL;
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
};

/* Resolved image path for the Images tab. NULL when no usable file was
 * found (bundled assets missing AND no YGREETER_IMAGE env var) — in that
 * case the tab falls back to the static IMAGE_NAV YAMLs (placeholder).
 *
 * Owned by main; freed before exit. We use a global because load_entry
 * is called from many spots and threading a path argument through every
 * caller for the sake of one tab would clutter the signatures. */
static char *g_image_path = NULL;

/* Index of the Images tab. Held as a global so load_entry can route
 * image-tab loads through the buffer path instead of YAML. */
#define IMAGES_TAB_INDEX 2

struct app {
    struct yetty_ygui_engine *engine;
    struct yetty_ygui_widget *outer;
    struct yetty_ygui_widget *tabbar;
    struct tab_state tabs[4];
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
    if (tab_index < 0 || tab_index >= 4) {
        return;
    }
    struct tab_state *t = &app->tabs[tab_index];
    if (entry_index < 0 || entry_index >= t->n_entries) {
        return;
    }
    /* Images tab: when we have a resolved file, build a fresh ypaint
     * buffer with the yimage prim + a title TEXT_SPAN and install it
     * via set_buffer (which takes ownership). Falls through to the
     * static YAML placeholder when no image was found. */
    if (tab_index == IMAGES_TAB_INDEX && g_image_path) {
        struct yetty_ypaint_core_buffer *buf =
            build_image_buffer(t->entries[entry_index].label, g_image_path);
        if (buf) {
            yetty_ygui_widget_rich_set_buffer(t->rich, buf);
            return;
        }
        /* build_image_buffer failed (file disappeared, decode failed) —
         * fall through to the placeholder YAML so the tab isn't blank. */
    }
    const char *yaml = yaml_for(t, entry_index);
    struct yetty_ycore_void_result r =
        yetty_ygui_widget_rich_set_yaml(t->rich, yaml, strlen(yaml));
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Click handler bound per-button. Each nav-row button carries its own
 * row_link (allocated in register_row_link) as click_userdata, so we
 * don't have to look up the click target after dispatch. */
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

    /* Nav: a flex-column vbox of button rows. We tried using a `list`
     * with label children first — but ygui's grid_query routes clicks
     * to the deepest hit, and labels have no on_press, so the list's
     * on_select never fired. Buttons have on_press AND the engine
     * fires their click_callback on mouse-up out of the box. */
    snprintf(id_buf, sizeof(id_buf), "%s_nav", id_prefix);
    struct yetty_ygui_widget *nav =
        yetty_ygui_engine_vbox(app->engine, id_buf, 0, 0, 0, 0);
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

    /* Resolve the bundled image BEFORE building the Images tab, so
     * load_entry's first call (during build_tab_body) already sees the
     * path and goes through the yimage buffer path instead of the YAML
     * placeholder. Falls back to placeholder when no image is found. */
    g_image_path = probe_default_image(argv[0]);
    struct yetty_ygui_widget *images = yetty_ygui_widget_tabbar_add_tab(app.tabbar, "Images");
    build_tab_body(&app, IMAGES_TAB_INDEX, images, IMAGE_NAV,
                   (int)(sizeof(IMAGE_NAV) / sizeof(IMAGE_NAV[0])), "images");

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

    free(g_image_path);
    g_image_path = NULL;
    free_row_links();
    yetty_ygui_engine_destroy(app.engine);
    yetty_ygui_shutdown();
    return 0;
}
