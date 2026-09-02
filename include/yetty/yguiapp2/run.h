/* yguiapp2 — the generic ygui2 application host (terminal mode).
 *
 * Packages the pure-PTY app loop every ygui2 tool shares: raw termios, the
 * alternate screen (fullscreen mode of the strategy — the insertion lives
 * for the whole run), framework make/attach/viewport, a select loop that
 * feeds stdin bytes to the framework, an optional animation tick, and
 * emit-on-dirty. The app supplies ONE build callback that populates the
 * widget tree; q / Ctrl-C quits.
 */
#ifndef YETTY_YGUIAPP2_RUN_H
#define YETTY_YGUIAPP2_RUN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_yguiapp2_build_fn)(
    struct yetty_yclass_object *framework, void *userdata);
typedef struct yetty_ycore_void_result (*yetty_yguiapp2_tick_fn)(
    struct yetty_yclass_object *framework, void *userdata);

/* Runs until quit; returns the process exit code. tick may be NULL;
 * tick_ms <= 0 defaults to 250. */
int yetty_yguiapp2_terminal_main(yetty_yguiapp2_build_fn build, yetty_yguiapp2_tick_fn tick,
                                 int tick_ms, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUIAPP2_RUN_H */
