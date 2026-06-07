/*
 * yrich-app.h — standalone window host for the ygui-decorated yrich
 * editors.
 *
 * Runs exactly like the other ygui apps (ycompositor-ygui / yaudio):
 * brings up a window via yinit_run + yframework, spins an in-process
 * yfigure container, and drives a ygui framework into it through the
 * yclass slot path (framework set_container_obj — no PTY, no OSC). The
 * widget tree is the decorated editor shell for the requested document
 * kind.
 *
 * This is the shared app entry the thin ydoc / ysheet / yslide tools
 * call; it lives in the yrich module so all shared yrich code stays
 * under src/yetty/yrich.
 */
#ifndef YETTY_YRICH_YRICH_APP_H
#define YETTY_YRICH_YRICH_APP_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrich_document;

/* Which decorated editor shell to host. */
enum yetty_yrich_app_kind {
    YETTY_YRICH_APP_YDOC = 0, /* rich-text editor */
    YETTY_YRICH_APP_YSHEET,   /* spreadsheet */
    YETTY_YRICH_APP_YSLIDE,   /* slide deck */
};

/* Open a window and run the decorated editor for `doc`. Takes ownership
 * of `doc` (destroyed with the widget tree on exit). `argc`/`argv` are
 * forwarded to yinit_run for its own flag parsing. Blocks until the
 * window closes. On success the value is a process exit code (0 = clean). */
struct yetty_ycore_int_result yetty_yrich_app_run(int argc, char **argv,
                                                  struct yetty_yrich_document *doc,
                                                  enum yetty_yrich_app_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_APP_H */
