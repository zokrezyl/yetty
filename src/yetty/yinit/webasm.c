/* WebAssembly main.c - Application entry point
 *
 * Single-threaded model: main() sets up everything and starts the event loop.
 * HTML5 callbacks write events to PlatformInputPipe which notifies listeners.
 */

#include <yetty/yetty/yetty.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <string.h>

/* Forward declarations for window.c and surface.c */
int yetty_yplatform_webasm_create_window(struct yetty_yconfig_config *config);
void yetty_yplatform_webasm_destroy_window(void);
void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height);
int yetty_yplatform_webasm_update_canvas_size(void);
WGPUSurface yetty_yplatform_webasm_create_surface(WGPUInstance instance);

/*=============================================================================
 * Key mapping: DOM code -> GLFW key code
 *===========================================================================*/

static int dom_key_to_glfw(const char *code, const char *key)
{
    (void)key;

    /* KeyA-KeyZ -> GLFW_KEY_A-GLFW_KEY_Z (65-90) */
    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y') {
        return 65 + (code[3] - 'A');
    }

    /* Digit0-Digit9 -> GLFW_KEY_0-GLFW_KEY_9 (48-57) */
    if (code[0] == 'D' && code[1] == 'i' && code[2] == 'g') {
        return 48 + (code[5] - '0');
    }

    /* Function keys F1-F12 */
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        int fnum = code[1] - '0';
        if (code[2] >= '0' && code[2] <= '9') {
            fnum = fnum * 10 + (code[2] - '0');
        }
        return 289 + fnum; /* GLFW_KEY_F1 = 290 */
    }

    /* Named keys */
    if (!strcmp(code, "Enter")) {
        return 257;
    }
    if (!strcmp(code, "NumpadEnter")) {
        return 335;
    }
    if (!strcmp(code, "Escape")) {
        return 256;
    }
    if (!strcmp(code, "Tab")) {
        return 258;
    }
    if (!strcmp(code, "Backspace")) {
        return 259;
    }
    if (!strcmp(code, "Insert")) {
        return 260;
    }
    if (!strcmp(code, "Delete")) {
        return 261;
    }
    if (!strcmp(code, "ArrowRight")) {
        return 262;
    }
    if (!strcmp(code, "ArrowLeft")) {
        return 263;
    }
    if (!strcmp(code, "ArrowDown")) {
        return 264;
    }
    if (!strcmp(code, "ArrowUp")) {
        return 265;
    }
    if (!strcmp(code, "PageUp")) {
        return 266;
    }
    if (!strcmp(code, "PageDown")) {
        return 267;
    }
    if (!strcmp(code, "Home")) {
        return 268;
    }
    if (!strcmp(code, "End")) {
        return 269;
    }
    if (!strcmp(code, "CapsLock")) {
        return 280;
    }
    if (!strcmp(code, "ScrollLock")) {
        return 281;
    }
    if (!strcmp(code, "NumLock")) {
        return 282;
    }
    if (!strcmp(code, "PrintScreen")) {
        return 283;
    }
    if (!strcmp(code, "Pause")) {
        return 284;
    }
    if (!strcmp(code, "Space")) {
        return 32;
    }
    if (!strcmp(code, "Minus")) {
        return 45;
    }
    if (!strcmp(code, "Equal")) {
        return 61;
    }
    if (!strcmp(code, "BracketLeft")) {
        return 91;
    }
    if (!strcmp(code, "BracketRight")) {
        return 93;
    }
    if (!strcmp(code, "Backslash")) {
        return 92;
    }
    if (!strcmp(code, "Semicolon")) {
        return 59;
    }
    if (!strcmp(code, "Quote")) {
        return 39;
    }
    if (!strcmp(code, "Backquote")) {
        return 96;
    }
    if (!strcmp(code, "Comma")) {
        return 44;
    }
    if (!strcmp(code, "Period")) {
        return 46;
    }
    if (!strcmp(code, "Slash")) {
        return 47;
    }
    if (!strcmp(code, "ShiftLeft")) {
        return 340;
    }
    if (!strcmp(code, "ShiftRight")) {
        return 344;
    }
    if (!strcmp(code, "ControlLeft")) {
        return 341;
    }
    if (!strcmp(code, "ControlRight")) {
        return 345;
    }
    if (!strcmp(code, "AltLeft")) {
        return 342;
    }
    if (!strcmp(code, "AltRight")) {
        return 346;
    }
    if (!strcmp(code, "MetaLeft")) {
        return 343;
    }
    if (!strcmp(code, "MetaRight")) {
        return 347;
    }

    return 0;
}

