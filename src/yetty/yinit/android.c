/* Android main - Application entry point using native_app_glue */

#include <android/looper.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_window.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <webgpu/webgpu.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <yetty/yetty/yetty.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>

#define LOG_TAG "yetty"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Keyboard/scroll modifier bits, matching the GLFW-derived values used
 * across yui/yetty (no shared header declares them yet). The pinch
 * gesture synthesises a Ctrl+scroll so it lands on the existing visual
 * zoom path. */
#define YETTY_MOD_CONTROL 0x0002

/* Pinch sensitivity: zoom-scale "wheel notches" produced per pixel of
 * change in the finger-to-finger distance. The desktop Ctrl+wheel path
 * multiplies notches by 0.1 to get the scale delta, so each pixel of
 * spread contributes ~0.002 to the visual zoom scale. Tunable. */
#define YETTY_ANDROID_PINCH_NOTCH_PER_PX 0.02f

/* Forward declarations */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_config_dir(void);
WGPUSurface yetty_yplatform_create_surface_from_window(WGPUInstance instance,
                                                       ANativeWindow *window);

/* App state */
struct yetty_yplatform_app_state {
    struct android_app *app;
    ANativeWindow *window;
    struct yetty_yetty_yetty *yetty;
    /* Owned by us; outlives yetty. Built in init_yetty from a synthetic
     * yinit_runtime and torn down after yetty_destroy. */
    struct yetty_yframework *yframework;
    struct yetty_ycore_xthread_event_pipe *pipe;
    struct yetty_yconfig_config *config;
    struct yetty_yplatform_pty_factory *pty_factory;
    WGPUInstance instance;
    WGPUSurface surface;
    pthread_t render_thread;
    int running;
    int initialized;
    /* Two-finger pinch-to-zoom state. While pinch_active is set, pointer
     * motion drives the visual magnify (Ctrl+wheel equivalent) instead of
     * being forwarded as a mouse drag/selection. pinch_last_dist is the
     * previous finger-to-finger distance in pixels. */
    int pinch_active;
    float pinch_last_dist;
};

/* Render thread args */
struct yetty_yplatform_render_thread_args {
    struct yetty_yetty_yetty *yetty;
    int *running;
};

/* Pop / dismiss the soft IME via JNI to InputMethodManager. The
 * NDK ANativeActivity_showSoftInput shim is silently ignored on EMUI
 * and several Android 13+ skins, so we drive it directly. The pattern:
 *   imm = activity.getSystemService(INPUT_METHOD_SERVICE)
 *   decor = activity.getWindow().getDecorView()
 *   decor.setFocusable(true); decor.setFocusableInTouchMode(true);
 *   decor.requestFocus()
 *   imm.showSoftInput(decor, 0)   // or hideSoftInputFromWindow(...)
 *
 * Both flavors share the boilerplate of attaching this thread to the
 * Java VM and resolving the chain of method IDs, so factor it out. */
