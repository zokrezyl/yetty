/* GENERATED — do not edit. */
/* Public interface for regular class(es) `map` (module: ymap).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YMAP_MAP_H
#define YETTY_YCLASSGEN_YMAP_MAP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymap_map;
YETTY_YRESULT_DECLARE(yetty_ymap_map_ptr, struct yetty_ymap_map *);
struct yetty_ymap_map_ptr_result yetty_ymap_map_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ymap_map_to(struct yetty_ymap_map *data);

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;

struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    double latitude, double longitude,
                                                    uint32_t zoom, uint32_t width_px,
                                                    uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       const char *name);
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, const char *url_template,
    int is_vector, const char *file_extension, uint32_t max_zoom, const char *attribution);
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     double latitude, double longitude);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj, uint32_t zoom);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        double delta_x, double delta_y);
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, int32_t step,
                                                    double anchor_x, double anchor_y);
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ymap_configure_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  double, double, uint32_t,
                                                                  uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_provider_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_custom_provider_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, int, const char *,
    uint32_t, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_center_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *,
                                                                   double, double);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_zoom_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *,
                                                                 uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_viewport_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_pan_by_pixels_fn)(struct yetty_yclass_ctx *,
                                                                      struct yetty_yclass_object *,
                                                                      double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_zoom_by_at_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  int32_t, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_get_zoom_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_geolocate_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ymap_attribution_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymap_is_vector_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymap_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_destroy_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymap_register(void);

struct yetty_ydraw_drawable_list;

/* render() returns struct yetty_ydraw_drawable_list_result by value, so the
 * generated map.h needs the complete type. */
#include <yetty/ydraw-core/drawable-list.h>
uint32_t yetty_ymap_provider_count(void);
struct yetty_ycore_void_result yetty_ymap_provider_info(uint32_t index, const char **out_name,
                                                        const char **out_attribution,
                                                        uint32_t *out_max_zoom, int *out_is_vector);
struct yetty_ycore_void_result yetty_ymap_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                   int fd);

#endif
