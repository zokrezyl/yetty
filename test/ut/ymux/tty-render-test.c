/*
 * ymux TTY renderer byte-parity (#699): the ported tty_cursor emitter
 * (src/yetty/ymux/tty-render.c) must produce byte-for-byte the same cursor
 * movement bytes tmux's tty_cursor produces for the fixed xterm-256color
 * profile — headless, no GPU, no yvterm. This is the first slice of the
 * differential parity contract; the sequences below are the exact bytes tmux
 * (pinned d5afb67) emits for each transition with that terminfo.
 */
#include <string.h>

#include <yetty/ycore/types.h>

#include "ytest.h"

/* Module-internal renderer header (not a public API surface). */
#include "../../../src/yetty/ymux/tty-render.h"

/* Emit a cursor move and assert the produced bytes match `expected` exactly,
 * then that the assumed cursor advanced. */
static void check_move(struct ytest *test, struct yetty_ymux_tty *tty, uint32_t cx, uint32_t cy,
                       const char *expected)
{
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(64).value;
    struct yetty_ycore_void_result res = yetty_ymux_tty_cursor(tty, &out, cx, cy);
    YTEST_REQUIRE_OK(test, res);
    size_t expected_len = strlen(expected);
    YTEST_CHECK_EQ_SIZE(test, out.size, expected_len);
    if (out.size == expected_len && out.data) {
        YTEST_CHECK(test, memcmp(out.data, expected, expected_len) == 0);
    }
    YTEST_CHECK_EQ_INT(test, (int)tty->cx, (int)cx);
    YTEST_CHECK_EQ_INT(test, (int)tty->cy, (int)cy);
    yetty_ycore_buffer_destroy(&out);
}

/* tty_cursor shortest-move selection, byte-identical to tmux for
 * xterm-256color (home, \r/\r\n, cub1/cuf1/cuu1/cud1, cub/cuf/cuu/cud, hpa/vpa,
 * absolute cup). */
static void test_tty_cursor_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    check_move(test, &tty, 5, 0, "\x1b[5C");       /* cuf: |change|<=cx */
    check_move(test, &tty, 0, 0, "\x1b[H");        /* home */
    check_move(test, &tty, 0, 1, "\r\n");          /* zero on next line */
    check_move(test, &tty, 0, 0, "\x1b[H");        /* home */
    check_move(test, &tty, 0, 5, "\x1b[5B");       /* cud (row down) */
    check_move(test, &tty, 1, 5, "\x1b[C");        /* cuf1: one right */
    check_move(test, &tty, 0, 5, "\r");            /* to left edge */
    check_move(test, &tty, 10, 10, "\x1b[11;11H"); /* absolute cup (1-based) */
    check_move(test, &tty, 9, 10, "\b");           /* cub1: one left */
    check_move(test, &tty, 10, 10, "\x1b[C");      /* cuf1: one right */
    check_move(test, &tty, 10, 9, "\x1b[A");       /* cuu1: one up */
    check_move(test, &tty, 10, 10, "\n");          /* cud1: one down */
    check_move(test, &tty, 2, 10, "\x1b[3G");      /* hpa: |change|=8 > cx=2 */

    /* cub1 x2 optimization for a left move of exactly two columns. */
    yetty_ymux_tty_init(&tty, 24, 80);
    check_move(test, &tty, 12, 0, "\x1b[12C");
    check_move(test, &tty, 10, 0, "\b\b");
}

/* The tty_cursor branches not exercised by test_tty_cursor_parity (which covers
 * home / \r / \r\n / cub1 / cuf1 / cub1x2 / cuu1 / cud1 / cud / cuf / hpa /
 * absolute): the multi-step cub, the multi-step cuu, and the vpa fallback when
 * the absolute row param is shorter than the relative move. Each is pinned to
 * the exact byte form the tmux-port emitter produces. */
static void test_tty_cursor_all_branches(struct ytest *test)
{
    struct yetty_ymux_tty tty;

    /* Multi-step cub: a left move of >2 columns whose magnitude fits (<=cx), so
     * it is cheaper than the absolute hpa. */
    yetty_ymux_tty_init(&tty, 24, 80);
    check_move(test, &tty, 40, 0, "\x1b[40C"); /* cuf out to column 40 */
    check_move(test, &tty, 35, 0, "\x1b[5D");  /* cub: left 5 (>2, magnitude<=cx) */

    /* Multi-step cuu: an up move of >1 rows within the scroll region, magnitude
     * fits, so it is cheaper than vpa. Establish the low row with vpa first: a
     * long DOWN move from home would cross the region boundary
     * (2*cy - thisy = 30 > rlower 23), so the emitter picks the absolute vpa. */
    yetty_ymux_tty_init(&tty, 24, 80);
    check_move(test, &tty, 0, 15, "\x1b[16d"); /* vpa down to row 15 (region-crossing) */
    check_move(test, &tty, 0, 10, "\x1b[5A");  /* cuu: up 5 (>1, magnitude<=cy) */

    /* vpa fallback: an up move whose magnitude exceeds the target row, so the
     * 1-based absolute row param (\e[<n>d) is shorter than the relative cuu. */
    check_move(test, &tty, 0, 2, "\x1b[3d"); /* vpa: |change|=8 > cy=2 */
}