static void ime_call(struct android_app *app, int show)
{
    ANativeActivity *activity = app->activity;
    JavaVM *vm = activity->vm;
    JNIEnv *env = NULL;
    int needed_attach = 0;

    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            LOGE("ime_call: AttachCurrentThread failed");
            return;
        }
        needed_attach = 1;
    }

    /* We call this from the native_app_glue thread (NOT the UI thread).
     * View methods like setFocusable/requestFocus are NOT thread-safe and
     * throw CalledFromWrongThreadException. InputMethodManager methods,
     * however, are pure IPC stubs and work from any thread.
     *
     * Use toggleSoftInput() to show — it doesn't need a focused View
     * and does the right thing for NativeActivity. To hide we still need
     * a window token, which getWindowToken() does NOT touch the view
     * tree (it just returns a Binder), so it's safe from this thread. */
    jclass actCls = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID midGetSysSvc = (*env)->GetMethodID(env, actCls, "getSystemService",
                                                 "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring jsImm = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, activity->clazz, midGetSysSvc, jsImm);
    (*env)->DeleteLocalRef(env, jsImm);

    jclass immCls = (*env)->GetObjectClass(env, imm);

    if (show) {
        /* toggleSoftInput(showFlags=SHOW_FORCED=2, hideFlags=0). Despite
         * the name, when the IME is currently hidden this just shows it. */
        jmethodID midToggle = (*env)->GetMethodID(env, immCls, "toggleSoftInput", "(II)V");
        (*env)->CallVoidMethod(env, imm, midToggle, 2 /* SHOW_FORCED */, 0);
    } else {
        jmethodID midGetWindow =
            (*env)->GetMethodID(env, actCls, "getWindow", "()Landroid/view/Window;");
        jobject window = (*env)->CallObjectMethod(env, activity->clazz, midGetWindow);
        jclass winCls = (*env)->GetObjectClass(env, window);
        jmethodID midGetDecor =
            (*env)->GetMethodID(env, winCls, "getDecorView", "()Landroid/view/View;");
        jobject decor = (*env)->CallObjectMethod(env, window, midGetDecor);
        jclass viewCls = (*env)->GetObjectClass(env, decor);
        jmethodID midGetWindowToken =
            (*env)->GetMethodID(env, viewCls, "getWindowToken", "()Landroid/os/IBinder;");
        jobject token = (*env)->CallObjectMethod(env, decor, midGetWindowToken);
        if (token) {
            jmethodID midHide = (*env)->GetMethodID(env, immCls, "hideSoftInputFromWindow",
                                                    "(Landroid/os/IBinder;I)Z");
            (*env)->CallBooleanMethod(env, imm, midHide, token, 0);
            (*env)->DeleteLocalRef(env, token);
        }
        (*env)->DeleteLocalRef(env, viewCls);
        (*env)->DeleteLocalRef(env, decor);
        (*env)->DeleteLocalRef(env, winCls);
        (*env)->DeleteLocalRef(env, window);
    }

    (*env)->DeleteLocalRef(env, immCls);
    (*env)->DeleteLocalRef(env, imm);
    (*env)->DeleteLocalRef(env, actCls);

    if (needed_attach) {
        (*vm)->DetachCurrentThread(vm);
    }
}

