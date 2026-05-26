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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yplot/yplot.h>

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
    TAB_KIND_RICH = 0,
    TAB_KIND_PLOTS,
    TAB_KIND_IMAGES,
};

#define TAB_COUNT 4

static const char *TAB_LABELS[TAB_COUNT] = {"Welcome", "Plots", "Images", "Code"};

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

    /* Client mode back-pointer for the key-handler's stop_cb path.
     * NULL in standalone mode. */
    struct client_state *client;

    /* Standalone-mode resources, NULL in client mode. */
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
};

/* Image-path scratch: filled from get_data_dir/logo-N.jpeg at startup,
 * referenced from the Images nav entries. Heap so the count can grow. */
static char **g_image_paths = NULL;
static int g_image_path_count = 0;

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
    {{{"    yetty_ygui_button_class_get(), parent);", BRAND_TEXT}}},
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
static void discover_logo_images(void)
{
    if (g_image_paths) return;
    const char *data_dir = yetty_yplatform_get_data_dir();
    if (!data_dir || !*data_dir) return;
    char path_buf[1024];
    char **paths = NULL;
    int count = 0;
    int cap = 0;
    for (int i = 1; i <= 8; ++i) {
        snprintf(path_buf, sizeof(path_buf), "%s/logo-%d.jpeg", data_dir, i);
        if (access(path_buf, R_OK) != 0) continue;
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
    g_image_paths = paths;
    g_image_path_count = count;
}

/*-----------------------------------------------------------------------------
 * Loaders — populate the per-tab content widget from a given entry.
 *---------------------------------------------------------------------------*/

/* Wipe a content widget's children and recreate it as the requested kind
 * — the new ygui rich/yplot/yimage widgets do not expose a clear API
 * comparable to ygui-old's set_yaml replacement. Recreating is the
 * simplest correct way to swap content. */
static struct yetty_ycore_void_result rebuild_tab_content(struct app *app, int tab_index);

static struct yetty_ycore_void_result load_plot_entry(struct yetty_ygui_object *plot,
                                                      const struct nav_entry *entry)
{
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

static struct yetty_ycore_void_result load_image_entry(struct yetty_ygui_object *image,
                                                       const char *path)
{
    if (!path) {
        return yetty_ygui_yimage_set_bytes(image, NULL, 0);
    }
    FILE *f = fopen(path, "rb");
    if (!f) return YETTY_ERR(yetty_ycore_void, "load_image_entry: fopen");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "load_image_entry: empty");
    }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "load_image_entry: malloc");
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    struct yetty_ycore_void_result r = yetty_ygui_yimage_set_bytes(image, buf, got);
    free(buf);
    return r;
}