/* Emit a cell-style change and assert the produced SGR bytes match exactly. */
static void check_attr(struct ytest *test, struct yetty_ymux_tty *tty, uint16_t attr, int fg,
                       int bg, const char *expected)
{
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(64).value;
    struct yetty_ycore_void_result res = yetty_ymux_tty_attributes(tty, &out, attr, fg, bg);
    YTEST_REQUIRE_OK(test, res);
    size_t expected_len = strlen(expected);
    YTEST_CHECK_EQ_SIZE(test, out.size, expected_len);
    if (out.size == expected_len && out.data) {
        YTEST_CHECK(test, memcmp(out.data, expected, expected_len) == 0);
    }
    yetty_ycore_buffer_destroy(&out);
}

/* tty_attributes SGR minimization, byte-identical to tmux for xterm-256color:
 * emit only newly-set bits; reset (sgr0 = \e(B\e[m) whenever a bit is cleared;
 * no output when unchanged. (Default colours throughout.) */
static void test_tty_attributes_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);
    int def = YMUX_TTY_COLOR_DEFAULT;

    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, def, def, "\x1b[1m");
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD | YMUX_TTY_ATTR_UNDERLINE, def, def, "\x1b[4m");
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, def, def, "\x1b(B\x1b[m\x1b[1m"); /* clear ul */
    check_attr(test, &tty, 0, def, def, "\x1b(B\x1b[m");                         /* clear all */
    check_attr(test, &tty, 0, def, def, "");                                     /* unchanged */
    check_attr(test, &tty, YMUX_TTY_ATTR_BLINK | YMUX_TTY_ATTR_REVERSE, def, def, "\x1b[5m\x1b[7m");
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD | YMUX_TTY_ATTR_ITALICS, def, def,
               "\x1b(B\x1b[m\x1b[1m\x1b[3m");
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD | YMUX_TTY_ATTR_ITALICS | YMUX_TTY_ATTR_STRIKE, def,
               def, "\x1b[9m"); /* add strike (smxx) */

    /* The two remaining attribute bits: dim (\e[2m) and hidden/invis (\e[8m),
     * each set from a clean pen so it emits only the newly-set SGR. */
    yetty_ymux_tty_init(&tty, 24, 80);
    check_attr(test, &tty, YMUX_TTY_ATTR_DIM, def, def, "\x1b[2m"); /* dim */
    check_attr(test, &tty, YMUX_TTY_ATTR_DIM | YMUX_TTY_ATTR_HIDDEN, def, def,
               "\x1b[8m");                               /* +invis */
    check_attr(test, &tty, 0, def, def, "\x1b(B\x1b[m"); /* clear both -> reset */
}

/* tty_colours, byte-identical to tmux for xterm-256color: \e[39m/\e[49m to go
 * default; setaf/setab for basic (3n/4n), bright (9(n-8)/10(n-8)) and 256-colour
 * (38;5;n / 48;5;n); a cleared attribute resets colours too. */
static void test_tty_colours_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);
    int def = YMUX_TTY_COLOR_DEFAULT;

    check_attr(test, &tty, 0, 2, def, "\x1b[32m");                   /* fg basic green */
    check_attr(test, &tty, 0, 2, 4, "\x1b[44m");                     /* add bg basic blue */
    check_attr(test, &tty, 0, def, 4, "\x1b[39m");                   /* fg -> default */
    check_attr(test, &tty, 0, 10, 4, "\x1b[92m");                    /* fg bright (8..15) */
    check_attr(test, &tty, 0, 200, def, "\x1b[49m\x1b[38;5;200m");   /* bg default + fg 256 */
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, 200, def, "\x1b[1m"); /* colours unchanged */
    check_attr(test, &tty, 0, def, def, "\x1b(B\x1b[m"); /* clear bold: reset also clears colours */

    /* Truecolour fg (profile advertises RGB): \e[38;2;R;G;Bm — matches real
     * tmux (verified by the parity harness for (255,100,0)). */
    check_attr(test, &tty, 0, YMUX_TTY_COLOR_RGB(255, 100, 0), def, "\x1b[38;2;255;100;0m");
    check_attr(test, &tty, 0, def, YMUX_TTY_COLOR_RGB(0, 128, 255), "\x1b[39m\x1b[48;2;0;128;255m");
}

