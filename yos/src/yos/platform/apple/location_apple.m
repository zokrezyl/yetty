/*
 * Apple location backend — CLLocationManager.
 *
 * The CLLocationManager has to live for the duration of updates; its
 * delegate must be set; on iOS it needs an Info.plist key
 * (NSLocationWhenInUseUsageDescription) and a request* call. The
 * delegate's locationManager:didUpdateLocations: drops fixes into a
 * vfd ring that the client drains via ydev_loc_read.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/location.h>

#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

@class YdevLocDelegate;

struct ydev_loc {
    struct ydev_vfd        vfd;
    void                  *delegate;     /* CFBridgingRetain'd YdevLocDelegate */
    ydev_loc_accuracy_t    accuracy;
    int                    started;
};

@interface YdevLocDelegate : NSObject <CLLocationManagerDelegate>
{
@public
    CLLocationManager *mgr;
    struct ydev_loc   *owner;
}
@end

@implementation YdevLocDelegate
- (void)locationManager:(CLLocationManager *)manager
     didUpdateLocations:(NSArray<CLLocation *> *)locations
{
    (void)manager;
    if (!owner) return;
    for (CLLocation *loc in locations) {
        ydev_loc_fix_t fix = {0};
        fix.ts_ns       = (uint64_t)([loc.timestamp timeIntervalSince1970] * 1.0e9);
        fix.lat         = loc.coordinate.latitude;
        fix.lon         = loc.coordinate.longitude;
        fix.alt_m       = loc.altitude;
        fix.horiz_acc_m = (float)loc.horizontalAccuracy;
        fix.vert_acc_m  = (float)loc.verticalAccuracy;
        fix.speed_mps   = (float)loc.speed;
        fix.bearing_deg = (float)loc.course;
        ydev_vfd_push(&owner->vfd, &fix);
    }
}

- (void)locationManager:(CLLocationManager *)manager
       didFailWithError:(NSError *)error
{
    (void)manager;
    ydev_set_error("loc: %s", error.localizedDescription.UTF8String ?: "unknown");
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager *)manager
{
    if (@available(macOS 11.0, iOS 14.0, *)) {
        CLAuthorizationStatus s = manager.authorizationStatus;
        ydev_perm_status_t mapped = YDEV_PERM_UNKNOWN;
        switch (s) {
        case kCLAuthorizationStatusNotDetermined:    mapped = YDEV_PERM_UNKNOWN;    break;
        case kCLAuthorizationStatusRestricted:       mapped = YDEV_PERM_RESTRICTED; break;
        case kCLAuthorizationStatusDenied:           mapped = YDEV_PERM_DENIED;     break;
        case kCLAuthorizationStatusAuthorizedAlways: mapped = YDEV_PERM_GRANTED;    break;
        default:                                                                    break;
        }
#if TARGET_OS_IPHONE
        if (s == kCLAuthorizationStatusAuthorizedWhenInUse) mapped = YDEV_PERM_GRANTED;
#endif
        ydev_perm_set(YDEV_CAP_LOCATION, mapped);
    }
}
@end

static CLLocationAccuracy desired_acc(ydev_loc_accuracy_t a)
{
    switch (a) {
    case YDEV_LOC_NAVIGATION: return kCLLocationAccuracyBestForNavigation;
    case YDEV_LOC_CITY:       return kCLLocationAccuracyKilometer;
    case YDEV_LOC_LOW_POWER:  return kCLLocationAccuracyThreeKilometers;
    case YDEV_LOC_BEST:
    default:                  return kCLLocationAccuracyBest;
    }
}

ydev_loc_t *ydev_loc_open(ydev_loc_accuracy_t accuracy)
{
    ydev_loc_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->accuracy = accuracy;
    if (ydev_vfd_init(&h->vfd, sizeof(ydev_loc_fix_t), 32, NULL) != 0) {
        free(h);
        return NULL;
    }

    YdevLocDelegate *d = [[YdevLocDelegate alloc] init];
    d->mgr               = [[CLLocationManager alloc] init];
    d->mgr.delegate      = d;
    d->mgr.desiredAccuracy = desired_acc(accuracy);
    d->owner             = h;
    h->delegate = (void *)CFBridgingRetain(d);
    return h;
}

ydev_result_t ydev_loc_start(ydev_loc_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    YdevLocDelegate *d = (__bridge YdevLocDelegate *)h->delegate;

#if TARGET_OS_IPHONE
    /* Asking is cheap, idempotent, and required before startUpdatingLocation. */
    [d->mgr requestWhenInUseAuthorization];
#endif

    [d->mgr startUpdatingLocation];
    h->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_loc_stop(ydev_loc_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    YdevLocDelegate *d = (__bridge YdevLocDelegate *)h->delegate;
    [d->mgr stopUpdatingLocation];
    h->started = 0;
    ydev_vfd_close(&h->vfd);
    return YDEV_OK;
}

void ydev_loc_close(ydev_loc_t *h)
{
    if (!h) return;
    if (h->started) ydev_loc_stop(h);
    if (h->delegate) {
        YdevLocDelegate *d = (YdevLocDelegate *)CFBridgingRelease(h->delegate);
        d->mgr.delegate = nil;
        d->owner        = NULL;
        d = nil;
    }
    ydev_vfd_destroy(&h->vfd);
    free(h);
}

int ydev_loc_fd(ydev_loc_t *h) { return h ? ydev_vfd_fd(&h->vfd) : -1; }

ssize_t ydev_loc_read(ydev_loc_t *h, ydev_loc_fix_t *out, size_t cap, int timeout_ms)
{
    if (!h || !out || cap == 0) { errno = EINVAL; return -1; }
    size_t got = 0;
    while (got < cap) {
        ydev_result_t r = ydev_vfd_pop(&h->vfd, &out[got],
                                       got == 0 ? timeout_ms : 0);
        if (r == YDEV_AGAIN) break;
        if (r != YDEV_OK)    { errno = EIO; return got ? (ssize_t)got : -1; }
        got++;
    }
    return (ssize_t)got;
}
