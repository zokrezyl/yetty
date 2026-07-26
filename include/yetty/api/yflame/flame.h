/* GENERATED — do not edit. */
/* Object API for regular class(es) `flame` (implementation module: yflame).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YFLAME_FLAME_H
#define YETTY_YCLASSGEN_API_YFLAME_FLAME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFLAME_CONSTANT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFLAME_CONSTANT
/* Public flags / hit-test sentinels. Defined here in the owning .c; codegen
 * reproduces the enum into the generated flame.h for consumers. */
enum yetty_yflame_constant {
    YETTY_YFLAME_FLAG_LABELS = 1,
    YETTY_YFLAME_FLAG_ICICLE = 2,
    YETTY_YFLAME_HIT_UP = -2,
    YETTY_YFLAME_HIT_ROOT = -3,
};
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yflame_flame;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFLAME_FLAME_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFLAME_FLAME_PTR_RESULT
struct yetty_yflame_flame_ptr_result {
    int ok;
    union {
        struct yetty_yflame_flame *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yflame_flame_ptr_result yetty_yflame_flame_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yflame_flame_to(struct yetty_yflame_flame *data);

/* configure: set graph width, row height, min visible box width, and flags.
 * 0 selects the default for each. Call after create(), before parse(). */
struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_object * obj, float width, float frame_height, float min_width, uint32_t flags);
/* parse: ingest folded-stack text, build the call tree, index it. Resets focus
 * to the root and clears any hover highlight. */
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_object * obj, const char * input, size_t len);
/* render: lay out the focus subtree and emit it as a fresh ydraw drawable list
 * (caller owns it). Pointer return -> local-only. */
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_object * obj);
/* hit_test: id of the frame at content-coordinate (x,y) in the current focus
 * layout, or -1 if none. */
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_object * obj, float x, float y);
/* focus: zoom so the given node fills the full width (its subtree is shown). */
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_object * obj, int32_t node_id);
/* focus_parent: zoom out one level (focus the current node's parent). */
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_object * obj);
/* reset: zoom back out to the whole graph (root). */
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_object * obj);
/* set_highlight: mark a node as hovered (-1 clears) for the next render. */
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_object * obj, int32_t node_id);
/* highlight_name: paint every frame whose name contains `name` (case-insensitive)
 * magenta — drives both free-text search and the symbol-table cross-highlight.
 * NULL / zero-length clears the highlight. */
struct yetty_ycore_void_result yetty_yflame_highlight_name(struct yetty_yclass_object * obj, const char * name, size_t len);
/* focus_name: zoom so the heaviest frame whose name exactly equals `name` fills
 * the width. Used to jump the flame to a symbol picked in the table. */
struct yetty_ycore_void_result yetty_yflame_focus_name(struct yetty_yclass_object * obj, const char * name, size_t len);
/* set_baseline: load a second folded profile as a diff baseline; frames then
 * colour by the delta of their sample fraction (red = grew, blue = shrank).
 * NULL / zero-length clears diff mode. Call after parse(). */
struct yetty_ycore_void_result yetty_yflame_set_baseline(struct yetty_yclass_object * obj, const char * folded, size_t len);
/* node_name: borrowed name of the node with this id (valid until the next
 * parse), or "" for an invalid id — used to build hover detail. */
struct yetty_ycore_const_char_ptr_result yetty_yflame_node_name(struct yetty_yclass_object * obj, int32_t id);
/* node_value: total samples passing through the node with this id (0 if bad). */
struct yetty_ycore_uint64_result yetty_yflame_node_value(struct yetty_yclass_object * obj, int32_t id);
/* root_value: total samples in the whole profile — the percent denominator. */
struct yetty_ycore_uint64_result yetty_yflame_root_value(struct yetty_yclass_object * obj);
/* destroy: free the tree, the node index, and the object. */
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object * obj);

struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list, int fd);

#ifdef __cplusplus
}
#endif

#endif
