/*
 * Demo 35_collapsing_header_open: the ygreeter "Elements" tab.
 *
 * A menubar on top, then a scrollarea holding one collapsing_header per
 * widget category — Inputs, Selectors, Display, Lists & Trees, Layout &
 * Containers, Overlays. Every non-figure widget the toolkit ships gets
 * one instance so this doubles as a visual catalog and as the regression
 * surface for collapsing_header itself (click a section header to fold
 * it; the children disappear and reappear, the section resizes).
 *
 * Figure-backed widgets (yplot, yimage, ypdf, ybrowser, …) are
 * deliberately excluded — they need a live figure pipeline and belong in
 * their own demos.
 *
 * The flex layout has no intrinsic content sizing, so every leaf carries
 * an authored height and each section's height is derived from the sum of
 * its children (see finalize_section).
 *
 * Standalone-mode ygui demo. Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

#include <stdlib.h>
#include <string.h>

#define ROW_H 28.0f

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Add `cls` under `parent` and author its main-axis height. Returns NULL
 * on allocation failure (the demo simply skips that widget). */
static struct yetty_yclass_object *add_w(struct yetty_yclass_object *parent,
                                         const struct yetty_yclass *cls, float height)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(r.value);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return r.value;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.height = height;
    err_ok(yetty_ygui_widget_layout_set(r.value, &l));
    return r.value;
}

static void set_width(struct yetty_yclass_object *w, float width)
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
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

static void set_grow(struct yetty_yclass_object *w, float grow)
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
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* Open collapsing_header section, titled, initially expanded. */
static struct yetty_yclass_object *add_section(struct yetty_yclass_object *area, const char *title)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(area, yetty_ygui_collapsing_header_class_get().value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_collapsing_header_set_title(r.value, title));
    err_ok(yetty_ygui_collapsing_header_set_open(r.value, 1));
    return r.value;
}

/* Derive a section's open height from its children: header strip +
 * paddings + the sum of authored child heights + inter-row gaps. Must be
 * called after every child has been added. */
