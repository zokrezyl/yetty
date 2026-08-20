/* GENERATED — do not edit. */
/* Public interface for regular class(es) `pane` (module: ymux).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUX_PANE_H
#define YETTY_YCLASSGEN_YMUX_PANE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymux_engine_host;

/* The pane — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_pane_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_pane;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PANE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PANE_PTR_RESULT
struct yetty_ymux_pane_ptr_result {
    int ok;
    union {
        struct yetty_ymux_pane *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_pane_ptr_result yetty_ymux_pane_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_pane_to(struct yetty_ymux_pane *data);

struct yetty_yclass_object_ptr_result yetty_ymux_pane_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymux_register(void);

struct yetty_yclass_object_ptr_result yetty_ymux_pane_make(
    uint32_t rows, uint32_t cols, uint32_t hot_rows, uint64_t total_row_cap,
    const struct yetty_ymux_engine_host *host);
struct yetty_ycore_void_result yetty_ymux_pane_dispose(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_pane_feed(struct yetty_yclass_object *obj,
                                                    const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_ymux_pane_resize(struct yetty_yclass_object *obj,
                                                      uint32_t rows, uint32_t cols);
/* The composed engine (input encoding, snapshot, cursor, dims — callers use
 * the engine API directly on this object). Borrowed. */
struct yetty_yclass_object_ptr_result yetty_ymux_pane_engine(struct yetty_yclass_object *obj);
/* The composed history store. Borrowed. */
struct yetty_yclass_object_ptr_result yetty_ymux_pane_history(struct yetty_yclass_object *obj);
/* The pane's canonical rich store (#695). Borrowed. */
struct yetty_yclass_object_ptr_result yetty_ymux_pane_rich_store(struct yetty_yclass_object *obj);
/* Timeline geometry: [floor, live_top) is history; [live_top,
 * live_top + rows) is the live screen (live_top == pushed rows). */
struct yetty_ycore_void_result yetty_ymux_pane_timeline(struct yetty_yclass_object *obj,
                                                        uint64_t *out_floor,
                                                        uint64_t *out_live_top);
/* Resolve ANY timeline row (history or live). Pointers are borrowed and
 * valid only until the next mutation — the projector copies. Alt-screen
 * panes serve the alt surface for live rows; history rows always come from
 * the primary timeline. */
struct yetty_ymux_history_row_result yetty_ymux_pane_resolve_row(struct yetty_yclass_object *obj,
                                                                 uint64_t timeline_idx);

#ifdef __cplusplus
}
#endif

#endif