/* Cursor show/hide emits civis/cnorm only on a change (tmux MODE_CURSOR). */
static void test_tty_cursor_visible_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    const char *cnorm = "\x1b[?12l\x1b[?25h";
    const char *civis = "\x1b[?25l";

    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor_visible(&tty, &out, 1)); /* show: cnorm */
    YTEST_CHECK_EQ_SIZE(test, out.size, strlen(cnorm));
    if (out.size == strlen(cnorm) && out.data) {
        YTEST_CHECK(test, memcmp(out.data, cnorm, strlen(cnorm)) == 0);
    }
    yetty_ycore_buffer_destroy(&out);

    out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor_visible(&tty, &out, 1)); /* unchanged */
    YTEST_CHECK_EQ_SIZE(test, out.size, 0);
    yetty_ycore_buffer_destroy(&out);

    out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor_visible(&tty, &out, 0)); /* hide: civis */
    YTEST_CHECK_EQ_SIZE(test, out.size, strlen(civis));
    if (out.size == strlen(civis) && out.data) {
        YTEST_CHECK(test, memcmp(out.data, civis, strlen(civis)) == 0);
    }
    yetty_ycore_buffer_destroy(&out);
}

/* DECSCUSR parity: shape+blink -> \e[<n> q with n = (shape-1)*2 + (blink?1:2),
 * emitted only on change. */
static void expect_decscusr(struct ytest *test, struct yetty_ymux_tty *tty, int shape, int blink,
                            const char *want)
{
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(32).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor_shape(tty, &out, shape, blink));
    YTEST_CHECK_EQ_SIZE(test, out.size, strlen(want));
    if (out.size == strlen(want) && out.data) {
        YTEST_CHECK(test, memcmp(out.data, want, strlen(want)) == 0);
    }
    yetty_ycore_buffer_destroy(&out);
}

static void test_tty_cursor_shape_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    expect_decscusr(test, &tty, 1, 0, "\x1b[2 q"); /* steady block  */
    expect_decscusr(test, &tty, 1, 0, "");         /* unchanged: no emit */
    expect_decscusr(test, &tty, 2, 1, "\x1b[3 q"); /* blink underline */
    expect_decscusr(test, &tty, 2, 0, "\x1b[4 q"); /* steady underline */
    expect_decscusr(test, &tty, 3, 0, "\x1b[6 q"); /* steady bar */
    expect_decscusr(test, &tty, 1, 1, "\x1b[1 q"); /* blink block */
    /* Unknown/zero shape falls back to a steady block param (2). */
    expect_decscusr(test, &tty, 0, 0, "\x1b[2 q");
}

/* tty_putn: text bytes emitted verbatim; cursor advances by width so a later
 * move to that position is a no-op (tmux never repositions after a run); filling
 * the last column parks the cursor at sx (deferred autowrap) so the next move is
 * absolute. */
static void test_tty_text_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(128).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_putn(&tty, &out, "hello", 5, 5));
    YTEST_CHECK(test, out.size == 5 && memcmp(out.data, "hello", 5) == 0);
    YTEST_CHECK_EQ_INT(test, (int)tty.cx, 5);
    YTEST_CHECK_EQ_INT(test, (int)tty.cy, 0);
    yetty_ycore_buffer_destroy(&out);

    /* Cursor already advanced by the text -> a move there emits nothing. */
    struct yetty_ycore_buffer none = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &none, 5, 0));
    YTEST_CHECK_EQ_SIZE(test, none.size, 0);
    yetty_ycore_buffer_destroy(&none);

    /* Fill the last column, then move: the parked cursor forces absolute cup. */
    yetty_ymux_tty_init(&tty, 24, 80);
    struct yetty_ycore_buffer sink = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &sink, 79, 0));
    yetty_ycore_buffer_destroy(&sink);

    struct yetty_ycore_buffer ch = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_putn(&tty, &ch, "X", 1, 1));
    YTEST_CHECK(test, ch.size == 1 && ((const char *)ch.data)[0] == 'X');
    YTEST_CHECK_EQ_INT(test, (int)tty.cx, 80); /* parked at sx */
    yetty_ycore_buffer_destroy(&ch);

    struct yetty_ycore_buffer wrap = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &wrap, 0, 1));
    YTEST_CHECK_EQ_SIZE(test, wrap.size, strlen("\x1b[2;1H"));
    if (wrap.size == strlen("\x1b[2;1H") && wrap.data) {
        YTEST_CHECK(test, memcmp(wrap.data, "\x1b[2;1H", strlen("\x1b[2;1H")) == 0);
    }
    yetty_ycore_buffer_destroy(&wrap);
}

