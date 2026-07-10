/*
 * Apple sensor backend — CoreMotion.
 *
 * Active only on iOS (CMMotionManager exists on macOS but the data
 * APIs we care about — accelerometer, gyroscope, magnetometer,
 * device-motion-derived orientation — are iOS+watchOS only). On
 * macOS and tvOS every open returns UNSUPPORTED.
 *
 * CMMotionManager pushes samples to an NSOperationQueue we own. Each
 * delivery copies the relevant fields into a ydev_sensor_record_t and
 * pushes into a vfd ring; the client reads from the ring via
 * ydev_sensor_read.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/sensor.h>

#import <Foundation/Foundation.h>

#if TARGET_OS_IOS
  #import <CoreMotion/CoreMotion.h>
  #define YDEV_SENSOR_REAL 1
#else
  #define YDEV_SENSOR_REAL 0
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct ydev_sensor {
    struct ydev_vfd       vfd;
    ydev_sensor_kind_t    kind;
    uint32_t              rate_hz;
#if YDEV_SENSOR_REAL
    void                 *mgr;          /* CFBridgingRetain'd CMMotionManager  */
    void                 *alt;          /* CFBridgingRetain'd CMAltimeter      */
    void                 *queue;        /* CFBridgingRetain'd NSOperationQueue */
#endif
    int                   started;
};

ydev_sensor_t *ydev_sensor_open(ydev_sensor_kind_t kind, uint32_t rate_hz)
{
#if !YDEV_SENSOR_REAL
    (void)kind; (void)rate_hz;
    ydev_set_error("sensor: CoreMotion data API not available on this Apple platform");
    return NULL;
#else
    if (rate_hz == 0 || rate_hz > 1000) {
        ydev_set_error("sensor_open: rate_hz out of range");
        return NULL;
    }
    ydev_sensor_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->kind    = kind;
    h->rate_hz = rate_hz;

    if (ydev_vfd_init(&h->vfd, sizeof(ydev_sensor_record_t), 128, NULL) != 0) {
        free(h);
        return NULL;
    }

    NSOperationQueue *q = [[NSOperationQueue alloc] init];
    q.maxConcurrentOperationCount = 1;
    h->queue = (void *)CFBridgingRetain(q);

    if (kind == YDEV_SENSOR_BARO) {
        if (![CMAltimeter isRelativeAltitudeAvailable]) {
            ydev_set_error("sensor_open: barometer not available");
            ydev_vfd_destroy(&h->vfd);
            CFBridgingRelease(h->queue);
            free(h);
            return NULL;
        }
        h->alt = (void *)CFBridgingRetain([[CMAltimeter alloc] init]);
    } else {
        CMMotionManager *m = [[CMMotionManager alloc] init];
        m.accelerometerUpdateInterval  = 1.0 / (double)rate_hz;
        m.gyroUpdateInterval           = 1.0 / (double)rate_hz;
        m.magnetometerUpdateInterval   = 1.0 / (double)rate_hz;
        m.deviceMotionUpdateInterval   = 1.0 / (double)rate_hz;
        h->mgr = (void *)CFBridgingRetain(m);
    }
    return h;
#endif
}

#if YDEV_SENSOR_REAL
static uint64_t cm_ts_to_ns(NSTimeInterval bootTime)
{
    /* CoreMotion timestamps are seconds since boot (mach_absolute_time
     * units, sort of). We use them directly as a monotonic clock. */
    return (uint64_t)(bootTime * 1.0e9);
}
#endif

