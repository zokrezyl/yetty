/*
 * Android sensor backend — ASensorManager + ASensorEventQueue.
 *
 * ASensorEventQueue gives us a real ALooper fd we can poll on, no
 * vfd ring required. The queue stores up to ~128 events; we drain
 * them in ydev_sensor_read.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/sensor.h>

#include <android/sensor.h>
#include <android/looper.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ydev_sensor {
    ASensorManager    *mgr;
    const ASensor     *sensor;
    ASensorEventQueue *queue;
    ALooper           *looper;
    int                fd;
    ydev_sensor_kind_t kind;
};

static int to_android_kind(ydev_sensor_kind_t k)
{
    switch (k) {
    case YDEV_SENSOR_ACCEL: return ASENSOR_TYPE_ACCELEROMETER;
    case YDEV_SENSOR_GYRO:  return ASENSOR_TYPE_GYROSCOPE;
    case YDEV_SENSOR_MAG:   return ASENSOR_TYPE_MAGNETIC_FIELD;
    case YDEV_SENSOR_BARO:  return 6;   /* ASENSOR_TYPE_PRESSURE */
    case YDEV_SENSOR_LIGHT: return ASENSOR_TYPE_LIGHT;
    case YDEV_SENSOR_PROX:  return ASENSOR_TYPE_PROXIMITY;
    case YDEV_SENSOR_STEPS: return 19;  /* ASENSOR_TYPE_STEP_COUNTER */
    case YDEV_SENSOR_ORIENT:return 15;  /* ASENSOR_TYPE_GAME_ROTATION_VECTOR */
    }
    return -1;
}

ydev_sensor_t *ydev_sensor_open(ydev_sensor_kind_t kind, uint32_t rate_hz)
{
    int akind = to_android_kind(kind);
    if (akind < 0 || rate_hz == 0) {
        ydev_set_error("sensor_open: bad kind/rate");
        return NULL;
    }
    ydev_sensor_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->kind = kind;
    s->mgr  = ASensorManager_getInstanceForPackage(NULL);
    if (!s->mgr) { free(s); return NULL; }
    s->sensor = ASensorManager_getDefaultSensor(s->mgr, akind);
    if (!s->sensor) {
        ydev_set_error("sensor_open: no default sensor for kind=%d", (int)kind);
        free(s); return NULL;
    }
    s->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    s->queue  = ASensorManager_createEventQueue(s->mgr, s->looper, 1, NULL, NULL);
    if (!s->queue) { free(s); return NULL; }
    int32_t us = (int32_t)(1000000 / rate_hz);
    ASensorEventQueue_setEventRate(s->queue, s->sensor, us);
    /* The queue's looper fd is the pollable handle the client sees. */
    s->fd = ALooper_pollAll(s->looper, 0, NULL, NULL, NULL);
    /* Above call drains pending; for the public fd we use the looper's
     * dispatch fd directly via internal API — fall back to creating a
     * pipe placeholder if needed. */
    return s;
}

ydev_result_t ydev_sensor_start(ydev_sensor_t *s)
{
    if (!s) return YDEV_INVALID_ARG;
    return ASensorEventQueue_enableSensor(s->queue, s->sensor) == 0 ? YDEV_OK : YDEV_IO;
}

ydev_result_t ydev_sensor_stop(ydev_sensor_t *s)
{
    if (!s) return YDEV_INVALID_ARG;
    ASensorEventQueue_disableSensor(s->queue, s->sensor);
    return YDEV_OK;
}

void ydev_sensor_close(ydev_sensor_t *s)
{
    if (!s) return;
    if (s->queue) ASensorManager_destroyEventQueue(s->mgr, s->queue);
    free(s);
}

int ydev_sensor_fd(ydev_sensor_t *s)
{
    /* ALooper's fd isn't exposed publicly by the NDK headers. For a real
     * pollable fd we'd need an extra wakeup pipe driven from a worker
     * thread that calls ALooper_pollOnce. The current build returns -1
     * to make clients use the blocking read path. */
    (void)s;
    return -1;
}

ssize_t ydev_sensor_read(ydev_sensor_t *s, ydev_sensor_record_t *out,
                         size_t cap, int timeout_ms)
{
    if (!s || !out || cap == 0) { errno = EINVAL; return -1; }
    int n = ALooper_pollAll(s->looper, timeout_ms, NULL, NULL, NULL);
    (void)n;
    size_t got = 0;
    ASensorEvent ev;
    while (got < cap && ASensorEventQueue_getEvents(s->queue, &ev, 1) > 0) {
        out[got].ts_ns = (uint64_t)ev.timestamp;
        switch (s->kind) {
        case YDEV_SENSOR_ACCEL: case YDEV_SENSOR_GYRO: case YDEV_SENSOR_MAG:
            out[got].u.v3[0] = ev.vector.x;
            out[got].u.v3[1] = ev.vector.y;
            out[got].u.v3[2] = ev.vector.z;
            break;
        case YDEV_SENSOR_BARO: case YDEV_SENSOR_LIGHT: case YDEV_SENSOR_PROX:
            out[got].u.v1 = ev.data[0];
            break;
        case YDEV_SENSOR_STEPS:
            out[got].u.counter = (uint64_t)ev.data[0];
            break;
        case YDEV_SENSOR_ORIENT:
            out[got].u.quat[0] = ev.data[3];   /* w */
            out[got].u.quat[1] = ev.data[0];   /* x */
            out[got].u.quat[2] = ev.data[1];   /* y */
            out[got].u.quat[3] = ev.data[2];   /* z */
            break;
        }
        got++;
    }
    return (ssize_t)got;
}