static inline void show_soft_keyboard(struct android_app *app)
{
    ime_call(app, 1);
}
static inline void hide_soft_keyboard(struct android_app *app)
{
    ime_call(app, 0);
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    size_t len;
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

YETTY_EXTERNAL_CALLBACK
static void *render_thread_func(void *arg)
{
    struct yetty_yplatform_render_thread_args *args = arg;
    yetty_run(args->yetty);
    *(args->running) = 0;
    free(args);
    return NULL;
}

YETTY_EXTERNAL_CALLBACK
static void init_yetty(struct yetty_yplatform_app_state *state)
{
    const char *cache_dir;
    const char *runtime_dir;
    struct yetty_yconfig_paths paths;
    struct yetty_yconfig_result config_result;
    struct yetty_yplatform_input_pipe_result pipe_result;
    struct yetty_yplatform_pty_factory_ptr_result pty_result;
    struct yetty_yinit_runtime yinit_rt;
    struct yetty_yetty_yetty_result yetty_result;
    struct yetty_yplatform_render_thread_args *args;
    int32_t width, height;

    if (state->initialized || !state->window) {
        return;
    }

    LOGI("Initializing yetty...");

    cache_dir = yetty_yplatform_get_cache_dir();
    runtime_dir = yetty_yplatform_get_runtime_dir();

    mkdir_p(cache_dir);
    mkdir_p(runtime_dir);
    /* extract-assets writes to data_dir/{shaders,fonts,yemu,...} and
     * config_dir/temu — make sure both roots exist. Default Android
     * only creates /data/data/<pkg>/files, not /files/data. */
    {
        const char *data_dir = yetty_yplatform_get_data_dir();
        const char *config_dir = yetty_yplatform_get_config_dir();
        mkdir_p(data_dir);
        mkdir_p(config_dir);

        /* Match glfw-main.c: shaders/fonts live under <data_dir>/{shaders,fonts}
         * which is exactly where extract-assets puts them. */
        static char shaders_dir[512];
        static char fonts_dir[512];
        snprintf(shaders_dir, sizeof(shaders_dir), "%s/shaders", data_dir);
        snprintf(fonts_dir, sizeof(fonts_dir), "%s/fonts", data_dir);

        paths.shaders_dir = shaders_dir;
        paths.fonts_dir = fonts_dir;
        paths.config_dir = config_dir;
    }
    paths.runtime_dir = runtime_dir;
    paths.bin_dir = NULL;

    /* Extract embedded assets (kernel, opensbi, alpine rootfs, qemu binary,
     * cdb fonts, config.yaml...) onto disk where tinyemu / qemu / fontloader
     * / config-loader can read them. Must run BEFORE yetty_yconfig_create
     * so the bundled config.yaml is on disk on first launch. Without this,
     * tinyemu_pty_create() also fails to open kernel-riscv64.bin and the
     * process silently exits because ytrace logs go to stderr, which
     * Android's NativeActivity routes to /dev/null. */
    {
        struct yetty_ycore_void_result extract_result = yetty_platform_extract_assets();
        if (!YETTY_IS_OK(extract_result)) {
            LOGE("Failed to extract assets: %s",
                 extract_result.error.msg ? extract_result.error.msg : "(no message)");
            /* Fatal — without assets nothing will work. */
            return;
        }
        LOGI("Assets extracted to runtime dir");
    }

    /* Config — default to --qemu on Android (spawns external qemu loaded
     * from nativeLibraryDir/libqemu-system-riscv64.so, then telnets to
     * 127.0.0.1:QEMU_TELNET_PORT, same as desktop --qemu). There's no
     * shell command line on Android, so synthesize one. */
    {
        char *fake_argv[] = {(char *)"yetty", (char *)"--qemu", NULL};
        int fake_argc = 2;
        config_result = yetty_yconfig_create(fake_argc, fake_argv, &paths);
    }
    if (!YETTY_IS_OK(config_result)) {
        LOGE("Failed to create config");
        return;
    }
    state->config = config_result.value;

    /* Platform input pipe */
    pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        LOGE("Failed to create input pipe");
        return;
    }
    state->pipe = pipe_result.value;

    /* PTY factory */
    pty_result = yetty_yplatform_pty_factory_create(state->config, NULL);
    if (!YETTY_IS_OK(pty_result)) {
        LOGE("Failed to create PTY factory");
        return;
    }
    state->pty_factory = pty_result.value;

    /* WebGPU instance.
     * TimedWaitAny is required by yplatform/webgpu/default.c — GPU futures
     * are waited on with non-zero timeoutNS from worker threads. */
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    state->instance = wgpuCreateInstance(&instance_desc);
    if (!state->instance) {
        LOGE("Failed to create WebGPU instance");
        return;
    }

    /* Surface */
    state->surface = yetty_yplatform_create_surface_from_window(state->instance, state->window);
    if (!state->surface) {
        LOGE("Failed to create surface");
        return;
    }

    /* Get window size */
    width = ANativeWindow_getWidth(state->window);
    height = ANativeWindow_getHeight(state->window);

    /* Assemble a yinit_runtime in-place so yframework_create can take the
     * same code path the desktop worker uses. Android doesn't go through
     * yetty_yinit_run — the NDK drives the OS loop and we bootstrap here
     * inline — so the struct gets stamped by hand. No argv/output_pipe/
     * clipboard/window_manager on Android. */
    memset(&yinit_rt, 0, sizeof(yinit_rt));
    yinit_rt.config = state->config;
    yinit_rt.instance = state->instance;
    yinit_rt.surface = state->surface;
    yinit_rt.surface_width = (uint32_t)width;
    yinit_rt.surface_height = (uint32_t)height;
    yinit_rt.content_scale = 1.0f;
    yinit_rt.platform_input_pipe = state->pipe;

    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(&yinit_rt);
    if (!YETTY_IS_OK(yrt_res)) {
        LOGE("Failed to create yframework: %s",
             yrt_res.error.msg ? yrt_res.error.msg : "(no message)");
        yetty_ycore_error_destroy(yrt_res.error);
        return;
    }
    state->yframework = yrt_res.value;

    yetty_result = yetty_create(state->yframework, state->pty_factory);
    if (!YETTY_IS_OK(yetty_result)) {
        LOGE("Failed to create Yetty: %s",
             yetty_result.error.msg ? yetty_result.error.msg : "(no message)");
        yetty_yframework_destroy(state->yframework);
        state->yframework = NULL;
        return;
    }
    state->yetty = yetty_result.value;
    state->initialized = 1;
    state->running = 1;

    /* Start render thread */
    args = malloc(sizeof(struct yetty_yplatform_render_thread_args));
    args->yetty = state->yetty;
    args->running = &state->running;

    //TODO: use the platform abstraction yetty_yplatform_pthread_create() so that we can try to unify the main functions
    pthread_create(&state->render_thread, NULL, render_thread_func, args);

    /* Initial RESIZE event so the terminal/kernel get the real surface
     * size at startup (otherwise they stick at the libvterm default 80x24
     * until the user manually rotates the screen). Mirrors glfw-main.c. */
    {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_RESIZE;
        ev.resize.width = (float)width;
        ev.resize.height = (float)height;
        state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
        LOGI("Initial RESIZE posted: %dx%d", width, height);
    }

    /* Pop the soft IME up at launch — there's no other input method on
     * a phone. We re-show on tap (handle_input below) so the user can
     * dismiss with Back and recover with a tap. */
    show_soft_keyboard(state->app);

    LOGI("Yetty initialized successfully");
}

