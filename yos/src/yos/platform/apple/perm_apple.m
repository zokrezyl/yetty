/*
 * Apple permissions — AVCaptureDevice / AVAudioApplication /
 * CLLocationManager / CMMotionActivityManager.
 *
 * `ydev_perm_query_platform` is the synchronous lookup that the core
 * calls on every ydev_perm_status(). `ydev_perm_request_platform`
 * kicks off the asynchronous prompt and posts the result back via
 * ydev_perm_set when the completion block fires.
 */

#include "../../impl/ydev/internal.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#if TARGET_OS_OSX || TARGET_OS_IOS
  #import <CoreLocation/CoreLocation.h>
  #define YDEV_HAVE_CL 1
#else
  #define YDEV_HAVE_CL 0
#endif

#if TARGET_OS_IOS
  #import <CoreMotion/CoreMotion.h>
  #define YDEV_HAVE_CM 1
#else
  #define YDEV_HAVE_CM 0
#endif

static ydev_perm_status_t map_av(AVAuthorizationStatus s)
{
    switch (s) {
    case AVAuthorizationStatusNotDetermined: return YDEV_PERM_UNKNOWN;
    case AVAuthorizationStatusAuthorized:    return YDEV_PERM_GRANTED;
    case AVAuthorizationStatusDenied:        return YDEV_PERM_DENIED;
    case AVAuthorizationStatusRestricted:    return YDEV_PERM_RESTRICTED;
    }
    return YDEV_PERM_UNKNOWN;
}

ydev_perm_status_t ydev_perm_query_platform(ydev_capability_t cap)
{
    switch (cap) {
    case YDEV_CAP_CAMERA:
        return map_av([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]);
    case YDEV_CAP_MIC:
        return map_av([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
    case YDEV_CAP_LOCATION:
#if YDEV_HAVE_CL
      {
        if (![CLLocationManager locationServicesEnabled]) return YDEV_PERM_DENIED;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            CLLocationManager *lm = [[CLLocationManager alloc] init];
            CLAuthorizationStatus s = lm.authorizationStatus;
            switch (s) {
            case kCLAuthorizationStatusNotDetermined:    return YDEV_PERM_UNKNOWN;
            case kCLAuthorizationStatusRestricted:       return YDEV_PERM_RESTRICTED;
            case kCLAuthorizationStatusDenied:           return YDEV_PERM_DENIED;
            case kCLAuthorizationStatusAuthorizedAlways: return YDEV_PERM_GRANTED;
            default:                                     break;
            }
#if TARGET_OS_IPHONE
            if (s == kCLAuthorizationStatusAuthorizedWhenInUse) return YDEV_PERM_GRANTED;
#endif
        }
        return YDEV_PERM_UNKNOWN;
      }
#else
        return YDEV_PERM_RESTRICTED;
#endif
    case YDEV_CAP_MOTION:
#if YDEV_HAVE_CM
      {
        CMAuthorizationStatus s = [CMMotionActivityManager authorizationStatus];
        switch (s) {
        case CMAuthorizationStatusNotDetermined: return YDEV_PERM_UNKNOWN;
        case CMAuthorizationStatusRestricted:    return YDEV_PERM_RESTRICTED;
        case CMAuthorizationStatusDenied:        return YDEV_PERM_DENIED;
        case CMAuthorizationStatusAuthorized:    return YDEV_PERM_GRANTED;
        }
        return YDEV_PERM_UNKNOWN;
      }
#else
        return YDEV_PERM_GRANTED;  /* macOS/tvOS: no runtime gate, sensors ungated */
#endif
    }
    return YDEV_PERM_UNKNOWN;
}

ydev_result_t ydev_perm_request_platform(ydev_capability_t cap)
{
    switch (cap) {
    case YDEV_CAP_CAMERA:
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
            ydev_perm_set(YDEV_CAP_CAMERA,
                          granted ? YDEV_PERM_GRANTED : YDEV_PERM_DENIED);
        }];
        return YDEV_OK;
    case YDEV_CAP_MIC:
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
            ydev_perm_set(YDEV_CAP_MIC,
                          granted ? YDEV_PERM_GRANTED : YDEV_PERM_DENIED);
        }];
        return YDEV_OK;
    case YDEV_CAP_LOCATION:
        /* Location permission is requested by the location backend
         * (CLLocationManager.requestWhenInUseAuthorization) on its
         * own at ydev_loc_open time — there is no separate prompt
         * to fire from here. Return YDEV_UNSUPPORTED so the caller
         * skips a useless ydev_perm_request() and goes straight to
         * ydev_loc_open(); the kCLAuthorizationStatus callback then
         * publishes the result through ydev_perm_set. */
        return YDEV_UNSUPPORTED;
    case YDEV_CAP_MOTION:
        return YDEV_UNSUPPORTED;
    }
    return YDEV_INVALID_ARG;
}
