/*
 * ymux/key-encode.c — figure-key envelope → terminal bytes (see key-encode.h).
 *
 * Pure function, no state: the attach bridge calls it from both the
 * chrome-focused path and the fallthrough-to-daemon path so the two can
 * never diverge, and the unit test drives the full modifier matrix.
 */

#include "key-encode.h"

#include <yetty/api/ymux/engine.h> /* enum yetty_ymux_key (decode return values) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* xterm modifier parameter: 1 + shift|alt<<1|ctrl<<2; 0 = unmodified. */
static unsigned key_mod_param(uint32_t glfw_mods)
{
    unsigned bits = 0;
    if (glfw_mods & YETTY_YMUX_KEY_MOD_SHIFT) {
        bits |= 1;
    }
    if (glfw_mods & YETTY_YMUX_KEY_MOD_ALT) {
        bits |= 2;
    }
    if (glfw_mods & YETTY_YMUX_KEY_MOD_CTRL) {
        bits |= 4;
    }
    return bits ? bits + 1 : 0;
}

static size_t utf8_encode(uint32_t codepoint, uint8_t *out)
{
    if (codepoint < 0x80) {
        out[0] = (uint8_t)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        out[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (codepoint >> 12));
        out[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (uint8_t)(0xF0 | (codepoint >> 18));
        out[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* Ctrl+<char> → control byte, the terminal way: C-@..C-_ map to 0x00..0x1F,
 * lowercase letters fold to their uppercase control, C-? is DEL, C-Space is
 * NUL. Returns 1 and writes the byte, or 0 when the character has no control
 * form (the caller then falls back to the plain encoding). */
static int control_byte_for(uint32_t codepoint, uint8_t *out)
{
    if (codepoint >= 'a' && codepoint <= 'z') {
        *out = (uint8_t)(codepoint - 'a' + 1);
        return 1;
    }
    if (codepoint >= '@' && codepoint <= '_') {
        *out = (uint8_t)(codepoint & 0x1F);
        return 1;
    }
    if (codepoint == '?') {
        *out = 0x7F;
        return 1;
    }
    if (codepoint == ' ') {
        *out = 0x00;
        return 1;
    }
    return 0;
}

/* CSI special with the xterm modifier form: unmodified `ESC [ <base>`,
 * modified `ESC [ 1 ; m <base>`. */
static size_t emit_csi_letter(char base, unsigned mod_param, uint8_t *out, size_t out_cap)
{
    int written = mod_param ? snprintf((char *)out, out_cap, "\x1b[1;%u%c", mod_param, base)
                            : snprintf((char *)out, out_cap, "\x1b[%c", base);
    return written > 0 && (size_t)written < out_cap ? (size_t)written : 0;
}

/* CSI tilde special: unmodified `ESC [ n ~`, modified `ESC [ n ; m ~`. */
static size_t emit_csi_tilde(unsigned number, unsigned mod_param, uint8_t *out, size_t out_cap)
{
    int written = mod_param ? snprintf((char *)out, out_cap, "\x1b[%u;%u~", number, mod_param)
                            : snprintf((char *)out, out_cap, "\x1b[%u~", number);
    return written > 0 && (size_t)written < out_cap ? (size_t)written : 0;
}

/* F1..F4 are SS3 P..S unmodified; the modified forms switch to CSI 1;m. */
static size_t emit_ss3_fkey(char base, unsigned mod_param, uint8_t *out, size_t out_cap)
{
    int written = mod_param ? snprintf((char *)out, out_cap, "\x1b[1;%u%c", mod_param, base)
                            : snprintf((char *)out, out_cap, "\x1bO%c", base);
    return written > 0 && (size_t)written < out_cap ? (size_t)written : 0;
}

size_t yetty_ymux_key_encode(uint32_t kind, uint32_t glfw_key, uint32_t codepoint,
                             uint32_t glfw_mods, uint8_t *out, size_t out_cap)
{
    if (!out || out_cap < YETTY_YMUX_KEY_ENCODE_MAX) {
        return 0;
    }
    unsigned mod_param = key_mod_param(glfw_mods);
    int alt_held = (glfw_mods & YETTY_YMUX_KEY_MOD_ALT) != 0;
    int ctrl_held = (glfw_mods & YETTY_YMUX_KEY_MOD_CTRL) != 0;
    int shift_held = (glfw_mods & YETTY_YMUX_KEY_MOD_SHIFT) != 0;
    size_t len = 0;

    if (kind == YETTY_YMUX_KEY_ENCODE_CHAR) {
        if (codepoint == 0) {
            return 0;
        }
        /* The CHAR carries the composed CHARACTER for this keystroke and is
         * encoded here as text. Whether it DUPLICATES a control byte the
         * KEY_DOWN phase already folded (Ctrl-B → 0x02) is a STATEFUL, per-press
         * decision the bridge makes by correlating this CHAR with its preceding
         * DOWN (yetty_ymux_key_down_folds_ctrl + yetty_ymux_key_char_suppressed)
         * — NOT a blanket drop of every Ctrl-held CHAR, which loses layout /
         * composed characters whose DOWN produced no control byte (cycle-24 P1).
         * Ctrl is intentionally NOT special-cased in the text encoding. */
        (void)ctrl_held;
        if (alt_held) {
            out[len++] = 0x1B; /* Meta: ESC prefix */
        }
        size_t text_len = utf8_encode(codepoint, out + len);
        return text_len ? len + text_len : 0;
    }

    /* KEY_ENCODE_UP and any unrecognized kind emit NOTHING — a release has no
     * terminal encoding, and re-emitting the DOWN form here would double every
     * keystroke (cycle-22 P0). Only DOWN reaches the special-key table below. */
    if (kind != YETTY_YMUX_KEY_ENCODE_DOWN) {
        return 0;
    }

    switch (glfw_key) {
    case 256: /* Escape */
        if (alt_held) {
            out[len++] = 0x1B;
        }
        out[len++] = 0x1B;
        return len;
    case 257: /* Enter */
    case 335: /* keypad Enter */
        if (alt_held) {
            out[len++] = 0x1B;
        }
        out[len++] = '\r';
        return len;
    case 258: /* Tab: Shift-Tab is CSI Z (back-tab) */
        if (shift_held) {
            memcpy(out, "\x1b[Z", 3);
            return 3;
        }
        if (alt_held) {
            out[len++] = 0x1B;
        }
        out[len++] = '\t';
        return len;
    case 259: /* Backspace: DEL, Ctrl form is BS */
        if (alt_held) {
            out[len++] = 0x1B;
        }
        out[len++] = ctrl_held ? 0x08 : 0x7F;
        return len;
    case 260: /* Insert */
        return emit_csi_tilde(2, mod_param, out, out_cap);
    case 261: /* Delete */
        return emit_csi_tilde(3, mod_param, out, out_cap);
    case 262: /* Right */
        return emit_csi_letter('C', mod_param, out, out_cap);
    case 263: /* Left */
        return emit_csi_letter('D', mod_param, out, out_cap);
    case 264: /* Down */
        return emit_csi_letter('B', mod_param, out, out_cap);
    case 265: /* Up */
        return emit_csi_letter('A', mod_param, out, out_cap);
    case 266: /* Page Up */
        return emit_csi_tilde(5, mod_param, out, out_cap);
    case 267: /* Page Down */
        return emit_csi_tilde(6, mod_param, out, out_cap);
    case 268: /* Home */
        return emit_csi_letter('H', mod_param, out, out_cap);
    case 269: /* End */
        return emit_csi_letter('F', mod_param, out, out_cap);
    case 290: /* F1..F4: SS3 P..S */
        return emit_ss3_fkey('P', mod_param, out, out_cap);
    case 291:
        return emit_ss3_fkey('Q', mod_param, out, out_cap);
    case 292:
        return emit_ss3_fkey('R', mod_param, out, out_cap);
    case 293:
        return emit_ss3_fkey('S', mod_param, out, out_cap);
    case 294: /* F5..F12: CSI n~ with the historical gaps */
        return emit_csi_tilde(15, mod_param, out, out_cap);
    case 295:
        return emit_csi_tilde(17, mod_param, out, out_cap);
    case 296:
        return emit_csi_tilde(18, mod_param, out, out_cap);
    case 297:
        return emit_csi_tilde(19, mod_param, out, out_cap);
    case 298:
        return emit_csi_tilde(20, mod_param, out, out_cap);
    case 299:
        return emit_csi_tilde(21, mod_param, out, out_cap);
    case 300:
        return emit_csi_tilde(23, mod_param, out, out_cap);
    case 301:
        return emit_csi_tilde(24, mod_param, out, out_cap);
    default:
        break;
    }

    /* Printable GLFW keycodes (space..grave). Their text arrives via the
     * matching KEY_CHAR event — encoding them here would double every
     * letter — EXCEPT when Ctrl is held: Ctrl suppresses the CHAR event,
     * so the control byte must come from the DOWN. GLFW letter keycodes
     * are uppercase ASCII; the control fold is case-insensitive. */
    if (ctrl_held && glfw_key >= 32 && glfw_key <= 96) {
        uint8_t control = 0;
        if (control_byte_for(glfw_key, &control)) {
            if (alt_held) {
                out[len++] = 0x1B;
            }
            out[len++] = control;
            return len;
        }
    }
    return 0;
}

int yetty_ymux_key_down_ctrl_byte(uint32_t glfw_key, uint32_t glfw_mods)
{
    /* The control BYTE a KEY_DOWN folds to when Ctrl is held and the GLFW key is
     * a printable ASCII code with a control form — the SAME value the DOWN
     * encoder emits (space..grave, control_byte_for). Returns that byte (0..0x7F,
     * so Ctrl-Space's NUL is representable) as the fold IDENTITY, or -1 when the
     * DOWN does not fold (a layout / composed key with no representable GLFW
     * control form). The bridge records this identity so the matching CHAR — and
     * ONLY the matching CHAR — is recognised as its duplicate (cycle-25 P1). */
    if (!(glfw_mods & YETTY_YMUX_KEY_MOD_CTRL)) {
        return -1;
    }
    if (glfw_key < 32 || glfw_key > 96) {
        return -1;
    }
    uint8_t control = 0;
    if (!control_byte_for(glfw_key, &control)) {
        return -1;
    }
    return (int)control;
}

int yetty_ymux_key_char_ctrl_byte(uint32_t codepoint, uint32_t glfw_mods)
{
    /* The control BYTE this CHAR corresponds to, for matching against a folded
     * DOWN's identity: -1 when Ctrl is not held (never a duplicate). A codepoint
     * already IN the C0 range is its own control byte (the host may deliver the
     * folded 0x02 directly); otherwise its control-fold (e.g. 'b' -> 0x02) is
     * used, so both host conventions map a Ctrl-B CHAR to 0x02. A codepoint with
     * NO control form yields -1 and is kept. */
    if (!(glfw_mods & YETTY_YMUX_KEY_MOD_CTRL)) {
        return -1;
    }
    if (codepoint < 0x20) {
        return (int)codepoint;
    }
    uint8_t control = 0;
    if (!control_byte_for(codepoint, &control)) {
        return -1;
    }
    return (int)control;
}

int yetty_ymux_key_correlate(int *pending_control, uint32_t kind, uint32_t glfw_key,
                             uint32_t codepoint, uint32_t glfw_mods)
{
    /* One-slot Ctrl DOWN/CHAR correlator (the bridge owns *pending_control,
     * initialised to -1). A folding DOWN records its control-byte identity; the
     * NEXT CHAR is suppressed only if its own control byte matches (the
     * duplicate), and the window closes on that CHAR either way. UP / unknown
     * resets. This is robust to repeat (each DOWN re-arms), missing CHAR (the
     * next DOWN overwrites), mismatched CHAR (kept, window closed), and
     * UP-before-CHAR (reset) — cycle-25 P1. Returns 1 to suppress, else 0. */
    if (!pending_control) {
        return 0;
    }
    if (kind == YETTY_YMUX_KEY_ENCODE_DOWN) {
        *pending_control = yetty_ymux_key_down_ctrl_byte(glfw_key, glfw_mods);
        return 0;
    }
    if (kind == YETTY_YMUX_KEY_ENCODE_CHAR) {
        int char_control = yetty_ymux_key_char_ctrl_byte(codepoint, glfw_mods);
        int suppress = *pending_control >= 0 && char_control == *pending_control;
        *pending_control = -1;
        return suppress;
    }
    *pending_control = -1; /* UP / unknown ends the correlation window */
    return 0;
}

/* xterm modifier PARAMETER (1 + shift|alt<<1|ctrl<<2) -> YETTY_YMUX_KEY_MOD_*.
 * A missing/1 parameter means no modifiers. */
static unsigned key_decode_mods(long mod_param)
{
    if (mod_param <= 1) {
        return 0;
    }
    unsigned bits = (unsigned)(mod_param - 1);
    unsigned out = 0;
    if (bits & 1) {
        out |= YETTY_YMUX_KEY_MOD_SHIFT;
    }
    if (bits & 2) {
        out |= YETTY_YMUX_KEY_MOD_ALT;
    }
    if (bits & 4) {
        out |= YETTY_YMUX_KEY_MOD_CTRL;
    }
    return out;
}

/* CSI/SS3 final LETTER -> structured key: cursor (A-D), home/end (H/F), and the
 * SS3 function keys F1-F4 (P-S). Returns 0 for any other final. */
static int key_from_final_letter(uint8_t final)
{
    switch (final) {
    case 'A':
        return YETTY_YMUX_KEY_UP;
    case 'B':
        return YETTY_YMUX_KEY_DOWN;
    case 'C':
        return YETTY_YMUX_KEY_RIGHT;
    case 'D':
        return YETTY_YMUX_KEY_LEFT;
    case 'H':
        return YETTY_YMUX_KEY_HOME;
    case 'F':
        return YETTY_YMUX_KEY_END;
    case 'P':
        return YETTY_YMUX_KEY_F1;
    case 'Q':
        return YETTY_YMUX_KEY_F2;
    case 'R':
        return YETTY_YMUX_KEY_F3;
    case 'S':
        return YETTY_YMUX_KEY_F4;
    default:
        return 0;
    }
}

/* SS3 application-keypad final (ESC O p..y / j..o / M / X) -> structured KP key. */
static int key_from_ss3_keypad(uint8_t final)
{
    if (final >= 'p' && final <= 'y') {
        return YETTY_YMUX_KEY_KP_0 + (final - 'p');
    }
    switch (final) {
    case 'j':
        return YETTY_YMUX_KEY_KP_MULT;
    case 'k':
        return YETTY_YMUX_KEY_KP_PLUS;
    case 'l':
        return YETTY_YMUX_KEY_KP_COMMA;
    case 'm':
        return YETTY_YMUX_KEY_KP_MINUS;
    case 'n':
        return YETTY_YMUX_KEY_KP_PERIOD;
    case 'o':
        return YETTY_YMUX_KEY_KP_DIVIDE;
    case 'M':
        return YETTY_YMUX_KEY_KP_ENTER;
    case 'X':
        return YETTY_YMUX_KEY_KP_EQUAL;
    default:
        return 0;
    }
}

/* CSI `\e[<n>~` number -> structured nav/function key (editing keys + F1-F12). */
static int key_from_tilde_number(long number)
{
    switch (number) {
    case 1:
    case 7:
        return YETTY_YMUX_KEY_HOME;
    case 2:
        return YETTY_YMUX_KEY_INSERT;
    case 3:
        return YETTY_YMUX_KEY_DELETE;
    case 4:
    case 8:
        return YETTY_YMUX_KEY_END;
    case 5:
        return YETTY_YMUX_KEY_PAGE_UP;
    case 6:
        return YETTY_YMUX_KEY_PAGE_DOWN;
    case 11:
        return YETTY_YMUX_KEY_F1;
    case 12:
        return YETTY_YMUX_KEY_F2;
    case 13:
        return YETTY_YMUX_KEY_F3;
    case 14:
        return YETTY_YMUX_KEY_F4;
    case 15:
        return YETTY_YMUX_KEY_F5;
    case 17:
        return YETTY_YMUX_KEY_F6;
    case 18:
        return YETTY_YMUX_KEY_F7;
    case 19:
        return YETTY_YMUX_KEY_F8;
    case 20:
        return YETTY_YMUX_KEY_F9;
    case 21:
        return YETTY_YMUX_KEY_F10;
    case 23:
        return YETTY_YMUX_KEY_F11;
    case 24:
        return YETTY_YMUX_KEY_F12;
    default:
        return 0;
    }
}

int yetty_ymux_tty_key_decode(const uint8_t *bytes, size_t len, size_t offset, size_t *consumed,
                              unsigned *out_mods, uint32_t *out_codepoint)
{
    if (out_mods) {
        *out_mods = 0;
    }
    if (out_codepoint) {
        *out_codepoint = 0;
    }
    if (!bytes || !consumed || offset >= len) {
        return 0;
    }
    size_t remain = len - offset;
    if (remain < 3 || bytes[offset] != 0x1b) {
        return 0;
    }
    uint8_t intro = bytes[offset + 1];

    /* SS3 (`\eO<final>`): always 3 bytes, never parameterised — cursor/home/end,
     * F1-F4, or an application-keypad key. */
    if (intro == 'O') {
        uint8_t final = bytes[offset + 2];
        int key = key_from_final_letter(final);
        if (key == 0) {
            key = key_from_ss3_keypad(final);
        }
        if (key != 0) {
            *consumed = 3;
            return key;
        }
        return 0;
    }
    if (intro != '[') {
        return 0;
    }

    /* CSI (`\e[ <params> <final>`): collect the ';'-separated numeric params
     * then the final byte. An empty field is a 0 param; the sequence is
     * INCOMPLETE (return 0, the carry holds it) until the final byte arrives. */
    long params[4] = {0, 0, 0, 0};
    int nparams = 0;
    int in_param = 0;
    size_t pos = offset + 2;
    for (; pos < len; ++pos) {
        uint8_t scan = bytes[pos];
        if (scan >= '0' && scan <= '9') {
            if (!in_param) {
                if (nparams >= (int)(sizeof(params) / sizeof(params[0]))) {
                    return 0; /* more params than any key form uses */
                }
                params[nparams++] = 0;
                in_param = 1;
            }
            params[nparams - 1] = params[nparams - 1] * 10 + (scan - '0');
        } else if (scan == ';') {
            if (!in_param) {
                if (nparams >= (int)(sizeof(params) / sizeof(params[0]))) {
                    return 0;
                }
                params[nparams++] = 0; /* empty field -> 0 */
            }
            in_param = 0;
        } else {
            break; /* the final byte */
        }
    }
    if (pos >= len) {
        return 0; /* no final byte yet — incomplete, carried to the next chunk */
    }
    uint8_t final = bytes[pos];
    size_t seq_len = pos - offset + 1;

    if (final == '~') {
        long number = nparams >= 1 ? params[0] : 0;
        /* modifyOtherKeys: `\e[27;<mod>;<code>~` -> a modified Unicode char. */
        if (number == 27 && nparams >= 3) {
            if (out_mods) {
                *out_mods = key_decode_mods(params[1]);
            }
            if (out_codepoint) {
                *out_codepoint = (uint32_t)params[2];
            }
            *consumed = seq_len;
            return YETTY_YMUX_KEY_DECODE_CHAR;
        }
        int key = key_from_tilde_number(number);
        if (key == 0) {
            return 0;
        }
        if (out_mods && nparams >= 2) {
            *out_mods = key_decode_mods(params[1]);
        }
        *consumed = seq_len;
        return key;
    }
    if (final == 'u') {
        /* CSI-u (fixterm / modifyOtherKeys=2): `\e[<code>;<mod>u`. */
        if (nparams < 1) {
            return 0;
        }
        if (out_codepoint) {
            *out_codepoint = (uint32_t)params[0];
        }
        if (out_mods && nparams >= 2) {
            *out_mods = key_decode_mods(params[1]);
        }
        *consumed = seq_len;
        return YETTY_YMUX_KEY_DECODE_CHAR;
    }
    /* Cursor/home/end/F1-F4 letter, bare (`\e[A`) or modified (`\e[1;<mod>A`). */
    int key = key_from_final_letter(final);
    if (key == 0) {
        return 0;
    }
    if (out_mods && nparams >= 2) {
        *out_mods = key_decode_mods(params[1]);
    }
    *consumed = seq_len;
    return key;
}

size_t yetty_ymux_tty_key_carry_len(const uint8_t *bytes, size_t offset, size_t len)
{
    /* How many trailing bytes at bytes[offset..] form an INCOMPLETE escape
     * sequence that must be carried to the next chunk before decoding (the
     * streaming half of the production key parser). A COMPLETE sequence carries
     * 0: it is either matched by yetty_ymux_tty_key_decode or forwarded whole.
     * An INCOMPLETE CSI/SS3 is held so a sequence split across chunk boundaries
     * still reassembles — this covers a lone ESC, ESC[/ESCO awaiting the final,
     * and ANY parameterised CSI (multi-digit \e[<n>~ function keys, CSI-u
     * extended keys \e[<code>;<mods>u) up to the carry buffer. */
    if (!bytes) {
        return 0;
    }
    size_t remain = len - offset;
    if (remain == 0 || bytes[offset] != 0x1b) {
        return 0;
    }
    if (remain == 1) {
        return 1; /* lone ESC at end — may become CSI/SS3 */
    }
    uint8_t intro = bytes[offset + 1];
    if (intro != '[' && intro != 'O') {
        return 0; /* ESC + non-CSI/SS3: a real 2-byte thing, not carriable */
    }
    /* Over-long: a sequence longer than the carry buffer is forwarded rather
     * than held — the receiver's VT parser (or the escape-timeout) resolves it,
     * and we never overflow esc_carry. */
    if (remain > YETTY_YMUX_KEY_CARRY_MAX) {
        return 0;
    }
    if (intro == 'O') {
        return remain == 2 ? 2 : 0; /* SS3: ESC O <final>, incomplete at 2 bytes */
    }
    /* CSI: ESC [ (param 0x30-0x3F | intermediate 0x20-0x2F)* final 0x40-0x7E.
     * Incomplete until a final byte appears; a byte outside the param/final
     * ranges is not a carriable CSI (forward as-is). */
    for (size_t index = offset + 2; index < offset + remain; ++index) {
        uint8_t byte = bytes[index];
        if (byte >= 0x40 && byte <= 0x7E) {
            return 0; /* final byte present → sequence complete */
        }
        if (byte < 0x20 || byte > 0x3F) {
            return 0; /* not a valid CSI param/intermediate → not carriable */
        }
    }
    return remain; /* all params/intermediates, no final yet → hold the prefix */
}

void yetty_ymux_key_stream_init(struct yetty_ymux_key_stream *stream)
{
    memset(stream, 0, sizeof(*stream));
}

/* Process one CONTIGUOUS keystroke buffer (no carry prepend). Splits by escape
 * sequence and UTF-8; a trailing incomplete escape is stashed in the stream's
 * carry. Returns 1 on a detach chord. */
static int key_stream_process_buffer(struct yetty_ymux_key_stream *stream, const uint8_t *keys,
                                     size_t count, yetty_ymux_key_stream_emit_fn emit,
                                     void *userdata)
{
    for (size_t index = 0; index < count; ++index) {
        unsigned char key = keys[index];
        /* Mid-sequence: consume a continuation byte, or abandon a truncated one. */
        if (stream->utf8_remaining > 0) {
            if ((key & 0xC0) == 0x80) {
                stream->utf8_codepoint = (stream->utf8_codepoint << 6) | (uint32_t)(key & 0x3F);
                if (--stream->utf8_remaining == 0) {
                    emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, stream->utf8_codepoint, 0);
                }
                continue;
            }
            /* Not a continuation: the previous sequence was truncated. Emit a
             * replacement and re-process this byte as a fresh start. */
            stream->utf8_remaining = 0;
            emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, 0xFFFDu, 0);
        }
        /* tmux prefix — only on a fresh start byte. */
        if (stream->prefix_armed) {
            stream->prefix_armed = 0;
            if (key == 'd') {
                emit(userdata, YETTY_YMUX_KEY_STREAM_DETACH, 0, 0);
                return 1; /* detach */
            }
            /* C-b C-b (or C-b <other>): fall through and deliver the key. */
        } else if (key == 0x02) { /* C-b */
            stream->prefix_armed = 1;
            continue;
        }
        /* Mode-aware special keys: a cursor/nav escape sequence is re-delivered
         * STRUCTURED so the daemon encodes it against the pane's DECCKM. A
         * sequence SPLIT across chunks is carried and completed next chunk; a
         * lone ESC or an unmapped sequence falls through per-codepoint. */
        if (key == 0x1b) {
            size_t consumed = 0;
            unsigned decoded_mods = 0;
            uint32_t decoded_codepoint = 0;
            int ymux_key = yetty_ymux_tty_key_decode(keys, count, index, &consumed, &decoded_mods,
                                                     &decoded_codepoint);
            if (ymux_key == YETTY_YMUX_KEY_DECODE_CHAR) {
                /* CSI-u / modifyOtherKeys: a modified Unicode char — the daemon
                 * re-encodes it against the pane's extended-key mode. */
                emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, decoded_codepoint, decoded_mods);
                index += consumed - 1; /* loop ++ advances past the last byte */
                continue;
            }
            if (ymux_key != 0) {
                emit(userdata, YETTY_YMUX_KEY_STREAM_KEY, (uint32_t)ymux_key, decoded_mods);
                index += consumed - 1; /* loop ++ advances past the last byte */
                continue;
            }
            size_t carry = yetty_ymux_tty_key_carry_len(keys, index, count);
            if (carry > 0) {
                memcpy(stream->esc_carry, keys + index, carry);
                stream->esc_carry_len = (uint8_t)carry;
                return 0; /* rest of a split sequence — wait for the next chunk */
            }
        }
        /* Classify the fresh byte. */
        if (key < 0x80) {
            emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, key, 0);
        } else if ((key & 0xE0) == 0xC0) {
            stream->utf8_codepoint = key & 0x1Fu;
            stream->utf8_remaining = 1;
        } else if ((key & 0xF0) == 0xE0) {
            stream->utf8_codepoint = key & 0x0Fu;
            stream->utf8_remaining = 2;
        } else if ((key & 0xF8) == 0xF0) {
            stream->utf8_codepoint = key & 0x07u;
            stream->utf8_remaining = 3;
        } else {
            /* Invalid lead / stray continuation. */
            emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, 0xFFFDu, 0);
        }
    }
    return 0;
}