static struct yetty_ycore_void_result term_yetty(struct yetty_yplatform_app_state *state)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!state->initialized) {
        return YETTY_OK_VOID();
    }

    LOGI("Terminating yetty...");

    state->running = 0;
    if (state->render_thread) {
        pthread_join(state->render_thread, NULL);
        state->render_thread = 0;
    }

    if (state->yetty) {
        struct yetty_ycore_void_result r = yetty_destroy(state->yetty);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
        state->yetty = NULL;
    }
    if (state->yframework) {
        struct yetty_ycore_void_result r = yetty_yframework_destroy(state->yframework);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        state->yframework = NULL;
        /* yframework_destroy already unconfigured + released the surface +
         * instance. Null them here so the legacy fallbacks below skip. */
        state->surface = NULL;
        state->instance = NULL;
    }
    if (state->surface) {
        wgpuSurfaceRelease(state->surface);
        state->surface = NULL;
    }
    if (state->instance) {
        wgpuInstanceRelease(state->instance);
        state->instance = NULL;
    }
    if (state->pty_factory) {
        state->pty_factory->ops->destroy(state->pty_factory);
        state->pty_factory = NULL;
    }
    if (state->pipe) {
        state->pipe->ops->destroy(state->pipe);
        state->pipe = NULL;
    }
    if (state->config) {
        state->config->ops->destroy(state->config);
        state->config = NULL;
    }

    state->initialized = 0;

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "term_yetty: yetty destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

/* Android AKEYCODE_* -> GLFW key code (the codes yetty's event handlers
 * speak; see src/yetty/yplatform/webasm/main.c:dom_key_to_glfw). 0 means
 * no GLFW equivalent — we don't synthesize a key event in that case. */
