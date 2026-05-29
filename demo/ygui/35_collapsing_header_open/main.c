/*
 * Demo 35_collapsing_header_open: multiple collapsing headers stacked
 * inside a scrollarea — mirrors the shape of ygreeter's Elements tab so
 * header-rendering regressions show up here in isolation.
 *
 * Layout quirk this demo exercises: the flex pass in layout.c treats
 * `height = -1` (unset) as `main_pref = 0` — i.e. nothing measures its
 * intrinsic content size. Children with no authored height collapse to
 * zero rect and end up stacked at the same Y position, with only their
 * paint-helpers strokes (title strip, checkbox glyph, etc.) visible.
 * Real apps therefore have to author sizes on every leaf widget. This
 * demo does that so we know what "working" looks like when iterating
 * on the Elements tab.
 *
 * Standalone-mode ygui demo. Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include "../runner.h"
#include <yetty/ygui/ygui.h>

/* Width is irrelevant — vbox stretches cross-axis. We only set heights. */
#define ROW_H 28.0f
#define HEADER_H 28.0f
#define SECTION_PAD 8.0f
#define SECTION_GAP 4.0f

/* Compute the height a collapsing_header needs to fit `n` rows of
 * `ROW_H` plus header strip + paddings + inter-row gaps. The header
 * widget's own constructor already reserves `HEADER_H + 4` of
 * padding_top and 4 of padding_bottom, so we only add the row stack on
 * top of those. */
static float section_height(int n_rows)
{
    if (n_rows < 0) n_rows = 0;
    float content = (float)n_rows * ROW_H;
    if (n_rows > 1) content += (float)(n_rows - 1) * SECTION_GAP;
    return (HEADER_H + 4.0f) + content + 4.0f;
}

static struct yetty_ycore_void_result set_height(struct yetty_ygui_object *w, float h)
{
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
    l.height = h;
    return yetty_ygui_widget_layout_set(w, &l);
}

static struct yetty_ygui_object_ptr_result add_section(struct yetty_ygui_object *parent,
                                                       const char *title, int n_rows)
{
    struct yetty_ygui_object_ptr_result hr =
        yetty_ygui_add(yetty_ygui_collapsing_header_class_get().value, parent);
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, hr, "add_section: header");
    struct yetty_ycore_void_result tr =
        yetty_ygui_collapsing_header_set_title(hr.value, title);
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, tr, "add_section: set_title");
    struct yetty_ycore_void_result or = yetty_ygui_collapsing_header_set_open(hr.value, 1);
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, or, "add_section: set_open");
    struct yetty_ycore_void_result hh = set_height(hr.value, section_height(n_rows));
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, hh, "add_section: set_height");
    return hr;
}

static struct yetty_ycore_void_result add_label(struct yetty_ygui_object *parent, const char *text)
{
    struct yetty_ygui_object_ptr_result lr =
        yetty_ygui_add(yetty_ygui_label_class_get().value, parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "add_label: add");
    struct yetty_ycore_void_result tr = yetty_ygui_label_set_text(lr.value, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "add_label: set_text");
    struct yetty_ycore_void_result hh = set_height(lr.value, ROW_H);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hh, "add_label: set_height");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result add_button(struct yetty_ygui_object *parent, const char *text)
{
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_button_class_get().value, parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "add_button: add");
    struct yetty_ycore_void_result tr = yetty_ygui_button_set_label(br.value, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "add_button: set_label");
    struct yetty_ycore_void_result hh = set_height(br.value, ROW_H);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hh, "add_button: set_height");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;

    /* Scrollarea wraps the section stack so a tall combined content
     * stays reachable. Matches the Elements tab shape. */
    struct yetty_ygui_object_ptr_result sr =
        yetty_ygui_add(yetty_ygui_scrollarea_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build: scrollarea");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(sr.value);
        l.flex_grow = 1.0f;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(sr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "build: scrollarea layout");
    }
    struct yetty_ygui_object *area = sr.value;

    /* ---- Inputs (4 rows) ---- */
    {
        struct yetty_ygui_object_ptr_result hr = add_section(area, "Inputs", 4);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "build: Inputs section");
        struct yetty_ygui_object *sec = hr.value;

        struct yetty_ycore_void_result r;
        r = add_button(sec, "Button");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "Inputs: button");

        struct yetty_ygui_object_ptr_result tr =
            yetty_ygui_add(yetty_ygui_textinput_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "Inputs: textinput");
        struct yetty_ycore_void_result pr =
            yetty_ygui_textinput_set_placeholder(tr.value, "type here…");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "Inputs: textinput placeholder");
        struct yetty_ycore_void_result trh = set_height(tr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, trh, "Inputs: textinput height");

        struct yetty_ygui_object_ptr_result cr =
            yetty_ygui_add(yetty_ygui_checkbox_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "Inputs: checkbox");
        struct yetty_ycore_void_result clr =
            yetty_ygui_checkbox_set_label(cr.value, "Enabled");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clr, "Inputs: checkbox label");
        struct yetty_ycore_void_result ccr = yetty_ygui_checkbox_set_checked(cr.value, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ccr, "Inputs: checkbox checked");
        struct yetty_ycore_void_result crh = set_height(cr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, crh, "Inputs: checkbox height");

        struct yetty_ygui_object_ptr_result slr =
            yetty_ygui_add(yetty_ygui_slider_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slr, "Inputs: slider");
        struct yetty_ycore_void_result sla =
            yetty_ygui_slider_set_range(slr.value, 0.0f, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sla, "Inputs: slider range");
        struct yetty_ycore_void_result slv = yetty_ygui_slider_set_value(slr.value, 0.4f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slv, "Inputs: slider value");
        struct yetty_ycore_void_result slh = set_height(slr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slh, "Inputs: slider height");
    }

    /* ---- Display (2 rows) ---- */
    {
        struct yetty_ygui_object_ptr_result hr = add_section(area, "Display", 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "build: Display section");
        struct yetty_ygui_object *sec = hr.value;

        struct yetty_ycore_void_result r;
        r = add_label(sec, "Plain label");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "Display: label");

        struct yetty_ygui_object_ptr_result pr =
            yetty_ygui_add(yetty_ygui_progress_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "Display: progress");
        struct yetty_ycore_void_result pv = yetty_ygui_progress_set_value(pr.value, 0.65f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "Display: progress value");
        struct yetty_ycore_void_result ph = set_height(pr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ph, "Display: progress height");
    }

    /* ---- Chrome (2 rows) ---- */
    {
        struct yetty_ygui_object_ptr_result hr = add_section(area, "Chrome", 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "build: Chrome section");
        struct yetty_ygui_object *sec = hr.value;

        struct yetty_ygui_object_ptr_result spr =
            yetty_ygui_add(yetty_ygui_separator_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, spr, "Chrome: separator");
        struct yetty_ycore_void_result sph = set_height(spr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sph, "Chrome: separator height");

        struct yetty_ygui_object_ptr_result sbr =
            yetty_ygui_add(yetty_ygui_statusbar_class_get().value, sec);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sbr, "Chrome: statusbar");
        struct yetty_ycore_void_result sbh = set_height(sbr.value, ROW_H);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sbh, "Chrome: statusbar height");
    }

    (void)SECTION_PAD;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "35_collapsing_header_open", build);
}
