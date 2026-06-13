/* GENERATED — do not edit. */
/* Public interface for regular class(es) `circuit` (module: ycircuit).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YCIRCUIT_CIRCUIT_H
#define YETTY_YCLASSGEN_YCIRCUIT_CIRCUIT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

struct yetty_ycircuit_circuit;

struct yetty_ycircuit_circuit_ptr_result {
    int ok;
    union {
        struct yetty_ycircuit_circuit *value;
        struct yetty_ycore_error error;
    };
};

enum yetty_ycircuit_constant {
    YETTY_YCIRCUIT_NO_ELEMENT = -1,
    YETTY_YCIRCUIT_FLAG_NONE = 0,
};

struct yetty_yclass_ptr_result yetty_ycircuit_circuit_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ycircuit_circuit_ptr_result yetty_ycircuit_circuit_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ycircuit_circuit_to(struct yetty_ycircuit_circuit *data);

struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        float grid_px, uint32_t flags);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *input, size_t len);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const char *kind, float x, float y,
                                                           int32_t rotation_deg, const char *name,
                                                           const char *value);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y, int32_t rotation_deg, const char *name,
                                                    const char *value, const char *pins_left,
                                                    const char *pins_right, const char *pins_top,
                                                    const char *pins_bottom);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float x0,
                                                      float y0, float x1, float y1);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj, float x,
                                                       float y, const char *text);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float x,
                                                      float y);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int32_t element_id);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ycircuit_configure_fn)(struct yetty_yclass_ctx *,
                                                                      struct yetty_yclass_object *,
                                                                      float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_parse_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_clear_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_component_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, float, float, int32_t,
    const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_ic_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int32_t, const char *,
    const char *, const char *, const char *, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_wire_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_junction_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_label_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     float, float, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycircuit_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_hit_test_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_set_highlight_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_destroy_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ycircuit_register(void);

struct yetty_ycore_void_result yetty_ycircuit_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                       int fd);

#endif
