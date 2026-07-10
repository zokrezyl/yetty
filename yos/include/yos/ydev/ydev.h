/*
 * ydev — cross-platform device abstraction.
 *
 * Camera, microphone, speaker, sensors, location, and the permission flows
 * that gate them. Every device handle exposes one real kernel fd that the
 * client passes to poll/select/kqueue. The fd becomes readable when there
 * is work to do; the client never sees a framework callback.
 *
 * Designed to be linked into any host-side app (the camera server, the yos
 * host, future tools) — no wasm or yos runtime dependency.
 *
 * Public surface lives in:
 *   <yos/ydev/ydev.h>      common types, init/shutdown, strerror
 *   <yos/ydev/perm.h>      permissions
 *   <yos/ydev/camera.h>    camera
 *   <yos/ydev/audio.h>     mic + speaker
 *   <yos/ydev/sensor.h>    accel / gyro / mag / baro / light / steps / quat
 *   <yos/ydev/location.h>  GPS / fused location
 */

#ifndef YOS_YDEV_H
#define YOS_YDEV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_OK            =  0,
    YDEV_AGAIN         = -1,   /* non-blocking acquire, no data ready    */
    YDEV_DENIED        = -2,   /* user denied permission                  */
    YDEV_RESTRICTED    = -3,   /* policy denial (iOS managed config)      */
    YDEV_UNSUPPORTED   = -4,   /* format/rate/device not available        */
    YDEV_IO            = -5,   /* device error or unplugged               */
    YDEV_INVALID_ARG   = -6,
    YDEV_NO_MEM        = -7,
    YDEV_BUSY          = -8,   /* already in use by another process       */
    YDEV_INTERNAL      = -9,
} ydev_result_t;

typedef enum {
    YDEV_CAP_CAMERA    = 1,
    YDEV_CAP_MIC       = 2,
    YDEV_CAP_LOCATION  = 3,
    YDEV_CAP_MOTION    = 4,
} ydev_capability_t;

/*
 * Process-wide init. Safe to call multiple times. On Android the caller
 * must pass a non-NULL JavaVM* in init->jvm before any JNI-backed flow
 * (camera permission, location, sensors above the NDK floor) is used.
 */
typedef struct {
    void *jvm;          /* JavaVM* on Android; ignored elsewhere */
    void *reserved[3];
} ydev_init_t;

ydev_result_t ydev_init(const ydev_init_t *init);
void          ydev_shutdown(void);

const char *ydev_strerror(ydev_result_t r);

/*
 * Thread-local detail string for the most recent failure on the calling
 * thread. Cleared by every successful ydev_* call. Always returns a valid
 * pointer; empty string when there is nothing to report.
 */
const char *ydev_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
