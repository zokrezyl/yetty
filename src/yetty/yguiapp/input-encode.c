/*
 * yguiapp/input-encode.c — shared keyboard translation helpers (see
 * input-encode.h). GPU-free plain C, compiled into yetty_yguiapp_terminal so
 * both the terminal host (run.c) and the standalone host (app.c) resolve
 * them from one place.
 */

#include "input-encode.h"

#include <stdio.h>

/* Encode a Unicode codepoint to UTF-8; returns the byte count (1..4, 0 if out
 * of range). */
size_t yguiapp_utf8_encode(uint32_t codepoint, char *out)
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* GLFW-style special-keycode → terminal byte sequence, carrying the xterm
 * modifier parameter (1 + shift|alt<<1|ctrl<<2) so Shift/Ctrl reach widgets —
 * that is what lets Shift+Arrow extend a textinput selection and Ctrl+Arrow
 * jump by word. Printable text is NOT encoded here: it arrives via
 * YETTY_YCORE_CHAR already mapped through the OS keyboard layout (correct case
 * and symbols), which the CHAR handler feeds. */
const char *yguiapp_encode_key(uint32_t key, int glfw_mods, char *scratch, size_t scratch_n,
                               size_t *out_len)
{
    int mod_bits = 0;
    if (glfw_mods & 0x0001) { /* GLFW_MOD_SHIFT */
        mod_bits |= 1;
    }
    if (glfw_mods & 0x0004) { /* GLFW_MOD_ALT */
        mod_bits |= 2;
    }
    if (glfw_mods & 0x0002) { /* GLFW_MOD_CONTROL */
        mod_bits |= 4;
    }
    int mod_param = mod_bits ? mod_bits + 1 : 0;
    switch (key) {
    case 256:
        scratch[0] = 0x1B;
        *out_len = 1;
        return scratch; /* ESC */
    case 257:
        scratch[0] = '\r';
        *out_len = 1;
        return scratch; /* Enter */
    case 259:
        scratch[0] = 0x7F;
        *out_len = 1;
        return scratch; /* Backspace */
    case 261:           /* Delete */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[3;%d~", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[3~");
        return scratch;
    case 263: /* ← */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dD", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[D");
        return scratch;
    case 262: /* → */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dC", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[C");
        return scratch;
    case 265: /* ↑ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dA", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[A");
        return scratch;
    case 264: /* ↓ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dB", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[B");
        return scratch;
    case 268: /* Home */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dH", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[H");
        return scratch;
    case 269: /* End */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dF", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[F");
        return scratch;
    default:
        *out_len = 0;
        return NULL;
    }
}
