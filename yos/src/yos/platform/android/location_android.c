/*
 * Android location backend — JNI to android.location.LocationManager.
 *
 * Requires a JavaVM*; the host app must pass it via ydev_init(). On
 * each onLocationChanged the Java glue calls a JNI native method that
 * pushes a ydev_loc_fix_t into the vfd ring.
 *
 * This file declares the JNI native; the matching Java side
 * (Yos.YdevLocationBridge) lives in the app's source set.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/location.h>

#include <jni.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct ydev_loc {
    struct ydev_vfd      vfd;
    jobject              listener;        /* GlobalRef                  */
    int                  started;
};

/* JNI: yos.YdevLocationBridge.nativePush */
JNIEXPORT void JNICALL
Java_yos_YdevLocationBridge_nativePush(JNIEnv *env, jobject thiz,
                                       jlong handle, jlong ts_ms,
                                       jdouble lat, jdouble lon, jdouble alt,
                                       jfloat hacc, jfloat vacc,
                                       jfloat speed, jfloat bearing)
{
    (void)env; (void)thiz;
    ydev_loc_t *h = (ydev_loc_t *)(intptr_t)handle;
    if (!h) return;
    ydev_loc_fix_t fix = {0};
    fix.ts_ns       = (uint64_t)ts_ms * 1000000ull;
    fix.lat         = lat;
    fix.lon         = lon;
    fix.alt_m       = alt;
    fix.horiz_acc_m = hacc;
    fix.vert_acc_m  = vacc;
    fix.speed_mps   = speed;
    fix.bearing_deg = bearing;
    ydev_vfd_push(&h->vfd, &fix);
}

ydev_loc_t *ydev_loc_open(ydev_loc_accuracy_t a)
{
    (void)a;
    ydev_loc_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if (ydev_vfd_init(&h->vfd, sizeof(ydev_loc_fix_t), 32, NULL) != 0) {
        free(h); return NULL;
    }
    return h;
}

ydev_result_t ydev_loc_start(ydev_loc_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    JavaVM *vm = (JavaVM *)g_ydev.jvm;
    if (!vm) {
        ydev_set_error("loc_start: ydev_init was not called with a JavaVM*");
        return YDEV_INVALID_ARG;
    }
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return YDEV_IO;
    }
    jclass cls = (*env)->FindClass(env, "yos/YdevLocationBridge");
    if (!cls) { ydev_set_error("loc_start: missing yos.YdevLocationBridge"); return YDEV_IO; }
    jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jmethodID start = (*env)->GetMethodID(env, cls, "start", "()V");
    jobject obj = (*env)->NewObject(env, cls, ctor, (jlong)(intptr_t)h);
    h->listener = (*env)->NewGlobalRef(env, obj);
    (*env)->CallVoidMethod(env, h->listener, start);
    h->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_loc_stop(ydev_loc_t *h)
{
    if (!h || !h->started) return YDEV_OK;
    JavaVM *vm = (JavaVM *)g_ydev.jvm;
    if (vm) {
        JNIEnv *env = NULL;
        if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK ||
            (*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK) {
            jclass cls = (*env)->GetObjectClass(env, h->listener);
            jmethodID stop = (*env)->GetMethodID(env, cls, "stop", "()V");
            (*env)->CallVoidMethod(env, h->listener, stop);
            (*env)->DeleteGlobalRef(env, h->listener);
            h->listener = NULL;
        }
    }
    h->started = 0;
    ydev_vfd_close(&h->vfd);
    return YDEV_OK;
}

void ydev_loc_close(ydev_loc_t *h)
{
    if (!h) return;
    if (h->started) ydev_loc_stop(h);
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
