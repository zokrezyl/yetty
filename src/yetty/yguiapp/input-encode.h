/*
 * yguiapp/input-encode.h — shared keyboard translation helpers.
 *
 * Both hosts turn a keystroke into the byte stream ygui's input decoder
 * (yetty_ygui_framework_feed_input) understands:
 *   - the standalone host (app.c) receives platform KEY_DOWN / CHAR events,
 *   - the terminal/client host (run.c) receives the same information as
 *     structured YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY envelopes forwarded by
 *     the hosting yetty once a figure is click-focused.
 * The translation is identical, so it lives here and both include it.
 */

#ifndef YETTY_YGUIAPP_INPUT_ENCODE_H
#define YETTY_YGUIAPP_INPUT_ENCODE_H

#include <stddef.h>
#include <stdint.h>

/* UTF-8 encode a single codepoint into `out` (needs up to 4 bytes). Returns the
 * number of bytes written (0 for a control codepoint below 0x20). */
size_t yguiapp_utf8_encode(uint32_t codepoint, char *out);

/* Encode a GLFW keycode + GLFW mod bitmask into the terminal byte sequence a
 * navigation/editing key produces (ESC, CR, DEL, CSI arrows/Home/End/Delete
 * with xterm modifier params). Writes into `scratch`; sets `*out_len` and
 * returns `scratch`, or returns NULL with `*out_len == 0` for a key that
 * carries no such sequence (e.g. a printable — that arrives as a CHAR). */
const char *yguiapp_encode_key(uint32_t key, int glfw_mods, char *scratch, size_t scratch_n,
                               size_t *out_len);

#endif /* YETTY_YGUIAPP_INPUT_ENCODE_H */
