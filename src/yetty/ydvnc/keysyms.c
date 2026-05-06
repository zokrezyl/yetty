/*
 * keysyms.c — yetty key/char → X11 keysym translation.
 *
 * GLFW key constants (the values yetty's input layer reports) are listed in
 * GLFW/glfw3.h. The X11 keysym numerical constants come from X.org's
 * keysymdef.h. Both are public protocol identifiers.
 */

#include "keysyms.h"

#include <stdint.h>

#include <yetty/ycore/types.h>

/*=============================================================================
 * GLFW key constants we care about (mirrored locally so we don't have to
 * pull in glfw.h here — those are fixed protocol numbers anyway).
 *===========================================================================*/

#define KEY_SPACE              32
#define KEY_APOSTROPHE         39
#define KEY_COMMA              44
#define KEY_MINUS              45
#define KEY_PERIOD             46
#define KEY_SLASH              47
#define KEY_0                  48
#define KEY_9                  57
#define KEY_SEMICOLON          59
#define KEY_EQUAL              61
#define KEY_A                  65
#define KEY_Z                  90
#define KEY_LEFT_BRACKET       91
#define KEY_BACKSLASH          92
#define KEY_RIGHT_BRACKET      93
#define KEY_GRAVE_ACCENT       96
#define KEY_ESCAPE            256
#define KEY_ENTER             257
#define KEY_TAB               258
#define KEY_BACKSPACE         259
#define KEY_INSERT            260
#define KEY_DELETE            261
#define KEY_RIGHT             262
#define KEY_LEFT              263
#define KEY_DOWN              264
#define KEY_UP                265
#define KEY_PAGE_UP           266
#define KEY_PAGE_DOWN         267
#define KEY_HOME              268
#define KEY_END               269
#define KEY_CAPS_LOCK         280
#define KEY_SCROLL_LOCK       281
#define KEY_NUM_LOCK          282
#define KEY_PRINT_SCREEN      283
#define KEY_PAUSE             284
#define KEY_F1                290
#define KEY_F25               314
#define KEY_KP_0              320
#define KEY_KP_9              329
#define KEY_KP_DECIMAL        330
#define KEY_KP_DIVIDE         331
#define KEY_KP_MULTIPLY       332
#define KEY_KP_SUBTRACT       333
#define KEY_KP_ADD            334
#define KEY_KP_ENTER          335
#define KEY_KP_EQUAL          336
#define KEY_LEFT_SHIFT        340
#define KEY_LEFT_CONTROL      341
#define KEY_LEFT_ALT          342
#define KEY_LEFT_SUPER        343
#define KEY_RIGHT_SHIFT       344
#define KEY_RIGHT_CONTROL     345
#define KEY_RIGHT_ALT         346
#define KEY_RIGHT_SUPER       347
#define KEY_MENU              348

/*=============================================================================
 * X11 keysyms (from keysymdef.h — public protocol identifiers).
 *===========================================================================*/

#define XK_BACKSPACE     0xFF08
#define XK_TAB           0xFF09
#define XK_RETURN        0xFF0D
#define XK_ESCAPE        0xFF1B
#define XK_DELETE        0xFFFF
#define XK_HOME          0xFF50
#define XK_LEFT          0xFF51
#define XK_UP            0xFF52
#define XK_RIGHT         0xFF53
#define XK_DOWN          0xFF54
#define XK_PAGE_UP       0xFF55
#define XK_PAGE_DOWN     0xFF56
#define XK_END           0xFF57
#define XK_INSERT        0xFF63
#define XK_MENU          0xFF67
#define XK_NUM_LOCK      0xFF7F
#define XK_KP_ENTER      0xFF8D
#define XK_KP_EQUAL      0xFFBD
#define XK_KP_MULTIPLY   0xFFAA
#define XK_KP_ADD        0xFFAB
#define XK_KP_SUBTRACT   0xFFAD
#define XK_KP_DECIMAL    0xFFAE
#define XK_KP_DIVIDE     0xFFAF
#define XK_KP_0          0xFFB0
#define XK_F1            0xFFBE
#define XK_F25           0xFFD6
#define XK_SHIFT_L       0xFFE1
#define XK_SHIFT_R       0xFFE2
#define XK_CONTROL_L     0xFFE3
#define XK_CONTROL_R     0xFFE4
#define XK_CAPS_LOCK     0xFFE5
#define XK_ALT_L         0xFFE9
#define XK_ALT_R         0xFFEA
#define XK_SUPER_L       0xFFEB
#define XK_SUPER_R       0xFFEC
#define XK_SCROLL_LOCK   0xFF14
#define XK_PRINT         0xFF61
#define XK_PAUSE         0xFF13

