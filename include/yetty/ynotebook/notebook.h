/* GENERATED — do not edit. */
/* Public interface for regular class(es) `output, cell, notebook` (module: ynotebook).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YNOTEBOOK_NOTEBOOK_H
#define YETTY_YCLASSGEN_YNOTEBOOK_NOTEBOOK_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ynotebook_output_class_get(void);
struct yetty_yclass_ptr_result yetty_ynotebook_cell_class_get(void);
struct yetty_yclass_ptr_result yetty_ynotebook_notebook_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ynotebook_output;
struct yetty_ynotebook_output_ptr_result {
    int ok;
    union {
        struct yetty_ynotebook_output *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ynotebook_output_ptr_result yetty_ynotebook_output_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_to(struct yetty_ynotebook_output *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ynotebook_cell;
struct yetty_ynotebook_cell_ptr_result {
    int ok;
    union {
        struct yetty_ynotebook_cell *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ynotebook_cell_ptr_result yetty_ynotebook_cell_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_to(struct yetty_ynotebook_cell *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ynotebook_notebook;
struct yetty_ynotebook_notebook_ptr_result {
    int ok;
    union {
        struct yetty_ynotebook_notebook *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ynotebook_notebook_ptr_result yetty_ynotebook_notebook_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_to(struct yetty_ynotebook_notebook *data);

/* output_type: "stream", "display_data", "execute_result", "error", or the raw
 * type string of an output kind Yetty does not model. */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(struct yetty_yclass_object * obj);
/* stream_name: "stdout" / "stderr" for a stream output; "" otherwise. */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(struct yetty_yclass_object * obj);
/* text: the stream text of a stream output (owned; caller frees). Empty string
 * for non-stream outputs. */
struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object * obj);
/* execution_count: the execute_result prompt number, or -1 when absent/null. */
struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(struct yetty_yclass_object * obj);
/* error_name / error_value: the ename / evalue of an error output; "" else. */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(struct yetty_yclass_object * obj);
/* bundle: the mime_bundle object for a display_data / execute_result output.
 * Errors when the output carries no rich data. The bundle is owned by the
 * output — do not destroy it directly. */
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(struct yetty_yclass_object * obj);
/* destroy: free the mime_bundle and the yclass allocation. */
struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object * obj);
/* cell_type: "code", "markdown", "raw", or a raw type Yetty does not model. */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object * obj);
/* cell_id: the cell id (nbformat 4.5+); "" when absent. */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object * obj);
/* cell_source: the cell source, joined to one string (owned; caller frees). */
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object * obj);
/* cell_set_source: replace the cell source with `text` (stored as one string). */
struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object * obj, const char * text);
/* cell_execution_count: code-cell prompt number, or -1 when absent/null. */
struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object * obj);
/* cell_output_count: number of outputs (code cells). */
struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object * obj);
/* cell_output_at: the output object at `index` (owned by the cell). */
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(struct yetty_yclass_object * obj, size_t index);
/* cell_metadata_json: the cell metadata object as JSON text (owned). */
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(struct yetty_yclass_object * obj);
/* apply_message: fold a decoded IOPub output message (msg_type + its content as
 * JSON text) into this cell's outputs, mutating the retained tree. Implements
 * the nbformat output rules: consecutive same-name stream records coalesce;
 * clear_output(wait=true) defers the clear until the next output arrives;
 * display_data / execute_result / error append. Non-output message types
 * (status, execute_input, …) are ignored. */
struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object * obj, const char * msg_type, const char * content_json);
/* destroy: free the output objects and the yclass allocation. */
struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object * obj);
/* load_text: parse an .ipynb document from JSON text, replacing any current
 * content. */
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object * obj, const char * json);
/* load_file: read and parse an .ipynb file, replacing any current content. */
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object * obj, const char * path);
/* to_text: serialize the notebook to nbformat JSON text (owned; caller frees).
 * The retained document tree is re-emitted, so unknown fields survive. */
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(struct yetty_yclass_object * obj);
/* save_file: serialize and write the notebook atomically (temp sibling then
 * rename over the target). */
struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object * obj, const char * path);
/* nbformat / nbformat_minor: the document format version. */
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(struct yetty_yclass_object * obj);
/* cell_count: number of cells. */
struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object * obj);
/* cell_at: the cell object at `index` (owned by the notebook). */
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(struct yetty_yclass_object * obj, size_t index);
/* metadata_json: the notebook-level metadata object as JSON text (owned). */
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(struct yetty_yclass_object * obj);
/* destroy: free every cell/output, the master document, and the allocation. */
struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object * obj);

typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_type_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_stream_name_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_output_text_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_output_execution_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_name_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_value_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_output_bundle_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_output_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_type_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_id_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_source_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_set_source_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_cell_execution_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_cell_output_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_cell_output_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_metadata_json_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_apply_message_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_text_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_file_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_to_text_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_save_file_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_minor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_notebook_cell_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_notebook_cell_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_metadata_json_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ynotebook_register(void);

#ifdef __cplusplus
}
#endif

#endif
