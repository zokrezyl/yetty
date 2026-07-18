/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yrich_app_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_element_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_shape_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_slides_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_cell_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void);
size_t yetty_yrich_constructor_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_content_width_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_content_height_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_undo_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_redo_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_down_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_up_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_drag_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_double_click_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_key_down_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_text_input_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_hit_test_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_is_editable_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_begin_edit_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_end_edit_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_is_editing_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_insert_text_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_element_delete_sel_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_slides_set_current_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_slides_next_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_slides_prev_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_grid_size_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_row_height_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_col_width_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_cell_value_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_toggle_format_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_text_color_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_alignment_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_line_spacing_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_adjust_indent_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_highlight_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_clear_format_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_heading_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_change_font_size_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_font_size_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yrich_register(void);

/* ---- yrich: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yrich_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yrich_app") == 0) {
        return yetty_yrich_app_class_get();
    }
    if (strcmp(name, "yetty_yrich_document") == 0) {
        return yetty_yrich_document_class_get();
    }
    if (strcmp(name, "yetty_yrich_element") == 0) {
        return yetty_yrich_element_class_get();
    }
    if (strcmp(name, "yetty_yrich_shape") == 0) {
        return yetty_yrich_shape_class_get();
    }
    if (strcmp(name, "yetty_yrich_slides") == 0) {
        return yetty_yrich_slides_class_get();
    }
    if (strcmp(name, "yetty_yrich_cell") == 0) {
        return yetty_yrich_cell_class_get();
    }
    if (strcmp(name, "yetty_yrich_spreadsheet") == 0) {
        return yetty_yrich_spreadsheet_class_get();
    }
    if (strcmp(name, "yetty_yrich_paragraph") == 0) {
        return yetty_yrich_paragraph_class_get();
    }
    if (strcmp(name, "yetty_yrich_inline_image") == 0) {
        return yetty_yrich_inline_image_class_get();
    }
    if (strcmp(name, "yetty_yrich_ydoc") == 0) {
        return yetty_yrich_ydoc_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yrich: slot -> skel, name-keyed static data --------------- */

struct yetty_yrich_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yrich_skel_row yetty_yrich_skel_rows[] = {
    {"yetty_yrich_constructor", yetty_yrich_constructor_skel},
    {"yetty_yrich_document_content_width", yetty_yrich_document_content_width_skel},
    {"yetty_yrich_document_content_height", yetty_yrich_document_content_height_skel},
    {"yetty_yrich_document_undo", yetty_yrich_document_undo_skel},
    {"yetty_yrich_document_redo", yetty_yrich_document_redo_skel},
    {"yetty_yrich_document_on_mouse_down", yetty_yrich_document_on_mouse_down_skel},
    {"yetty_yrich_document_on_mouse_up", yetty_yrich_document_on_mouse_up_skel},
    {"yetty_yrich_document_on_mouse_drag", yetty_yrich_document_on_mouse_drag_skel},
    {"yetty_yrich_document_on_mouse_double_click", yetty_yrich_document_on_mouse_double_click_skel},
    {"yetty_yrich_document_on_key_down", yetty_yrich_document_on_key_down_skel},
    {"yetty_yrich_document_on_text_input", yetty_yrich_document_on_text_input_skel},
    {"yetty_yrich_element_hit_test", yetty_yrich_element_hit_test_skel},
    {"yetty_yrich_element_is_editable", yetty_yrich_element_is_editable_skel},
    {"yetty_yrich_element_begin_edit", yetty_yrich_element_begin_edit_skel},
    {"yetty_yrich_element_end_edit", yetty_yrich_element_end_edit_skel},
    {"yetty_yrich_element_is_editing", yetty_yrich_element_is_editing_skel},
    {"yetty_yrich_element_insert_text", yetty_yrich_element_insert_text_skel},
    {"yetty_yrich_element_delete_sel", yetty_yrich_element_delete_sel_skel},
    {"yetty_yrich_slides_set_current", yetty_yrich_slides_set_current_skel},
    {"yetty_yrich_slides_next", yetty_yrich_slides_next_skel},
    {"yetty_yrich_slides_prev", yetty_yrich_slides_prev_skel},
    {"yetty_yrich_spreadsheet_set_grid_size", yetty_yrich_spreadsheet_set_grid_size_skel},
    {"yetty_yrich_spreadsheet_set_row_height", yetty_yrich_spreadsheet_set_row_height_skel},
    {"yetty_yrich_spreadsheet_set_col_width", yetty_yrich_spreadsheet_set_col_width_skel},
    {"yetty_yrich_spreadsheet_set_cell_value", yetty_yrich_spreadsheet_set_cell_value_skel},
    {"yetty_yrich_ydoc_toggle_format", yetty_yrich_ydoc_toggle_format_skel},
    {"yetty_yrich_ydoc_set_text_color", yetty_yrich_ydoc_set_text_color_skel},
    {"yetty_yrich_ydoc_set_alignment", yetty_yrich_ydoc_set_alignment_skel},
    {"yetty_yrich_ydoc_set_line_spacing", yetty_yrich_ydoc_set_line_spacing_skel},
    {"yetty_yrich_ydoc_adjust_indent", yetty_yrich_ydoc_adjust_indent_skel},
    {"yetty_yrich_ydoc_set_highlight", yetty_yrich_ydoc_set_highlight_skel},
    {"yetty_yrich_ydoc_clear_format", yetty_yrich_ydoc_clear_format_skel},
    {"yetty_yrich_ydoc_set_heading", yetty_yrich_ydoc_set_heading_skel},
    {"yetty_yrich_ydoc_change_font_size", yetty_yrich_ydoc_change_font_size_skel},
    {"yetty_yrich_ydoc_set_font_size", yetty_yrich_ydoc_set_font_size_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yrich_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_yrich_skel_rows) / sizeof(yetty_yrich_skel_rows[0]); ++i) {
        if (strcmp(yetty_yrich_skel_rows[i].name, name) == 0) {
            return yetty_yrich_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yrich: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yrich_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yrich_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yrich_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yrich_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yrich_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
