/* webasm-input.c — HTML5 input plumbing for the WebAssembly platform.
 *
 * The DOM→GLFW key/modifier mapping plus the emscripten keyboard/mouse/wheel/
 * resize callbacks, each translating a browser event into a yetty_yui_event and
 * writing it to the platform input pipe. webasm_platform (yplatform/webasm.c)
 * registers these via yetty_yplatform_webasm_setup_input_callbacks() during
 * init; the browser then drives them.
 *
 * (Lifted out of the deleted yinit/webasm-run.c bootstrap — the GPU/surface/
 * runtime assembly that used to live alongside it now lives in webasm_platform.)
 */

#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <stdlib.h>
#include <string.h>

/* Implemented in yplatform/window/webasm.c. */
void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height);
int yetty_yplatform_webasm_update_canvas_size(void);

/* Public entry: register every HTML5 input callback against `pipe`. */
void yetty_yplatform_webasm_setup_input_callbacks(struct yetty_ycore_xthread_event_pipe *pipe);

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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

YETTY_EXTERNAL_CALLBACK
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

void yetty_yplatform_webasm_setup_input_callbacks(struct yetty_ycore_xthread_event_pipe *pipe)
{
    const char *target = "#canvas";

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_key_down);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_key_up);
    emscripten_set_mousedown_callback(target, pipe, 1, on_mouse_down);
    emscripten_set_mouseup_callback(target, pipe, 1, on_mouse_up);
    emscripten_set_mousemove_callback(target, pipe, 1, on_mouse_move);
    emscripten_set_wheel_callback(target, pipe, 1, on_wheel);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, pipe, 1, on_resize);

    ydebug("yinit_run: Input callbacks registered");
}
