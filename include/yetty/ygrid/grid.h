/* GENERATED — do not edit. */
/* Public interface for regular class(es) `grid` (module: ygrid).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGRID_GRID_H
#define YETTY_YCLASSGEN_YGRID_GRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygrid_grid;
YETTY_YRESULT_DECLARE(yetty_ygrid_grid_ptr, struct yetty_ygrid_grid *);
struct yetty_ygrid_grid_ptr_result yetty_ygrid_grid_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygrid_grid_to(struct yetty_ygrid_grid *data);

struct yetty_ycore_void_result;

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_ctx *ctx,
                                                 struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_ctx *,
                                                               struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygrid_register(void);

#endif
