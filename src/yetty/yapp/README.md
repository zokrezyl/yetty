# yapp — abstract application base class

`yapp` defines the yclass class `yapp:app`: the contract between the generic
platform bootstrap and whatever program is linked into the binary. The
per-platform entry points (`../yplatform/ymain/glfw.c`, `webasm.c`,
`android.c`) know only this class — they create the concrete app through the
`yetty_yapp_create_app()` injection point and drive it through the virtuals
below. The terminal ([`../yetty`](../yetty/README.md)), the generic ygui host
([`../yguiapp`](../yguiapp/README.md)) and standalone tools (`tools/ymaze`,
`tools/yzoo`, `tools/yjungle`, `tools/ycompositor`) are all `yapp:app`
subclasses.

## How it works

The class carries no state (`char reserved`) and three virtual slots:

- `init(app, platform)` — default: no-op success.
- `run(app, platform)` — default: error (`"yapp:app:run not implemented"`);
  every concrete app overrides this with its full bring-up + main loop.
- `quit(app)` — ask the app to end its run loop; default: no-op (an app
  without an event loop has nothing to stop).

The linchpin is the app-injection point:

```c
/* Declaration only — no default implementation exists. */
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);
```

The platform bootstrap calls this fixed-name forwarder; the concrete app
module linked into the binary defines it to register and build its own app
class (see `../yetty/app.c` for the canonical implementation). A binary that
links a platform entry without defining it fails to link, by design —
platform code never names a concrete app.

## Public API sketch

```c
#include "yetty/gen/impl/yapp/app.h"   /* generated */

struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *app,
                                               struct yetty_yclass_object *platform);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *app,
                                              struct yetty_yclass_object *platform);
struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object *app);

struct yetty_yclass_object_ptr_result yetty_yapp_app_create(struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yapp_register(void);
```

## Build wiring

The module has no `CMakeLists.txt` of its own: `app.c` + `rpc.gen.c` are
compiled into `yetty_yplatform_core` (see `../yplatform/CMakeLists.txt`), so
the platform layer can host any `yapp:app` subclass without a dependency on
it. Note: `docs/architecture.md` historically listed a `ymain` module; there
is no `src/yetty/ymain` directory — the bootstrap files live at
`src/yetty/yplatform/ymain/<platform>.c` and `yapp` is the abstraction they
drive.

## Files

| file | role |
|------|------|
| `app.c` | hand-written annotated class: the three virtuals + the `yetty_yapp_create_app` declaration |
| `app.gen.c` | codegen output — registration, method-dispatch shims (`#include`d at the foot of `app.c`) |
| `rpc.gen.c` | codegen output — RPC skeletons for wire-proxied apps |
| `model.yaml` | codegen output — canonical class model for FFI / host-language bindings |

The public header `include/yetty/yapp/app.h` is fully generated — never
hand-edit it or the `*.gen.c` / `model.yaml` outputs; edit `app.c` and re-run
`make codegen` (see [`../yclass/README.md`](../yclass/README.md)).

## Consumers

- `../yplatform/ymain/{glfw,webasm,android}.c` — call `yetty_yapp_create_app`
  then `init`/`run` through the platform run loop.
- Concrete subclasses: `yetty:app` ([`../yetty`](../yetty/README.md)),
  `yguiapp:app` ([`../yguiapp`](../yguiapp/README.md)), `tools/ymaze`,
  `tools/yzoo`, `tools/yjungle`, `tools/ycompositor`.

See [`../../../docs/contexts.md`](../../../docs/contexts.md) for the
platform → framework → app ownership chain.