static int dom_mods_to_glfw(const EmscriptenKeyboardEvent *e)
{
    int mods = 0;
    if (e->shiftKey) {
        mods |= 0x0001;
    }
    if (e->ctrlKey) {
        mods |= 0x0002;
    }
    if (e->altKey) {
        mods |= 0x0004;
    }
    if (e->metaKey) {
        mods |= 0x0008;
    }
    return mods;
}

static int mouse_mods_to_glfw(const EmscriptenMouseEvent *e)
{
    int mods = 0;
    if (e->shiftKey) {
        mods |= 0x0001;
    }
    if (e->ctrlKey) {
        mods |= 0x0002;
    }
    if (e->altKey) {
        mods |= 0x0004;
    }
    if (e->metaKey) {
        mods |= 0x0008;
    }
    return mods;
}

static int wheel_mods_to_glfw(const EmscriptenWheelEvent *e)
{
    int mods = 0;
    if (e->mouse.shiftKey) {
        mods |= 0x0001;
    }
    if (e->mouse.ctrlKey) {
        mods |= 0x0002;
    }
    if (e->mouse.altKey) {
        mods |= 0x0004;
    }
    if (e->mouse.metaKey) {
        mods |= 0x0008;
    }
    return mods;
}

/*=============================================================================
 * HTML5 input callbacks
 *===========================================================================*/

static EM_BOOL on_key_down(int event_type, const EmscriptenKeyboardEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};
    int key, mods;

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    key = dom_key_to_glfw(e->code, e->key);
    mods = dom_mods_to_glfw(e);
    ydebug("on_key_down: code='%s' key='%s' glfw_key=%d mods=%d", e->code, e->key, key, mods);

    /* Printable char with Ctrl/Alt -> charInputWithMods */
    if ((mods & (0x0002 | 0x0004)) && e->key[0] && !e->key[1]) {
        uint32_t ch = (uint32_t)(unsigned char)e->key[0];
        event.type = YETTY_YCORE_CHAR;
        event.chr.codepoint = ch;
        event.chr.mods = mods;
        pipe->ops->write(pipe, &event, sizeof(event));
        ydebug("on_key_down: sent CharInputWithMods ch=%u mods=%d", ch, mods);
        return EM_TRUE;
    }

    /* Key down event */
    event.type = YETTY_YCORE_KEY_DOWN;
    event.key.key = key;
    event.key.mods = mods;
    event.key.scancode = 0;
    pipe->ops->write(pipe, &event, sizeof(event));
    ydebug("on_key_down: sent KeyDown key=%d", key);

    /* Printable char without Ctrl/Alt -> also send Char event */
    if (!(mods & (0x0002 | 0x0004)) && e->key[0] && !e->key[1]) {
        uint32_t ch = (uint32_t)(unsigned char)e->key[0];
        event.type = YETTY_YCORE_CHAR;
        event.chr.codepoint = ch;
        event.chr.mods = 0;
        pipe->ops->write(pipe, &event, sizeof(event));
        ydebug("on_key_down: sent CharInput ch=%u", ch);
    }

    return EM_TRUE;
}

