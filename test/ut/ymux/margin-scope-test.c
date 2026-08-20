/*
 * #699 cdx-2 item 2 — DECSLRM (horizontal margins) SCOPING proof.
 *
 * ymux runs two classes of libvterm instance:
 *
 *   - the DAEMON pane engine (engine.c), which must behave like a tmux pane:
 *     an application's DEC ?69 (DECLRMM) + DECSLRM must be a NO-OP, because
 *     the pane owns a full-width virtual screen and tmux never lets the app
 *     drive real horizontal margins. engine.c is the ONLY caller of
 *     vterm_state_set_ignore_leftright_margin().
 *
 *   - the RECEIVER grid (the yscene libvterm the client renders into), which
 *     must keep DECSLRM ENABLED so the compositor can address a partial-width
 *     client rectangle exactly as tmux's own tty layer does.
 *
 * This test pins that split at the libvterm layer: the ignore flag makes an
 * app's DECSLRM a true no-op (matches the daemon pane), while a default
 * instance (the receiver) honours it. We assert the EFFECTIVE scroll-region
 * columns — the SCROLLREGION_LEFT/RIGHT macros the scroll / IL / DL / ICH /
 * DCH paths actually consume — not merely a stored mode bit, so the proof is
 * that margins take EFFECT (or don't), not just that a flag flipped.
 */
#include "ytest.h"

#include "vterm.h"
/* White-box: mode.leftrightmargin + the SCROLLREGION_* macros are the exact
 * inputs the scroll/insert/delete emission reads. The fork's screen cell holds
 * a resolved glyph_index rather than a codepoint, so a screen readback cannot
 * observe a scroll without a glyph resolver — the internal margin state is the
 * definitive, resolver-free observable. */
#include "../../../src/libvterm-0.3.3/src/vterm_internal.h"

/* A fresh 4x6 vterm with a clean state; sets the ymux-daemon ignore flag when
 * asked, exactly as engine.c does for a pane. */
static VTerm *make_vterm(int ignore_leftright)
{
    VTerm *vterm = vterm_new(4, 6);
    vterm_set_utf8(vterm, 1);
    VTermState *state = vterm_obtain_state(vterm);
    vterm_state_reset(state, 1);
    if (ignore_leftright) {
        vterm_state_set_ignore_leftright_margin(state, 1);
    }
    return vterm;
}

/* The RECEIVER (yscene) libvterm: DECLRMM enable + DECSLRM must take effect so
 * the compositor can drive a partial-width rectangle. */
static void test_receiver_honours_decslrm(struct ytest *test)
{
    VTerm *vterm = make_vterm(0);
    VTermState *state = vterm_obtain_state(vterm);

    /* ?69h enables horizontal margins; DECSLRM sets left=2 right=4 (1-based,
     * columns index 1..3). */
    vterm_input_write(vterm, "\x1b[?69h", 6);
    YTEST_CHECK_EQ_INT(test, state->mode.leftrightmargin, 1);
    vterm_input_write(vterm, "\x1b[2;4s", 6);

    /* The EFFECTIVE scroll columns the scroll/IL/DL paths read are the margin
     * range — the receiver clips to columns 1..4, not the full 0..6. */
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_LEFT(state), 1);
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_RIGHT(state), 4);

    /* ?69l disables it again — the enable is a live toggle, not latched. */
    vterm_input_write(vterm, "\x1b[?69l", 6);
    YTEST_CHECK_EQ_INT(test, state->mode.leftrightmargin, 0);
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_LEFT(state), 0);
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_RIGHT(state), state->cols);

    vterm_free(vterm);
}

/* The DAEMON pane engine: the app's DECLRMM/DECSLRM is a NO-OP — margins stay
 * full-width so the pane behaves as tmux's full-width virtual screen. */
static void test_daemon_engine_ignores_decslrm(struct ytest *test)
{
    VTerm *vterm = make_vterm(1);
    VTermState *state = vterm_obtain_state(vterm);

    /* Same app sequence the receiver honoured. */
    vterm_input_write(vterm, "\x1b[?69h", 6);
    /* The enable itself is swallowed: DECLRMM never latches on a pane. */
    YTEST_CHECK_EQ_INT(test, state->mode.leftrightmargin, 0);
    vterm_input_write(vterm, "\x1b[2;4s", 6);

    /* Effective scroll columns remain the full width — the app cannot carve a
     * horizontal margin inside a tmux pane. */
    YTEST_CHECK_EQ_INT(test, state->mode.leftrightmargin, 0);
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_LEFT(state), 0);
    YTEST_CHECK_EQ_INT(test, SCROLLREGION_RIGHT(state), state->cols);

    vterm_free(vterm);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_margin_scope");
    YTEST_RUN(&test, test_receiver_honours_decslrm);
    YTEST_RUN(&test, test_daemon_engine_ignores_decslrm);
    return ytest_end(&test);
}
