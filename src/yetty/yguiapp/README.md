# yguiapp — generic ygui application host

`yguiapp` is the reusable host for ygui applications: a yclass class
`yguiapp:app` (subclass of [`yapp:app`](../yapp/README.md)) that owns the
running environment — window/GPU bring-up via
[`yframework`](../yframework/README.md), a local
[`yfigure`](../yfigure/README.md) container, a
[`ygui`](../ygui/README.md) framework wired onto it, a styled root/body and
optional window chrome — plus a plain-C dual-mode launcher. It generalizes
the bring-up that used to live in `demo/ygui/runner.c`; every `demo/ygui/*`
demo and the yffi language bindings sit on it.

## The model

An app subclasses `yguiapp:app`, overrides the `build` virtual, and its
`main()` is a one-liner:

```c
struct [[clang::annotate("class@demoygui:06_hello_button")]]
       [[clang::annotate("parent@yguiapp:app")]] yetty_demoygui_06_hello_button { int unused; };

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{ /* add widgets under root */ }

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_06_hello_button_class_get().value);
}
```

`yetty_yguiapp_run_main` decides the mode with `yetty_running_under_yetty()`
(`ycore/terminal-detect.h`, keyed on `TERM_PROGRAM=yetty`):

- **STANDALONE** — registers the platform + yapp classes, creates the shared
  `glfw_platform` and drives the app's `run()` override (app.c): yframework,
  a local yfigure container, a ygui framework, the styled two-level
  root/body, window chrome (a draggable/resizable caption strip via
  [`ychrome`](../ychrome/README.md), rendered through a pinned ygrid figure),
  a ~30 fps animation pump for self-dirtying widgets, and default
  'q'/Ctrl-C quit (subclasses with their own key handling call
  `yetty_yguiapp_app_quit`).
- **TERMINAL** — inside a host yetty: a libuv loop over a single
  transport-pty + `ywire_connection` (the single-reader path). The
  framework's figure output ships on the rpc channel, forwarded mouse
  arrives on the input channel, raw keystrokes on the raw channel, and
  resize rides the connection's winsize pickup. Terminal raw mode is
  restored on every exit and error path. Compiled out without libuv
  (`YETTY_YGUI_HAS_UV`, set on non-Windows) — then it returns a clear error.

From the ygui framework's perspective the two modes are identical: an output
transport to write to and a caller pushing input bytes.

## Public API sketch

```c
#include <yetty/yguiapp/app.h>   /* generated */
#include <yetty/yguiapp/run.h>   /* hand-written launcher */

struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_root_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yguiapp_app_quit(struct yetty_yclass_object *obj);

int yetty_yguiapp_run_main(int argc, char **argv, const struct yetty_yclass *app_class);
struct yetty_ycore_void_result yetty_yguiapp_run_terminal(struct yetty_yclass_object *app);
struct yetty_ycore_void_result yetty_yguiapp_build_root_body(struct yetty_yclass_object *engine,
                                                             float chrome_inset_top,
                                                             struct yetty_yclass_object **root_out,
                                                             struct yetty_yclass_object **body_out);
```

`build_root_body` constructs the shared canvas both modes use: an outer
stretch vbox owning the viewport plus an inner brand body panel (column,
padding, gap, justify-center) the app builds into; `chrome_inset_top` clears
the caption strip in standalone chrome mode (0 in terminal mode).

## Files

| file | role |
|------|------|
| `app.c` | annotated yclass `yguiapp:app`: data slice (engine, root, container, yframework, registry, font, chrome, listeners), `build` virtual, `init/run/quit` overrides, chrome caption, event listener |
| `run.c` | plain C (not codegen-scanned): `run_main` dual-mode launcher, the in-terminal host, `build_root_body` |
| `input-encode.h` | small helpers shared by app.c / run.c for encoding forwarded input |
| `app.gen.c`, `rpc.gen.c`, `model.yaml` | codegen outputs — never hand-edit |
| `CMakeLists.txt` | STATIC lib `yetty_yguiapp`; links ygui/yfigure/ygrid/yframework/ychrome/ywire/… and the yplatform bring-up targets |

## Consumers

- `demo/ygui/*` — dozens of demos, each a `yguiapp:app` subclass.
- `../yffi` — the bindings layer exposes yclass app classes such as this one
  to host languages (see [`../../../docs/ffi-gen.md`](../../../docs/ffi-gen.md)).

## Cross-references

- [`../yapp/README.md`](../yapp/README.md) — the base class and `yapp:app` virtuals
- [`../ygui/README.md`](../ygui/README.md) — the widget framework being hosted
- [`../ywire/README.md`](../ywire/README.md) — the wire connection the terminal host rides
- [`../yclass/README.md`](../yclass/README.md) — annotations and codegen
