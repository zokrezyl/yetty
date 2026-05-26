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

#ifdef YETTY_YGUI_HAS_UV
#include <uv.h>
#endif

#include <sys/ioctl.h>
#include <termios.h>

/*=============================================================================
 * Tab descriptors — shared by both modes.
 *===========================================================================*/

enum tab_kind {
    TAB_WELCOME = 0,
    TAB_PLOTS,
    TAB_IMAGES,
    TAB_CODE,
    TAB_ELEMENTS,
    TAB_COUNT,
};

static const char *TAB_LABELS[TAB_COUNT] = {"Welcome", "Plots", "Images", "Code", "Elements"};

/* Brand-palette packed RGBA (R in low byte). */
#define BRAND_TEXT 0xFFE4E5E0u
#define BRAND_MUTED 0xFFA8A79Fu
#define BRAND_ACCENT 0xFF92A86Bu
#define CODE_KEYWORD 0xFFEC8B4Eu  /* warm orange — int / return */
#define CODE_TYPE 0xFFB9D8FFu     /* light blue — struct names */
#define CODE_STRING 0xFFA8E0A8u   /* mint — literals */
#define CODE_COMMENT 0xFF8B8B8Bu  /* gray */
#define CODE_PUNCT 0xFFC0C0C0u    /* off-white */

/*=============================================================================
 * App state.
 *===========================================================================*/

/* Forward decl: client-mode loop state is opaque to non-client code. */
struct client_state;

struct app {
    struct yetty_ygui_runtime *engine;
    struct yetty_ygui_object *root;
    struct yetty_ygui_object *tabbar;
    struct yetty_ygui_object *body_panel;
    struct yetty_ygui_object *statusbar;
    struct yetty_ygui_object *menubar;

    /* Per-menubar popup menus + about dialog (absolute children of
     * root so they paint over the body). */
    struct yetty_ygui_object *menu_file;
    struct yetty_ygui_object *menu_edit;
    struct yetty_ygui_object *menu_view;
    struct yetty_ygui_object *menu_help;
    struct yetty_ygui_object *about_dialog;