static int akeycode_to_glfw(int akey)
{
    /* AKEYCODE_A=29 .. AKEYCODE_Z=54 -> GLFW 65..90 */
    if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z) {
        return 65 + (akey - AKEYCODE_A);
    }
    /* AKEYCODE_0=7 .. AKEYCODE_9=16 -> GLFW 48..57 */
    if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9) {
        return 48 + (akey - AKEYCODE_0);
    }
    /* F1=131..F12=142 -> GLFW 290..301 */
    if (akey >= AKEYCODE_F1 && akey <= AKEYCODE_F12) {
        return 290 + (akey - AKEYCODE_F1);
    }

    switch (akey) {
    case AKEYCODE_ENTER:
        return 257;
    case AKEYCODE_NUMPAD_ENTER:
        return 335;
    case AKEYCODE_ESCAPE:
        return 256;
    case AKEYCODE_TAB:
        return 258;
    case AKEYCODE_DEL:
        return 259; /* Android DEL = backspace */
    case AKEYCODE_FORWARD_DEL:
        return 261; /* Android FORWARD_DEL = delete */
    case AKEYCODE_INSERT:
        return 260;
    case AKEYCODE_DPAD_RIGHT:
        return 262;
    case AKEYCODE_DPAD_LEFT:
        return 263;
    case AKEYCODE_DPAD_DOWN:
        return 264;
    case AKEYCODE_DPAD_UP:
        return 265;
    case AKEYCODE_PAGE_UP:
        return 266;
    case AKEYCODE_PAGE_DOWN:
        return 267;
    case AKEYCODE_MOVE_HOME:
        return 268;
    case AKEYCODE_MOVE_END:
        return 269;
    case AKEYCODE_CAPS_LOCK:
        return 280;
    case AKEYCODE_SCROLL_LOCK:
        return 281;
    case AKEYCODE_NUM_LOCK:
        return 282;
    case AKEYCODE_SYSRQ:
        return 283; /* PrintScreen */
    case AKEYCODE_BREAK:
        return 284; /* Pause */
    case AKEYCODE_SPACE:
        return 32;
    case AKEYCODE_MINUS:
        return 45;
    case AKEYCODE_EQUALS:
        return 61;
    case AKEYCODE_LEFT_BRACKET:
        return 91;
    case AKEYCODE_RIGHT_BRACKET:
        return 93;
    case AKEYCODE_BACKSLASH:
        return 92;
    case AKEYCODE_SEMICOLON:
        return 59;
    case AKEYCODE_APOSTROPHE:
        return 39;
    case AKEYCODE_GRAVE:
        return 96;
    case AKEYCODE_COMMA:
        return 44;
    case AKEYCODE_PERIOD:
        return 46;
    case AKEYCODE_SLASH:
        return 47;
    case AKEYCODE_SHIFT_LEFT:
        return 340;
    case AKEYCODE_SHIFT_RIGHT:
        return 344;
    case AKEYCODE_CTRL_LEFT:
        return 341;
    case AKEYCODE_CTRL_RIGHT:
        return 345;
    case AKEYCODE_ALT_LEFT:
        return 342;
    case AKEYCODE_ALT_RIGHT:
        return 346;
    case AKEYCODE_META_LEFT:
        return 343;
    case AKEYCODE_META_RIGHT:
        return 347;
    default:
        return 0;
    }
}

/* AMeta state -> GLFW mod mask (shift=0x1, ctrl=0x2, alt=0x4, super=0x8). */
static int ameta_to_glfw_mods(int32_t meta)
{
    int mods = 0;
    if (meta & AMETA_SHIFT_ON) {
        mods |= 0x0001;
    }
    if (meta & AMETA_CTRL_ON) {
        mods |= 0x0002;
    }
    if (meta & AMETA_ALT_ON) {
        mods |= 0x0004;
    }
    if (meta & AMETA_META_ON) {
        mods |= 0x0008;
    }
    return mods;
}