ydev_result_t ydev_sensor_start(ydev_sensor_t *h)
{
#if !YDEV_SENSOR_REAL
    (void)h; return YDEV_UNSUPPORTED;
#else
    if (!h) return YDEV_INVALID_ARG;
    NSOperationQueue *q = (__bridge NSOperationQueue *)h->queue;

    if (h->kind == YDEV_SENSOR_BARO) {
        CMAltimeter *a = (__bridge CMAltimeter *)h->alt;
        [a startRelativeAltitudeUpdatesToQueue:q
                                  withHandler:^(CMAltitudeData *d, NSError *e) {
            if (!d || e) return;
            ydev_sensor_record_t rec = {0};
            rec.ts_ns = cm_ts_to_ns(d.timestamp);
            /* relativeAltitude is in metres; pressure (kPa) → hPa. */
            rec.u.v1 = d.pressure.floatValue * 10.0f;
            ydev_vfd_push(&h->vfd, &rec);
        }];
        h->started = 1;
        return YDEV_OK;
    }

    CMMotionManager *m = (__bridge CMMotionManager *)h->mgr;
    switch (h->kind) {
    case YDEV_SENSOR_ACCEL:
        if (!m.accelerometerAvailable) return YDEV_UNSUPPORTED;
        [m startAccelerometerUpdatesToQueue:q
                                withHandler:^(CMAccelerometerData *d, NSError *e) {
            if (!d || e) return;
            ydev_sensor_record_t rec = {0};
            rec.ts_ns   = cm_ts_to_ns(d.timestamp);
            /* CoreMotion reports in g; convert to m/s^2. */
            rec.u.v3[0] = (float)(d.acceleration.x * 9.80665);
            rec.u.v3[1] = (float)(d.acceleration.y * 9.80665);
            rec.u.v3[2] = (float)(d.acceleration.z * 9.80665);
            ydev_vfd_push(&h->vfd, &rec);
        }];
        break;
    case YDEV_SENSOR_GYRO:
        if (!m.gyroAvailable) return YDEV_UNSUPPORTED;
        [m startGyroUpdatesToQueue:q
                       withHandler:^(CMGyroData *d, NSError *e) {
            if (!d || e) return;
            ydev_sensor_record_t rec = {0};
            rec.ts_ns   = cm_ts_to_ns(d.timestamp);
            rec.u.v3[0] = (float)d.rotationRate.x;
            rec.u.v3[1] = (float)d.rotationRate.y;
            rec.u.v3[2] = (float)d.rotationRate.z;
            ydev_vfd_push(&h->vfd, &rec);
        }];
        break;
    case YDEV_SENSOR_MAG:
        if (!m.magnetometerAvailable) return YDEV_UNSUPPORTED;
        [m startMagnetometerUpdatesToQueue:q
                               withHandler:^(CMMagnetometerData *d, NSError *e) {
            if (!d || e) return;
            ydev_sensor_record_t rec = {0};
            rec.ts_ns   = cm_ts_to_ns(d.timestamp);
            rec.u.v3[0] = (float)d.magneticField.x;
            rec.u.v3[1] = (float)d.magneticField.y;
            rec.u.v3[2] = (float)d.magneticField.z;
            ydev_vfd_push(&h->vfd, &rec);
        }];
        break;
    case YDEV_SENSOR_ORIENT:
        if (!m.deviceMotionAvailable) return YDEV_UNSUPPORTED;
        [m startDeviceMotionUpdatesToQueue:q
                               withHandler:^(CMDeviceMotion *d, NSError *e) {
            if (!d || e) return;
            ydev_sensor_record_t rec = {0};
            rec.ts_ns     = cm_ts_to_ns(d.timestamp);
            rec.u.quat[0] = (float)d.attitude.quaternion.w;
            rec.u.quat[1] = (float)d.attitude.quaternion.x;
            rec.u.quat[2] = (float)d.attitude.quaternion.y;
            rec.u.quat[3] = (float)d.attitude.quaternion.z;
            ydev_vfd_push(&h->vfd, &rec);
        }];
        break;
    default:
        return YDEV_UNSUPPORTED;
    }
    h->started = 1;
    return YDEV_OK;
#endif
}

ydev_result_t ydev_sensor_stop(ydev_sensor_t *h)
{
#if !YDEV_SENSOR_REAL
    (void)h; return YDEV_UNSUPPORTED;
#else
    if (!h) return YDEV_INVALID_ARG;
    if (h->kind == YDEV_SENSOR_BARO) {
        if (h->alt) [(__bridge CMAltimeter *)h->alt stopRelativeAltitudeUpdates];
    } else if (h->mgr) {
        CMMotionManager *m = (__bridge CMMotionManager *)h->mgr;
        [m stopAccelerometerUpdates];
        [m stopGyroUpdates];
        [m stopMagnetometerUpdates];
        [m stopDeviceMotionUpdates];
    }
    /* Drain the operation queue so any in-flight handler — which
     * captured `h` by value and dereferences h->vfd — has finished
     * before we let the caller (ydev_sensor_close) free `h`. Without
     * this, stopXxxUpdates returns immediately but a pending block
     * can still fire and write to freed memory. */
    if (h->queue) {
        NSOperationQueue *q = (__bridge NSOperationQueue *)h->queue;
        [q waitUntilAllOperationsAreFinished];
    }
    h->started = 0;
    ydev_vfd_close(&h->vfd);
    return YDEV_OK;
#endif
}

void ydev_sensor_close(ydev_sensor_t *h)
{
    if (!h) return;
#if YDEV_SENSOR_REAL
    if (h->started) ydev_sensor_stop(h);
    if (h->mgr)   CFBridgingRelease(h->mgr);
    if (h->alt)   CFBridgingRelease(h->alt);
    if (h->queue) CFBridgingRelease(h->queue);
#endif
    ydev_vfd_destroy(&h->vfd);
    free(h);
}

int ydev_sensor_fd(ydev_sensor_t *h) { return h ? ydev_vfd_fd(&h->vfd) : -1; }

ssize_t ydev_sensor_read(ydev_sensor_t *h, ydev_sensor_record_t *out,
                         size_t cap, int timeout_ms)
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