    /* Popup menus bound to the dropdown/combobox in the Elements tab.
     * Lifetime spans tab switches, so they live on the app, not on the
     * tab body that gets recreated. */
    struct yetty_ygui_object *elements_dropdown_menu;
    struct yetty_ygui_object *elements_combo_menu;

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

/*=============================================================================
 * UI build + tab navigation.
 *===========================================================================*/

static void clear_children(struct yetty_ygui_object *parent)
{
    while (1) {
        struct yetty_ygui_object *c = yetty_ygui_object_first_child(parent);
        if (!c) break;
        struct yetty_ycore_void_result r = yetty_ygui_del(c);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
    }
}

/* Tiny helper — error-destroy-or-noop for the side-effect callbacks
 * below where we only care about the happy path. */
static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

static struct yetty_ycore_void_result body_add_label(struct yetty_ygui_object *body,
                                                     const char *text, float font_size)
{
    struct yetty_ygui_object_ptr_result lr =
        yetty_ygui_add(yetty_ygui_label_class_get(), body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "body_add_label: add");
    struct yetty_ycore_void_result r = yetty_ygui_label_set_text(lr.value, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "body_add_label: text");
    r = yetty_ygui_label_set_font_size(lr.value, font_size);
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_welcome_tab(struct yetty_ygui_object *body)
{
    struct yetty_ygui_object_ptr_result rr = yetty_ygui_add(yetty_ygui_rich_class_get(), body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "welcome: rich add");
    struct yetty_ygui_object *rich = rr.value;
    struct {
        const char *text;
        float fs;
        uint32_t color;
        int new_line_first;
    } spans[] = {
        {"Welcome to yetty", 22.0f, BRAND_ACCENT, 1},
        {"  ", 16.0f, BRAND_TEXT, 0},
        {"— a GPU terminal that draws more than text.", 16.0f, BRAND_TEXT, 0},
        {"", 0, 0, 1},
        {"Plots, images, rich docs — all next to your shell.", 14.0f, BRAND_MUTED, 1},
        {"Switch tabs to see what the GPU layer can do.", 14.0f, BRAND_MUTED, 1},
        {"", 0, 0, 1},
        {"Keyboard:", 14.0f, BRAND_TEXT, 1},
        {"  q       — quit", 13.0f, BRAND_MUTED, 1},
        {"  ←/→     — switch tabs", 13.0f, BRAND_MUTED, 1},
        {"  click   — interact with widgets", 13.0f, BRAND_MUTED, 1},
    };
    for (size_t i = 0; i < sizeof(spans) / sizeof(spans[0]); ++i) {
        if (spans[i].new_line_first) {
            struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(rich);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "welcome: rich_add_line");
        }
        if (spans[i].text[0]) {
            struct yetty_ycore_void_result sr =
                yetty_ygui_rich_add_span(rich, spans[i].text, spans[i].fs, spans[i].color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "welcome: rich_add_span");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_code_tab(struct yetty_ygui_object *body)
{
    struct yetty_ygui_object_ptr_result rr = yetty_ygui_add(yetty_ygui_rich_class_get(), body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "code: rich add");
    struct yetty_ygui_object *rich = rr.value;
    struct line_def {
        struct span_def {
            const char *text;
            uint32_t color;
        } spans[8];
    } lines[] = {
        {{{"/* Minimal ygui app — fits in main(). */", CODE_COMMENT}, {0, 0}}},
        {{{"#include ", CODE_KEYWORD}, {"<yetty/ygui/ygui.h>", CODE_STRING}, {0, 0}}},
        {{{0, 0}}},
        {{{"int", CODE_KEYWORD},
          {" main", CODE_TYPE},
          {"(", CODE_PUNCT},
          {"void", CODE_KEYWORD},
          {") {", CODE_PUNCT},
          {0, 0}}},
        {{{"    ", BRAND_TEXT},
          {"struct ", CODE_KEYWORD},
          {"yetty_ygui_runtime ", CODE_TYPE},
          {"*engine = framework_create(pty);", BRAND_TEXT},
          {0, 0}}},
        {{{"    framework_set_root(engine, build_ui());", BRAND_TEXT}, {0, 0}}},
        {{{"    framework_emit(engine);", BRAND_TEXT}, {0, 0}}},
        {{{"    ", BRAND_TEXT},
          {"return ", CODE_KEYWORD},
          {"0", CODE_STRING},
          {";", CODE_PUNCT},
          {0, 0}}},
        {{{"}", CODE_PUNCT}, {0, 0}}},
    };
    for (size_t li = 0; li < sizeof(lines) / sizeof(lines[0]); ++li) {
        struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(rich);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "code: rich_add_line");
        for (size_t si = 0; si < sizeof(lines[li].spans) / sizeof(lines[li].spans[0]); ++si) {
            if (!lines[li].spans[si].text) break;
            struct yetty_ycore_void_result sr = yetty_ygui_rich_add_span(
                rich, lines[li].spans[si].text, 13.0f, lines[li].spans[si].color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "code: rich_add_span");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_plots_tab(struct yetty_ygui_object *body)
{
    struct yetty_ygui_object_ptr_result pr = yetty_ygui_add(yetty_ygui_yplot_class_get(), body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "plots: yplot add");
    struct yetty_ygui_object *plot = pr.value;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(plot);
    l.flex_grow = 1.0f;
    l.min_height = 240.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(plot, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "plots: yplot layout");
    return yetty_ygui_yplot_set_source(
        plot, "f = sin(x); g = cos(x); @f.color = #6BA892; @g.color = #EC8B4E");
}

static struct yetty_ycore_void_result build_images_tab(struct yetty_ygui_object *body)
{
    /* Locate logo-2.jpeg under the runtime assets dir. */
    const char *assets = yetty_yplatform_get_assets_dir();
    if (!assets) {
        return body_add_label(body, "Images tab — assets_dir not available", 14.0f);
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/logo-2.jpeg", assets);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return body_add_label(body, "Images tab — logo-2.jpeg not found at the assets path",
                              14.0f);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return body_add_label(body, "Images tab — logo-2.jpeg empty", 14.0f);
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "images: malloc");
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    struct yetty_ygui_object_ptr_result ir = yetty_ygui_add(yetty_ygui_yimage_class_get(), body);
    if (YETTY_IS_ERR(ir)) {
        free(buf);
        return YETTY_ERR(yetty_ycore_void, "images: yimage add", ir);
    }
    struct yetty_ygui_object *img = ir.value;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(img);
    l.flex_grow = 1.0f;
    l.min_height = 240.0f;
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(img, &l));
    struct yetty_ycore_void_result br = yetty_ygui_yimage_set_bytes(img, buf, got);
    free(buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "images: yimage_set_bytes");
    return YETTY_OK_VOID();
}

/* Build one collapsing section with a title; returns the section so
 * the caller can attach rows to it. */
static struct yetty_ygui_object_ptr_result section(struct yetty_ygui_object *parent,
                                                   const char *title)
{
    struct yetty_ygui_object_ptr_result sr =
        yetty_ygui_add(yetty_ygui_collapsing_header_class_get(), parent);
    if (YETTY_IS_ERR(sr)) return sr;
    yetty_ycore_error_destroy_safe(yetty_ygui_collapsing_header_set_title(sr.value, title));
    return sr;
}

static struct yetty_ycore_void_result add_row(struct yetty_ygui_object *parent,
                                              const struct yetty_ygui_class *(*cls)(void),
                                              float height, struct yetty_ygui_object **out)
{
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(cls(), parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "elements: add_row");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(r.value);
    l.height = height;
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(r.value, &l));
    if (out) *out = r.value;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_elements_tab(struct app *app,
                                                         struct yetty_ygui_object *body)
{
    struct yetty_ygui_object *w;

    /*-- Inputs section --*/
    struct yetty_ygui_object_ptr_result inputs = section(body, "Inputs");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, inputs, "elements: inputs section");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_textinput_class_get, 30, &w),
                        "inputs: textinput");
    yetty_ycore_error_destroy_safe(yetty_ygui_textinput_set_placeholder(w, "Search…"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_textarea_class_get, 60, &w),
                        "inputs: textarea");
    yetty_ycore_error_destroy_safe(
        yetty_ygui_textarea_set_text(w, "Multi-line\ntextarea content."));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_checkbox_class_get, 24, &w),
                        "inputs: checkbox");
    yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_label(w, "Enable feature"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_radio_class_get, 24, &w),
                        "inputs: radio1");
    yetty_ycore_error_destroy_safe(yetty_ygui_radio_set_label(w, "Option A"));
    yetty_ycore_error_destroy_safe(yetty_ygui_radio_set_selected(w, 1));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_radio_class_get, 24, &w),
                        "inputs: radio2");
    yetty_ycore_error_destroy_safe(yetty_ygui_radio_set_label(w, "Option B"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_toggle_class_get, 28, &w),
                        "inputs: toggle");
    yetty_ycore_error_destroy_safe(yetty_ygui_toggle_set_label(w, "Auto-update"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_slider_class_get, 24, &w),
                        "inputs: slider");
    yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_value(w, 0.3f));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_spinner_class_get, 28, &w),
                        "inputs: spinner");
    yetty_ycore_error_destroy_safe(yetty_ygui_spinner_set_value(w, 42));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_progress_class_get, 12, &w),
                        "inputs: progress");
    yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(w, 0.4f));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(inputs.value, yetty_ygui_button_class_get, 32, &w),
                        "inputs: button");
    yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(w, "Apply"));

    /*-- Selectors section --*/
    struct yetty_ygui_object_ptr_result sels = section(body, "Selectors");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sels, "elements: selectors section");
    /* Dropdown + its menu. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(sels.value, yetty_ygui_dropdown_class_get, 30, &w),
                        "sels: dropdown");
    if (!app->elements_dropdown_menu) {
        struct yetty_ygui_object_ptr_result mr =
            yetty_ygui_add(yetty_ygui_popup_menu_class_get(), app->root);
        if (YETTY_IS_OK(mr)) app->elements_dropdown_menu = mr.value;
    }
    if (app->elements_dropdown_menu) {
        yetty_ycore_error_destroy_safe(
            yetty_ygui_dropdown_set_menu(w, app->elements_dropdown_menu));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(w, "Apple"));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(w, "Banana"));
        yetty_ycore_error_destroy_safe(yetty_ygui_dropdown_add_option(w, "Cherry"));
    }
    /* Combobox + its menu. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(sels.value, yetty_ygui_combobox_class_get, 30, &w),
                        "sels: combobox");
    if (!app->elements_combo_menu) {
        struct yetty_ygui_object_ptr_result mr =
            yetty_ygui_add(yetty_ygui_popup_menu_class_get(), app->root);
        if (YETTY_IS_OK(mr)) app->elements_combo_menu = mr.value;
    }
    if (app->elements_combo_menu) {
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_set_menu(w, app->elements_combo_menu));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_set_text(w, "Type or pick…"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(w, "alpha"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(w, "beta"));
        yetty_ycore_error_destroy_safe(yetty_ygui_combobox_add_suggestion(w, "gamma"));
    }
    /* Choicebox — multi-select. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(sels.value, yetty_ygui_choicebox_class_get, 72, &w),
                        "sels: choicebox");
    yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(w, "Bold"));
    yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(w, "Italic"));
    yetty_ycore_error_destroy_safe(yetty_ygui_choicebox_add(w, "Underline"));

    /*-- Data section --*/
    struct yetty_ygui_object_ptr_result data = section(body, "Data");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "elements: data section");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_row(data.value, yetty_ygui_list_class_get, 100, &w),
                        "data: list");
    yetty_ycore_error_destroy_safe(yetty_ygui_list_add(w, "Row 1"));
    yetty_ycore_error_destroy_safe(yetty_ygui_list_add(w, "Row 2"));
    yetty_ycore_error_destroy_safe(yetty_ygui_list_add(w, "Row 3"));
    yetty_ycore_error_destroy_safe(yetty_ygui_list_set_selected(w, 0));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_row(data.value, yetty_ygui_table_class_get, 96, &w),
                        "data: table");
    {
        const char *headers[] = {"Name", "Type", "Size"};
        yetty_ycore_error_destroy_safe(yetty_ygui_table_set_columns(w, 3, headers));
        const char *r1[] = {"button.c", "C", "4.2K"};
        const char *r2[] = {"slider.c", "C", "5.8K"};
        const char *r3[] = {"rich.c", "C", "6.1K"};
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(w, r1, 3));
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(w, r2, 3));
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(w, r3, 3));
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(data.value, yetty_ygui_tree_node_class_get, -1, &w),
                        "data: tree_node");
    yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_set_label(w, "src/"));
    {
        struct yetty_ygui_object_ptr_result cr =
            yetty_ygui_add(yetty_ygui_label_class_get(), w);
        if (YETTY_IS_OK(cr)) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(cr.value, "main.c"));
        }
    }

    /*-- Visual section --*/
    struct yetty_ygui_object_ptr_result vis = section(body, "Visual");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vis, "elements: visual section");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_colorpicker_class_get, 32, &w),
                        "vis: colorpicker");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_row(vis.value, yetty_ygui_chip_class_get, 22, &w),
                        "vis: chip");
    yetty_ycore_error_destroy_safe(yetty_ygui_chip_set_label(w, "tag"));
    yetty_ycore_error_destroy_safe(yetty_ygui_chip_set_closable(w, 1));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_breadcrumbs_class_get, 22, &w),
                        "vis: breadcrumbs");
    yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(w, "Home"));
    yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(w, "Settings"));
    yetty_ycore_error_destroy_safe(yetty_ygui_breadcrumbs_add(w, "Network"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_stepper_class_get, 56, &w), "vis: stepper");
    yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(w, "Connect"));
    yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(w, "Configure"));
    yetty_ycore_error_destroy_safe(yetty_ygui_stepper_add_step(w, "Done"));
    yetty_ycore_error_destroy_safe(yetty_ygui_stepper_set_current(w, 1));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_selectable_class_get, 24, &w),
                        "vis: selectable");
    yetty_ycore_error_destroy_safe(yetty_ygui_selectable_set_text(w, "Selectable row"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_separator_class_get, 1, NULL),
                        "vis: separator");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        add_row(vis.value, yetty_ygui_splitter_class_get, 8, NULL),
                        "vis: splitter");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_tab(struct app *app, int index)
{
    if (index < 0 || index >= TAB_COUNT) return YETTY_OK_VOID();
    clear_children(app->body_panel);
    struct yetty_ycore_void_result r;
    switch (index) {
    case TAB_WELCOME:
        r = build_welcome_tab(app->body_panel);
        break;
    case TAB_PLOTS:
        r = build_plots_tab(app->body_panel);
        break;
    case TAB_IMAGES:
        r = build_images_tab(app->body_panel);
        break;
    case TAB_CODE:
        r = build_code_tab(app->body_panel);
        break;
    case TAB_ELEMENTS:
        r = build_elements_tab(app, app->body_panel);
        break;
    default:
        r = YETTY_OK_VOID();
        break;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "load_tab: body");
    yetty_ygui_framework_mark_dirty(app->engine);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_tab_change(struct yetty_ygui_object *target,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)target;
    return load_tab((struct app *)userdata, event->i0);
}

