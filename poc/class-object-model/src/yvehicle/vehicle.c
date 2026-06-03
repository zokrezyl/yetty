/* Vehicle — base class default impls.
 *
 * Members are annotated to control their exposure:
 *   property  -> read + write accessors generated (mileage, speed)
 *   (none)    -> private; no accessor, only this class can touch it (fuel_level)
 *
 * A class reaches its OWN members through the opaque data handle
 * yvehicle_vehicle_data(obj) (the struct is complete only here). Other
 * classes get at mileage/speed through the generated getters/setters and
 * cannot see fuel_level at all. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/vehicle.h" /* data handle + member accessor decls */

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:vehicle")]] vehicle_data {
    [[clang::annotate("property")]] int mileage;
    [[clang::annotate("property")]] int speed;
    int fuel_level; /* private: managed only by vehicle */
};

[[clang::annotate("override@yvehicle:vehicle:vehicle_ctor")]]
static struct yetty_ycore_void_result vehicle_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    self->mileage = 0;
    self->speed = 0;
    self->fuel_level = 100;
    ydebug("obj=%p mileage=%d speed=%d fuel=%d", (void *)obj, self->mileage, self->speed,
           self->fuel_level);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_dtor")]]
static struct yetty_ycore_void_result vehicle_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    ydebug("obj=%p final mileage=%d", (void *)obj, self->mileage);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_start")]]
static struct yetty_ycore_void_result vehicle_default_start(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    self->speed = 0;
    ydebug("obj=%p started, mileage=%d", (void *)obj, self->mileage);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_accelerate")]]
static struct yetty_ycore_int_result vehicle_default_accelerate(struct ctx *ctx, struct object *obj,
                                                                float speed)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    self->speed += (int)speed;
    self->mileage += (int)speed / 10 + 1;
    self->fuel_level -= 1; /* private bookkeeping */
    ydebug("obj=%p speed=%d mileage=%d fuel=%d", (void *)obj, self->speed, self->mileage,
           self->fuel_level);
    return YETTY_OK(yetty_ycore_int, self->speed);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_brake")]]
static struct yetty_ycore_int_result vehicle_default_brake(struct ctx *ctx, struct object *obj,
                                                           float intensity)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    self->speed -= (int)(self->speed * intensity);
    if (self->speed < 0) {
        self->speed = 0;
    }
    ydebug("obj=%p intensity=%.1f speed=%d", (void *)obj, intensity, self->speed);
    return YETTY_OK(yetty_ycore_int, self->speed);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_describe")]]
static struct str_result vehicle_default_describe(struct ctx *ctx, struct object *obj,
                                                  float distance)
{
    (void)ctx;
    struct vehicle_data *self = yvehicle_vehicle_data(obj);
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "vehicle@%p mileage=%d speed=%d fuel=%d (distance=%.1f)",
             (void *)obj, self->mileage, self->speed, self->fuel_level, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "vehicle.gen.c"