/* Clear-to-EOL is el (\e[K); erase-N is ech (\e[<n>X); erase 0 emits nothing. */
static void test_tty_clear_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    struct yetty_ycore_buffer el = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_clear_line(&tty, &el));
    YTEST_CHECK(test, el.size == 3 && memcmp(el.data, "\x1b[K", 3) == 0);
    yetty_ycore_buffer_destroy(&el);

    struct yetty_ycore_buffer ech = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_clear_chars(&tty, &ech, 7));
    YTEST_CHECK(test, ech.size == strlen("\x1b[7X") && memcmp(ech.data, "\x1b[7X", 4) == 0);
    yetty_ycore_buffer_destroy(&ech);

    struct yetty_ycore_buffer zero = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_clear_chars(&tty, &zero, 0));
    YTEST_CHECK_EQ_SIZE(test, zero.size, 0);
    yetty_ycore_buffer_destroy(&zero);
}

/* Cycle-23 P0: the emitter must consult the resolved terminfo model, not a
 * hard-coded literal. A cancelled cap (`cap@`) must NOT be reintroduced as its
 * literal; a cancelled CURSOR cap must fall back to absolute `cup` so the
 * cursor stays in sync; a `cap=` OVERRIDE must drive the emitted bytes. The
 * bare-tty path (tty->term == NULL) keeps emitting the legacy literal — that
 * is the parity-gate profile and is covered by the sibling tests above. */
static void test_tty_emitter_honours_model(struct ytest *test)
{
    int def = YMUX_TTY_COLOR_DEFAULT;

    /* --- bold@ : attributes emit NOTHING, not the \e[1m literal. --- */
    struct yetty_ymux_tty_term bold_off;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&bold_off, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&bold_off, "bold@"));
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &bold_off;
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, def, def, ""); /* cancelled: no bold */
    yetty_ymux_tty_term_free(&bold_off);

    /* Control: with a FULL model, bold is present and emits \e[1m. */
    struct yetty_ymux_tty_term full;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&full, "xterm-256color", NULL));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &full;
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, def, def, "\x1b[1m");
    /* home present -> the optimised home form. Warm the cursor away first. */
    struct yetty_ycore_buffer warm = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &warm, 5, 5));
    yetty_ycore_buffer_destroy(&warm);
    check_move(test, &tty, 0, 0, "\x1b[H"); /* home */
    yetty_ymux_tty_term_free(&full);

    /* --- home@ : move-to-(0,0) falls back to absolute cup, not nothing. --- */
    struct yetty_ymux_tty_term home_off;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&home_off, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&home_off, "home@"));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &home_off;
    struct yetty_ycore_buffer warm2 = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &warm2, 5, 5));
    yetty_ycore_buffer_destroy(&warm2);
    check_move(test, &tty, 0, 0, "\x1b[1;1H"); /* cup fallback keeps cursor synced */
    yetty_ymux_tty_term_free(&home_off);

    /* --- el@ : clear_line emits NOTHING (no \e[K literal reintroduced). --- */
    struct yetty_ymux_tty_term el_off;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&el_off, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&el_off, "el@"));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &el_off;
    struct yetty_ycore_buffer el_cancel = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_clear_line(&tty, &el_cancel));
    YTEST_CHECK_EQ_SIZE(test, el_cancel.size, 0);
    yetty_ycore_buffer_destroy(&el_cancel);
    yetty_ymux_tty_term_free(&el_off);

    /* --- el=<custom> : the override drives clear_line's bytes. --- */
    struct yetty_ymux_tty_term el_ovr;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&el_ovr, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&el_ovr, "el=\\E[9K"));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &el_ovr;
    struct yetty_ycore_buffer el_over = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_clear_line(&tty, &el_over));
    YTEST_CHECK(test, el_over.size == 4 && memcmp(el_over.data, "\x1b[9K", 4) == 0);
    yetty_ycore_buffer_destroy(&el_over);
    yetty_ymux_tty_term_free(&el_ovr);
}

