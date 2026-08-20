/*
 * The #699 VT projection contract: project_vt's operation-driven tmux framing
 * (civis + per-row EL, shortest cursor moves, SGR intent), incremental deltas,
 * scroll, capability downgrade, recovery redraw, cursor-only changes, and
 * combining-mark passthrough. (The retired semantic paint path's parity tests
 * lived here until #699.3 removed the surface class.)
 */

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>
#include <yetty/ycore/types.h>

#include "../../../src/yetty/ymux/proto.h" /* YMUX_TERM_CAP_* flags */

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct rig {
    struct yetty_yclass_object *pane;
    struct yetty_yclass_object *attachment;
    struct yetty_yclass_object *projector;
};

static struct rig rig_make(struct ytest *test, uint32_t rows, uint32_t cols)
{
    struct rig rig;
    rig.pane = yetty_ymux_pane_make(rows, cols, /*hot_rows=*/16, /*total_row_cap=*/0, NULL).value;
    YTEST_REQUIRE_NOT_NULL(test, rig.pane);
    rig.attachment = yetty_ymux_attachment_make(rig.pane, rows, cols).value;
    YTEST_REQUIRE_NOT_NULL(test, rig.attachment);
    rig.projector = yetty_ymux_projector_make(rig.pane, rig.attachment).value;
    YTEST_REQUIRE_NOT_NULL(test, rig.projector);
    return rig;
}

static void rig_dispose(struct rig *rig)
{
    yetty_ymux_projector_dispose(rig->projector);
    yetty_ymux_attachment_dispose(rig->attachment);
    yetty_ymux_pane_dispose(rig->pane);
}

/* Find `needle` (ASCII) anywhere in the projected VT byte stream. */
static int vt_contains(const uint8_t *bytes, size_t len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || len < needle_len) {
        return 0;
    }
    for (size_t offset = 0; offset + needle_len <= len; ++offset) {
        if (memcmp(bytes + offset, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* #699: the projector emits the viewport as ordinary terminal (VT) redraw
 * bytes — a tmux-style projected redraw the yscene client libvterm consumes.
 * Assert the redraw carries the expected framing + text + attributes. */
static void test_vt_projection(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "Hello\r\n\x1b[31mWorld", 17));

    struct yetty_ycore_buffer_result buffer_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, buffer_res);
    struct yetty_ycore_buffer buffer = buffer_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &buffer));
    YTEST_REQUIRE(test, buffer.size > 0);

    const uint8_t *vt = buffer.data;
    size_t len = buffer.size;
    /* #699 operation-driven framing (tmux tty_draw_line): the FULL redraw hides
     * the cursor (civis), homes, and draws EVERY row with a trailing EL — NO
     * whole-screen \e[2J and NO autowrap-disable \e[?7l. */
    YTEST_CHECK(test, vt_contains(vt, len, "\x1b[?25l")); /* cursor hidden for redraw */
    YTEST_CHECK(test, !vt_contains(vt, len, "\x1b[?7l")); /* NO autowrap-off preamble */
    YTEST_CHECK(test, !vt_contains(vt, len, "\x1b[2J"));  /* NO whole-screen clear */
    YTEST_CHECK(test, vt_contains(vt, len, "\x1b[H"));    /* cursor home (first move) */
    YTEST_CHECK(test, vt_contains(vt, len, "\x1b[K"));    /* per-line EL clear (BCE) */
    /* Move from end of row 0 to the start of row 1 is the SHORTEST tmux move
     * (cr + cud1 = "\r\n"), not an absolute CUP — byte-parity with tty_cursor. */
    YTEST_CHECK(test, vt_contains(vt, len, "\r\n"));  /* shortest move to row 2 */
    YTEST_CHECK(test, vt_contains(vt, len, "Hello")); /* row 0 text */
    YTEST_CHECK(test, vt_contains(vt, len, "World")); /* row 1 text */
    /* SGR 31 is an INDEXED colour: the reverse-map recovers tmux's colour
     * intent, so red re-emits as setaf (\e[31m) — never truecolour — matching
     * what tmux would send for the same indexed pen. Default bg emits nothing. */
    YTEST_CHECK(test, vt_contains(vt, len, "\x1b[31m")); /* red fg = setaf 1 */
    YTEST_CHECK(test, !vt_contains(vt, len, "38;2;"));   /* indexed, not truecolour */
    YTEST_CHECK(test, !vt_contains(vt, len, "48;2;"));   /* default bg = no SGR */
    YTEST_CHECK(test, vt_contains(vt, len, "\x1b[?25")); /* cursor visibility */

    yetty_ycore_buffer_destroy(&buffer);
    rig_dispose(&rig);
}

