/* GENERATED — do not edit. */
/* Object API for regular class(es) `history` (implementation module: ymux).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMUX_HISTORY_H
#define YETTY_YCLASSGEN_API_YMUX_HISTORY_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymux_cell;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_ROW
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_ROW
/* One retained row as the store keeps it (hot) or serves it (inflated). */
struct yetty_ymux_history_row {
    const struct yetty_ymux_cell *cells;
    uint32_t cols;
    uint64_t logical_line_id;
    uint32_t logical_cell_start;
    int continuation;
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_ROW_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_ROW_RESULT
struct yetty_ymux_history_row_result {
    int ok;
    union {
        struct yetty_ymux_history_row value;
        struct yetty_ycore_error error;
    };
};
#endif

/* The tiered store — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_history_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_history;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_HISTORY_PTR_RESULT
struct yetty_ymux_history_ptr_result {
    int ok;
    union {
        struct yetty_ymux_history *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_history_ptr_result yetty_ymux_history_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_history_to(struct yetty_ymux_history *data);

struct yetty_yclass_object_ptr_result yetty_ymux_history_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ymux_history_make(uint32_t hot_rows,
                                                              uint64_t total_row_cap);
struct yetty_ycore_void_result yetty_ymux_history_dispose(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_history_set_budgets(struct yetty_yclass_object *obj,
                                                              uint64_t warm_bytes,
                                                              uint64_t file_max_bytes);
/* Push one row scrolling off a pane's screen top. Signature matches the
 * engine's scroll_out host callback so a pane can wire it directly. */
struct yetty_ycore_void_result yetty_ymux_history_push(struct yetty_yclass_object *obj,
                                                       const struct yetty_ymux_cell *cells,
                                                       uint32_t cols, uint64_t logical_line_id,
                                                       uint32_t logical_cell_start,
                                                       int continuation);
struct yetty_ycore_uint64_result yetty_ymux_history_pushed_rows(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_ymux_history_floor(struct yetty_yclass_object *obj);
/* Resolve one retained row by timeline index. The returned pointers stay
 * valid until the next resolve/push that recycles their backing (hot ring
 * slot or cache entry) — callers copy what they keep. A dropped or
 * out-of-range index yields an error. */
struct yetty_ymux_history_row_result yetty_ymux_history_resolve(struct yetty_yclass_object *obj,
                                                                uint64_t timeline_idx);

#ifdef __cplusplus
}
#endif

#endif