static int buf_has(const struct yetty_ycore_buffer *buffer, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (buffer->size < needle_len) {
        return 0;
    }
    for (size_t offset = 0; offset + needle_len <= buffer->size; ++offset) {
        if (memcmp(buffer->data + offset, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Cycle-25 P0/B: more capabilities routed through the model. SGR0 (the reset)
 * takes a `sgr0=` OVERRIDE; and a truecolour pen DOWNGRADES to the nearest
 * palette index when the model CANCELLED setrgbf/setrgbb, instead of emitting a
 * raw truecolour CSI the terminal declared it cannot honour. */
static void test_tty_emitter_capability_fallbacks(struct ytest *test)
{
    int def = YMUX_TTY_COLOR_DEFAULT;

    /* sgr0= override drives the reset bytes. Set bold, then clear → the reset. */
    struct yetty_ymux_tty_term sgr0_ovr;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&sgr0_ovr, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&sgr0_ovr, "sgr0=\\E[0m"));
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &sgr0_ovr;
    check_attr(test, &tty, YMUX_TTY_ATTR_BOLD, def, def, "\x1b[1m");
    check_attr(test, &tty, 0, def, def, "\x1b[0m"); /* reset via the override, not \e(B\e[m */
    yetty_ymux_tty_term_free(&sgr0_ovr);

    /* setrgbf@,setrgbb@: a truecolour fg downgrades to a palette index — NO raw
     * \e[38;2; truecolour CSI is emitted for a cap the model cancelled. */
    struct yetty_ymux_tty_term rgb_off;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&rgb_off, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&rgb_off, "RGB"));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&rgb_off, "setrgbf@,setrgbb@"));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &rgb_off;
    struct yetty_ycore_buffer rgb_out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(
        test, yetty_ymux_tty_attributes(&tty, &rgb_out, 0, YMUX_TTY_COLOR_RGB(200, 40, 40), def));
    YTEST_CHECK(test, !buf_has(&rgb_out, "\x1b[38;2;")); /* no raw truecolour */
    YTEST_CHECK(test, buf_has(&rgb_out, "\x1b[38;5;"));  /* downgraded to palette */
    yetty_ycore_buffer_destroy(&rgb_out);
    yetty_ymux_tty_term_free(&rgb_off);

    /* Control: with truecolour present, the same pen emits truecolour. */
    struct yetty_ymux_tty_term rgb_on;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&rgb_on, "xterm-256color", NULL));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&rgb_on, "RGB"));
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.term = &rgb_on;
    struct yetty_ycore_buffer rgb_on_out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_attributes(&tty, &rgb_on_out, 0,
                                                     YMUX_TTY_COLOR_RGB(200, 40, 40), def));
    YTEST_CHECK(test, buf_has(&rgb_on_out, "\x1b[38;2;200;40;40m"));
    yetty_ycore_buffer_destroy(&rgb_on_out);
    yetty_ymux_tty_term_free(&rgb_on);
}

static struct yetty_ymux_tty_cell blank_cell(void)
{
    struct yetty_ymux_tty_cell cell = {.text = " ",
                                       .len = 1,
                                       .attr = 0,
                                       .fg = YMUX_TTY_COLOR_DEFAULT,
                                       .bg = YMUX_TTY_COLOR_DEFAULT,
                                       .width = 1};
    return cell;
}

/* tty_draw_line composes cursor + attributes + text and clears trailing default
 * blanks with EL: "hi" + blanks -> "hi\e[K"; a bold-red 'A' + blanks ->
 * SGR(colour,attr) + 'A' + reset + EL. */
static void test_tty_draw_line_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    struct yetty_ymux_tty_cell row[80];

    yetty_ymux_tty_init(&tty, 24, 80);
    for (int i = 0; i < 80; ++i) {
        row[i] = blank_cell();
    }
    row[0].text = "h";
    row[1].text = "i";
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(256).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_draw_line(&tty, &out, row, 80, 0));
    const char *expected = "hi\x1b[K";
    YTEST_CHECK_EQ_SIZE(test, out.size, strlen(expected));
    if (out.size == strlen(expected) && out.data) {
        YTEST_CHECK(test, memcmp(out.data, expected, strlen(expected)) == 0);
    }
    yetty_ycore_buffer_destroy(&out);

    yetty_ymux_tty_init(&tty, 24, 80);
    for (int i = 0; i < 80; ++i) {
        row[i] = blank_cell();
    }
    row[0].text = "A";
    row[0].attr = YMUX_TTY_ATTR_BOLD;
    row[0].fg = 1; /* red */
    struct yetty_ycore_buffer styled = yetty_ycore_buffer_create(256).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_draw_line(&tty, &styled, row, 80, 0));
    const char *exp2 = "\x1b[31m\x1b[1mA\x1b(B\x1b[m\x1b[K";
    YTEST_CHECK_EQ_SIZE(test, styled.size, strlen(exp2));
    if (styled.size == strlen(exp2) && styled.data) {
        YTEST_CHECK(test, memcmp(styled.data, exp2, strlen(exp2)) == 0);
    }
    yetty_ycore_buffer_destroy(&styled);
}

/* After tty_invalidate the assumed state is discarded: the next cursor move is
 * absolute (cup) regardless of where it was, and the pen re-establishes from
 * default. */