/* #699 tmux parity: after the first (FULL) projection establishes the client,
 * a single new character must project as an incremental delta — the changed
 * cell and nothing else (no clear, no re-dump). This is the property that
 * keeps a keystroke echo ~1 byte instead of a whole-screen redraw. */
static void test_vt_incremental(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abc", 3));

    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    YTEST_CHECK(test, vt_contains(full.data, full.size, "\x1b[?25l")); /* FULL hides cursor */
    YTEST_CHECK(test, !vt_contains(full.data, full.size, "\x1b[2J"));  /* no whole-screen clear */
    YTEST_CHECK(test, vt_contains(full.data, full.size, "abc"));

    /* One more character. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "d", 1));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));

    YTEST_CHECK(test, delta.size > 0);
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "\x1b[2J")); /* NOT a redraw */
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "d"));
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "abc")); /* unchanged, not resent */
    YTEST_CHECK(test, delta.size < 16); /* ~1 byte: the changed cell, at the parked cursor */

    yetty_ycore_buffer_destroy(&full);
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
}

/* #699 tmux parity: a newline on the BOTTOM row scrolls the whole viewport up
 * one line. The delta must use tmux's scrollup idiom — LF at the bottom row
 * (which pushes the top line into the CLIENT terminal's scrollback, unlike
 * \e[1S which would discard it) plus the BCE clear of the vacated row and the
 * one new bottom line — NOT a re-emit of every shifted row. */
static void test_vt_scroll(struct ytest *test)
{
    struct rig rig = rig_make(test, 4, 20);
    /* Fill all four rows; cursor ends on the bottom row, no scroll yet. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "L0\r\nL1\r\nL2\r\nL3", 14));

    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    YTEST_CHECK(test, vt_contains(full.data, full.size, "\x1b[?25l")); /* FULL hides cursor */

    /* Newline on the bottom row → scroll up one; new bottom line "L4". */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "\r\nL4", 4));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));

    YTEST_CHECK(test, delta.size > 0);
    /* The scroll idiom: cursor parks at (0, bottom) then LF + \e[K. The LF
     * rides at the very start when the cursor is already there. */
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "\n"));
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "\x1b[K"));   /* vacated-row BCE */
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "\x1b[1S")); /* NOT the SU form */
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "L4"));       /* the new line */
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "\x1b[2J")); /* NOT a redraw */
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "L1"));      /* shifted, not resent */
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "L2"));
    YTEST_CHECK(test, delta.size < 48); /* scroll op + one short line */

    yetty_ycore_buffer_destroy(&full);
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
}

/* #699 perf guard: on a LARGE, mostly-blank viewport a single keystroke must
 * still project as a tiny delta. Scroll detection walks the whole grid every
 * frame; a naive O(rows^2 * cols) row-memcmp scan (blank rows match across every
 * shift) would make THIS test take seconds. Hash-based detection keeps it O(n),
 * so this both checks correctness and pins the cost. */
static void test_vt_large_screen_fast(struct ytest *test)
{
    struct rig rig = rig_make(test, 100, 300);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "prompt$ ", 8));

    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(1u << 20);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));

    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "x", 1));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(1u << 20);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));

    YTEST_CHECK(test, delta.size > 0);
    YTEST_CHECK(test, delta.size < 32); /* one changed cell, not a screen re-emit */
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "x"));
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "\x1b[2J"));

    yetty_ycore_buffer_destroy(&full);
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
}

/* #699 terminfo capability handshake: a client that advertises truecolor gets
 * an RGB cell passed through as \e[38;2;R;G;Bm; a client that does NOT gets the
 * same cell downgraded to the nearest 256-palette index (\e[38;5;Nm) — the same
 * profile-dependent choice tmux makes. */
