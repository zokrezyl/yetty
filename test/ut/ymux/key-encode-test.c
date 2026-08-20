/*
 * ymux figure-key encoder matrix (#699 review cycle 21): the ONE
 * envelope→bytes encoder both attach-bridge paths share must be
 * modifier-correct — Ctrl → control bytes (Ctrl-B must survive for the
 * prefix/detach chord), Alt → ESC prefix, xterm modifier parameters on
 * special keys, full 1..4-byte UTF-8 including non-BMP — and must encode a
 * printable KEY_DOWN only when Ctrl suppresses the matching KEY_CHAR
 * (exactly-once delivery: never both).
 */
#include <string.h>

#include "ytest.h"

#include "../../../src/yetty/ymux/key-encode.h"

static void check_encode(struct ytest *test, uint32_t kind, uint32_t key, uint32_t codepoint,
                         uint32_t mods, const char *expected, size_t expected_len)
{
    uint8_t out[YETTY_YMUX_KEY_ENCODE_MAX];
    size_t len = yetty_ymux_key_encode(kind, key, codepoint, mods, out, sizeof(out));
    YTEST_CHECK_EQ_SIZE(test, len, expected_len);
    if (len == expected_len && expected_len > 0) {
        YTEST_CHECK(test, memcmp(out, expected, expected_len) == 0);
    }
}

/* Ctrl chords arrive as KEY_DOWN (Ctrl suppresses the CHAR event). The GLFW
 * letter keycodes are uppercase ASCII. */
static void test_control_chords(struct ytest *test)
{
    /* Ctrl-C — interrupt. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'C', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x03", 1);
    /* Ctrl-B — the ymux prefix/detach chord MUST survive figure focus. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'B', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x02", 1);
    /* Ctrl-Z, Ctrl-A boundaries of the letter fold. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'Z', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1a", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'A', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x01", 1);
    /* Ctrl-Space is NUL. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, ' ', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x00", 1);
    /* Ctrl-[ is ESC. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, '[', 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1b", 1);
    /* Ctrl-Alt-B: Meta prefixes the control byte. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'B', 0,
                 YETTY_YMUX_KEY_MOD_CTRL | YETTY_YMUX_KEY_MOD_ALT, "\x1b\x02", 2);
    /* Shift+Ctrl+letter folds to the same control byte. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'C', 0,
                 YETTY_YMUX_KEY_MOD_CTRL | YETTY_YMUX_KEY_MOD_SHIFT, "\x03", 1);
    /* A Ctrl-held CHAR now ENCODES its codepoint here — the duplicate-of-a-
     * folded-DOWN suppression is a STATEFUL bridge decision (correlation), not a
     * blanket drop, so a layout/composed CHAR whose DOWN did not fold is never
     * lost (cycle-24 P1). The correlation predicates are tested separately. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 'c', YETTY_YMUX_KEY_MOD_CTRL, "c", 1);
}

/* Cycle-25 P1: DOWN/CHAR correlation by control-byte IDENTITY. */
static void test_ctrl_char_correlation(struct ytest *test)
{
    enum { CTRL = YETTY_YMUX_KEY_MOD_CTRL };
    /* Fold identity: a printable ASCII Ctrl key folds to its control byte;
     * Ctrl-Space folds to NUL (0), which is a REAL identity (not "no fold"). */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_down_ctrl_byte('B', CTRL), 0x02);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_down_ctrl_byte(' ', CTRL), 0x00);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_down_ctrl_byte('B', 0), -1);      /* no Ctrl */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_down_ctrl_byte(65456, CTRL), -1); /* keypad: no fold */
    /* CHAR identity: a folded control byte OR its base char both map to 0x02. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_char_ctrl_byte(0x02, CTRL), 0x02);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_char_ctrl_byte('b', CTRL), 0x02);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_char_ctrl_byte('b', 0), -1); /* no Ctrl: kept */

    /* The STATE MACHINE, driven directly with the interleaved streams the
     * single-bit design got wrong. 0 = keep, 1 = suppress. */
    int pending = -1;
    uint32_t DOWN = YETTY_YMUX_KEY_ENCODE_DOWN, CHAR = YETTY_YMUX_KEY_ENCODE_CHAR,
             UP = YETTY_YMUX_KEY_ENCODE_UP;

    /* Simple pair: Ctrl-B DOWN folds, its CHAR is suppressed. */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x02, CTRL) == 1);
    /* Repeat: the next Ctrl-B pair re-arms and suppresses again. */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x02, CTRL) == 1);
    /* MISMATCHED CHAR: Ctrl-B DOWN then a Ctrl-C CHAR (0x03) — kept, not the
     * duplicate; the window closes. */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x03, CTRL) == 0);
    /* MISSING CHAR then a second folded DOWN: the second overwrites the first,
     * and its own CHAR matches — no stale suppression of the wrong CHAR. */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'C', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x03, CTRL) == 1); /* Ctrl-C */
    /* UP-before-CHAR resets: a later Ctrl CHAR is NOT wrongly suppressed. */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, UP, 'B', 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x02, CTRL) == 0); /* kept */
    /* Composed / layout CHAR whose DOWN did not fold is kept (no character lost). */
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, DOWN, 65456, 0, CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending, CHAR, 0, 0x00E9 /* é */, CTRL) == 0);
}

