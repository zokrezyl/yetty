/* GENERATED — do not edit. */
/* Object API for regular class(es) `textinput` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_TEXTINPUT_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_TEXTINPUT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_textinput;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TEXTINPUT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TEXTINPUT_PTR_RESULT
struct yetty_ygui_textinput_ptr_result {
    int ok;
    union {
        struct yetty_ygui_textinput *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_textinput_ptr_result yetty_ygui_textinput_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_textinput_to(struct yetty_ygui_textinput *data);

struct yetty_yclass_object_ptr_result yetty_ygui_textinput_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_yclass_object *obj,
                                                             const char *text);
/* Selected substring as a fresh NUL-terminated heap string (caller frees), or
 * NULL when nothing is selected. */
struct yetty_ycore_char_ptr_result yetty_ygui_textinput_get_selection(
    const struct yetty_yclass_object *obj);
/* Select the whole field (Ctrl-A / context-menu "Select All"). */
struct yetty_ycore_void_result yetty_ygui_textinput_select_all(struct yetty_yclass_object *obj);
/* Replace the current selection (if any) with `text`, leaving the caret after
 * the inserted run and no selection. Powers paste (insert clipboard text) and
 * cut/delete-selection (pass "" to just drop the selection). Only printable
 * ASCII 0x20..0x7e is taken — the field is single-line ASCII, so newlines /
 * control bytes from a multi-line clipboard paste are dropped, not inserted. */
struct yetty_ycore_void_result yetty_ygui_textinput_insert_text(struct yetty_yclass_object *obj,
                                                                const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_textinput_get_text(
    const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_yclass_object *obj,
                                                                    const char *placeholder);
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_yclass_object *obj,
                                                              int focused);
/* `mods` is the YETTY_YGUI_MOD_* bitset for this key (Shift/Ctrl/Alt).
 * Clipboard chords (copy/cut/paste) are the app's concern — they need a
 * clipboard the widget has no handle to — so they arrive already consumed and
 * never reach here; Ctrl-A (select all) needs no external state, so it is
 * handled locally. */
struct yetty_ycore_int_result yetty_ygui_textinput_handle_key(struct yetty_yclass_object *obj,
                                                              uint32_t key, int mods);

#ifdef __cplusplus
}
#endif

#endif