static void test_vt_capabilities_downgrade(struct ytest *test)
{
    /* Truecolor client (the projector default): RGB passes through. */
    struct rig rgb = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rgb.pane, "\x1b[38;2;255;100;0mZ", 18));
    struct yetty_ycore_buffer tc = yetty_ycore_buffer_create(16384).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rgb.projector, &tc));
    YTEST_CHECK(test, vt_contains(tc.data, tc.size, "\x1b[38;2;255;100;0m")); /* truecolor */
    yetty_ycore_buffer_destroy(&tc);
    rig_dispose(&rgb);

    /* 256-only client (capabilities cleared): the same RGB cell downgrades to a
     * palette index — a 38;5; SGR, and NOT a 38;2; truecolor SGR. */
    struct rig idx = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_capabilities(idx.projector, 0));
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(idx.pane, "\x1b[38;2;255;100;0mZ", 18));
    struct yetty_ycore_buffer dn = yetty_ycore_buffer_create(16384).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(idx.projector, &dn));
    YTEST_CHECK(test, vt_contains(dn.data, dn.size, "\x1b[38;5;"));  /* 256 index */
    YTEST_CHECK(test, !vt_contains(dn.data, dn.size, "\x1b[38;2;")); /* not truecolor */
    yetty_ycore_buffer_destroy(&dn);
    rig_dispose(&idx);
}

/* #699 slow-client recovery (the projector half): after the attachment TTY is
 * invalidated (what the daemon does when it discards an unusable queued stream),
 * the NEXT VT projection must be a fresh COMPLETE redraw — the operation-driven
 * full redraw (civis + home + every row drawn with trailing EL) with the full
 * viewport content — never a stale incremental delta that would desync it. */
static void test_vt_recovery_complete_redraw(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abc\r\ndef", 8));

    struct yetty_ycore_buffer full = yetty_ycore_buffer_create(16384).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    YTEST_CHECK(test, vt_contains(full.data, full.size, "\x1b[?25l")); /* FULL hides cursor */
    yetty_ycore_buffer_destroy(&full);

    /* An incremental change projects as a small delta (no clear). */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "x", 1));
    struct yetty_ycore_buffer delta = yetty_ycore_buffer_create(16384).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));
    YTEST_CHECK(test, !vt_contains(delta.data, delta.size, "\x1b[2J")); /* delta, not redraw */
    yetty_ycore_buffer_destroy(&delta);

    /* Invalidate (the recovery step), then project: a COMPLETE redraw returns —
     * the operation-driven full redraw (civis + home + every row) with the whole
     * viewport content re-emitted, not a stale incremental delta. */
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_invalidate(rig.projector));
    struct yetty_ycore_buffer redraw = yetty_ycore_buffer_create(16384).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &redraw));
    YTEST_CHECK(test, vt_contains(redraw.data, redraw.size, "\x1b[?25l")); /* FULL hides cursor */
    YTEST_CHECK(test, !vt_contains(redraw.data, redraw.size, "\x1b[2J"));  /* no 2J (per-line EL) */
    YTEST_CHECK(test, vt_contains(redraw.data, redraw.size, "abc"));       /* content redrawn */
    YTEST_CHECK(test, vt_contains(redraw.data, redraw.size, "def"));
    YTEST_CHECK(test, vt_contains(redraw.data, redraw.size, "x")); /* incl. the late change */
    yetty_ycore_buffer_destroy(&redraw);

    rig_dispose(&rig);
}

/* A cursor-only move (CUP over unchanged cells) produces NO semantic paint
 * delta, but the VT path must still emit the cursor move — otherwise the client
 * cursor goes stale. This pins the projector half; the daemon drives project_vt
 * independently of the paint diff so the frame is actually sent. */
static void test_vt_cursor_only_change(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abc", 3));

    /* Establish the VT shadow. */
    struct yetty_ycore_buffer vt0 = yetty_ycore_buffer_create(8192).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &vt0));
    yetty_ycore_buffer_destroy(&vt0);

    /* Move the cursor with NO cell change. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "\x1b[1;1H", 6));

    /* A cursor-only change still emits the move on the VT path. */
    struct yetty_ycore_buffer vt = yetty_ycore_buffer_create(8192).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &vt));
    YTEST_CHECK(test, vt.size > 0);
    yetty_ycore_buffer_destroy(&vt);
    rig_dispose(&rig);
}

/* Combining marks a base glyph carries must be emitted after the base codepoint
 * so the receiver composes the same grapheme (dropping them loses accents). */