/* Alt+printable = ESC prefix, then the character's UTF-8. */
static void test_alt_printable(struct ytest *test)
{
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 'x', YETTY_YMUX_KEY_MOD_ALT,
                 "\x1b"
                 "x",
                 2);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 'X',
                 YETTY_YMUX_KEY_MOD_ALT | YETTY_YMUX_KEY_MOD_SHIFT,
                 "\x1b"
                 "X",
                 2);
    /* Alt with a multi-byte character keeps the full UTF-8 after ESC. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0x00E9 /* é */, YETTY_YMUX_KEY_MOD_ALT,
                 "\x1b\xc3\xa9", 3);
}

/* Plain text: 1..4-byte UTF-8, including the non-BMP plane. */
static void test_utf8_planes(struct ytest *test)
{
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 'a', 0, "a", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0x00E9, 0, "\xc3\xa9", 2);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0x20AC /* € */, 0, "\xe2\x82\xac", 3);
    /* U+1F600 GRINNING FACE — non-BMP requires FOUR bytes (the old
     * chrome-path encoder emitted an invalid 3-byte form). */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0x1F600, 0, "\xf0\x9f\x98\x80", 4);
    /* Codepoints beyond Unicode are refused, not garbled. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0x110000, 0, "", 0);
}

/* Arrows and navigation keys with the xterm modifier parameter. */
static void test_modified_specials(struct ytest *test)
{
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 265, 0, 0, "\x1b[A", 3);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 265, 0, YETTY_YMUX_KEY_MOD_SHIFT, "\x1b[1;2A",
                 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 265, 0, YETTY_YMUX_KEY_MOD_ALT, "\x1b[1;3A", 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 262, 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1b[1;5C", 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 263, 0,
                 YETTY_YMUX_KEY_MOD_CTRL | YETTY_YMUX_KEY_MOD_SHIFT, "\x1b[1;6D", 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 268, 0, 0, "\x1b[H", 3);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 269, 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1b[1;5F", 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 266, 0, 0, "\x1b[5~", 4);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 267, 0, YETTY_YMUX_KEY_MOD_SHIFT, "\x1b[6;2~",
                 6);
    /* Insert / Delete — previously dropped entirely. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 260, 0, 0, "\x1b[2~", 4);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 261, 0, 0, "\x1b[3~", 4);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 261, 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1b[3;5~", 6);
}

/* Function keys — previously dropped entirely. */
static void test_function_keys(struct ytest *test)
{
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 290, 0, 0, "\x1bOP", 3);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 293, 0, 0, "\x1bOS", 3);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 290, 0, YETTY_YMUX_KEY_MOD_SHIFT, "\x1b[1;2P",
                 6);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 294, 0, 0, "\x1b[15~", 5);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 299, 0, 0, "\x1b[21~", 5);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 301, 0, 0, "\x1b[24~", 5);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 301, 0, YETTY_YMUX_KEY_MOD_CTRL, "\x1b[24;5~",
                 7);
}

/* Editing keys and phase/dedup rules. */
static void test_editing_and_dedup(struct ytest *test)
{
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 257, 0, 0, "\r", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 257, 0, YETTY_YMUX_KEY_MOD_ALT, "\x1b\r", 2);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 258, 0, 0, "\t", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 258, 0, YETTY_YMUX_KEY_MOD_SHIFT, "\x1b[Z", 3);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 259, 0, 0, "\x7f", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 259, 0, YETTY_YMUX_KEY_MOD_CTRL, "\x08", 1);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 259, 0, YETTY_YMUX_KEY_MOD_ALT, "\x1b\x7f", 2);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 256, 0, 0, "\x1b", 1);
    /* EXACTLY-ONCE: a printable KEY_DOWN without Ctrl encodes nothing —
     * its text arrives via the matching KEY_CHAR. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'A', 0, 0, "", 0);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'A', 0, YETTY_YMUX_KEY_MOD_SHIFT, "", 0);
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 'A', 0, YETTY_YMUX_KEY_MOD_ALT, "", 0);
    /* Bare modifier keycodes (GLFW 340 = left shift) encode nothing. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_DOWN, 340, 0, YETTY_YMUX_KEY_MOD_SHIFT, "", 0);
    /* Empty CHAR encodes nothing. */
    check_encode(test, YETTY_YMUX_KEY_ENCODE_CHAR, 0, 0, 0, "", 0);
}