static EM_BOOL on_key_up(int event_type, const EmscriptenKeyboardEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};
    int key, mods;

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    key = dom_key_to_glfw(e->code, e->key);
    mods = dom_mods_to_glfw(e);

    event.type = YETTY_YCORE_KEY_UP;
    event.key.key = key;
    event.key.mods = mods;
    event.key.scancode = 0;
    pipe->ops->write(pipe, &event, sizeof(event));

    return EM_TRUE;
}

static EM_BOOL on_mouse_down(int event_type, const EmscriptenMouseEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    event.type = YETTY_YCORE_MOUSE_DOWN;
    event.mouse.x = (float)e->targetX;
    event.mouse.y = (float)e->targetY;
    event.mouse.button = (int)e->button;
    event.mouse.mods = mouse_mods_to_glfw(e);
    pipe->ops->write(pipe, &event, sizeof(event));

    return EM_TRUE;
}

static EM_BOOL on_mouse_up(int event_type, const EmscriptenMouseEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    event.type = YETTY_YCORE_MOUSE_UP;
    event.mouse.x = (float)e->targetX;
    event.mouse.y = (float)e->targetY;
    event.mouse.button = (int)e->button;
    event.mouse.mods = mouse_mods_to_glfw(e);
    pipe->ops->write(pipe, &event, sizeof(event));

    return EM_TRUE;
}

static EM_BOOL on_mouse_move(int event_type, const EmscriptenMouseEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    event.type = YETTY_YCORE_MOUSE_MOVE;
    event.mouse.x = (float)e->targetX;
    event.mouse.y = (float)e->targetY;
    event.mouse.mods = mouse_mods_to_glfw(e);
    pipe->ops->write(pipe, &event, sizeof(event));

    return EM_FALSE;
}

static EM_BOOL on_wheel(int event_type, const EmscriptenWheelEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};
    float dx, dy;

    (void)event_type;

    if (!pipe) {
        return EM_FALSE;
    }

    dx = (float)(-e->deltaX / 100.0);
    dy = (float)(-e->deltaY / 100.0);

    event.type = YETTY_YCORE_MOUSE_SCROLL;
    event.mouse_scroll.x = (float)e->mouse.targetX;
    event.mouse_scroll.y = (float)e->mouse.targetY;
    event.mouse_scroll.dx = dx;
    event.mouse_scroll.dy = dy;
    event.mouse_scroll.mods = wheel_mods_to_glfw(e);
    pipe->ops->write(pipe, &event, sizeof(event));

    return EM_TRUE;
}

static EM_BOOL on_resize(int event_type, const EmscriptenUiEvent *e, void *user_data)
{
    struct yetty_ycore_xthread_event_pipe *pipe = user_data;
    struct yetty_yui_event event = {0};
    int width, height;

    (void)event_type;
    (void)e;

    if (!pipe) {
        return EM_FALSE;
    }

    /* First update the canvas size to match container, then read new dimensions */
    yetty_yplatform_webasm_update_canvas_size();
    yetty_yplatform_webasm_get_framebuffer_size(&width, &height);

    event.type = YETTY_YCORE_RESIZE;
    event.resize.width = (float)width;
    event.resize.height = (float)height;
    pipe->ops->write(pipe, &event, sizeof(event));

    ydebug("on_resize: Posted resize event %dx%d", width, height);
    return EM_FALSE;
}

static void setup_input_callbacks(struct yetty_ycore_xthread_event_pipe *pipe)
{
    const char *target = "#canvas";

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_key_down);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_key_up);
    emscripten_set_mousedown_callback(target, pipe, 1, on_mouse_down);
    emscripten_set_mouseup_callback(target, pipe, 1, on_mouse_up);
    emscripten_set_mousemove_callback(target, pipe, 1, on_mouse_move);
    emscripten_set_wheel_callback(target, pipe, 1, on_wheel);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_resize);

    ydebug("main: Input callbacks registered");
}

/*=============================================================================
 * main
 *===========================================================================*/