/* AKEYCODE -> US-layout printable ASCII (shift-aware). The NDK doesn't
 * expose AKeyEvent_getUnicodeChar() — getting the real char requires a
 * JNI call into KeyCharacterMap. For the terminal use case, US-layout
 * ASCII covers virtually all inbound typing; IME composition would need
 * a JNI bridge added later. Returns 0 if no printable mapping. */
static uint32_t akey_to_ascii(int akey, int32_t meta)
{
    int shift = (meta & AMETA_SHIFT_ON) != 0;
    int caps = (meta & AMETA_CAPS_LOCK_ON) != 0;
    int upper = shift ^ caps;

    if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z) {
        char c = 'a' + (akey - AKEYCODE_A);
        return (uint32_t)(upper ? (c - 32) : c);
    }
    if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9) {
        if (!shift) {
            return (uint32_t)('0' + (akey - AKEYCODE_0));
        }
        /* Shift+digit: US layout shifted symbols. */
        static const char shifted[] = ")!@#$%^&*(";
        return (uint32_t)shifted[akey - AKEYCODE_0];
    }
    switch (akey) {
    case AKEYCODE_SPACE:
        return ' ';
    case AKEYCODE_MINUS:
        return shift ? '_' : '-';
    case AKEYCODE_EQUALS:
        return shift ? '+' : '=';
    case AKEYCODE_LEFT_BRACKET:
        return shift ? '{' : '[';
    case AKEYCODE_RIGHT_BRACKET:
        return shift ? '}' : ']';
    case AKEYCODE_BACKSLASH:
        return shift ? '|' : '\\';
    case AKEYCODE_SEMICOLON:
        return shift ? ':' : ';';
    case AKEYCODE_APOSTROPHE:
        return shift ? '"' : '\'';
    case AKEYCODE_GRAVE:
        return shift ? '~' : '`';
    case AKEYCODE_COMMA:
        return shift ? '<' : ',';
    case AKEYCODE_PERIOD:
        return shift ? '>' : '.';
    case AKEYCODE_SLASH:
        return shift ? '?' : '/';
    case AKEYCODE_TAB:
        return '\t';
    case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER:
        return '\r';
    default:
        return 0;
    }
}

/* Euclidean distance between the first two active pointers. */
static float pinch_distance(const AInputEvent *event)
{
    float x0 = AMotionEvent_getX(event, 0);
    float y0 = AMotionEvent_getY(event, 0);
    float x1 = AMotionEvent_getX(event, 1);
    float y1 = AMotionEvent_getY(event, 1);
    float dx = x1 - x0;
    float dy = y1 - y0;
    return sqrtf(dx * dx + dy * dy);
}

/* Midpoint between the first two active pointers — used as the zoom
 * anchor so the content under the pinch centre stays put. */
static void pinch_center(const AInputEvent *event, float *center_x, float *center_y)
{
    *center_x = (AMotionEvent_getX(event, 0) + AMotionEvent_getX(event, 1)) * 0.5f;
    *center_y = (AMotionEvent_getY(event, 0) + AMotionEvent_getY(event, 1)) * 0.5f;
}

