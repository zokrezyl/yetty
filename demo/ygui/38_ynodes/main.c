/*
 * Demo 38_ynodes: ynodes — a node-graph editor.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 *
 * A ynodes editor fills the window below an instruction strip. Three
 * nodes sit on the grid, each holding ordinary ygui widgets in its body
 * (labels, a slider, a checkbox, buttons) to show that a node is a small
 * form, not just a labelled box. Two links are pre-wired.
 *
 * Interact:
 *   - drag a node's title/body      → move it (grid space)
 *   - drag from one pin to another  → create a link (output → input)
 *   - drag the empty canvas         → pan
 *   - mouse wheel                   → zoom toward the cursor
 *   - press 'q' (or Ctrl-C/Ctrl-D)  → quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

#define COLOR_TEXT 0xFFE4E5E0u  /* BRAND_TEXT_PRIMARY */
#define COLOR_MUTED 0xFFA8A79Fu /* BRAND_TEXT_SECONDARY */

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static inline void u32_ok(struct uint32_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Instantiate `cls` under `parent`, swallowing (and freeing) any error so
 * the build keeps going — a demo wants best-effort, not abort-on-first. */
static struct yetty_yclass_object *add_obj(struct yetty_yclass_object *parent,
                                           struct yetty_yclass_ptr_result cls)
{
    if (YETTY_IS_ERR(cls)) {
        yetty_ycore_error_destroy(cls.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls.value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

/* Author the body widget's height; width is stretched by the node's vbox. */
static void set_h(struct yetty_yclass_object *o, float h)
{
    if (!o) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(o);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.height = h;
    err_ok(yetty_ygui_widget_layout_set(o, &l));
}

static struct yetty_yclass_object *make_node(struct yetty_yclass_object *editor, float gx, float gy,
                                             float gw, float gh, const char *title)
{
    struct yetty_yclass_object_ptr_result nr = yetty_ygui_ynodes_add_node(editor, gx, gy);
    if (YETTY_IS_ERR(nr)) {
        yetty_ycore_error_destroy(nr.error);
        return NULL;
    }
    err_ok(yetty_ygui_ynode_set_graph_size(nr.value, gw, gh));
    err_ok(yetty_ygui_ynode_set_title(nr.value, title));
    return nr.value;
}

/* Register one insertable widget kind, swallowing a failed class lookup. */
static void reg_widget(struct yetty_yclass_object *editor, const char *name,
                       struct yetty_yclass_ptr_result cls)
{
    if (YETTY_IS_ERR(cls)) {
        yetty_ycore_error_destroy(cls.error);
        return;
    }
    err_ok(yetty_ygui_ynodes_register_widget(editor, name, cls.value));
}

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:38_ynodes")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_38_ynodes {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_38_ynodes_ptr, struct yetty_demoygui_38_ynodes *);
struct yetty_yclass_ptr_result yetty_demoygui_38_ynodes_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    /* Instruction strip. */
    struct yetty_yclass_object *hint = add_obj(root, yetty_ygui_label_class_get());
    if (hint) {
        err_ok(yetty_ygui_label_set_text(
            hint, "drag nodes  •  drag pin\xe2\x86\x92pin to connect  •  right-click for menu  •  "
                  "pan: drag canvas  •  wheel: zoom  •  q quits"));
        err_ok(yetty_ygui_label_set_color(hint, (struct yetty_ycore_rgba){168, 167, 159, 255}));
        set_h(hint, 24.0f);
    }

    /* The editor fills the rest of the window. */
    struct yetty_yclass_object *editor = add_obj(root, yetty_ygui_ynodes_class_get());
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "38_ynodes: ynodes create failed");
    }
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(editor);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "38_ynodes: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(editor, &l));
    }

    /* Palette of widgets the node context menu can insert ("Add <name>"). */
    reg_widget(editor, "Label", yetty_ygui_label_class_get());
    reg_widget(editor, "Button", yetty_ygui_button_class_get());
    reg_widget(editor, "Slider", yetty_ygui_slider_class_get());
    reg_widget(editor, "Checkbox", yetty_ygui_checkbox_class_get());
    reg_widget(editor, "Toggle", yetty_ygui_toggle_class_get());
    reg_widget(editor, "Progress", yetty_ygui_progress_class_get());

    /* Node A — a source with one output. */
    struct yetty_yclass_object *a = make_node(editor, 60.0f, 70.0f, 190.0f, 130.0f, "Source");
    if (a) {
        u32_ok(yetty_ygui_ynode_add_output(a, "value"));
        struct yetty_yclass_object *lbl = add_obj(a, yetty_ygui_label_class_get());
        if (lbl) {
            err_ok(yetty_ygui_label_set_text(lbl, "amplitude"));
            err_ok(yetty_ygui_label_set_color(lbl, (struct yetty_ycore_rgba){224, 229, 228, 255}));
            set_h(lbl, 18.0f);
        }
        struct yetty_yclass_object *sld = add_obj(a, yetty_ygui_slider_class_get());
        if (sld) {
            err_ok(yetty_ygui_slider_set_range(sld, 0.0f, 1.0f));
            err_ok(yetty_ygui_slider_set_value(sld, 0.6f));
            set_h(sld, 26.0f);
        }
    }

    /* Node B — a processor: two inputs, one output, a couple of controls. */
    struct yetty_yclass_object *b = make_node(editor, 350.0f, 130.0f, 200.0f, 160.0f, "Mixer");
    if (b) {
        u32_ok(yetty_ygui_ynode_add_input(b, "a"));
        u32_ok(yetty_ygui_ynode_add_input(b, "b"));
        u32_ok(yetty_ygui_ynode_add_output(b, "out"));
        struct yetty_yclass_object *chk = add_obj(b, yetty_ygui_checkbox_class_get());
        if (chk) {
            err_ok(yetty_ygui_checkbox_set_label(chk, "enabled"));
            err_ok(yetty_ygui_checkbox_set_checked(chk, 1));
            set_h(chk, 24.0f);
        }
        struct yetty_yclass_object *btn = add_obj(b, yetty_ygui_button_class_get());
        if (btn) {
            err_ok(yetty_ygui_button_set_label(btn, "Apply"));
            set_h(btn, 32.0f);
        }
    }

    /* Node C — a sink with one input. */
    struct yetty_yclass_object *c = make_node(editor, 680.0f, 90.0f, 180.0f, 120.0f, "Output");
    if (c) {
        u32_ok(yetty_ygui_ynode_add_input(c, "result"));
        struct yetty_yclass_object *lbl = add_obj(c, yetty_ygui_label_class_get());
        if (lbl) {
            err_ok(yetty_ygui_label_set_text(lbl, "preview"));
            err_ok(yetty_ygui_label_set_color(lbl, (struct yetty_ycore_rgba){168, 167, 159, 255}));
            set_h(lbl, 18.0f);
        }
        struct yetty_yclass_object *btn = add_obj(c, yetty_ygui_button_class_get());
        if (btn) {
            err_ok(yetty_ygui_button_set_label(btn, "Save"));
            set_h(btn, 32.0f);
        }
    }

    /* Pre-wire: Source.value → Mixer.a, Mixer.out → Output.result. */
    if (a && b) {
        err_ok(yetty_ygui_ynodes_link(editor, a, 0, b, 0));
    }
    if (b && c) {
        err_ok(yetty_ygui_ynodes_link(editor, b, 0, c, 0));
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_38_ynodes_class_get().value);
}

#include "main.gen.c"
