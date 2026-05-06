#ifndef YETTY_YDVNC_KEYSYMS_H
#define YETTY_YDVNC_KEYSYMS_H

/*
 * Translates yetty key codes (GLFW-style; produced by the platform input
 * layer) and Unicode codepoints into X11 keysyms suitable for sending in an
 * RFB KeyEvent.
 *
 * The keysym numerical values are the canonical ones from X.org's
 * keysymdef.h. They are protocol identifiers — not copyrightable — and the
 * naming is the standard X11 one.
 *
 * Layout note: GLFW reports physical-position key codes (i.e. layout-
 * independent, US-QWERTY identifiers) for KEY_DOWN/KEY_UP, plus separate
 * CHAR events for the layout-resolved Unicode codepoint. To preserve the
 * user's local layout (e.g. Dvorak), the viewer should:
 *
 *   - For modifier keys (Shift/Ctrl/Alt/Super): send the modifier keysym
 *     on press/release (use yetty_ydvnc_keysym_from_glfw_key).
 *   - For non-printable special keys (Esc, Arrow, F-keys, ...):
 *     send the keysym from the GLFW key code on press/release.
 *   - For printable keys: ignore KEY_DOWN/KEY_UP (would yield the QWERTY
 *     glyph). Instead handle CHAR events: emit press+release of the keysym
 *     derived from the Unicode codepoint via yetty_ydvnc_keysym_from_codepoint.
 *
 * On Wayland/X11 platforms with Dvorak active, this means RFB KeyEvents
 * carry the user's intended characters (XK_apostrophe rather than XK_q
 * for the Dvorak ' key).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 if no mapping exists. The caller decides whether the event
 * should still be sent based on whether 0 is acceptable. */
uint32_t yetty_ydvnc_keysym_from_glfw_key(int glfw_key);

/* Map a Unicode codepoint to an X11 keysym.
 * - ASCII printable (0x20..0x7e): keysym == codepoint.
 * - Other Unicode: keysym = 0x01000000 | codepoint
 *   (X.org "Unicode keysym" form — supported by all real RFB servers). */
uint32_t yetty_ydvnc_keysym_from_codepoint(uint32_t codepoint);

/* Predicates for input dispatch decisions in the viewer. */
int yetty_ydvnc_keysym_is_modifier(uint32_t keysym);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDVNC_KEYSYMS_H */