uint32_t yetty_ydvnc_keysym_from_glfw_key(int glfw_key)
{
    /* F-keys: contiguous in both spaces. */
    if (glfw_key >= KEY_F1 && glfw_key <= KEY_F25) {
        return (uint32_t)(XK_F1 + (glfw_key - KEY_F1));
    }
    /* Keypad digits: contiguous in both spaces. */
    if (glfw_key >= KEY_KP_0 && glfw_key <= KEY_KP_9) {
        return (uint32_t)(XK_KP_0 + (glfw_key - KEY_KP_0));
    }

    switch (glfw_key) {
    case KEY_BACKSPACE:    return XK_BACKSPACE;
    case KEY_TAB:          return XK_TAB;
    case KEY_ENTER:        return XK_RETURN;
    case KEY_ESCAPE:       return XK_ESCAPE;
    case KEY_DELETE:       return XK_DELETE;
    case KEY_HOME:         return XK_HOME;
    case KEY_LEFT:         return XK_LEFT;
    case KEY_UP:           return XK_UP;
    case KEY_RIGHT:        return XK_RIGHT;
    case KEY_DOWN:         return XK_DOWN;
    case KEY_PAGE_UP:      return XK_PAGE_UP;
    case KEY_PAGE_DOWN:    return XK_PAGE_DOWN;
    case KEY_END:          return XK_END;
    case KEY_INSERT:       return XK_INSERT;
    case KEY_MENU:         return XK_MENU;
    case KEY_NUM_LOCK:     return XK_NUM_LOCK;
    case KEY_CAPS_LOCK:    return XK_CAPS_LOCK;
    case KEY_SCROLL_LOCK:  return XK_SCROLL_LOCK;
    case KEY_PRINT_SCREEN: return XK_PRINT;
    case KEY_PAUSE:        return XK_PAUSE;
    case KEY_KP_DECIMAL:   return XK_KP_DECIMAL;
    case KEY_KP_DIVIDE:    return XK_KP_DIVIDE;
    case KEY_KP_MULTIPLY:  return XK_KP_MULTIPLY;
    case KEY_KP_SUBTRACT:  return XK_KP_SUBTRACT;
    case KEY_KP_ADD:       return XK_KP_ADD;
    case KEY_KP_ENTER:     return XK_KP_ENTER;
    case KEY_KP_EQUAL:     return XK_KP_EQUAL;
    case KEY_LEFT_SHIFT:   return XK_SHIFT_L;
    case KEY_RIGHT_SHIFT:  return XK_SHIFT_R;
    case KEY_LEFT_CONTROL: return XK_CONTROL_L;
    case KEY_RIGHT_CONTROL:return XK_CONTROL_R;
    case KEY_LEFT_ALT:     return XK_ALT_L;
    case KEY_RIGHT_ALT:    return XK_ALT_R;
    case KEY_LEFT_SUPER:   return XK_SUPER_L;
    case KEY_RIGHT_SUPER:  return XK_SUPER_R;
    default:               return 0;
    }
}

uint32_t yetty_ydvnc_keysym_from_codepoint(uint32_t codepoint)
{
    if (codepoint >= 0x20 && codepoint <= 0x7e) {
        return codepoint;
    }
    if (codepoint == '\n' || codepoint == '\r') {
        return XK_RETURN;
    }
    if (codepoint == '\t') {
        return XK_TAB;
    }
    if (codepoint == 0x08 || codepoint == 0x7f) {
        return XK_BACKSPACE;
    }
    if (codepoint == 0) {
        return 0;
    }
    return 0x01000000u | codepoint;
}

int yetty_ydvnc_keysym_is_modifier(uint32_t keysym)
{
    switch (keysym) {
    case XK_SHIFT_L:
    case XK_SHIFT_R:
    case XK_CONTROL_L:
    case XK_CONTROL_R:
    case XK_ALT_L:
    case XK_ALT_R:
    case XK_SUPER_L:
    case XK_SUPER_R:
    case XK_CAPS_LOCK:
    case XK_NUM_LOCK:
        return 1;
    default:
        return 0;
    }
}
