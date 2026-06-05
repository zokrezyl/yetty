#ifndef YETTY_YCORE_TERMINAL_DETECT_H
#define YETTY_YCORE_TERMINAL_DETECT_H

/*
 * yetty_running_under_yetty() — is the HOST terminal yetty?
 *
 * This answers ONE question only: should this tool emit rich (ydraw /
 * figure) output at all. It is true both directly under yetty and inside
 * tmux-inside-yetty.
 *
 * yetty marks itself by exporting YETTY_TMUX_PASSTHROUGH=1 (src/yetty/
 * ymain/glfw.c). Unlike TERM_PROGRAM — which a multiplexer (tmux) rewrites
 * to its own name per pane — this custom var passes through tmux unchanged,
 * so the answer stays correct inside tmux.
 *
 * It is NOT the "must I tmux-passthrough-wrap my output?" question. That is
 * separate and narrower — true only when a tmux actually sits between this
 * tool and yetty — and is answered by yetty_ywire_tmux_passthrough_active()
 * (this var AND $TMUX). Do not use this function to gate wrapping.
 */

#include <stdlib.h>

static inline int yetty_running_under_yetty(void)
{
    const char *marker = getenv("YETTY_TMUX_PASSTHROUGH");
    return marker && marker[0] == '1';
}

#endif /* YETTY_YCORE_TERMINAL_DETECT_H */
