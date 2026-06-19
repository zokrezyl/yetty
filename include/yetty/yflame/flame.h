/* GENERATED — do not edit. */
/* Public interface for regular class(es) `flame` (module: yflame).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YFLAME_FLAME_H
#define YETTY_YCLASSGEN_YFLAME_FLAME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* Public flags / hit-test sentinels. Defined here in the owning .c; codegen
 * reproduces the enum into the generated flame.h for consumers. */
enum yetty_yflame_constant {
    YETTY_YFLAME_FLAG_LABELS = 1,
    YETTY_YFLAME_FLAG_ICICLE = 2,
    YETTY_YFLAME_HIT_UP = -2,
    YETTY_YFLAME_HIT_ROOT = -3,
};

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yflame_flame;
struct yetty_yflame_flame_ptr_result {
    int ok;
    union {
        struct yetty_yflame_flame *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yflame_flame_ptr_result yetty_yflame_flame_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yflame_flame_to(struct yetty_yflame_flame *data);

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
/* destroy: free the tree, the node index, and the object. */
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object * obj);

typedef struct yetty_ycore_void_result (*yetty_yflame_configure_fn)(struct yetty_yclass_object *, float, float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_parse_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_yflame_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yflame_hit_test_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_parent_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_reset_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_highlight_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yflame_register(void);

struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list, int fd);

#ifdef __cplusplus
}
#endif

#endif