/* Cycle-22 P0: a full DOWN+CHAR+UP event stream (as the host forwards it for
 * a printable key and for a special key) must emit the encoding EXACTLY ONCE.
 * KEY_UP and any unknown wire kind emit nothing; the wire-kind mapping is the
 * single place both bridge paths share. */
static void test_down_char_up_stream(struct ytest *test)
{
    uint8_t out[YETTY_YMUX_KEY_ENCODE_MAX];

    /* A printable 'a' arrives as DOWN(key='A') + CHAR(cp='a') + UP(key='A').
     * DOWN(no ctrl) emits nothing (text comes via CHAR), CHAR emits "a", UP
     * emits nothing — total one byte. */
    int down_kind = yetty_ymux_key_encode_kind_from_wire(0);
    int up_kind = yetty_ymux_key_encode_kind_from_wire(1);
    int char_kind = yetty_ymux_key_encode_kind_from_wire(2);
    YTEST_CHECK_EQ_INT(test, down_kind, YETTY_YMUX_KEY_ENCODE_DOWN);
    YTEST_CHECK_EQ_INT(test, up_kind, YETTY_YMUX_KEY_ENCODE_UP);
    YTEST_CHECK_EQ_INT(test, char_kind, YETTY_YMUX_KEY_ENCODE_CHAR);

    YTEST_CHECK_EQ_SIZE(test,
                        yetty_ymux_key_encode((uint32_t)down_kind, 'A', 0, 0, out, sizeof(out)),
                        0); /* printable DOWN: nothing */
    YTEST_CHECK_EQ_SIZE(test,
                        yetty_ymux_key_encode((uint32_t)char_kind, 0, 'a', 0, out, sizeof(out)),
                        1); /* CHAR: "a" */
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_key_encode((uint32_t)up_kind, 'A', 0, 0, out, sizeof(out)),
                        0); /* UP: nothing (was doubling the press) */

    /* A special key (Up arrow) arrives as DOWN(key=265) + UP(key=265), no
     * CHAR. DOWN emits the CSI once, UP emits nothing. */
    YTEST_CHECK_EQ_SIZE(test,
                        yetty_ymux_key_encode((uint32_t)down_kind, 265, 0, 0, out, sizeof(out)),
                        3); /* "\e[A" */
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_key_encode((uint32_t)up_kind, 265, 0, 0, out, sizeof(out)),
                        0); /* UP: NOT a second "\e[A" */

    /* A control chord (Ctrl-B) arrives as DOWN(key='B',ctrl) + CHAR(cp,ctrl)
     * + UP — the exact stream that broke the ymux prefix (cycle-23). DOWN emits
     * \x02 ONCE and folds; the bridge correlation then suppresses the matching
     * CHAR (a duplicate); UP emits nothing — so the pane sees a single 0x02 and
     * the following `d` still detaches. The bridge decision is modelled here
     * with the two predicates (the encoder itself no longer blanket-drops). */
    YTEST_CHECK_EQ_SIZE(test,
                        yetty_ymux_key_encode((uint32_t)down_kind, 'B', 0, YETTY_YMUX_KEY_MOD_CTRL,
                                              out, sizeof(out)),
                        1);
    YTEST_CHECK(test, out[0] == 0x02);
    /* The bridge correlation folds the DOWN's 0x02 identity, then suppresses the
     * matching CHAR (a duplicate) — driven through the real state machine. */
    int pending_stream = -1;
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending_stream, (uint32_t)down_kind, 'B', 0,
                                               YETTY_YMUX_KEY_MOD_CTRL) == 0);
    YTEST_CHECK(test, yetty_ymux_key_correlate(&pending_stream, (uint32_t)char_kind, 0, 0x02,
                                               YETTY_YMUX_KEY_MOD_CTRL) == 1);
    YTEST_CHECK_EQ_SIZE(
        test,
        yetty_ymux_key_encode((uint32_t)up_kind, 'B', 0, YETTY_YMUX_KEY_MOD_CTRL, out, sizeof(out)),
        0);

    /* An unrecognized wire kind is skipped, not mis-encoded. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_key_encode_kind_from_wire(99), -1);
}

/* Cycle-22 P0: the bridge decodes cursor/nav escape sequences BACK to
 * structured keys so the daemon re-encodes them against the pane's terminal
 * modes (SS3 under DECCKM). This pins the decoder: CSI + SS3 cursor keys, the
 * `~` editing keys, exact byte consumption, and the fall-through cases (lone
 * ESC, split sequence, unmapped final). YETTY_YMUX_KEY_* values come from
 * <yetty/api/ymux/engine.h>. */