static void test_vt_combining_marks(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    /* 'e' + U+0301 combining acute (UTF-8 CC 81). */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "e\xcc\x81", 3));
    struct yetty_ycore_buffer vt = yetty_ycore_buffer_create(8192).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &vt));
    YTEST_CHECK(test, vt_contains(vt.data, vt.size, "e"));        /* base glyph */
    YTEST_CHECK(test, vt_contains(vt.data, vt.size, "\xcc\x81")); /* combining mark */
    yetty_ycore_buffer_destroy(&vt);
    rig_dispose(&rig);
}

/* Cycle-22 P0: the resolved terminfo model must DRIVE production emission and
 * survive the attach-preamble reset and every full redraw (tty_init zeroes
 * tty->term; the projector must re-point it). Install a distinctive cursor-
 * address override (cup ends in Z, not H) and assert a REAL projector redraw
 * — the production path, not a bare tty — emits it. Then invalidate (forcing
 * the full-redraw reset) and assert the override STILL drives the bytes. */
static void test_vt_terminfo_model_drives_production(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    /* Override cursor_home with a distinctive final byte (Z, not H) via the
     * features channel's terminal-overrides (cap=value) semantics. The redraw
     * deterministically moves the cursor to (0,0) at its start, which
     * tty_cursor emits as `home`. */
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(rig.projector, "xterm-256color",
                                                             "256,RGB,home=\\E[7Z"));
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "top\r\nbottom", 11));

    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    /* The overridden home drove production cursor emission (final byte Z). */
    YTEST_CHECK(test, vt_contains(full.data, full.size, "\x1b[7Z"));
    yetty_ycore_buffer_destroy(&full);

    /* Force the full-redraw reset path (tty_init) and prove the model still
     * drives the bytes — the cycle-22 regression was the model being erased
     * here, silently reverting to the hard-coded H form. */
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_invalidate(rig.projector));
    struct yetty_ycore_buffer_result redraw_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, redraw_res);
    struct yetty_ycore_buffer redraw = redraw_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &redraw));
    YTEST_CHECK(test, vt_contains(redraw.data, redraw.size, "\x1b[7Z"));
    yetty_ycore_buffer_destroy(&redraw);

    rig_dispose(&rig);
}

/* Drive a bounded mid-line erase (content remains after the cleared span, so
 * the projector cannot substitute EL) and return whether the delta emitted an
 * ECH (`\e[<n>X`) sequence vs the escape-less spaces tail. `features` sets the
 * terminal so the caller can compare ECH present vs cancelled. */
static int project_bounded_erase_uses_ech(struct ytest *test, const char *features)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_projector_set_terminal(rig.projector, "xterm-256color", features));
    /* Baseline: text + a cursor park at col 3 (the tmux-diff `ech-mid-row`
     * shape). Project it so the delta below is JUST the erase op. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abcdef\x1b[1;3H", 12));
    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    yetty_ycore_buffer_destroy(&full);

    /* ECH-erase 2 chars at col 3 ("cd"): "ef" remains after — a BOUNDED erase
     * the projector reflects as ECH (caps.ech) or the escape-less spaces tail
     * (cancelled), never EL. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "\x1b[2X", 4));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));
    int uses_ech = vt_contains(delta.data, delta.size, "\x1b[2X");
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
    return uses_ech;
}

/* Cycle-22 P0: a CANCELLED capability must flip the projector's STRATEGY, not
 * silently re-emit the operation as a literal. With ECH present the bounded
 * erase emits `\e[6X`; with `ech@` it must take the escape-less spaces tail
 * (no ECH). Before the model-derived strategy booleans, caps.ech stayed true
 * and ECH was emitted in both cases. */
static void test_vt_cancelled_ech_takes_spaces(struct ytest *test)
{
    YTEST_CHECK(test, project_bounded_erase_uses_ech(test, "256,RGB") == 1);
    YTEST_CHECK(test, project_bounded_erase_uses_ech(test, "256,RGB,ech@") == 0);
}

/* Cycle-23 P1: the public capability MASK and the renderer STRATEGY must not
 * disagree after cancellation. `ech@` / `csr@` must clear the corresponding
 * mask bits (they share the authoritative caps_resolve verdict); the synthetic
 * xterm-nocsr family must report DECSTBM absent WITHOUT an explicit csr@. */