static struct yetty_ycore_void_result on_menu_quit(struct yetty_ygui_object *menu, int item,
                                                   void *userdata)
{
    (void)menu;
    (void)item;
    struct app *app = (struct app *)userdata;
    /* Posting an arbitrary key event that the on_key handler maps to
     * shutdown — keeps shutdown wiring in one place. */
    yetty_ygui_framework_mark_dirty(app->engine);
    if (app->yframework && app->yframework->event_loop &&
        app->yframework->event_loop->ops->stop) {
        app->yframework->event_loop->ops->stop(app->yframework->event_loop);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_menu_about(struct yetty_ygui_object *menu, int item,
                                                    void *userdata)
{
    (void)menu;
    (void)item;
    struct app *app = (struct app *)userdata;
    if (!app->about_dialog) return YETTY_OK_VOID();
    float ew = 800.0f, eh = 600.0f;
    yetty_ygui_framework_viewport(app->engine, &ew, &eh);
    float dw = 400.0f, dh = 180.0f;
    return yetty_ygui_dialog_open_at(app->about_dialog, (ew - dw) * 0.5f, (eh - dh) * 0.5f, dw,
                                     dh);
}

static struct yetty_ycore_void_result on_menu_noop(struct yetty_ygui_object *menu, int item,
                                                   void *userdata)
{
    (void)menu;
    (void)item;
    (void)userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_about_close(struct yetty_ygui_object *obj, void *userdata)
{
    (void)obj;
    struct app *app = (struct app *)userdata;
    return yetty_ygui_dialog_close(app->about_dialog);
}

static struct yetty_ycore_void_result build_menus(struct app *app)
{
    struct yetty_ygui_object_ptr_result mr;
#define MK_MENU(field)                                                                             \
    mr = yetty_ygui_add(yetty_ygui_popup_menu_class_get(), app->root);                             \
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "build_menus: " #field);                             \
    app->field = mr.value
    MK_MENU(menu_file);
    MK_MENU(menu_edit);
    MK_MENU(menu_view);
    MK_MENU(menu_help);
#undef MK_MENU

    struct yetty_ycore_void_result r;
    r = yetty_ygui_popup_menu_add_item(app->menu_file, "New tab", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: file/new");
    r = yetty_ygui_popup_menu_add_item(app->menu_file, "Reload", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: file/reload");
    r = yetty_ygui_popup_menu_add_separator(app->menu_file);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: file/sep");
    r = yetty_ygui_popup_menu_add_item(app->menu_file, "Quit", on_menu_quit, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: file/quit");

    r = yetty_ygui_popup_menu_add_item(app->menu_edit, "Cut", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: edit/cut");
    r = yetty_ygui_popup_menu_add_item(app->menu_edit, "Copy", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: edit/copy");
    r = yetty_ygui_popup_menu_add_item(app->menu_edit, "Paste", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: edit/paste");

    r = yetty_ygui_popup_menu_add_item(app->menu_view, "Zoom In", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: view/zin");
    r = yetty_ygui_popup_menu_add_item(app->menu_view, "Zoom Out", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: view/zout");
    r = yetty_ygui_popup_menu_add_item(app->menu_view, "Reset", on_menu_noop, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: view/reset");

    r = yetty_ygui_popup_menu_add_item(app->menu_help, "About", on_menu_about, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menus: help/about");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_about_dialog(struct app *app)
{
    struct yetty_ygui_object_ptr_result dr =
        yetty_ygui_add(yetty_ygui_dialog_class_get(), app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "about: dialog add");
    app->about_dialog = dr.value;
    struct yetty_ycore_void_result tr =
        yetty_ygui_dialog_set_title(app->about_dialog, "About ygreeter");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "about: set_title");
    struct yetty_ygui_object_ptr_result lr =
        yetty_ygui_add(yetty_ygui_label_class_get(), app->about_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "about: label add");
    tr = yetty_ygui_label_set_text(
        lr.value, "ygreeter — yetty's first-contact tool.\nBuilt on the new ygui toolkit.");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "about: label text");
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_button_class_get(), app->about_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "about: button add");
    tr = yetty_ygui_button_set_label(br.value, "Close");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "about: button label");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(br.value);
        l.width = 100.0f;
        l.height = 32.0f;
        struct yetty_ycore_void_result wl = yetty_ygui_widget_layout_set(br.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wl, "about: button layout");
    }
    return yetty_ygui_clickable_on_click_set(br.value, on_about_close, app);
}

static struct yetty_ycore_void_result build_ui(struct app *app)
{
    struct yetty_ygui_object_ptr_result rr = yetty_ygui_add(yetty_ygui_vbox_class_get(), NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "build_ui: root add");
    app->root = rr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->root);
        l.gap = 6;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->root, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: root layout");
    }
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->engine, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build_ui: set_root");

    /* Menubar — File / Edit / View / Help. */
    struct yetty_ygui_object_ptr_result mbr =
        yetty_ygui_add(yetty_ygui_menubar_class_get(), app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mbr, "build_ui: menubar add");
    app->menubar = mbr.value;

    /* Tabbar. */
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
        l.width = 110;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(hr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: header layout");
    }
    struct yetty_ycore_void_result subr = yetty_ygui_object_subscribe(
        app->tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_change, app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, subr, "build_ui: subscribe");

    /* Body panel (vbox so per-tab content can stack vertically). */
    struct yetty_ygui_object_ptr_result bpr =
        yetty_ygui_add(yetty_ygui_vbox_class_get(), app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bpr, "build_ui: body panel add");
    app->body_panel = bpr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->body_panel);
        l.flex_grow = 1;
        l.padding_top = 16;
        l.padding_left = l.padding_right = 24;
        l.padding_bottom = 16;
        l.gap = 8;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(app->body_panel, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "build_ui: body panel layout");
    }

    /* Statusbar — bottom strip. */
    struct yetty_ygui_object_ptr_result sbr =
        yetty_ygui_add(yetty_ygui_statusbar_class_get(), app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sbr, "build_ui: statusbar add");
    app->statusbar = sbr.value;
    yetty_ycore_error_destroy_safe(
        yetty_ygui_statusbar_set_left(app->statusbar, "Ready — ygreeter"));
    yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_right(app->statusbar, "v0.3"));

    /* Menus + about dialog — absolute children of root, painted last so
     * they layer on top. Build AFTER the in-flow children so paint order
     * has them on top. */
    struct yetty_ycore_void_result mr = build_menus(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "build_ui: build_menus");
    struct yetty_ycore_void_result br = build_about_dialog(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "build_ui: about dialog");

    /* Wire the menubar trigger buttons. Done after menus exist. */
    yetty_ycore_error_destroy_safe(yetty_ygui_menubar_add(app->menubar, "File", app->menu_file));
    yetty_ycore_error_destroy_safe(yetty_ygui_menubar_add(app->menubar, "Edit", app->menu_edit));
    yetty_ycore_error_destroy_safe(yetty_ygui_menubar_add(app->menubar, "View", app->menu_view));
    yetty_ycore_error_destroy_safe(yetty_ygui_menubar_add(app->menubar, "Help", app->menu_help));

    return load_tab(app, 0);
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