static void check_decode(struct ytest *test, const char *seq, size_t len, int expect_key,
                         size_t expect_consumed)
{
    size_t consumed = 0;
    int key = yetty_ymux_tty_key_decode((const uint8_t *)seq, len, 0, &consumed, NULL, NULL);
    YTEST_CHECK_EQ_INT(test, key, expect_key);
    if (key != 0) {
        YTEST_CHECK_EQ_SIZE(test, consumed, expect_consumed);
    }
}

/* Decode a sequence expected to carry MODIFIERS (and, for CSI-u /
 * modifyOtherKeys, a Unicode codepoint via the YETTY_YMUX_KEY_DECODE_CHAR
 * sentinel). Asserts the returned key/sentinel, the modifier bitmask, the
 * codepoint, and the exact byte consumption. */
static void check_decode_mod(struct ytest *test, const char *seq, size_t len, int expect_key,
                             unsigned expect_mods, uint32_t expect_codepoint,
                             size_t expect_consumed)
{
    size_t consumed = 0;
    unsigned mods = 0;
    uint32_t codepoint = 0;
    int key = yetty_ymux_tty_key_decode((const uint8_t *)seq, len, 0, &consumed, &mods, &codepoint);
    YTEST_CHECK_EQ_INT(test, key, expect_key);
    YTEST_CHECK_EQ_INT(test, (int)mods, (int)expect_mods);
    YTEST_CHECK_EQ_INT(test, (int)codepoint, (int)expect_codepoint);
    if (key != 0) {
        YTEST_CHECK_EQ_SIZE(test, consumed, expect_consumed);
    }
}

