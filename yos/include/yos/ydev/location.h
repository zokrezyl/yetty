/*
 * ydev/location.h — GPS / fused location.
 *
 * Each ydev_loc_fix_t is one position update from the platform. iOS and
 * Android can deliver fused fixes (GPS + WiFi + cell); Linux backs onto
 * gpsd by default.
 */

#ifndef YOS_YDEV_LOCATION_H
#define YOS_YDEV_LOCATION_H

#include <yos/ydev/ydev.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_LOC_BEST       = 0,   /* platform default                       */
    YDEV_LOC_NAVIGATION = 1,   /* continuous, high battery               */
    YDEV_LOC_CITY       = 2,
    YDEV_LOC_LOW_POWER  = 3,
} ydev_loc_accuracy_t;

typedef struct {
    uint64_t ts_ns;
    double   lat;
    double   lon;
    double   alt_m;
    float    horiz_acc_m;
    float    vert_acc_m;
    float    speed_mps;
    float    bearing_deg;
} ydev_loc_fix_t;

typedef struct ydev_loc ydev_loc_t;

ydev_loc_t    *ydev_loc_open(ydev_loc_accuracy_t accuracy);
ydev_result_t  ydev_loc_start(ydev_loc_t *);
ydev_result_t  ydev_loc_stop(ydev_loc_t *);
void           ydev_loc_close(ydev_loc_t *);

int            ydev_loc_fd(ydev_loc_t *);
ssize_t        ydev_loc_read(ydev_loc_t *, ydev_loc_fix_t *out, size_t cap,
                             int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