static struct yetty_ycore_void_result write_code_snippet(struct yetty_ygui_object *rich,
                                                         const char *snippet_id)
{
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

static struct yetty_ycore_void_result write_welcome_spans(struct yetty_ygui_object *rich,
                                                          const struct rich_span *spans,
                                                          size_t n_spans)
{
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

static struct yetty_ycore_void_result on_row_clicked(struct yetty_ygui_object *btn, void *userdata);

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

static int tab_entry_count(int tab_index)
{
    switch (tab_index) {
    case 0: return (int)(sizeof(welcome_nav_entries) / sizeof(welcome_nav_entries[0]));
    case 1: return (int)(sizeof(plot_nav_entries) / sizeof(plot_nav_entries[0]));
    case 2: return g_image_path_count > 0 ? g_image_path_count : 1;
    case 3: return (int)(sizeof(code_nav_entries) / sizeof(code_nav_entries[0]));
    default: return 0;
    }
}

static const char *tab_entry_label(int tab_index, int entry_index)
{
    switch (tab_index) {
    case 0: return welcome_nav_entries[entry_index].label;
    case 1: return plot_nav_entries[entry_index].label;
    case 2: {
        if (g_image_path_count <= 0) return "(no images found)";
        static char buf[64];
        snprintf(buf, sizeof(buf), "logo-%d", entry_index + 1);
        return buf;
    }
    case 3: return code_nav_entries[entry_index].label;
    default: return "";
    }
}

static enum tab_kind tab_kind_for(int tab_index)
{
    switch (tab_index) {
    case 1: return TAB_KIND_PLOTS;
    case 2: return TAB_KIND_IMAGES;
    case 0:
    case 3:
    default:
        return TAB_KIND_RICH;
    }
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
    int n = tab_entry_count(tab_index);
    t->n_entries = n;
    if (t->active_entry < 0 || t->active_entry >= n) t->active_entry = 0;

    /* Outer hbox: nav + content side-by-side. */
    struct yetty_ygui_object_ptr_result hr =
        yetty_ygui_add(yetty_ygui_hbox_class_get(), app->body_panel);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "rebuild: hbox");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(hr.value);
        l.flex_grow = 1.0f;
        l.gap = 12.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(hr.value, &l));
    }

    /* Nav vbox — fixed 220-px wide column of clickable rows. */
    struct yetty_ygui_object_ptr_result nr =
        yetty_ygui_add(yetty_ygui_vbox_class_get(), hr.value);
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
            yetty_ygui_add(yetty_ygui_button_class_get(), nr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "rebuild: nav button");
        yetty_ycore_error_destroy_safe(
            yetty_ygui_button_set_label(br.value, tab_entry_label(tab_index, i)));
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

    /* Content widget — class chosen by tab kind. */
    struct yetty_ygui_object *content = NULL;
    switch (t->kind) {
    case TAB_KIND_PLOTS: {
        struct yetty_ygui_object_ptr_result pr =
            yetty_ygui_add(yetty_ygui_yplot_class_get(), hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rebuild: yplot");
        content = pr.value;
        break;
    }
    case TAB_KIND_IMAGES: {
        struct yetty_ygui_object_ptr_result ir =
            yetty_ygui_add(yetty_ygui_yimage_class_get(), hr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "rebuild: yimage");
        content = ir.value;
        break;
    }
    case TAB_KIND_RICH:
    default: {
        struct yetty_ygui_object_ptr_result rr =
            yetty_ygui_add(yetty_ygui_rich_class_get(), hr.value);
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
        yetty_ycore_error_destroy_safe(load_plot_entry(content, e));
        break;
    }
    case TAB_KIND_IMAGES: {
        const char *path = g_image_path_count > 0 && t->active_entry < g_image_path_count
                               ? g_image_paths[t->active_entry]
                               : NULL;
        yetty_ycore_error_destroy_safe(load_image_entry(content, path));
        break;
    }
    case TAB_KIND_RICH:
    default: {
        if (tab_index == 0) {
            const struct welcome_nav *e = &welcome_nav_entries[t->active_entry];
            yetty_ycore_error_destroy_safe(write_welcome_spans(content, e->spans, e->n_spans));
        } else {
            yetty_ycore_error_destroy_safe(
                write_code_snippet(content, code_nav_entries[t->active_entry].payload));
        }
        break;
    }
    }

    yetty_ygui_framework_mark_dirty(app->engine);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_row_clicked(struct yetty_ygui_object *btn, void *userdata)
{
    (void)btn;
    struct row_link *rl = (struct row_link *)userdata;
    if (!rl) return YETTY_OK_VOID();
    if (rl->tab != yetty_ygui_tabbar_active(rl->app->tabbar)) {
        return YETTY_OK_VOID();
    }
    rl->app->tabs[rl->tab].active_entry = rl->entry;
    return rebuild_tab_content(rl->app, rl->tab);
}

static struct yetty_ycore_void_result on_tab_change(struct yetty_ygui_object *target,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)target;
    int idx = event->i0;
    if (idx < 0 || idx >= TAB_COUNT) return YETTY_OK_VOID();
    return rebuild_tab_content((struct app *)userdata, idx);
}

static struct yetty_ycore_void_result build_ui(struct app *app)
{
    discover_logo_images();

    struct yetty_ygui_object_ptr_result rr = yetty_ygui_add(yetty_ygui_vbox_class_get(), NULL);
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
        yetty_ygui_add(yetty_ygui_tabbar_class_get(), app->root);
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
        yetty_ygui_add(yetty_ygui_vbox_class_get(), app->root);
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
        yetty_ygui_add(yetty_ygui_statusbar_class_get(), app->root);
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
    struct app *app;
    int running;
};

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
    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        struct yetty_ycore_void_result r =
            yetty_ygui_framework_feed_input(cs->app->engine, buf, (size_t)n);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
    } else if (n == 0) {
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

    struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(app.engine);
    if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
    uv_loop_close(&cs.loop);
    return 0;
}

#endif /* YETTY_YGUI_HAS_UV */

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
            YETTY_YFIGURE_KIND_YPLOT, YETTY_YFIGURE_KIND_YIMAGE,
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
        struct yetty_yfigure_container_ptr_result cr =
            yetty_yfigure_container_create(root_rect, &ctx, app->figure_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "standalone: container_create");
        app->root_container = cr.value;
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
            app->wire_sm, YETTY_YWIRE_ENVELOPE_OSC, YETTY_OSC_YCOMPOSITOR_BIN,
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
    return YETTY_OK_VOID();
}

static int run_standalone_mode(int argc, char **argv)
{
    struct app app = {0};
    struct yetty_yinit_app_config cfg = {.extract_assets_fn = yetty_platform_extract_assets};
    return yetty_yinit_run(argc, argv, &cfg, standalone_worker, &app);
}

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
    return run_standalone_mode(argc, argv);
}