int main(int argc, char **argv)
{
    /* Force-enable all ytrace points by default on webasm so the headless
     * Chrome test (tools/test-chrome-headless.sh) and any browser-side
     * debugging see the full trace stream without env wiring on the JS
     * side. Mirrors `YTRACE_DEFAULT_ON=yes` on desktop. */
    //setenv("YTRACE_DEFAULT_ON", "yes", 1);

    struct yetty_yconfig_paths paths;
    struct yetty_yconfig_result config_result;
    struct yetty_yconfig_config *config;
    struct yetty_yplatform_input_pipe_result pipe_result;
    struct yetty_ycore_xthread_event_pipe *pipe;
    struct yetty_yplatform_pty_factory_ptr_result pty_factory_result;
    struct yetty_yplatform_pty_factory *pty_factory;
    WGPUInstance instance;
    WGPUSurface surface;
    int canvas_width, canvas_height;
    struct yetty_yinit_runtime yinit_rt;
    struct yetty_yframework *yframework;
    struct yetty_yetty_yetty_result yetty_result;
    struct yetty_yetty_yetty *yetty;
    struct yetty_yui_event event = {0};
    int fb_width, fb_height;
    struct yetty_ycore_void_result run_result;

    ydebug("main: WebASM starting");

    /* Platform paths (MEMFS). yetty_yplatform_extract_assets() decompresses
     * incbin'd brotli blobs into /data/{shaders,fonts,msdf-fonts,yemu}/ at
     * startup; runtime then reads them as ordinary files. Same model as
     * desktop, where the data dir lives on the user's disk. */
    paths.shaders_dir = "/data/shaders";
    paths.fonts_dir = "/data/fonts";
    paths.runtime_dir = "/tmp";
    paths.bin_dir = NULL;
    paths.config_dir = "/config";

    /* Export platform paths as YETTY_* env vars so config files
     * (e.g. tinyemu .cfg) can reference them via $YETTY_DATA_DIR /
     * $YETTY_RUNTIME_DIR / $YETTY_CONFIG_DIR. Mirrors glfw-main.c. */
    setenv("YETTY_SHADERS_DIR", "/data/shaders", 1);
    setenv("YETTY_FONTS_DIR", "/data/fonts", 1);
    setenv("YETTY_RUNTIME_DIR", "/tmp", 1);
    setenv("YETTY_DATA_DIR", "/data", 1);
    setenv("YETTY_CONFIG_DIR", "/config", 1);

    /* Extract incbin'd assets into MEMFS BEFORE config. Always "first time"
     * on web — MEMFS is fresh per page load, so this runs every startup.
     * Decompresses brotli'd shaders / fonts / msdf-fonts / yemu / config
     * blobs into /data/ and /config/, so yetty_yconfig_create can then read
     * the bundled config.yaml from disk. */
    {
        struct yetty_ycore_void_result extract_result = yetty_platform_extract_assets();
        if (!YETTY_IS_OK(extract_result)) {
            yerror("Failed to extract embedded assets: %s",
                   extract_result.error.msg ? extract_result.error.msg : "(no msg)");
            return 1;
        }
        ydebug("main: Embedded assets extracted to /data/");
    }

    /* Config */
    config_result = yetty_yconfig_create(argc, argv, &paths);
    if (!YETTY_IS_OK(config_result)) {
        yerror("Failed to create Config");
        return 1;
    }
    config = config_result.value;
    ydebug("main: Config created");

    /* Window (canvas) */
    if (!yetty_yplatform_webasm_create_window(config)) {
        yerror("Failed to create window");
        config->ops->destroy(config);
        return 1;
    }
    ydebug("main: Window created");

    /* PlatformInputPipe */
    pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        yerror("Failed to create PlatformInputPipe");
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    pipe = pipe_result.value;
    ydebug("main: PlatformInputPipe created");

    /* Setup HTML5 input callbacks */
    setup_input_callbacks(pipe);

    /* PtyFactory */
    pty_factory_result = yetty_yplatform_pty_factory_create(config, NULL);
    if (!YETTY_IS_OK(pty_factory_result)) {
        yerror("Failed to create PtyFactory");
        pipe->ops->destroy(pipe);
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    pty_factory = pty_factory_result.value;
    ydebug("main: PtyFactory created");

    /* WebGPU instance + surface */
    instance = wgpuCreateInstance(NULL);
    if (!instance) {
        yerror("Failed to create WebGPU instance");
        pty_factory->ops->destroy(pty_factory);
        pipe->ops->destroy(pipe);
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    ydebug("main: WebGPU instance created");

    surface = yetty_yplatform_webasm_create_surface(instance);
    if (!surface) {
        yerror("Failed to create WebGPU surface");
        wgpuInstanceRelease(instance);
        pty_factory->ops->destroy(pty_factory);
        pipe->ops->destroy(pipe);
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    ydebug("main: WebGPU surface created");

    /* Get canvas dimensions */
    canvas_width = EM_ASM_INT({
        var c = document.getElementById('canvas');
        return c ? c.width : window.innerWidth;
    });
    canvas_height = EM_ASM_INT({
        var c = document.getElementById('canvas');
        return c ? c.height : window.innerHeight;
    });

    /* Build a synthetic yinit_runtime from what we bootstrapped above,
     * then hand it to yframework_create — same code path as the desktop
     * worker. webasm doesn't go through yetty_yinit_run (Emscripten
     * drives the loop from JS) so we assemble the struct manually here.
     * No output_pipe / clipboard / window_manager on web. */
    memset(&yinit_rt, 0, sizeof(yinit_rt));
    yinit_rt.argc                = argc;
    yinit_rt.argv                = argv;
    yinit_rt.config              = config;
    yinit_rt.instance            = instance;
    yinit_rt.surface             = surface;
    yinit_rt.surface_width       = (uint32_t)canvas_width;
    yinit_rt.surface_height      = (uint32_t)canvas_height;
    yinit_rt.content_scale       = 1.0f;
    yinit_rt.platform_input_pipe = pipe;

    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(&yinit_rt);
    if (!YETTY_IS_OK(yrt_res)) {
        yerror("Failed to create yframework: %s",
               yrt_res.error.msg ? yrt_res.error.msg : "(no msg)");
        yetty_ycore_error_destroy(yrt_res.error);
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        pty_factory->ops->destroy(pty_factory);
        pipe->ops->destroy(pipe);
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    yframework = yrt_res.value;

    /* Yetty */
    yetty_result = yetty_create(yframework, pty_factory);
    if (!YETTY_IS_OK(yetty_result)) {
        yerror("Failed to create Yetty");
        yetty_yframework_destroy(yframework);
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        pty_factory->ops->destroy(pty_factory);
        pipe->ops->destroy(pipe);
        yetty_yplatform_webasm_destroy_window();
        config->ops->destroy(config);
        return 1;
    }
    yetty = yetty_result.value;
    ydebug("main: Yetty created");

    /* Initial resize event */
    yetty_yplatform_webasm_get_framebuffer_size(&fb_width, &fb_height);
    event.type = YETTY_YCORE_RESIZE;
    event.resize.width = (float)fb_width;
    event.resize.height = (float)fb_height;
    pipe->ops->write(pipe, &event, sizeof(event));
    ydebug("main: Posted initial resize %dx%d", fb_width, fb_height);

    /* Render runs on the main thread (rAF / emscripten_set_main_loop_arg).
     * yetty_run returns immediately; the JS event loop drives subsequent
     * ticks. Heavy compute (TinyEMU CPU) goes to a worker via pthread_create
     * inside the relevant platform-pty backend. */
    ydebug("main: Starting Yetty");
    run_result = yetty_run(yetty);
    if (!YETTY_IS_OK(run_result)) {
        yerror("yetty_run failed");
    }

    ydebug("main: returning (event loop continues asynchronously)");
    return 0;
}
