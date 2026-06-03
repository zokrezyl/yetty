/* Vehicle — base class default impls.
 *
 * Members are annotated to control their exposure:
 *   property  -> read + write accessors generated (mileage, speed)
 *   (none)    -> private; no accessor, only this class can touch it (fuel_level)
 *
 * A class reaches its OWN members through the data handle
 * yvehicle_vehicle_data_get(obj), which returns a Result wrapping the
 * (locally complete) struct pointer — a bad object surfaces as an error
 * rather than a silent wrong value. Other classes reach mileage/speed
 * through the generated getters/setters (also Result-returning) and cannot
 * see fuel_level at all. */

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
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "vehicle_default_ctor: data block");
    self.value->mileage = 0;
    self.value->speed = 0;
    self.value->fuel_level = 100;
    ydebug("obj=%p mileage=%d speed=%d fuel=%d", (void *)obj, self.value->mileage,
           self.value->speed, self.value->fuel_level);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_dtor")]]
static struct yetty_ycore_void_result vehicle_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "vehicle_default_dtor: data block");
    ydebug("obj=%p final mileage=%d", (void *)obj, self.value->mileage);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_start")]]
static struct yetty_ycore_void_result vehicle_default_start(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "vehicle_default_start: data block");
    self.value->speed = 0;
    ydebug("obj=%p started, mileage=%d", (void *)obj, self.value->mileage);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_accelerate")]]
static struct yetty_ycore_int_result vehicle_default_accelerate(struct ctx *ctx, struct object *obj,
                                                                float speed)
{
    (void)ctx;
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "vehicle_default_accelerate: data block");
    self.value->speed += (int)speed;
    self.value->mileage += (int)speed / 10 + 1;
    self.value->fuel_level -= 1; /* private bookkeeping */
    ydebug("obj=%p speed=%d mileage=%d fuel=%d", (void *)obj, self.value->speed,
           self.value->mileage, self.value->fuel_level);
    return YETTY_OK(yetty_ycore_int, self.value->speed);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_brake")]]
static struct yetty_ycore_int_result vehicle_default_brake(struct ctx *ctx, struct object *obj,
                                                           float intensity)
{
    (void)ctx;
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "vehicle_default_brake: data block");
    self.value->speed -= (int)(self.value->speed * intensity);
    if (self.value->speed < 0) {
        self.value->speed = 0;
    }
    ydebug("obj=%p intensity=%.1f speed=%d", (void *)obj, intensity, self.value->speed);
    return YETTY_OK(yetty_ycore_int, self.value->speed);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_describe")]]
static struct str_result vehicle_default_describe(struct ctx *ctx, struct object *obj,
                                                  float distance)
{
    (void)ctx;
    struct yvehicle_vehicle_data_ptr_result self = yvehicle_vehicle_data_get(obj);
    YETTY_RETURN_IF_ERR(str, self, "vehicle_default_describe: data block");
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "vehicle@%p mileage=%d speed=%d fuel=%d (distance=%.1f)",
             (void *)obj, self.value->mileage, self.value->speed, self.value->fuel_level, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "vehicle.gen.c"