static void test_tty_invalidate(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);

    struct yetty_ycore_buffer warm = yetty_ycore_buffer_create(32).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &warm, 5, 5)); /* establish (5,5) */
    yetty_ycore_buffer_destroy(&warm);

    yetty_ymux_tty_invalidate(&tty);

    /* Next move is absolute even though (5,5)->(6,5) would normally be cuf1. */
    struct yetty_ycore_buffer move = yetty_ycore_buffer_create(32).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor(&tty, &move, 6, 5));
    YTEST_CHECK_EQ_SIZE(test, move.size, strlen("\x1b[6;7H"));
    if (move.size == strlen("\x1b[6;7H") && move.data) {
        YTEST_CHECK(test, memcmp(move.data, "\x1b[6;7H", strlen("\x1b[6;7H")) == 0);
    }
    yetty_ycore_buffer_destroy(&move);

    /* Cursor visibility re-emits after invalidate (was unknown). */
    struct yetty_ycore_buffer vis = yetty_ycore_buffer_create(32).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_cursor_visible(&tty, &vis, 1));
    YTEST_CHECK(test, vis.size > 0);
    yetty_ycore_buffer_destroy(&vis);
}

/* tmux colour_find_rgb RGB->256 downgrade, hand-derived from the algorithm. */
static void test_rgb_to_256(struct ytest *test)
{
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(0, 0, 0), 16);           /* cube black */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(255, 255, 255), 231);    /* cube white */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(0x5f, 0, 0), 52);        /* exact cube red */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(8, 8, 8), 232);          /* first grey */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(128, 128, 128), 244);    /* mid grey */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_256(0xff, 0xd7, 0x00), 220); /* cube 16+180+... */
}

/* pack an RGB into the engine cell's 0xAABBGGRR form (r = low byte). */
static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/* Recover the emitter colour from a resolved-RGB cell: default -> DEFAULT,
 * palette-matching RGB -> its index (setaf), non-palette RGB -> truecolour. */
static void test_rgb_to_tty_color(struct ytest *test)
{
    uint32_t palette[256] = {0};
    palette[1] = pack_rgb(205, 0, 0);     /* index 1 (red) */
    palette[200] = pack_rgb(0, 175, 215); /* index 200 */
    uint32_t deflt = pack_rgb(224, 229, 228);

    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_tty_color(deflt, deflt, palette, 256),
                       YMUX_TTY_COLOR_DEFAULT);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rgb_to_tty_color(pack_rgb(205, 0, 0), deflt, palette, 256),
                       1);
    YTEST_CHECK_EQ_INT(
        test, yetty_ymux_rgb_to_tty_color(pack_rgb(0, 175, 215), deflt, palette, 256), 200);
    YTEST_CHECK_EQ_INT(test,
                       yetty_ymux_rgb_to_tty_color(pack_rgb(255, 100, 0), deflt, palette, 256),
                       YMUX_TTY_COLOR_RGB(255, 100, 0));
}

/* The 256 palette: base-16 preserved, cube + grayscale at the fixed xterm RGBs. */
static void test_build_palette256(struct ytest *test)
{
    uint32_t base16[16];
    for (int i = 0; i < 16; ++i) {
        base16[i] = pack_rgb((uint8_t)(i * 16), 0, 0);
    }
    uint32_t palette[256];
    yetty_ymux_build_palette256(base16, palette);

    /* The palette mirrors the ymux engine's libvterm ramps (ramp6/ramp24), NOT
     * the standard xterm ramps, so the reverse-map recovers engine-resolved
     * indices. */
    YTEST_CHECK(test, palette[3] == base16[3]);                 /* base-16 preserved */
    YTEST_CHECK(test, palette[16] == pack_rgb(0, 0, 0));        /* cube start = black */
    YTEST_CHECK(test, palette[231] == pack_rgb(255, 255, 255)); /* cube end = white */
    YTEST_CHECK(test, palette[196] == pack_rgb(255, 0, 0));     /* cube bright red */
    YTEST_CHECK(test, palette[208] == pack_rgb(255, 0x66, 0));  /* libvterm ramp6, not xterm */
    YTEST_CHECK(test, palette[232] == pack_rgb(0, 0, 0));       /* ramp24 start = black */
    YTEST_CHECK(test, palette[255] == pack_rgb(255, 255, 255)); /* ramp24 end = white */
}

/* Review #19: the terminfo/features STATE MODEL — TERM-implied defaults,
 * explicit feature adds, `name@` cancellation, and the unknown-TERM
 * MINIMAL profile (never silently xterm). */