static int32_t handle_input(struct android_app *app, AInputEvent *event)
{
    struct yetty_yplatform_app_state *state = app->userData;
    int32_t type;
    int32_t action;
    float x, y;
    struct yetty_yui_event ev = {0};

    if (!state->pipe) {
        return 0;
    }

    type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t akey = AKeyEvent_getKeyCode(event);
        int32_t kaction = AKeyEvent_getAction(event);
        int32_t meta = AKeyEvent_getMetaState(event);
        int glfw_key = akeycode_to_glfw(akey);
        int mods = ameta_to_glfw_mods(meta);

        LOGI("key event: akey=%d action=%d meta=0x%x glfw=%d mods=0x%x", akey, kaction, meta,
             glfw_key, mods);

        /* Don't swallow the BACK button — let Android handle it (close app). */
        if (akey == AKEYCODE_BACK) {
            return 0;
        }

        if (kaction == AKEY_EVENT_ACTION_DOWN) {
            if (glfw_key) {
                ev.type = YETTY_YCORE_KEY_DOWN;
                ev.key.key = glfw_key;
                ev.key.mods = mods;
                ev.key.scancode = AKeyEvent_getScanCode(event);
                state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
            }
            /* Send CHAR for the typed printable codepoint. Use a US-layout
             * ASCII mapping; full unicode + IME would need JNI to
             * KeyCharacterMap, which isn't exposed via the NDK input.h. */
            uint32_t unicode = akey_to_ascii(akey, meta);
            if (unicode > 0) {
                struct yetty_yui_event chev = {0};
                chev.type = YETTY_YCORE_CHAR;
                chev.chr.codepoint = unicode;
                chev.chr.mods = mods;
                state->pipe->ops->write(state->pipe, &chev, sizeof(chev));
            }
            return glfw_key ? 1 : 0;
        }
        if (kaction == AKEY_EVENT_ACTION_UP) {
            if (glfw_key) {
                ev.type = YETTY_YCORE_KEY_UP;
                ev.key.key = glfw_key;
                ev.key.mods = mods;
                ev.key.scancode = AKeyEvent_getScanCode(event);
                state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
                return 1;
            }
        }
        return 0;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t pointer_count = AMotionEvent_getPointerCount(event);
        action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;

        /* Pinch-to-zoom: two fingers spreading apart / together magnify the
         * view non-intrusively — the touch equivalent of Ctrl+wheel on the
         * desktop. We synthesise a Ctrl+MOUSE_SCROLL so it reuses the exact
         * visual-zoom path in yetty_event_handler (single source of truth
         * for sensitivity and anchor math). Once a second finger lands we
         * stay in pinch mode until every finger lifts, swallowing the
         * single-finger motion that remains so the terminal doesn't treat
         * it as a drag/selection. */
        if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || state->pinch_active) {
            switch (action) {
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                if (pointer_count >= 2) {
                    /* The first finger already emitted a MOUSE_DOWN; since
                     * we'll swallow the matching UP while pinching, release
                     * it now so the terminal's button state stays balanced
                     * (and any nascent selection is cancelled). */
                    ev.type = YETTY_YCORE_MOUSE_UP;
                    ev.mouse.x = AMotionEvent_getX(event, 0);
                    ev.mouse.y = AMotionEvent_getY(event, 0);
                    ev.mouse.button = 0;
                    ev.mouse.mods = 0;
                    state->pipe->ops->write(state->pipe, &ev, sizeof(ev));

                    state->pinch_active = 1;
                    state->pinch_last_dist = pinch_distance(event);
                }
                return 1;
            case AMOTION_EVENT_ACTION_MOVE:
                if (pointer_count >= 2 && state->pinch_last_dist > 0.0f) {
                    float dist = pinch_distance(event);
                    float center_x, center_y;
                    pinch_center(event, &center_x, &center_y);
                    ev.type = YETTY_YCORE_MOUSE_SCROLL;
                    ev.mouse_scroll.x = center_x;
                    ev.mouse_scroll.y = center_y;
                    ev.mouse_scroll.dx = 0.0f;
                    ev.mouse_scroll.dy =
                        (dist - state->pinch_last_dist) * YETTY_ANDROID_PINCH_NOTCH_PER_PX;
                    ev.mouse_scroll.mods = YETTY_MOD_CONTROL;
                    state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
                    state->pinch_last_dist = dist;
                }
                return 1;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                /* A finger lifted. The leaving pointer is still counted in
                 * this event; if three or more were down, re-baseline from
                 * the remaining pair, otherwise wait for the final UP. */
                if (pointer_count >= 3) {
                    state->pinch_last_dist = pinch_distance(event);
                }
                return 1;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                state->pinch_active = 0;
                state->pinch_last_dist = 0.0f;
                return 1;
            default:
                return 1;
            }
        }

        x = AMotionEvent_getX(event, 0);
        y = AMotionEvent_getY(event, 0);

        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            /* Re-show the soft keyboard on tap — gives the user a way
             * back after dismissing it with the Back button. */
            show_soft_keyboard(app);
            ev.type = YETTY_YCORE_MOUSE_DOWN;
            ev.mouse.x = x;
            ev.mouse.y = y;
            ev.mouse.button = 0;
            ev.mouse.mods = 0;
            state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
            return 1;
        case AMOTION_EVENT_ACTION_MOVE:
            ev.type = YETTY_YCORE_MOUSE_MOVE;
            ev.mouse.x = x;
            ev.mouse.y = y;
            state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
            return 1;
        case AMOTION_EVENT_ACTION_UP:
            ev.type = YETTY_YCORE_MOUSE_UP;
            ev.mouse.x = x;
            ev.mouse.y = y;
            ev.mouse.button = 0;
            ev.mouse.mods = 0;
            state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
            return 1;
        }
    }

    return 0;
}

