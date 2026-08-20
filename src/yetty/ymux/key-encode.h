/*
 * ymux/key-encode.h — the ONE figure-key → terminal-byte encoder.
 *
 * A CLIENT_INPUT_FIGURE_KEY envelope carries either a translated character
 * (KEY_CHAR: Unicode codepoint + GLFW mod bits) or a physical special key
 * (KEY_DOWN: GLFW keycode + GLFW mod bits). Both the attach bridge's
 * chrome-focused path and its fallthrough-to-daemon path must turn that into
 * the byte sequence a terminal would have produced — modifier-correct
 * (Ctrl → control bytes, Alt → ESC prefix, xterm 1+shift|alt<<1|ctrl<<2
 * parameters on special keys), with full 1..4-byte UTF-8 for characters.
 *
 * Encoding follows xterm/tmux client behavior in normal (non-application)
 * cursor-key mode: CSI A/B/C/D arrows, CSI H/F home/end, CSI 2~/3~/5~/6~
 * editing keys, SS3 P..S for F1..F4 and CSI 15~..24~ for F5..F12; every
 * special key gains a `1;m` / `n;m~` parameter when modifiers are held.
 * DECCKM application-mode re-encoding happens pane-side (the daemon writes
 * to the pane PTY; the pane application accepts the normal-mode forms the
 * same way tmux clients emit them).
 */

#ifndef YETTY_YMUX_KEY_ENCODE_H
#define YETTY_YMUX_KEY_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Envelope kinds — declared here so the encoder (and its unit test) need no
 * ymgui header. UP is EXPLICIT and emits NOTHING: a key release has no
 * terminal byte encoding, and collapsing it to DOWN would double every
 * keystroke (cycle-22 P0). Any other kind value is rejected (0 bytes). */
enum yetty_ymux_key_encode_kind {
    YETTY_YMUX_KEY_ENCODE_CHAR = 0,
    YETTY_YMUX_KEY_ENCODE_DOWN = 1,
    YETTY_YMUX_KEY_ENCODE_UP = 2,
};

/* Map a wire kind (YETTY_YMGUI_INPUT_KEY_DOWN=0 / _UP=1 / _CHAR=2) to the
 * encoder kind. Returns -1 for an unrecognized wire kind so callers skip it
 * rather than mis-encode it. Kept here (not in the ymgui header) so the
 * mapping is one place both bridge paths share. */
static inline int yetty_ymux_key_encode_kind_from_wire(uint32_t wire_kind)
{
    switch (wire_kind) {
    case 0:
        return YETTY_YMUX_KEY_ENCODE_DOWN;
    case 1:
        return YETTY_YMUX_KEY_ENCODE_UP;
    case 2:
        return YETTY_YMUX_KEY_ENCODE_CHAR;
    default:
        return -1;
    }
}

/* GLFW modifier bits as carried by yetty_client_input_key.mods. */
enum yetty_ymux_key_encode_mod {
    YETTY_YMUX_KEY_MOD_SHIFT = 0x1,
    YETTY_YMUX_KEY_MOD_CTRL = 0x2,
    YETTY_YMUX_KEY_MOD_ALT = 0x4,
};

/* Encode one figure-key event into `out` (needs up to
 * YETTY_YMUX_KEY_ENCODE_MAX bytes). For KEY_CHAR events `codepoint` is the
 * translated Unicode scalar; for KEY_DOWN events `glfw_key` is the GLFW
 * keycode. Returns the number of bytes written; 0 means the event has no
 * byte encoding (e.g. a bare modifier, or a printable KEY_DOWN whose text
 * arrives via the matching KEY_CHAR — encoding those here would double
 * every letter). */
enum { YETTY_YMUX_KEY_ENCODE_MAX = 16 };
size_t yetty_ymux_key_encode(uint32_t kind, uint32_t glfw_key, uint32_t codepoint,
                             uint32_t glfw_mods, uint8_t *out, size_t out_cap);