static void test_caps_features_model(struct ytest *test)
{
    /* xterm-256color: classic base + implied app features. */
    struct yetty_ymux_tty_caps xterm_caps = yetty_ymux_tty_caps_resolve("xterm-256color", NULL);
    YTEST_CHECK(test, xterm_caps.colors_256 == 1);
    YTEST_CHECK(test, xterm_caps.ech == 1);
    YTEST_CHECK(test, xterm_caps.decstbm == 1);
    YTEST_CHECK(test, xterm_caps.title == 1);
    YTEST_CHECK(test, xterm_caps.clipboard == 1);
    YTEST_CHECK(test, xterm_caps.cursor_style == 1);
    YTEST_CHECK(test, xterm_caps.colors_rgb == 0);
    YTEST_CHECK(test, xterm_caps.sync == 0);

    /* Explicit features ADD onto the base. */
    struct yetty_ymux_tty_caps added_caps =
        yetty_ymux_tty_caps_resolve("xterm-256color", "RGB,sync,hyperlinks,margins");
    YTEST_CHECK(test, added_caps.colors_rgb == 1);
    YTEST_CHECK(test, added_caps.sync == 1);
    YTEST_CHECK(test, added_caps.hyperlink == 1);
    YTEST_CHECK(test, added_caps.margins == 1);

    /* CANCELLATION: `name@` removes what the name would add — here the
     * tmux family's implied usstyle and title. */
    struct yetty_ymux_tty_caps cancelled_caps =
        yetty_ymux_tty_caps_resolve("tmux-256color", "usstyle@,title@");
    YTEST_CHECK(test, cancelled_caps.extended_underline == 0);
    YTEST_CHECK(test, cancelled_caps.underline_colour == 0);
    YTEST_CHECK(test, cancelled_caps.title == 0);
    YTEST_CHECK(test, cancelled_caps.colors_rgb == 1); /* the rest survives */
    YTEST_CHECK(test, cancelled_caps.hyperlink == 1);

    /* UNKNOWN TERM: the MINIMAL vt100-class profile — no colour, no ECH,
     * no implied app features; explicit features still apply. */
    struct yetty_ymux_tty_caps unknown_caps = yetty_ymux_tty_caps_resolve("frobnicator-9000", NULL);
    YTEST_CHECK(test, unknown_caps.colors_256 == 0);
    YTEST_CHECK(test, unknown_caps.ech == 0);
    YTEST_CHECK(test, unknown_caps.bce == 0);
    YTEST_CHECK(test, unknown_caps.title == 0);
    YTEST_CHECK(test, unknown_caps.insert_delete_line == 1);
    YTEST_CHECK(test, unknown_caps.decstbm == 1);
    struct yetty_ymux_tty_caps unknown_added_caps =
        yetty_ymux_tty_caps_resolve("frobnicator-9000", "256,mouse");
    YTEST_CHECK(test, unknown_added_caps.colors_256 == 1);
    YTEST_CHECK(test, unknown_added_caps.mouse == 1);

    /* Named modern families resolve their pinned feature rows. */
    struct yetty_ymux_tty_caps kitty_caps = yetty_ymux_tty_caps_resolve("kitty", NULL);
    YTEST_CHECK(test, kitty_caps.colors_rgb == 1);
    YTEST_CHECK(test, kitty_caps.hyperlink == 1);
    YTEST_CHECK(test, kitty_caps.sync == 1);
    YTEST_CHECK(test, kitty_caps.overline == 1);
    YTEST_CHECK(test, kitty_caps.extended_underline == 1);
    struct yetty_ymux_tty_caps foot_caps = yetty_ymux_tty_caps_resolve("foot", NULL);
    YTEST_CHECK(test, foot_caps.sync == 1);
    YTEST_CHECK(test, foot_caps.extkeys == 1);
    YTEST_CHECK(test, foot_caps.hyperlink == 0);
    struct yetty_ymux_tty_caps screen_caps = yetty_ymux_tty_caps_resolve("screen", NULL);
    YTEST_CHECK(test, screen_caps.ech == 0);
    YTEST_CHECK(test, screen_caps.colors_256 == 0);
    YTEST_CHECK(test, screen_caps.title == 1);
}

/* Horizontal margins (tmux tty_margin / tty_cmd_scrollup partial-width path).
 * The expected bytes are the EXACT delta pinned tmux (d5afb67) emits for a
 * scroll inside the left pane of a real two-pane lr split on a margins-capable
 * client, captured with the split-mode oracle (TMUX_ORACLE_SPLIT=lr, 24x80,
 * term tmux-256color features 256,RGB,margins):
 *   \e[1;24r\e[1;40s\e[24;40H\n   — margin narrow + scroll (tty_margin emits
 *                                   the CSR itself, unconditionally; the
 *                                   cursor goes to the margin bottom-RIGHT)
 *   \e[1;24r\e[s                  — margin restore (again CSR-first)
 * plus the margin interactions on tty_cursor: CR shortcuts need rleft == 0
 * and multi-cell CUB/CUF are disabled under a margins profile. */