static void test_tty_key_decode(struct ytest *test)
{
    /* CSI cursor keys. */
    check_decode(test, "\x1b[A", 3, 5 /*UP*/, 3);
    check_decode(test, "\x1b[B", 3, 6 /*DOWN*/, 3);
    check_decode(test, "\x1b[C", 3, 8 /*RIGHT*/, 3);
    check_decode(test, "\x1b[D", 3, 7 /*LEFT*/, 3);
    check_decode(test, "\x1b[H", 3, 11 /*HOME*/, 3);
    check_decode(test, "\x1b[F", 3, 12 /*END*/, 3);
    /* SS3 cursor keys (application cursor mode form) decode the same. */
    check_decode(test, "\x1bOA", 3, 5 /*UP*/, 3);
    check_decode(test, "\x1bOD", 3, 7 /*LEFT*/, 3);
    check_decode(test, "\x1bOH", 3, 11 /*HOME*/, 3);
    /* CSI `~` editing / nav keys. */
    check_decode(test, "\x1b[1~", 4, 11 /*HOME*/, 4);
    check_decode(test, "\x1b[2~", 4, 9 /*INSERT*/, 4);
    check_decode(test, "\x1b[3~", 4, 10 /*DELETE*/, 4);
    check_decode(test, "\x1b[4~", 4, 12 /*END*/, 4);
    check_decode(test, "\x1b[5~", 4, 13 /*PAGE_UP*/, 4);
    check_decode(test, "\x1b[6~", 4, 14 /*PAGE_DOWN*/, 4);
    /* Fall-through: lone ESC (Escape key), split sequence, unmapped final,
     * and a non-ESC byte all return 0 (forwarded verbatim / per-codepoint). */
    check_decode(test, "\x1b", 1, 0, 0);
    check_decode(test, "\x1b[", 2, 0, 0);  /* split — not fully present */
    check_decode(test, "\x1b[Z", 3, 0, 0); /* CSI Z (back-tab) — not mapped here */
    check_decode(test, "\x1b[3", 3, 0, 0); /* `~` key missing its 4th byte */
    check_decode(test, "abc", 3, 0, 0);    /* not an escape */
    /* Application-keypad SS3 (ESC O p..y / j..o / M / X): decoded STRUCTURED so
     * the daemon re-encodes per the pane's keypad mode. KP_0=15..KP_9=24,
     * KP_MULT=25.., KP_ENTER=31, KP_EQUAL=32 (from <yetty/api/ymux/engine.h>). */
    check_decode(test, "\x1bOp", 3, 15 /*KP_0*/, 3);
    check_decode(test, "\x1bOy", 3, 24 /*KP_9*/, 3);
    check_decode(test, "\x1bOM", 3, 31 /*KP_ENTER*/, 3);
    check_decode(test, "\x1bOX", 3, 32 /*KP_EQUAL*/, 3);
    check_decode(test, "\x1bOj", 3, 25 /*KP_MULT*/, 3);
    check_decode(test, "\x1bOA", 3, 5 /*UP still cursor, not keypad*/, 3);

    /* Function keys F1-F12. F1-F4 arrive as SS3 P..S; F1-F12 also as the
     * CSI-tilde numbers 11..24 (with the two gaps 16/22 unused). F1=33..F12=44
     * from <yetty/api/ymux/engine.h>. */
    check_decode(test, "\x1bOP", 3, 33 /*F1*/, 3);
    check_decode(test, "\x1bOQ", 3, 34 /*F2*/, 3);
    check_decode(test, "\x1bOR", 3, 35 /*F3*/, 3);
    check_decode(test, "\x1bOS", 3, 36 /*F4*/, 3);
    check_decode(test, "\x1b[11~", 5, 33 /*F1*/, 5);
    check_decode(test, "\x1b[15~", 5, 37 /*F5*/, 5);
    check_decode(test, "\x1b[17~", 5, 38 /*F6*/, 5);
    check_decode(test, "\x1b[21~", 5, 42 /*F10*/, 5);
    check_decode(test, "\x1b[23~", 5, 43 /*F11*/, 5);
    check_decode(test, "\x1b[24~", 5, 44 /*F12*/, 5);

    /* Modifier parameter (xterm `1 + shift|alt<<1|ctrl<<2`): param 2=Shift,
     * 5=Ctrl, 6=Shift+Ctrl. Modified cursor/home/end/F1-F4 use `\e[1;<mod>L`;
     * modified tilde keys use `\e[<n>;<mod>~`. */
    check_decode_mod(test, "\x1b[1;5A", 6, 5 /*UP*/, YETTY_YMUX_KEY_MOD_CTRL, 0, 6);
    check_decode_mod(test, "\x1b[1;2C", 6, 8 /*RIGHT*/, YETTY_YMUX_KEY_MOD_SHIFT, 0, 6);
    check_decode_mod(test, "\x1b[1;6F", 6, 12 /*END*/,
                     YETTY_YMUX_KEY_MOD_SHIFT | YETTY_YMUX_KEY_MOD_CTRL, 0, 6);
    check_decode_mod(test, "\x1b[1;5P", 6, 33 /*F1*/, YETTY_YMUX_KEY_MOD_CTRL, 0, 6);
    check_decode_mod(test, "\x1b[3;5~", 6, 10 /*DELETE*/, YETTY_YMUX_KEY_MOD_CTRL, 0, 6);
    check_decode_mod(test, "\x1b[15;2~", 7, 37 /*F5*/, YETTY_YMUX_KEY_MOD_SHIFT, 0, 7);

    /* CSI-u (`\e[<code>;<mod>u`) and modifyOtherKeys (`\e[27;<mod>;<code>~`)
     * resolve to a MODIFIED Unicode char — the DECODE_CHAR sentinel, with the
     * codepoint in *out_codepoint and the modifiers in *out_mods. */
    check_decode_mod(test, "\x1b[97;5u", 7, YETTY_YMUX_KEY_DECODE_CHAR, YETTY_YMUX_KEY_MOD_CTRL,
                     97 /* 'a' */, 7);
    check_decode_mod(test, "\x1b[65;2u", 7, YETTY_YMUX_KEY_DECODE_CHAR, YETTY_YMUX_KEY_MOD_SHIFT,
                     65 /* 'A' */, 7);
    check_decode_mod(test, "\x1b[27;5;97~", 10, YETTY_YMUX_KEY_DECODE_CHAR, YETTY_YMUX_KEY_MOD_CTRL,
                     97 /* 'a' */, 10);
    /* A bare `\e[<code>u` (no modifier field) still resolves to the char. */
    check_decode_mod(test, "\x1b[97u", 5, YETTY_YMUX_KEY_DECODE_CHAR, 0, 97, 5);

    /* A cursor sequence embedded mid-buffer decodes at its offset. */
    {
        const uint8_t buf[] = {'x', 0x1b, '[', 'C', 'y'};
        size_t consumed = 0;
        YTEST_CHECK_EQ_INT(test,
                           yetty_ymux_tty_key_decode(buf, sizeof(buf), 1, &consumed, NULL, NULL),
                           8 /*RIGHT*/);
        YTEST_CHECK_EQ_SIZE(test, consumed, 3);
    }
}

/* Cycle-25 P0: the STREAMING production path (carry + decode) must reconstruct a
 * recognised key across a chunk boundary at EVERY byte split. For each split,
 * the leading partial does NOT decode, its bytes are carried in full, and the
 * carry prepended to the tail decodes to the key exactly once — the property the
 * bridge relies on for DECCKM arrows fragmented by the transport. */