/* DOWN/CHAR correlation for Ctrl chords, by control-byte IDENTITY (cycle-25 P1:
 * replaces the earlier single "did the last DOWN fold" bit, which could suppress
 * the wrong CHAR under interleaved/repeated streams). `key_down_ctrl_byte`
 * returns the control byte a DOWN folds to (0..0x7F) or -1 if it does not fold;
 * the bridge stores that as the pending fold identity. `key_char_ctrl_byte`
 * returns the control byte a CHAR corresponds to (-1 if none / no Ctrl). A CHAR
 * is the duplicate of the pending DOWN only when the two bytes are equal and
 * non-negative — a mismatched or composed CHAR is kept, so no character is lost. */
int yetty_ymux_key_down_ctrl_byte(uint32_t glfw_key, uint32_t glfw_mods);
int yetty_ymux_key_char_ctrl_byte(uint32_t codepoint, uint32_t glfw_mods);

/* The one-slot correlation STATE MACHINE the bridge runs: feed each figure-key
 * event (kind is a YETTY_YMUX_KEY_ENCODE_* value) with the bridge's persistent
 * `*pending_control` (init -1). Returns 1 when the event is a CHAR that
 * duplicates its folded DOWN and must be SUPPRESSED, 0 otherwise. Unit-testable
 * so interleaved DOWN/CHAR/UP streams can be driven directly (cycle-25 P1). */
int yetty_ymux_key_correlate(int *pending_control, uint32_t kind, uint32_t glfw_key,
                             uint32_t codepoint, uint32_t glfw_mods);

/* Decode a cursor/navigation escape sequence at bytes[offset] that is FULLY
 * present in [offset, len). On a match, returns the YETTY_YMUX_KEY_* value
 * (see <yetty/api/ymux/engine.h>) and sets *consumed to the sequence length;
 * returns 0 for a lone ESC, a split/partial sequence, or one this decoder
 * does not map (the caller then forwards those bytes verbatim). The bridge
 * feeds the matched key to the daemon STRUCTURED so libvterm re-encodes it
 * against the pane's terminal modes (SS3 under application cursor mode);
 * forwarding the pre-encoded CSI verbatim kept arrows as CSI even under
 * DECCKM (cycle-22 P0). Recognizes the full tmux tty-keys grammar the bridge
 * re-encodes: CSI/SS3 cursor+home+end `\e[A..D/H/F` `\eOA..D/H/F`, SS3 keypad
 * `\eOp..y/j..o/M/X`, SS3 function `\eOP..S` (F1-F4), CSI nav/function
 * `\e[1~..24~`, all with an optional `;<mod>` parameter, and the modified-key
 * forms CSI-u `\e[<code>;<mod>u` and modifyOtherKeys `\e[27;<mod>;<code>~`.
 *
 * Returns the structured YETTY_YMUX_KEY_* value (> 0) for a special key, the
 * sentinel YETTY_YMUX_KEY_DECODE_CHAR (< 0) when the sequence resolves to a
 * modified Unicode character (CSI-u / modifyOtherKeys — the codepoint is written
 * to *out_codepoint), or 0 when the bytes are not a recognized sequence. The
 * decoded modifier bitmask (YETTY_YMUX_KEY_MOD_*) is written to *out_mods. Both
 * out params may be NULL. */
enum { YETTY_YMUX_KEY_DECODE_CHAR = -1 };
int yetty_ymux_tty_key_decode(const uint8_t *bytes, size_t len, size_t offset, size_t *consumed,
                              unsigned *out_mods, uint32_t *out_codepoint);

/* The STREAMING half of the production key parser: the number of trailing bytes
 * at bytes[offset..len) that form an incomplete escape sequence and must be
 * carried to the next chunk before decoding (0 when nothing needs carrying). A
 * complete sequence is matched by yetty_ymux_tty_key_decode; this only holds
 * back a lone ESC, ESC[/ESCO awaiting a final, or ESC[<digit> awaiting `~`. */