static void test_tty_margin_parity(struct ytest *test)
{
    struct yetty_ymux_tty tty;
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.caps.margins = 1;

    /* Narrow + scroll: the probe's exact partial-width rectangle bytes. */
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(128).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_margin_scrollup(&tty, &out, 0, 23, 0, 39, 1));
    static const char scroll_expected[] = "\x1b[1;24r\x1b[1;40s\x1b[24;40H\n";
    YTEST_CHECK_EQ_SIZE(test, out.size, sizeof(scroll_expected) - 1);
    if (out.size == sizeof(scroll_expected) - 1) {
        YTEST_CHECK(test, memcmp(out.data, scroll_expected, out.size) == 0);
    }
    YTEST_CHECK_EQ_INT(test, (int)tty.rleft, 0);
    YTEST_CHECK_EQ_INT(test, (int)tty.rright, 39);
    yetty_ycore_buffer_destroy(&out);

    /* Restore: CSR (unconditional) + \e[s, cursor invalidated. */
    out = yetty_ycore_buffer_create(64).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_margin_off(&tty, &out));
    static const char off_expected[] = "\x1b[1;24r\x1b[s";
    YTEST_CHECK_EQ_SIZE(test, out.size, sizeof(off_expected) - 1);
    if (out.size == sizeof(off_expected) - 1) {
        YTEST_CHECK(test, memcmp(out.data, off_expected, out.size) == 0);
    }
    YTEST_CHECK(test, tty.cx == UINT32_MAX && tty.cy == UINT32_MAX);
    yetty_ycore_buffer_destroy(&out);

    /* Unchanged margins are deduped (no bytes). */
    out = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_margin_off(&tty, &out));
    YTEST_CHECK_EQ_SIZE(test, out.size, 0);
    yetty_ycore_buffer_destroy(&out);

    /* A non-margins profile emits nothing at all. */
    struct yetty_ymux_tty plain;
    yetty_ymux_tty_init(&plain, 24, 80);
    out = yetty_ycore_buffer_create(16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_margin(&plain, &out, 0, 39));
    YTEST_CHECK_EQ_SIZE(test, out.size, 0);
    yetty_ycore_buffer_destroy(&out);

    /* Cursor interactions (probe: the final move after a margin draw came out
     * as HPA, never CUB/CUF): multi-cell left/right moves are disabled on a
     * margins profile — HPA when it wins, else absolute cup. */
    yetty_ymux_tty_init(&tty, 24, 80);
    tty.caps.margins = 1;
    tty.cx = 40; /* as after drawing a 40-col pane row */
    tty.cy = 23;
    check_move(test, &tty, 4, 23, "\x1b[5G"); /* HPA — CUB is margin-disabled */
    /* CR-to-left-edge requires rleft == 0: with a narrowed left margin the
     * emitter must NOT use \r (it would stop at the margin, not column 0). */
    tty.rleft = 10;
    tty.rright = 49;
    tty.cx = 20;
    tty.cy = 5;
    check_move(test, &tty, 0, 5, "\x1b[1G"); /* hpa, NOT \r */
    /* And \r\n (zero on next line) is likewise suppressed: absolute cup. */
    tty.cx = 20;
    tty.cy = 5;
    check_move(test, &tty, 0, 6, "\x1b[7;1H"); /* cup, NOT \r\n */

    /* With full-width margins (rleft == 0) the CR shortcuts stay available. */
    tty.rleft = 0;
    tty.rright = 79;
    tty.cx = 20;
    tty.cy = 8;
    check_move(test, &tty, 0, 8, "\r");
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_tty_render");
    YTEST_RUN(&test, test_caps_features_model);
    YTEST_RUN(&test, test_rgb_to_256);
    YTEST_RUN(&test, test_rgb_to_tty_color);
    YTEST_RUN(&test, test_build_palette256);
    YTEST_RUN(&test, test_tty_cursor_parity);
    YTEST_RUN(&test, test_tty_cursor_all_branches);
    YTEST_RUN(&test, test_tty_attributes_parity);
    YTEST_RUN(&test, test_tty_colours_parity);
    YTEST_RUN(&test, test_tty_cursor_visible_parity);
    YTEST_RUN(&test, test_tty_cursor_shape_parity);
    YTEST_RUN(&test, test_tty_text_parity);
    YTEST_RUN(&test, test_tty_clear_parity);
    YTEST_RUN(&test, test_tty_emitter_honours_model);
    YTEST_RUN(&test, test_tty_emitter_capability_fallbacks);
    YTEST_RUN(&test, test_tty_draw_line_parity);
    YTEST_RUN(&test, test_tty_invalidate);
    YTEST_RUN(&test, test_tty_margin_parity);
    return ytest_end(&test);
}