static void finalize_section(struct yetty_yclass_object *sec)
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
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(sec);
    if (YETTY_IS_ERR(child_res)) {
        yetty_ycore_error_destroy(child_res.error);
        return;
    }
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ygui_layout_const_ptr_result child_layout_res =
            yetty_ygui_widget_layout_get(c);
        if (YETTY_IS_ERR(child_layout_res)) {
            yetty_ycore_error_destroy(child_layout_res.error);
            return;
        }
        const struct yetty_ygui_layout *cl = child_layout_res.value;
        total += cl->height > 0.0f ? cl->height : 0.0f;
        n++;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        if (YETTY_IS_ERR(next_res)) {
            yetty_ycore_error_destroy(next_res.error);
            return;
        }
        c = next_res.value;
    }
    if (n > 1) {
        total += sl->gap * (float)(n - 1);
    }
    struct yetty_ygui_layout_const_ptr_result final_layout_res = yetty_ygui_widget_layout_get(sec);
    if (YETTY_IS_ERR(final_layout_res)) {
        yetty_ycore_error_destroy(final_layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *final_layout_res.value;
    l.height = total;
    err_ok(yetty_ygui_widget_layout_set(sec, &l));
}

/* ---- Overlay trigger callbacks ---- */

static struct yetty_ycore_void_result on_menu_item(struct yetty_yclass_object *menu, int idx,
                                                   void *ud)
{
    (void)menu;
    (void)idx;
    (void)ud;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_open_dialog(struct yetty_yclass_object *obj, void *ud)
{
    (void)obj;
    return yetty_ygui_dialog_open_at((struct yetty_yclass_object *)ud, 240, 180, 380, 180);
}

static struct yetty_ycore_void_result on_open_menu(struct yetty_yclass_object *obj, void *ud)
{
    struct yetty_ycore_rectangle_result rect_res =
        yetty_ygui_widget_rect((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "35_collapsing_header_open: widget_rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    return yetty_ygui_popup_menu_toggle_at((struct yetty_yclass_object *)ud, r.min.x,
                                           r.max.y + 2.0f);
}

/* ---- Section builders ---- */

static void build_inputs(struct yetty_yclass_object *area)
{
    struct yetty_yclass_object *sec = add_section(area, "Inputs");

    struct yetty_yclass_object *btn = add_w(sec, yetty_ygui_button_class_get().value, 32);
    err_ok(yetty_ygui_button_set_label(btn, "Button"));

    struct yetty_yclass_object *ti = add_w(sec, yetty_ygui_textinput_class_get().value, ROW_H);
    err_ok(yetty_ygui_textinput_set_placeholder(ti, "type here…"));

    struct yetty_yclass_object *sl = add_w(sec, yetty_ygui_slider_class_get().value, ROW_H);
    err_ok(yetty_ygui_slider_set_range(sl, 0.0f, 1.0f));
    err_ok(yetty_ygui_slider_set_value(sl, 0.4f));

    struct yetty_yclass_object *spin_i = add_w(sec, yetty_ygui_spinner_class_get().value, 30);
    err_ok(yetty_ygui_spinner_set_range(spin_i, 1.0f, 100.0f, 1.0f));
    err_ok(yetty_ygui_spinner_set_value(spin_i, 42.0f));

    struct yetty_yclass_object *spin_f = add_w(sec, yetty_ygui_spinner_class_get().value, 30);
    err_ok(yetty_ygui_spinner_set_range(spin_f, 0.0f, 10.0f, 0.25f));
    err_ok(yetty_ygui_spinner_set_value(spin_f, 2.5f));

    struct yetty_yclass_object *cb = add_w(sec, yetty_ygui_checkbox_class_get().value, 24);
    err_ok(yetty_ygui_checkbox_set_label(cb, "Enabled"));
    err_ok(yetty_ygui_checkbox_set_checked(cb, 1));

    struct yetty_yclass_object *tg = add_w(sec, yetty_ygui_toggle_class_get().value, 26);
    err_ok(yetty_ygui_toggle_set_label(tg, "Notifications"));
    err_ok(yetty_ygui_toggle_set_on(tg, 1));

    struct yetty_yclass_object *pr = add_w(sec, yetty_ygui_progress_class_get().value, 16);
    err_ok(yetty_ygui_progress_set_value(pr, 0.65f));

    struct yetty_yclass_object *ta = add_w(sec, yetty_ygui_textarea_class_get().value, 120);
    err_ok(yetty_ygui_textarea_set_text(ta, "Multi-line text area.\nClick to focus, then type.\n"));

    finalize_section(sec);
}

static void build_selectors(struct yetty_yclass_object *area)
{
    struct yetty_yclass_object *sec = add_section(area, "Selectors");

    static const char *fruits[] = {"Apple", "Banana", "Cherry"};
    for (int i = 0; i < 3; i++) {
        struct yetty_yclass_object *rb = add_w(sec, yetty_ygui_radio_class_get().value, 24);
        err_ok(yetty_ygui_radio_set_label(rb, fruits[i]));
        err_ok(yetty_ygui_radio_set_selected(rb, i == 0 ? 1 : 0));
    }

    struct yetty_yclass_object *dd = add_w(sec, yetty_ygui_dropdown_class_get().value, ROW_H);
    err_ok(yetty_ygui_dropdown_add_option(dd, "Option A"));
    err_ok(yetty_ygui_dropdown_add_option(dd, "Option B"));
    err_ok(yetty_ygui_dropdown_add_option(dd, "Option C"));
    err_ok(yetty_ygui_dropdown_set_selected(dd, 0));

    struct yetty_yclass_object *combo = add_w(sec, yetty_ygui_combobox_class_get().value, ROW_H);
    err_ok(yetty_ygui_combobox_set_text(combo, "red"));
    err_ok(yetty_ygui_combobox_add_suggestion(combo, "green"));
    err_ok(yetty_ygui_combobox_add_suggestion(combo, "blue"));
    err_ok(yetty_ygui_combobox_add_suggestion(combo, "magenta"));

    struct yetty_yclass_object *ch = add_w(sec, yetty_ygui_choicebox_class_get().value, 4 * ROW_H);
    err_ok(yetty_ygui_choicebox_add(ch, "Small"));
    err_ok(yetty_ygui_choicebox_add(ch, "Medium"));
    err_ok(yetty_ygui_choicebox_add(ch, "Large"));
    err_ok(yetty_ygui_choicebox_add(ch, "Huge"));

    struct yetty_yclass_object *cp = add_w(sec, yetty_ygui_colorpicker_class_get().value, 32);
    err_ok(yetty_ygui_colorpicker_set_color(cp, 0xFF92A86Bu));

    finalize_section(sec);
}

static void build_display(struct yetty_yclass_object *area)
{
    struct yetty_yclass_object *sec = add_section(area, "Display");

    struct yetty_yclass_object *lbl = add_w(sec, yetty_ygui_label_class_get().value, 24);
    err_ok(yetty_ygui_label_set_text(lbl, "Plain label"));

    add_w(sec, yetty_ygui_separator_class_get().value, 8);

    struct yetty_yclass_object *pr = add_w(sec, yetty_ygui_progress_class_get().value, 14);
    err_ok(yetty_ygui_progress_set_value(pr, 0.25f));

    struct yetty_yclass_object *tbl = add_w(sec, yetty_ygui_table_class_get().value, 120);
    static const char *cols[] = {"PID", "USER", "%CPU", "COMMAND"};
    err_ok(yetty_ygui_table_set_columns(tbl, 4, cols));
    static const char *row1[] = {"1", "root", "0.0", "/sbin/init"};
    static const char *row2[] = {"42", "misi", "1.3", "/usr/bin/yetty"};
    static const char *row3[] = {"1337", "misi", "0.2", "/usr/bin/ygreeter"};
    err_ok(yetty_ygui_table_add_row(tbl, row1, 4));
    err_ok(yetty_ygui_table_add_row(tbl, row2, 4));
    err_ok(yetty_ygui_table_add_row(tbl, row3, 4));

    struct yetty_yclass_object *crumbs = add_w(sec, yetty_ygui_breadcrumbs_class_get().value, 24);
    err_ok(yetty_ygui_breadcrumbs_add(crumbs, "Home"));
    err_ok(yetty_ygui_breadcrumbs_add(crumbs, "Projects"));
    err_ok(yetty_ygui_breadcrumbs_add(crumbs, "yetty"));
    err_ok(yetty_ygui_breadcrumbs_add(crumbs, "ygui"));

    /* Closable chip / tag row. */
    struct yetty_yclass_object *chip_row = add_w(sec, yetty_ygui_hbox_class_get().value, 24);
    if (chip_row) {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(chip_row);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
        } else {
            struct yetty_ygui_layout l = *layout_res.value;
            l.gap = 6.0f;
            err_ok(yetty_ygui_widget_layout_set(chip_row, &l));
        }
        static const char *tags[] = {"linux", "gpu", "rust-free"};
        for (int i = 0; i < 3; i++) {
            struct yetty_yclass_object *chip =
                add_w(chip_row, yetty_ygui_chip_class_get().value, 24);
            set_width(chip, 90);
            err_ok(yetty_ygui_chip_set_label(chip, tags[i]));
            err_ok(yetty_ygui_chip_set_closable(chip, i < 2 ? 1 : 0));
        }
    }

    struct yetty_yclass_object *step = add_w(sec, yetty_ygui_stepper_class_get().value, 56);
    err_ok(yetty_ygui_stepper_add_step(step, "Setup"));
    err_ok(yetty_ygui_stepper_add_step(step, "Install"));
    err_ok(yetty_ygui_stepper_add_step(step, "Done"));
    err_ok(yetty_ygui_stepper_set_current(step, 1));

    finalize_section(sec);
}

static void build_lists_trees(struct yetty_yclass_object *area)
{
    struct yetty_yclass_object *sec = add_section(area, "Lists & Trees");

    struct yetty_yclass_object *lst = add_w(sec, yetty_ygui_list_class_get().value, 4 * 24);
    err_ok(yetty_ygui_list_add(lst, "Apple"));
    err_ok(yetty_ygui_list_add(lst, "Banana"));
    err_ok(yetty_ygui_list_add(lst, "Cherry"));
    err_ok(yetty_ygui_list_add(lst, "Date"));
    err_ok(yetty_ygui_list_set_selected(lst, 0));

    /* tree_node owns its own header strip + indented children. */
    struct yetty_yclass_object *tn =
        add_w(sec, yetty_ygui_tree_node_class_get().value, 22 + 2 * 24 + 2);
    err_ok(yetty_ygui_tree_node_set_label(tn, "Tree root"));
    err_ok(yetty_ygui_tree_node_set_open(tn, 1));
    if (tn) {
        struct yetty_yclass_object *c1 = add_w(tn, yetty_ygui_label_class_get().value, 24);
        err_ok(yetty_ygui_label_set_text(c1, "child 1"));
        struct yetty_yclass_object *c2 = add_w(tn, yetty_ygui_label_class_get().value, 24);
        err_ok(yetty_ygui_label_set_text(c2, "child 2"));
    }

    /* Calendar. */
    struct yetty_yclass_object *dp = add_w(sec, yetty_ygui_datepicker_class_get().value, 220);
    err_ok(yetty_ygui_datepicker_set_date(dp, 2025, 4, 15)); /* May 15, 2025 */

    /* File browser, rooted at $HOME (or "/"). */
    struct yetty_yclass_object *fp = add_w(sec, yetty_ygui_filepicker_class_get().value, 240);
    const char *home = getenv("HOME");
    err_ok(yetty_ygui_filepicker_set_dir(fp, home && *home ? home : "/"));

    finalize_section(sec);
}

static void build_layout_containers(struct yetty_yclass_object *area)
{
    struct yetty_yclass_object *sec = add_section(area, "Layout & Containers");

    /* [left panel | splitter | right panel] — drag the splitter to resize. */
    struct yetty_yclass_object *row = add_w(sec, yetty_ygui_hbox_class_get().value, 80);
    if (row) {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(row);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
        } else {
            struct yetty_ygui_layout l = *layout_res.value;
            l.gap = 0.0f;
            err_ok(yetty_ygui_widget_layout_set(row, &l));
        }

        struct yetty_yclass_object *left = add_w(row, yetty_ygui_panel_class_get().value, 80);
        set_width(left, 220);
        err_ok(yetty_ygui_panel_set_bg(left, (struct yetty_ycore_rgba){30, 38, 44, 255}));

        struct yetty_yclass_object *div = add_w(row, yetty_ygui_splitter_class_get().value, 80);
        set_width(div, 6);

        struct yetty_yclass_object *right = add_w(row, yetty_ygui_panel_class_get().value, 80);
        set_grow(right, 1.0f);
        err_ok(yetty_ygui_panel_set_bg(right, (struct yetty_ycore_rgba){20, 26, 31, 255}));
    }

    finalize_section(sec);
}

/* Overlays section. The dialog + popup_menu themselves live under `root`
 * (passed in) so they float above the sections; the trigger buttons sit
 * inside the section and toggle them via callbacks. */
static void build_overlays(struct yetty_yclass_object *area, struct yetty_yclass_object *root)
{
    struct yetty_yclass_object *sec = add_section(area, "Overlays");

    struct yetty_yclass_object *tip = add_w(sec, yetty_ygui_tooltip_class_get().value, ROW_H);
    err_ok(yetty_ygui_tooltip_set_text(tip, "Tooltip example"));

    struct yetty_yclass_object *selr = add_w(sec, yetty_ygui_selectable_class_get().value, 26);
    err_ok(yetty_ygui_selectable_set_text(selr, "Selectable row"));

    struct yetty_yclass_object *open_dlg = add_w(sec, yetty_ygui_button_class_get().value, 30);
    err_ok(yetty_ygui_button_set_label(open_dlg, "Open dialog…"));

    struct yetty_yclass_object *open_menu = add_w(sec, yetty_ygui_button_class_get().value, 30);
    err_ok(yetty_ygui_button_set_label(open_menu, "Open menu…"));

    finalize_section(sec);

    /* Modal dialog — closed until the trigger fires. */
    struct yetty_yclass_object_ptr_result dr =
        yetty_ygui_widget_add(root, yetty_ygui_dialog_class_get().value);
    if (YETTY_IS_OK(dr)) {
        err_ok(yetty_ygui_dialog_set_title(dr.value, "Dialog"));
        struct yetty_yclass_object_ptr_result body =
            yetty_ygui_widget_add(dr.value, yetty_ygui_label_class_get().value);
        if (YETTY_IS_OK(body)) {
            err_ok(yetty_ygui_label_set_text(body.value, "This is a modal dialog."));
        }
        err_ok(yetty_ygui_clickable_on_click_set(open_dlg, on_open_dialog, dr.value));
    }

    /* Standalone popup menu. */
    struct yetty_yclass_object_ptr_result pm =
        yetty_ygui_widget_add(root, yetty_ygui_popup_menu_class_get().value);
    if (YETTY_IS_OK(pm)) {
        err_ok(yetty_ygui_popup_menu_add_item(pm.value, "First action", on_menu_item, NULL));
        err_ok(yetty_ygui_popup_menu_add_item(pm.value, "Second action", on_menu_item, NULL));
        err_ok(yetty_ygui_popup_menu_add_separator(pm.value));
        err_ok(yetty_ygui_popup_menu_add_item(pm.value, "Third action", on_menu_item, NULL));
        err_ok(yetty_ygui_clickable_on_click_set(open_menu, on_open_menu, pm.value));
    }
}

/* Build the File/Edit/View menubar. Its popup_menus are added under
 * `root` (last, so they overlay the sections). */
static void build_menubar(struct yetty_yclass_object *mb, struct yetty_yclass_object *root)
{
    struct {
        const char *title;
        const char *items[5];
        int n;
    } menus[] = {
        {"File", {"New", "Open…", "Save", "Quit"}, 4},
        {"Edit", {"Undo", "Redo", "Cut", "Copy", "Paste"}, 5},
        {"View", {"Zoom In", "Zoom Out"}, 2},
    };
    for (size_t m = 0; m < sizeof(menus) / sizeof(menus[0]); m++) {
        struct yetty_yclass_object_ptr_result pm =
            yetty_ygui_widget_add(root, yetty_ygui_popup_menu_class_get().value);
        if (YETTY_IS_ERR(pm)) {
            yetty_ycore_error_destroy(pm.error);
            continue;
        }
        for (int i = 0; i < menus[m].n; i++) {
            err_ok(yetty_ygui_popup_menu_add_item(pm.value, menus[m].items[i], on_menu_item, NULL));
        }
        err_ok(yetty_ygui_menubar_add(mb, menus[m].title, pm.value));
    }
}

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:35_collapsing_header_open")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_35_collapsing_header_open {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_35_collapsing_header_open_ptr,
                      struct yetty_demoygui_35_collapsing_header_open *);
struct yetty_yclass_ptr_result yetty_demoygui_35_collapsing_header_open_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    /* Menubar across the top. */
    struct yetty_yclass_object *mb = add_w(root, yetty_ygui_menubar_class_get().value, ROW_H);

    /* Scrollarea fills the rest and stacks the sections. */
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build: scrollarea");
    set_grow(sr.value, 1.0f);
    struct yetty_yclass_object *area = sr.value;

    build_inputs(area);
    build_selectors(area);
    build_display(area);
    build_lists_trees(area);
    build_layout_containers(area);
    build_overlays(area, root);

    /* Menubar menus are added under root last so they paint over the
     * sections when opened. */
    if (mb) {
        build_menubar(mb, root);
    }

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv,
                                  yetty_demoygui_35_collapsing_header_open_class_get().value);
}

#include "main.gen.c"