static void test_tty_key_streaming(struct ytest *test)
{
    struct {
        const char *seq;
        size_t len;
        int key;
    } cases[] = {
        {"\x1b[A", 3, 5 /*UP*/},
        {"\x1b[C", 3, 8 /*RIGHT*/},
        {"\x1b[H", 3, 11 /*HOME*/},
        {"\x1bOB", 3, 6 /*DOWN*/},
        {"\x1b[3~", 4, 10 /*DELETE*/},
        {"\x1b[5~", 4, 13 /*PAGE_UP*/},
        /* The extended grammar splits at every interior byte too. */
        {"\x1bOP", 3, 33 /*F1 (SS3)*/},
        {"\x1b[15~", 5, 37 /*F5 (CSI tilde)*/},
        {"\x1b[24~", 5, 44 /*F12*/},
        {"\x1b[1;5A", 6, 5 /*UP + Ctrl*/},
        {"\x1b[15;2~", 7, 37 /*F5 + Shift*/},
        {"\x1b[97;5u", 7, YETTY_YMUX_KEY_DECODE_CHAR /*CSI-u 'a'+Ctrl*/},
        {"\x1b[27;5;97~", 10, YETTY_YMUX_KEY_DECODE_CHAR /*modifyOtherKeys*/},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const uint8_t *seq = (const uint8_t *)cases[index].seq;
        size_t len = cases[index].len;
        /* A COMPLETE sequence needs no carry and decodes to the key. */
        size_t consumed = 0;
        YTEST_CHECK_EQ_INT(test, yetty_ymux_tty_key_decode(seq, len, 0, &consumed, NULL, NULL),
                           cases[index].key);
        YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(seq, 0, len), 0);
        /* Every interior split: partial carries fully, reconstruction decodes. */
        for (size_t split = 1; split < len; ++split) {
            size_t partial_consumed = 0;
            YTEST_CHECK_EQ_INT(
                test, yetty_ymux_tty_key_decode(seq, split, 0, &partial_consumed, NULL, NULL),
                0); /* partial does not decode */
            size_t carry = yetty_ymux_tty_key_carry_len(seq, 0, split);
            YTEST_CHECK_EQ_SIZE(test, carry, split); /* the whole partial is held */
            uint8_t reconstructed[16];
            memcpy(reconstructed, seq, carry);                       /* the carried bytes */
            memcpy(reconstructed + carry, seq + split, len - split); /* the next chunk */
            consumed = 0;
            YTEST_CHECK_EQ_INT(
                test, yetty_ymux_tty_key_decode(reconstructed, len, 0, &consumed, NULL, NULL),
                cases[index].key);
            YTEST_CHECK_EQ_SIZE(test, consumed, len);
        }
    }
}

/* Recording sink for the key-stream state machine: every emitted action is
 * appended so a test can assert the exact decoded output of a fed byte stream. */
struct key_stream_record {
    struct {
        enum yetty_ymux_key_stream_action action;
        uint32_t value;
        unsigned mods;
    } events[64];
    size_t count;
};

static void key_stream_record_emit(void *userdata, enum yetty_ymux_key_stream_action action,
                                   uint32_t value, unsigned mods)
{
    struct key_stream_record *record = userdata;
    if (record->count < sizeof(record->events) / sizeof(record->events[0])) {
        record->events[record->count].action = action;
        record->events[record->count].value = value;
        record->events[record->count].mods = mods;
        ++record->count;
    }
}

/* The PRODUCTION bridge path (key-encode.c key-stream), driven at every split
 * AND the escape-timeout boundary — the two the bridge could not exercise while
 * this lived in the tool's main.c. Each scenario is its OWN function so its
 * stream/record get a fresh frame (KEY_* values from <yetty/api/ymux/engine.h>:
 * UP=5, RIGHT=8). */

/* Splits: a cursor sequence fragmented at EVERY interior byte decodes to exactly
 * one structured key; the leading partial is fully carried, emitting nothing. */
static void test_key_stream_split(struct ytest *test)
{
    const uint8_t up_seq[] = {0x1b, '[', 'A'};
    for (size_t split = 1; split < sizeof(up_seq); ++split) {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        int detach =
            yetty_ymux_key_stream_feed(&stream, up_seq, split, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_INT(test, detach, 0);
        YTEST_CHECK_EQ_SIZE(test, record.count, 0);                /* nothing yet */
        YTEST_CHECK(test, stream.esc_carry_len == (uint8_t)split); /* whole partial held */
        yetty_ymux_key_stream_feed(&stream, up_seq + split, sizeof(up_seq) - split,
                                   key_stream_record_emit, &record);
        YTEST_CHECK_EQ_SIZE(test, record.count, 1);
        YTEST_CHECK_EQ_INT(test, record.events[0].action, YETTY_YMUX_KEY_STREAM_KEY);
        YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 5 /*UP*/);
        YTEST_CHECK(test, stream.esc_carry_len == 0);
    }
}

/* Escape-timeout: a lone ESC is carried, then flush_carry (the deadline action)
 * delivers it as a raw ESC codepoint — tmux's escape-time behavior. */