static void test_vt_capability_mask_consistency(struct ytest *test)
{
    struct rig base = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(
        test, yetty_ymux_projector_set_terminal(base.projector, "xterm-256color", "256,RGB"));
    struct yetty_ycore_uint32_result base_caps = yetty_ymux_projector_capabilities(base.projector);
    YTEST_REQUIRE_OK(test, base_caps);
    YTEST_CHECK(test, (base_caps.value & YMUX_TERM_CAP_ECH) != 0);
    YTEST_CHECK(test, (base_caps.value & YMUX_TERM_CAP_DECSTBM) != 0);
    rig_dispose(&base);

    /* ech@ + csr@ clear their mask bits. */
    struct rig cancelled = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(cancelled.projector, "xterm-256color",
                                                             "256,RGB,ech@,csr@"));
    struct yetty_ycore_uint32_result cancelled_caps =
        yetty_ymux_projector_capabilities(cancelled.projector);
    YTEST_REQUIRE_OK(test, cancelled_caps);
    YTEST_CHECK(test, (cancelled_caps.value & YMUX_TERM_CAP_ECH) == 0);
    YTEST_CHECK(test, (cancelled_caps.value & YMUX_TERM_CAP_DECSTBM) == 0);
    rig_dispose(&cancelled);

    /* The synthetic no-CSR family reports DECSTBM absent from the mask too —
     * cycle-24 folds the family cancellation INTO the model, and the strategy
     * is derived from the model, so both agree the cap is gone. */
    struct rig nocsr = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_projector_set_terminal(nocsr.projector, "xterm-nocsr", "256"));
    struct yetty_ycore_uint32_result nocsr_caps =
        yetty_ymux_projector_capabilities(nocsr.projector);
    YTEST_REQUIRE_OK(test, nocsr_caps);
    YTEST_CHECK(test, (nocsr_caps.value & YMUX_TERM_CAP_DECSTBM) == 0);
    rig_dispose(&nocsr);
}

/* Cycle-24 P0: ONE resolved capability authority — the strategy mask is derived
 * from the resolved MODEL, so a `cap@` cancellation, a `cap=` ADDITION, and a
 * terminfo value containing ':' all resolve consistently. */
static void test_vt_capability_authority(struct ytest *test)
{
    /* A `csr=` ADDITION must ENABLE the DECSTBM strategy (cycle-24: additions
     * change the model and the strategy derives from it — before, only feature
     * names/cancellations were recognised so an addition could not enable it).
     * Start from the no-CSR family, then add csr back. */
    struct rig readd = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(readd.projector, "xterm-nocsr",
                                                             "256,csr=\\E[%i%p1%d;%p2%dr"));
    struct yetty_ycore_uint32_result readd_caps =
        yetty_ymux_projector_capabilities(readd.projector);
    YTEST_REQUIRE_OK(test, readd_caps);
    YTEST_CHECK(test, (readd_caps.value & YMUX_TERM_CAP_DECSTBM) != 0); /* addition enabled it */
    rig_dispose(&readd);

    /* A ':'-bearing override value must NOT fragment the tokenizer. Applying a
     * Setulc override (value packed with '::') must enable the underline-colour
     * strategy without the split-on-':' bug corrupting the token stream. */
    struct rig colon = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(
                               colon.projector, "xterm-256color",
                               "256,RGB,Setulc=\\E[58::2::%p1%{65536}%/%d::%p1%{256}%/%{255}%&%d::"
                               "%p1%{255}%&%d%;m"));
    struct yetty_ycore_uint32_result colon_caps =
        yetty_ymux_projector_capabilities(colon.projector);
    YTEST_REQUIRE_OK(test, colon_caps);
    YTEST_CHECK(test, (colon_caps.value & YMUX_TERM_CAP_UNDERLINE_COLOUR) != 0);
    /* ECH/DECSTBM unaffected — the ':' did not leak into other tokens. */
    YTEST_CHECK(test, (colon_caps.value & YMUX_TERM_CAP_ECH) != 0);
    YTEST_CHECK(test, (colon_caps.value & YMUX_TERM_CAP_DECSTBM) != 0);
    rig_dispose(&colon);
}

/* Cycle-24 P0: IL and DL resolve INDEPENDENTLY. With `dl@` an insert-line op
 * still emits IL; with `il@` an insert-line op must take the fallback (no IL).
 * Baseline mirrors the incremental `il-one` shape. */