YETTY_EXTERNAL_CALLBACK
static void handle_cmd(struct android_app *app, int32_t cmd)
{
    struct yetty_yplatform_app_state *state = app->userData;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            state->window = app->window;
            init_yetty(state);
        }
        break;

    case APP_CMD_TERM_WINDOW:
        term_yetty(state);
        state->window = NULL;
        break;

    case APP_CMD_WINDOW_RESIZED:
        if (state->pipe && state->window) {
            int32_t w = ANativeWindow_getWidth(state->window);
            int32_t h = ANativeWindow_getHeight(state->window);
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_RESIZE;
            ev.resize.width = (float)w;
            ev.resize.height = (float)h;
            state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
        }
        break;

    case APP_CMD_DESTROY:
        term_yetty(state);
        break;
    }
}

/* Pipe stdout+stderr (ytrace, fprintf, perror, ...) into logcat so we
 * can see them in `adb logcat`. Without this, ytrace's output goes to
 * /dev/null because Android's NativeActivity does not connect stdout/
 * stderr to anything. Adapted from the standard Android NDK pattern. */
static void *yetty_stdio_logger(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[1024];
    size_t len = 0;

    while (1) {
        ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0) {
            break;
        }
        len += (size_t)n;
        buf[len] = '\0';
        char *line_start = buf;
        char *nl;
        while ((nl = strchr(line_start, '\n')) != NULL) {
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, "yetty.stdio", line_start);
            line_start = nl + 1;
        }
        len = strlen(line_start);
        if (line_start != buf) {
            memmove(buf, line_start, len + 1);
        }
        if (len == sizeof(buf) - 1) {
            __android_log_write(ANDROID_LOG_INFO, "yetty.stdio", buf);
            len = 0;
        }
    }
    return NULL;
}

static void redirect_stdio_to_logcat(void)
{
    int pipefd[2];
    pthread_t tid;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(pipefd) != 0) {
        return;
    }
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    pthread_create(&tid, NULL, yetty_stdio_logger, (void *)(intptr_t)pipefd[0]);
    pthread_detach(tid);
}

YETTY_EXTERNAL_CALLBACK
void android_main(struct android_app *app)
{
    struct yetty_yplatform_app_state state = {0};

    state.app = app;
    app->userData = &state;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    redirect_stdio_to_logcat();
    /* Make ytrace's debug/info/warn/error messages visible in logcat by
     * default. They route to stderr which our redirector pipes to the
     * `yetty.stdio` log tag. */
    //setenv("YTRACE_DEFAULT_ON", "yes", 1);

    LOGI("Android main started");

    while (1) {
        int events;
        struct android_poll_source *source;

        while (ALooper_pollAll(state.running ? 0 : -1, NULL, &events, (void **)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested) {
                LOGI("Destroy requested");
                term_yetty(&state);
                return;
            }
        }
    }
}