static void test_key_stream_timeout_esc(struct ytest *test)
{
    struct yetty_ymux_key_stream stream;
    yetty_ymux_key_stream_init(&stream);
    struct key_stream_record record = {0};
    const uint8_t esc[] = {0x1b};
    yetty_ymux_key_stream_feed(&stream, esc, 1, key_stream_record_emit, &record);
    YTEST_CHECK_EQ_SIZE(test, record.count, 0); /* held, not delivered */
    YTEST_CHECK(test, stream.esc_carry_len == 1);
    yetty_ymux_key_stream_flush_carry(&stream, key_stream_record_emit, &record);
    YTEST_CHECK_EQ_SIZE(test, record.count, 1);
    YTEST_CHECK_EQ_INT(test, record.events[0].action, YETTY_YMUX_KEY_STREAM_CODEPOINT);
    YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 0x1b);
    YTEST_CHECK(test, stream.esc_carry_len == 0);
}

/* Escape-timeout of a partial CSI (\e[): both carried bytes flush as raw
 * codepoints, in order. */
static void test_key_stream_timeout_csi(struct ytest *test)
{
    struct yetty_ymux_key_stream stream;
    yetty_ymux_key_stream_init(&stream);
    struct key_stream_record record = {0};
    const uint8_t partial[] = {0x1b, '['};
    yetty_ymux_key_stream_feed(&stream, partial, 2, key_stream_record_emit, &record);
    YTEST_CHECK(test, stream.esc_carry_len == 2);
    yetty_ymux_key_stream_flush_carry(&stream, key_stream_record_emit, &record);
    YTEST_CHECK_EQ_SIZE(test, record.count, 2);
    YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 0x1b);
    YTEST_CHECK_EQ_INT(test, (int)record.events[1].value, '[');
}

/* tmux prefix: C-b d detaches (feed returns 1 + DETACH action), whether the
 * chord arrives in one chunk or split across two (the armed-prefix state
 * persists in the stream). C-b C-b delivers ONE literal C-b. */
