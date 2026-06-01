/*
 * demo/ygui/runner.h — shared standalone bring-up for ygui demos.
 *
 * Each demo provides a `build_fn` that populates the widget tree under
 * the runner-owned root. The runner does everything else: GPU window,
 * font, figure registry, receiver-side container, wire SM, ygui
 * framework, input + mouse + resize plumbing, render loop.
 *
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */
#ifndef DEMO_YGUI_RUNNER_H
#define DEMO_YGUI_RUNNER_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/ygui.h>

#ifdef __cplusplus
extern "C" {
#endif

struct demo_runner;

/* Build the demo's widget tree under `root`. The root is a vbox by
 * default; the demo can change its layout via yetty_ygui_widget_layout_set. */
typedef struct yetty_ycore_void_result (*demo_build_fn)(struct demo_runner *runner,
                                                        struct yetty_ygui_object *root);

/* Run a demo. Returns 0 on clean shutdown, non-zero on error. */
int demo_runner_run(int argc, char **argv, const char *name, demo_build_fn build);

/* Accessors for demos that need them. */
struct yetty_ygui_framework *demo_runner_engine(struct demo_runner *runner);
struct yetty_ygui_object *demo_runner_root(struct demo_runner *runner);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_YGUI_RUNNER_H */