size_t yetty_ymux_tty_key_carry_len(const uint8_t *bytes, size_t offset, size_t len);

/* The PERSISTENT byte-stream state machine the attach bridge runs on raw
 * keystroke input, extracted from the bridge so the whole path is unit-testable
 * at every split AND timeout boundary (previously it lived in the tool's main.c
 * and could only be exercised end-to-end). It handles, in one place:
 *   - the tmux prefix detach chord (C-b d; C-b C-b = literal C-b);
 *   - multibyte UTF-8 reassembly across chunk boundaries;
 *   - structured decode of cursor/nav escape sequences (delivered to the daemon
 *     so it re-encodes against the pane's DECCKM), via yetty_ymux_tty_key_decode;
 *   - the split-escape CARRY (an incomplete sequence held for the next chunk)
 *     and its escape-timeout flush. */
/* Longest incomplete escape prefix the parser holds across a chunk boundary.
 * Sized for CSI-u / extended keys (\e[<codepoint>;<mods>:<event>u) and the
 * multi-digit \e[<n>~ function keys — well beyond the 4 bytes the old carry
 * held (\e[3~), which broke any longer sequence fragmented across chunks. */
enum { YETTY_YMUX_KEY_CARRY_MAX = 32 };

struct yetty_ymux_key_stream {
    uint8_t esc_carry[YETTY_YMUX_KEY_CARRY_MAX]; /* incomplete escape held for the next chunk */
    uint8_t esc_carry_len;
    uint32_t utf8_codepoint; /* multibyte UTF-8 reassembly accumulator */
    uint8_t utf8_remaining;
    uint8_t prefix_armed; /* C-b seen — the next byte selects the chord */
};

enum yetty_ymux_key_stream_action {
    YETTY_YMUX_KEY_STREAM_CODEPOINT = 0, /* value = Unicode scalar to deliver */
    YETTY_YMUX_KEY_STREAM_KEY = 1,       /* value = YETTY_YMUX_KEY_* structured key */
    YETTY_YMUX_KEY_STREAM_DETACH = 2,    /* value = 0; the detach chord (C-b d) */
};

/* Sink for the stream's decoded output. Called once per delivered codepoint,
 * structured key, or detach. `mods` carries the decoded modifier bitmask
 * (YETTY_YMUX_KEY_MOD_*) for CODEPOINT and KEY actions — the daemon re-encodes
 * the key/char against the pane mode WITH these modifiers; it is 0 for DETACH
 * and for plain unmodified bytes. */
typedef void (*yetty_ymux_key_stream_emit_fn)(void *userdata,
                                              enum yetty_ymux_key_stream_action action,
                                              uint32_t value, unsigned mods);

void yetty_ymux_key_stream_init(struct yetty_ymux_key_stream *stream);

/* Feed one raw keystroke chunk. Any escape prefix carried from the previous
 * chunk is prepended first, so a cursor sequence split across transport chunks
 * still decodes to one structured key. Emits via `emit`. A trailing incomplete
 * escape is stashed in the stream's carry — check stream->esc_carry_len
 * afterward to (re)arm the escape-timeout deadline. Returns 1 when a detach
 * chord was emitted. */
int yetty_ymux_key_stream_feed(struct yetty_ymux_key_stream *stream, const uint8_t *bytes,
                               size_t count, yetty_ymux_key_stream_emit_fn emit, void *userdata);

/* The escape-timeout action: flush a stranded escape carry as raw codepoints
 * (tmux's escape-time behavior, once the carry's independent deadline elapses).
 * No-op when nothing is carried. */
void yetty_ymux_key_stream_flush_carry(struct yetty_ymux_key_stream *stream,
                                       yetty_ymux_key_stream_emit_fn emit, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_KEY_ENCODE_H */
