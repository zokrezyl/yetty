#ifndef YETTY_YCORE_TERMINAL_DETECT_H
#define YETTY_YCORE_TERMINAL_DETECT_H

/*
 * Host-terminal detection for yetty's client tools (ycat, yecho, ygreeter,
 * ygui demos, …). Everything keys off the standard TERM_PROGRAM variable —
 * no custom yetty-only marker is needed.
 *
 * yetty exports TERM_PROGRAM=yetty into its child shell (src/yetty/ymain/
 * glfw.c). When a tmux runs inside that shell it rewrites TERM_PROGRAM to
 * "tmux" for its own panes, so the value a tool sees tells it where it sits:
 *
 *   TERM_PROGRAM=yetty   → directly under yetty. Emit rich (ydraw / figure)
 *                          output straight to yetty; no wrapping.
 *   TERM_PROGRAM=tmux    → inside tmux (in yetty's model, a tmux hosted by
 *                          yetty). Emit rich output, but tmux-passthrough-
 *                          wrap it so it survives the multiplexer (see
 *                          yetty_ywire_tmux_passthrough_active).
 *   TERM_PROGRAM unset   → not under yetty. Run standalone (plain output,
 *                          no rich envelopes).
 *
 * yetty_running_under_yetty() answers "should this tool emit rich output at
 * all" — true for both the yetty and tmux cases. yetty_term_program_is_tmux()
 * answers the narrower "must I passthrough-wrap" question.
 */

#include <stdlib.h>
#include <string.h>

/* TERM_PROGRAM value, or NULL when unset/empty. */
static inline const char *yetty_term_program(void)
{
    const char *value = getenv("TERM_PROGRAM");
    return (value && value[0]) ? value : NULL;
}

/* True when running inside tmux (TERM_PROGRAM=tmux). In yetty's model this
 * means a tmux hosted by yetty, so emitted envelopes must be passthrough-
 * wrapped to reach yetty. */
static inline int yetty_term_program_is_tmux(void)
{
    const char *value = yetty_term_program();
    return value && strcmp(value, "tmux") == 0;
}

/* True when this tool should emit rich (ydraw / figure) output: directly
 * under yetty (TERM_PROGRAM=yetty) or inside tmux-under-yetty
 * (TERM_PROGRAM=tmux). False when TERM_PROGRAM is unset — the tool then runs
 * standalone. */
static inline int yetty_running_under_yetty(void)
{
    const char *value = yetty_term_program();
    if (!value) {
        return 0;
    }
    return strcmp(value, "yetty") == 0 || strcmp(value, "tmux") == 0;
}

#endif /* YETTY_YCORE_TERMINAL_DETECT_H */