static int project_insert_line_emits_il(struct ytest *test, const char *features)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_projector_set_terminal(rig.projector, "xterm-256color", features));
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abc\r\ndef\x1b[2;1H", 14));
    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    yetty_ycore_buffer_destroy(&full);

    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "\x1b[1L", 4));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));
    int emits_il = vt_contains(delta.data, delta.size, "\x1b[1L");
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
    return emits_il;
}

static void test_vt_il_dl_independent(struct ytest *test)
{
    YTEST_CHECK(test, project_insert_line_emits_il(test, "256,RGB") == 1);     /* both present */
    YTEST_CHECK(test, project_insert_line_emits_il(test, "256,RGB,dl@") == 1); /* dl@ leaves il */
    YTEST_CHECK(test, project_insert_line_emits_il(test, "256,RGB,il@") == 0); /* il@ → fallback */
}

/* Cycle-23 P0: the projector's redraw erases must ROUTE through the resolved
 * model, not emit hard-coded \e[K/\e[J. Override clr_eol with a distinctive
 * sequence (\e[9K) and assert a real projector redraw — which clears every row
 * with EL at its start — emits the override, proving the erase capability is
 * taken from the model and a `el=` override (or `el@` cancellation) reaches
 * production. */
static void test_vt_projector_erase_uses_model(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(rig.projector, "xterm-256color",
                                                             "256,RGB,el=\\E[9K"));
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "hello\r\nworld", 12));

    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    /* The redraw clears each row with EL; the override drove those bytes. */
    YTEST_CHECK(test, vt_contains(full.data, full.size, "\x1b[9K"));
    yetty_ycore_buffer_destroy(&full);
    rig_dispose(&rig);
}

/* Cycle-23 P0: the projector's PARAMETERIZED edit ops (IL/DL/ICH/DCH) must also
 * route through the resolved model. Override parm_insert_line (il=\e[9L) and
 * assert a real IL delta — baseline established, then an insert-line op — emits
 * the override, proving vt_param_cap takes the sequence (and its count) from the
 * model. */
static void test_vt_projector_edit_uses_model(struct ytest *test)
{
    struct rig rig = rig_make(test, 24, 80);
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_set_terminal(rig.projector, "xterm-256color",
                                                             "256,RGB,il=\\E[9L"));
    /* Establish the client with two rows and park the cursor at (row 2, col 1),
     * matching the incremental `il-one` baseline. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "abc\r\ndef\x1b[2;1H", 14));
    struct yetty_ycore_buffer_result full_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, full_res);
    struct yetty_ycore_buffer full = full_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &full));
    yetty_ycore_buffer_destroy(&full);

    /* Insert one line: the delta emits IL, driven by the override. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(rig.pane, "\x1b[1L", 4));
    struct yetty_ycore_buffer_result delta_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, delta_res);
    struct yetty_ycore_buffer delta = delta_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(rig.projector, &delta));
    YTEST_CHECK(test, vt_contains(delta.data, delta.size, "\x1b[9L")); /* override drove IL */
    yetty_ycore_buffer_destroy(&delta);
    rig_dispose(&rig);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_projection");
    YTEST_RUN(&test, test_vt_projection);
    YTEST_RUN(&test, test_vt_terminfo_model_drives_production);
    YTEST_RUN(&test, test_vt_cancelled_ech_takes_spaces);
    YTEST_RUN(&test, test_vt_capability_mask_consistency);
    YTEST_RUN(&test, test_vt_capability_authority);
    YTEST_RUN(&test, test_vt_il_dl_independent);
    YTEST_RUN(&test, test_vt_projector_erase_uses_model);
    YTEST_RUN(&test, test_vt_projector_edit_uses_model);
    YTEST_RUN(&test, test_vt_incremental);
    YTEST_RUN(&test, test_vt_scroll);
    YTEST_RUN(&test, test_vt_large_screen_fast);
    YTEST_RUN(&test, test_vt_capabilities_downgrade);
    YTEST_RUN(&test, test_vt_recovery_complete_redraw);
    YTEST_RUN(&test, test_vt_cursor_only_change);
    YTEST_RUN(&test, test_vt_combining_marks);
    return ytest_end(&test);
}
