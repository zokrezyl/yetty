/*
 * ydev/sensor.h — IMU / barometer / light / proximity / step / orientation.
 *
 * One handle subscribes to one sensor at one requested rate. The backend
 * picks the closest supported rate; reads return packed records that the
 * client strides through.
 */

#ifndef YOS_YDEV_SENSOR_H
#define YOS_YDEV_SENSOR_H

#include <yos/ydev/ydev.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_SENSOR_ACCEL  = 1,   /* m/s^2,        3 axes               */
    YDEV_SENSOR_GYRO   = 2,   /* rad/s,        3 axes               */
    YDEV_SENSOR_MAG    = 3,   /* microtesla,   3 axes               */
    YDEV_SENSOR_BARO   = 4,   /* hPa,          scalar               */
    YDEV_SENSOR_LIGHT  = 5,   /* lux,          scalar               */
    YDEV_SENSOR_PROX   = 6,   /* cm (some platforms: bool 0/1)      */
    YDEV_SENSOR_STEPS  = 7,   /* uint64,       cumulative counter   */
    YDEV_SENSOR_ORIENT = 8,   /* quaternion w,x,y,z (fused)         */
} ydev_sensor_kind_t;

typedef struct {
    uint64_t ts_ns;
    union {
        float    v3[3];
        float    v1;
        uint64_t counter;
        float    quat[4];
    } u;
} ydev_sensor_record_t;

typedef struct ydev_sensor ydev_sensor_t;

ydev_sensor_t *ydev_sensor_open(ydev_sensor_kind_t kind, uint32_t rate_hz);
ydev_result_t  ydev_sensor_start(ydev_sensor_t *);
ydev_result_t  ydev_sensor_stop(ydev_sensor_t *);
void           ydev_sensor_close(ydev_sensor_t *);

int            ydev_sensor_fd(ydev_sensor_t *);
ssize_t        ydev_sensor_read(ydev_sensor_t *, ydev_sensor_record_t *out,
                                size_t cap, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
