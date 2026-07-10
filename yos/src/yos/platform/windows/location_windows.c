/*
 * Windows location — UNSUPPORTED stub. A future iteration could wrap
 * the Windows.Devices.Geolocation WinRT surface.
 */

#include "../../impl/ydev/internal.h"

#include <yos/ydev/location.h>

ydev_loc_t *ydev_loc_open(ydev_loc_accuracy_t a)
{
    (void)a;
    return NULL;
}

ydev_result_t ydev_loc_start(ydev_loc_t *h) { (void)h; return YDEV_UNSUPPORTED; }
ydev_result_t ydev_loc_stop (ydev_loc_t *h) { (void)h; return YDEV_UNSUPPORTED; }
void          ydev_loc_close(ydev_loc_t *h) { (void)h; }
int           ydev_loc_fd   (ydev_loc_t *h) { (void)h; return -1; }
