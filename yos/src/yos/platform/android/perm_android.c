/*
 * Android permissions — JNI to ActivityCompat / ContextCompat.
 *
 * Permission state is asked of the Java side via
 * yos.YdevPermissionBridge. The Activity calls back when the user
 * answers, and our nativeUpdate JNI native records the result.
 */

#include "../../impl/ydev/internal.h"

#include <jni.h>
#include <string.h>

static const char *cap_to_manifest(ydev_capability_t cap)
{
    switch (cap) {
    case YDEV_CAP_CAMERA:   return "android.permission.CAMERA";
    case YDEV_CAP_MIC:      return "android.permission.RECORD_AUDIO";
    case YDEV_CAP_LOCATION: return "android.permission.ACCESS_FINE_LOCATION";
    case YDEV_CAP_MOTION:   return "android.permission.ACTIVITY_RECOGNITION";
    }
    return NULL;
}

/* JNI: yos.YdevPermissionBridge.nativeUpdate(int capability, int status) */
JNIEXPORT void JNICALL
Java_yos_YdevPermissionBridge_nativeUpdate(JNIEnv *env, jclass cls,
                                           jint cap, jint status)
{
    (void)env; (void)cls;
    ydev_perm_status_t s;
    switch (status) {
    case 0:  s = YDEV_PERM_UNKNOWN;    break;
    case 1:  s = YDEV_PERM_PENDING;    break;
    case 2:  s = YDEV_PERM_GRANTED;    break;
    case 3:  s = YDEV_PERM_DENIED;     break;
    case 4:  s = YDEV_PERM_RESTRICTED; break;
    default: s = YDEV_PERM_UNKNOWN;    break;
    }
    ydev_perm_set((ydev_capability_t)cap, s);
}

ydev_perm_status_t ydev_perm_query_platform(ydev_capability_t cap)
{
    JavaVM *vm = (JavaVM *)g_ydev.jvm;
    const char *manifest = cap_to_manifest(cap);
    if (!vm || !manifest) return YDEV_PERM_UNKNOWN;

    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return YDEV_PERM_UNKNOWN;
    }
    jclass cls = (*env)->FindClass(env, "yos/YdevPermissionBridge");
    if (!cls) return YDEV_PERM_UNKNOWN;
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "queryStatus", "(I)I");
    jint st = (*env)->CallStaticIntMethod(env, cls, m, (jint)cap);
    switch (st) {
    case 2: return YDEV_PERM_GRANTED;
    case 3: return YDEV_PERM_DENIED;
    case 4: return YDEV_PERM_RESTRICTED;
    }
    return YDEV_PERM_UNKNOWN;
}

ydev_result_t ydev_perm_request_platform(ydev_capability_t cap)
{
    JavaVM *vm = (JavaVM *)g_ydev.jvm;
    if (!vm) return YDEV_INVALID_ARG;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return YDEV_IO;
    }
    jclass cls = (*env)->FindClass(env, "yos/YdevPermissionBridge");
    if (!cls) return YDEV_IO;
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "requestStatus", "(I)V");
    (*env)->CallStaticVoidMethod(env, cls, m, (jint)cap);
    return YDEV_OK;
}
