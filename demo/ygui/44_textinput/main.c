/*
 * Demo 44_textinput: the single-line edit box (ygui textinput) in isolation.
 *
 * A focused textinput plus three live status labels so the edit-box behaviour
 * is directly observable without any surrounding app:
 *   - "value:"     — the current text,
 *   - "selection:" — the currently selected substring (or "(none)"),
 *   - "clipboard:" — this demo's own copy buffer.
 *
 * Keys (routed to the edit box by this demo's own key callback, which replaces
 * the host's quit-only handler):
 *   - printable keys / Backspace / Delete / Home / End — edit + move caret,
 *   - Shift+Arrows / Shift+Home / Shift+End           — extend the selection,
 *   - Ctrl+Left / Ctrl+Right                          — move by word,
 *   - Ctrl+A                                          — select all,
 *   - Ctrl+C / Ctrl+X / Ctrl+V                        — copy / cut / paste
 *                                                       (this demo's buffer),
 *   - Esc                                             — quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ygui/framework-defs.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Demo app class: holds the edit box, the live labels, and a copy buffer. */
struct [[clang::annotate("class@demoygui:44_textinput")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_44_textinput {
    struct yetty_yclass_object *input;       /* the edit box under test */
    struct yetty_yclass_object *value_label; /* live "value: ..."     */
    struct yetty_yclass_object *sel_label;   /* live "selection: ..." */
    struct yetty_yclass_object *clip_label;  /* live "clipboard: ..." */
    char clipboard[512];                     /* demo-local copy buffer */
};

/* Result wrapper + class accessor/downcast forward-decls (this TU does not
 * include its own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_44_textinput_ptr, struct yetty_demoygui_44_textinput *);
struct yetty_yclass_ptr_result yetty_demoygui_44_textinput_class_get(void);
struct yetty_demoygui_44_textinput_ptr_result yetty_demoygui_44_textinput_from(
    struct yetty_yclass_object *obj);

/* Repaint the three status labels from the edit box's current state. */
static void refresh_labels(struct yetty_demoygui_44_textinput *d)
{
    char line[600];

    struct yetty_ycore_const_char_ptr_result text_res = yetty_ygui_textinput_get_text(d->input);
    const char *text = "";
    if (YETTY_IS_OK(text_res)) {
        text = text_res.value;
    } else {
        yetty_ycore_error_destroy(text_res.error);
    }
    snprintf(line, sizeof(line), "value: %s", text);
    err_ok(yetty_ygui_label_set_text(d->value_label, line));

    struct yetty_ycore_char_ptr_result sel_res = yetty_ygui_textinput_get_selection(d->input);
    char *selection = NULL;
    if (YETTY_IS_OK(sel_res)) {
        selection = sel_res.value;
    } else {
        yetty_ycore_error_destroy(sel_res.error);
    }
    snprintf(line, sizeof(line), "selection: %s", selection ? selection : "(none)");
    err_ok(yetty_ygui_label_set_text(d->sel_label, line));
    free(selection);

    snprintf(line, sizeof(line), "clipboard: %s", d->clipboard[0] ? d->clipboard : "(empty)");
    err_ok(yetty_ygui_label_set_text(d->clip_label, line));
}

/* Copy the current selection into the demo clipboard; on cut, drop it too. */
static void copy_selection(struct yetty_demoygui_44_textinput *d, int cut)
{
    struct yetty_ycore_char_ptr_result sel_res = yetty_ygui_textinput_get_selection(d->input);
    if (YETTY_IS_ERR(sel_res)) {
        yetty_ycore_error_destroy(sel_res.error);
        return;
    }
    if (!sel_res.value) {
        return; /* nothing selected */
    }
    snprintf(d->clipboard, sizeof(d->clipboard), "%s", sel_res.value);
    free(sel_res.value);
    if (cut) {
        err_ok(yetty_ygui_textinput_insert_text(d->input, "")); /* delete the selection */
    }
}

/* Our key router: replaces the host's quit-only handler (the runner installs it
 * just before build(), so setting ours in build() wins). Everything editing- or
 * selection-related goes to the edit box; Ctrl+C/X/V drive the demo clipboard;
 * Esc quits. */
static int demo_key_cb(struct yetty_yclass_object *engine, uint32_t key, int mods, void *userdata)
{
    struct yetty_yclass_object *app = userdata;
    struct yetty_demoygui_44_textinput_ptr_result d_res = yetty_demoygui_44_textinput_from(app);
    if (YETTY_IS_ERR(d_res)) {
        yetty_ycore_error_destroy(d_res.error);
        return 0;
    }
    struct yetty_demoygui_44_textinput *d = d_res.value;

    if (key == 0x1B) { /* Esc — quit */
        err_ok(yetty_yguiapp_app_quit(app));
        return 1;
    }
    if (key == 0x03 || key == 0x18) { /* Ctrl-C copy / Ctrl-X cut */
        copy_selection(d, key == 0x18);
        refresh_labels(d);
        yetty_ygui_framework_mark_dirty(engine);
        return 1;
    }
    if (key == 0x16) { /* Ctrl-V paste */
        if (d->clipboard[0]) {
            err_ok(yetty_ygui_textinput_insert_text(d->input, d->clipboard));
        }
        refresh_labels(d);
        yetty_ygui_framework_mark_dirty(engine);
        return 1;
    }

    struct yetty_ycore_int_result handled = yetty_ygui_textinput_handle_key(d->input, key, mods);
    if (YETTY_IS_ERR(handled)) {
        yetty_ycore_error_destroy(handled.error);
        return 0;
    }
    if (handled.value) {
        refresh_labels(d);
        yetty_ygui_framework_mark_dirty(engine);
    }
    return handled.value;
}

/* Give a widget a fixed minimum height so the stacked column reads clearly. */
static void set_min_height(struct yetty_yclass_object *widget, float min_height)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(widget);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout layout = *layout_res.value;
    layout.min_height = min_height;
    err_ok(yetty_ygui_widget_layout_set(widget, &layout));
}

/* Add a label to `root` and return it (NULL on failure). */
static struct yetty_yclass_object *add_label(struct yetty_yclass_object *root, const char *text)
{
    struct yetty_yclass_object_ptr_result label_res =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    if (YETTY_IS_ERR(label_res)) {
        yetty_ycore_error_destroy(label_res.error);
        return NULL;
    }
    err_ok(yetty_ygui_label_set_text(label_res.value, text));
    return label_res.value;
}

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    struct yetty_demoygui_44_textinput_ptr_result d_res = yetty_demoygui_44_textinput_from(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_res, "44_textinput: from");
    struct yetty_demoygui_44_textinput *d = d_res.value;
    d->clipboard[0] = '\0';

    if (!add_label(root, "ygui edit-box demo — type; Shift+Arrows select; Ctrl+A all; "
                         "Ctrl+C/X/V clipboard; Esc quit")) {
        return YETTY_ERR(yetty_ycore_void, "44_textinput: title label");
    }

    /* The edit box under test. */
    struct yetty_yclass_object_ptr_result input_res =
        yetty_ygui_widget_add(root, yetty_ygui_textinput_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "44_textinput: textinput");
    d->input = input_res.value;
    err_ok(yetty_ygui_textinput_set_placeholder(d->input, "type here…"));
    err_ok(yetty_ygui_textinput_set_text(d->input, "The quick brown fox"));
    err_ok(yetty_ygui_textinput_set_focus(d->input, 1));
    set_min_height(d->input, 34.0f);

    d->value_label = add_label(root, "value:");
    d->sel_label = add_label(root, "selection:");
    d->clip_label = add_label(root, "clipboard:");
    if (!d->value_label || !d->sel_label || !d->clip_label) {
        return YETTY_ERR(yetty_ycore_void, "44_textinput: status labels");
    }
    refresh_labels(d);

    /* Install our key router in place of the host's quit-only handler. */
    struct yetty_yclass_object_ptr_result fw_res = yetty_ygui_widget_framework(root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fw_res, "44_textinput: framework");
    yetty_ygui_framework_set_key_cb(fw_res.value, demo_key_cb, app);
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_44_textinput_class_get().value);
}

#include "yetty/gen/impl/demoygui/44_textinput/main.c"