int yetty_ymux_key_stream_feed(struct yetty_ymux_key_stream *stream, const uint8_t *bytes,
                               size_t count, yetty_ymux_key_stream_emit_fn emit, void *userdata)
{
    if (stream->esc_carry_len == 0) {
        return key_stream_process_buffer(stream, bytes, count, emit, userdata);
    }
    /* Prepend the carried escape prefix (<=4 bytes) to this chunk so a cursor
     * sequence split across chunk boundaries still decodes to a structured key. */
    uint8_t carry_len = stream->esc_carry_len;
    uint8_t carry_bytes[YETTY_YMUX_KEY_CARRY_MAX];
    memcpy(carry_bytes, stream->esc_carry, carry_len);
    stream->esc_carry_len = 0;
    size_t combined_len = (size_t)carry_len + count;
    uint8_t stack_buf[64];
    uint8_t *combined = combined_len <= sizeof(stack_buf) ? stack_buf : malloc(combined_len);
    if (!combined) {
        /* OOM reassembling the split sequence: flush the carried prefix as RAW
         * codepoints (the escape-timeout path), NEVER re-parse it — feeding
         * carry_bytes back through the parser would just re-carry the same
         * incomplete escape, and the following chunk would overtake it. Then
         * process the new chunk fresh. */
        for (uint8_t index = 0; index < carry_len; ++index) {
            emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, carry_bytes[index], 0);
        }
        return key_stream_process_buffer(stream, bytes, count, emit, userdata);
    }
    memcpy(combined, carry_bytes, carry_len);
    if (count > 0) {
        memcpy(combined + carry_len, bytes, count);
    }
    int detach = key_stream_process_buffer(stream, combined, combined_len, emit, userdata);
    if (combined != stack_buf) {
        free(combined);
    }
    return detach;
}

void yetty_ymux_key_stream_flush_carry(struct yetty_ymux_key_stream *stream,
                                       yetty_ymux_key_stream_emit_fn emit, void *userdata)
{
    if (stream->esc_carry_len == 0) {
        return;
    }
    uint8_t carry_len = stream->esc_carry_len;
    stream->esc_carry_len = 0;
    for (uint8_t index = 0; index < carry_len; ++index) {
        emit(userdata, YETTY_YMUX_KEY_STREAM_CODEPOINT, stream->esc_carry[index], 0);
    }
}