static void test_key_stream_prefix(struct ytest *test)
{
    {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        const uint8_t chord[] = {0x02, 'd'};
        int detach = yetty_ymux_key_stream_feed(&stream, chord, 2, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_INT(test, detach, 1);
        YTEST_CHECK_EQ_SIZE(test, record.count, 1);
        YTEST_CHECK_EQ_INT(test, record.events[0].action, YETTY_YMUX_KEY_STREAM_DETACH);
    }
    {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        const uint8_t cb[] = {0x02};
        const uint8_t dee[] = {'d'};
        /* Capture each feed result in a variable BEFORE asserting — the check
         * macro evaluates its argument twice, and feed() mutates the stream. */
        int arm_detach =
            yetty_ymux_key_stream_feed(&stream, cb, 1, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_INT(test, arm_detach, 0);
        YTEST_CHECK(test, stream.prefix_armed == 1);
        int split_detach =
            yetty_ymux_key_stream_feed(&stream, dee, 1, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_INT(test, split_detach, 1);
    }
    {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        const uint8_t chord[] = {0x02, 0x02};
        yetty_ymux_key_stream_feed(&stream, chord, 2, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_SIZE(test, record.count, 1);
        YTEST_CHECK_EQ_INT(test, record.events[0].action, YETTY_YMUX_KEY_STREAM_CODEPOINT);
        YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 0x02);
    }
}

/* UTF-8 (é = U+00E9 = 0xC3 0xA9) split across chunks reassembles to ONE
 * codepoint, not two mojibake bytes; and a structured key immediately followed
 * by a printable (\e[C x) yields RIGHT then 'x'. */
static void test_key_stream_utf8_and_mixed(struct ytest *test)
{
    {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        const uint8_t lead[] = {0xC3};
        const uint8_t cont[] = {0xA9};
        yetty_ymux_key_stream_feed(&stream, lead, 1, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_SIZE(test, record.count, 0); /* awaiting the continuation */
        yetty_ymux_key_stream_feed(&stream, cont, 1, key_stream_record_emit, &record);
        YTEST_CHECK_EQ_SIZE(test, record.count, 1);
        YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 0x00E9 /* é */);
    }
    {
        struct yetty_ymux_key_stream stream;
        yetty_ymux_key_stream_init(&stream);
        struct key_stream_record record = {0};
        const uint8_t seq[] = {0x1b, '[', 'C', 'x'};
        yetty_ymux_key_stream_feed(&stream, seq, sizeof(seq), key_stream_record_emit, &record);
        YTEST_CHECK_EQ_SIZE(test, record.count, 2);
        YTEST_CHECK_EQ_INT(test, record.events[0].action, YETTY_YMUX_KEY_STREAM_KEY);
        YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 8 /*RIGHT*/);
        YTEST_CHECK_EQ_INT(test, record.events[1].action, YETTY_YMUX_KEY_STREAM_CODEPOINT);
        YTEST_CHECK_EQ_INT(test, (int)record.events[1].value, 'x');
    }
}

/* Extended split-carry: the streaming carry must hold ANY parameterised CSI
 * fragmented across chunks — multi-digit \e[<n>~ function keys and CSI-u
 * extended keys — not just the old \e[<1-6> single-digit form, and must never
 * overflow the carry buffer. */
static void test_key_carry_extended(struct ytest *test)
{
    /* F5 = \e[15~ (two digits): complete carries 0; every interior split holds
     * its whole partial (the old carry leaked \e[15, breaking the split). */
    const uint8_t f5[] = {0x1b, '[', '1', '5', '~'};
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(f5, 0, sizeof(f5)), 0);
    for (size_t split = 1; split < sizeof(f5); ++split) {
        YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(f5, 0, split), split);
    }
    /* CSI-u extended key \e[97;5u: same property. */
    const uint8_t csi_u[] = {0x1b, '[', '9', '7', ';', '5', 'u'};
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(csi_u, 0, sizeof(csi_u)), 0);
    for (size_t split = 1; split < sizeof(csi_u); ++split) {
        YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(csi_u, 0, split), split);
    }
    /* Application-keypad SS3 (\eOp = KP_0 under DECKPAM): the ESC O prefix is
     * held until the final arrives, so a keypad key fragmented across chunks is
     * not forwarded as a stranded partial (then delivered whole to the pane). */
    const uint8_t kp0[] = {0x1b, 'O', 'p'};
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(kp0, 0, sizeof(kp0)), 0); /* complete */
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(kp0, 0, 1), 1);           /* ESC */
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(kp0, 0, 2), 2);           /* ESC O */
    /* ESC + letter is NOT a CSI/SS3 prefix — not carried. */
    const uint8_t esc_a[] = {0x1b, 'a'};
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(esc_a, 0, 2), 0);
    /* An over-long incomplete CSI is forwarded, not held — no esc_carry overflow. */
    uint8_t longcsi[YETTY_YMUX_KEY_CARRY_MAX + 4];
    longcsi[0] = 0x1b;
    longcsi[1] = '[';
    for (size_t index = 2; index < sizeof(longcsi); ++index) {
        longcsi[index] = '1'; /* all params, never a final */
    }
    YTEST_CHECK_EQ_SIZE(test, yetty_ymux_tty_key_carry_len(longcsi, 0, sizeof(longcsi)), 0);

    /* Streaming: \e[15~ fed as \e[15 then ~ is HELD across the boundary, then the
     * completed sequence DECODES to ONE structured F5 key — the daemon re-encodes
     * it against the pane mode, so it is never a stranded partial nor 5 raw
     * bytes (the pre-grammar behaviour, when \e[15~ was not recognised). */
    struct yetty_ymux_key_stream stream;
    yetty_ymux_key_stream_init(&stream);
    struct key_stream_record record = {0};
    yetty_ymux_key_stream_feed(&stream, f5, 4, key_stream_record_emit, &record);
    YTEST_CHECK_EQ_SIZE(test, record.count, 0); /* held, not leaked */
    YTEST_CHECK(test, stream.esc_carry_len == 4);
    yetty_ymux_key_stream_feed(&stream, f5 + 4, 1, key_stream_record_emit, &record);
    YTEST_CHECK_EQ_SIZE(test, record.count, 1); /* one structured F5 key */
    YTEST_CHECK_EQ_INT(test, (int)record.events[0].action, YETTY_YMUX_KEY_STREAM_KEY);
    YTEST_CHECK_EQ_INT(test, (int)record.events[0].value, 37 /*F5*/);
    YTEST_CHECK(test, stream.esc_carry_len == 0);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_key_encode");
    YTEST_RUN(&test, test_control_chords);
    YTEST_RUN(&test, test_ctrl_char_correlation);
    YTEST_RUN(&test, test_down_char_up_stream);
    YTEST_RUN(&test, test_tty_key_decode);
    YTEST_RUN(&test, test_tty_key_streaming);
    YTEST_RUN(&test, test_key_stream_split);
    YTEST_RUN(&test, test_key_stream_timeout_esc);
    YTEST_RUN(&test, test_key_stream_timeout_csi);
    YTEST_RUN(&test, test_key_stream_prefix);
    YTEST_RUN(&test, test_key_stream_utf8_and_mixed);
    YTEST_RUN(&test, test_key_carry_extended);
    YTEST_RUN(&test, test_alt_printable);
    YTEST_RUN(&test, test_utf8_planes);
    YTEST_RUN(&test, test_modified_specials);
    YTEST_RUN(&test, test_function_keys);
    YTEST_RUN(&test, test_editing_and_dedup);
    return ytest_end(&test);
}
