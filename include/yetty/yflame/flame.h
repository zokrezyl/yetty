/* GENERATED — do not edit. */
/* Public interface for regular class(es) `flame` (module: yflame).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YFLAME_FLAME_H
#define YETTY_YCLASSGEN_YFLAME_FLAME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yflame_flame;
YETTY_YRESULT_DECLARE(yetty_yflame_flame_ptr, struct yetty_yflame_flame *);
struct yetty_yflame_flame_ptr_result yetty_yflame_flame_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yflame_flame_to(struct yetty_yflame_flame *data);

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;

struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float frame_height, float min_width,
                                                      uint32_t flags);
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y);
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj, int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yflame_configure_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_parse_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_yflame_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yflame_hit_test_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_parent_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_reset_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_highlight_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_destroy_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yflame_register(void);

struct yetty_ydraw_drawable_list;

/* render() returns struct yetty_ydraw_drawable_list_result by value, so the
 * generated flame.h (and the dispatch TU that includes it) needs the complete
 * type — pull its defining header into the public header. */
#include <yetty/ydraw-core/drawable-list.h>
/* Public flags — copied verbatim into the generated flame.h. */
#define YETTY_YFLAME_FLAG_LABELS 0x1u /* draw truncated frame-name labels */
#define YETTY_YFLAME_FLAG_ICICLE 0x2u /* root at top, growing down (vs flame: bottom-up) */
/* hit_test returns these (in place of a node id) when a navigation button is
 * under the point: the caller should focus the parent / reset to root. */
#define YETTY_YFLAME_HIT_UP (-2)
#define YETTY_YFLAME_HIT_ROOT (-3)
struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd);

#endif
