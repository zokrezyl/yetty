/* GENERATED — do not edit. */
/* Public interface for regular class(es) `grid` (module: yvterm).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YVTERM_GRID_H
#define YETTY_YCLASSGEN_YVTERM_GRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yvterm_grid;

struct yetty_yvterm_grid_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_grid *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yvterm_grid_to(struct yetty_yvterm_grid *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

#endif
