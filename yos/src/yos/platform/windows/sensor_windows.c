/*
 * Windows sensor — UNSUPPORTED stub. Desktop Windows machines don't
 * expose accelerometer/gyro/etc. through a portable API; a future
 * iteration could wrap the Windows.Devices.Sensors WinRT surface.
 */

#include "../../impl/ydev/internal.h"

#include <yos/ydev/sensor.h>

ydev_sensor_t *ydev_sensor_open(ydev_sensor_kind_t kind, uint32_t rate_hz)
{
    (void)kind; (void)rate_hz;
    return NULL;
}

ydev_result_t ydev_sensor_start(ydev_sensor_t *s) { (void)s; return YDEV_UNSUPPORTED; }
ydev_result_t ydev_sensor_stop (ydev_sensor_t *s) { (void)s; return YDEV_UNSUPPORTED; }
void          ydev_sensor_close(ydev_sensor_t *s) { (void)s; }
int           ydev_sensor_fd   (ydev_sensor_t *s) { (void)s; return -1; }
