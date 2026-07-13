# yrdawn — remote WebGPU canvas over OSC (client + server)

`yrdawn` lets a separate process render with WebGPU *inside* a yetty
terminal as if Dawn lived in its own address space: the client side
implements `<webgpu/webgpu.h>` by encoding each `wgpu*` call as an OSC
record on stdout; the server side decodes it inside yetty, executes it on
yetty's own Dawn device, and presents the result as a
[figure](../yfigure/README.md) in the compositor. One client process may
open many canvases (one canvas == one yrdawn figure). Consumers:
[`yterminal`](../yterminal/README.md) links both halves,
[`yframework`](../yframework/README.md) registers the figure factory, and
the runnable examples live in `demo/yrdawn/`.

## Wire model

yrdawn drives the figure tree exactly like ymgui — through the typed
yfigure yclass stubs over yclass-RPC (`yetty_yfigure_producer_attach` on
the client's stdin/stdout pair):

- canvas open = `yetty_yfigure_create_child` on the root container, whose
  init payload is `u32 SUB_HELLO + struct yetty_yrdawn_wire_hello
  {session_id, figure_id}`;
- everything after = `yetty_yfigure_apply_child_body` addressed to that
  figure, body starting with a `u32 SUB_OP` discriminator (`CMD` /
  `BULK` / `BYE`);
- server→client replies, events, input and errors use dedicated OSC codes
  in the 72xxxx block (`wire.h`), each carrying the `figure_id` the client
  demuxes on.

Sessions: every client process picks a non-zero `session_id` (typically
its pid). The server lazily creates one `yetty_yrdawn_session` per
session_id holding what all of that client's canvases share — the WGPU
handle table (opaque client-chosen u64 handles), BULK reassembly slots,
and the *borrowed* Dawn instance/adapter/device/queue taken from
yframework's GPU context (a second in-process Dawn instance deadlocks
Vulkan during pipeline creation). Dispatcher callbacks receive
`ctx = figure` and navigate `figure->session`, so the generated dispatch
code is session-agnostic.

## Two codegen layers

1. **`generate.py` (this directory, run by hand)** emits the per-WebGPU-method
   shims from a curated method table: `include/yetty/yrdawn/methods.gen.h`
   (method ids + args structs), `client-stubs.gen.c` (~6 MB of `wgpu*`
   client stubs) and `server-dispatch.gen.c` (~6 MB dispatcher). These are
   committed; CMake never runs Python. The wire layout *is* the C struct
   layout, which is why the committed `include/yetty/yrdawn/webgpu/webgpu.h`
   is pinned `BEFORE` every Dawn prebuilt's header — client and server must
   agree bit-for-bit on every platform.
2. **yclass codegen (`make codegen`)** processes the annotated `figure.c`
   into the generated public `include/yetty/yrdawn/figure.h`,
   `figure.gen.c` (included at the foot of `figure.c`), `rpc.gen.c` and
   `model.yaml` — which also feeds the FFI bindings
   (`bindings/lua/yetty/generated/yrdawn.lua`). Never hand-edit any of
   these; see [`../yclass/README.md`](../yclass/README.md).

## Client API sketch

```c
struct yetty_yrdawn_client *client;   /* one per process, owns the PTY fds */
struct yetty_yrdawn_canvas *canvas;   /* one per remote figure */

yetty_yrdawn_client_create(STDIN_FILENO, STDOUT_FILENO, (uint32_t)getpid());
yetty_yrdawn_canvas_create(client, /*figure_id=*/1, x, y, w, h); /* becomes current */
/* ... plain wgpu* calls from client-stubs.gen.c target the current canvas ... */
yetty_yrdawn_canvas_make_current(other_canvas);      /* multi-canvas switch */
yetty_yrdawn_canvas_present_frame(canvas, w, h, rgba8, bytes); /* BULK + method 100 */
yetty_yrdawn_client_pump(client);   /* drain SC envelopes, fire callbacks */
```

Per-canvas callbacks deliver replies, device events, focused key input and
resize; a per-client raw-input callback receives non-envelope PTY bytes
(plain keystrokes such as `q` / Ctrl-C).

## File map

| file | role |
|---|---|
| `wire.c` + `include/yetty/yrdawn/wire.h` | record shapes, sub-ops, SC OSC codes, HELLO/REPLY/EVENT/BULK/ERROR/BYE |
| `client.c` / `client.h` | client + canvas lifecycle, producer-session attach, pump/demux |
| `client-stubs.gen.c` | generated `wgpu*` implementations (encode → CMD) |
| `figure.c` / `figure.h` | server: yfigure subclass, HELLO binding, frame texture, factory registration |
| `session.c` / `session.h` | server: per-client handle table, BULK slots, borrowed Dawn handles |
| `server-dispatch.gen.c` / `server.h` | generated method dispatcher + the layer-implemented helper hooks |
| `server-dispatch-stub.c` | link stub for mobile Dawn prebuilts (every CMD answers UNKNOWN_METHOD) |
| `figure.gen.c`, `rpc.gen.c`, `model.yaml` | yclass codegen outputs |
| `generate.py` | the method-table generator (run by hand) |
| `include/yetty/yrdawn/webgpu/webgpu.h` | the pinned canonical WebGPU header |

## Build shape

`CMakeLists.txt` builds two libraries: `yetty_yrdawn` (client — pure C, no
Dawn, links `yetty_yface` + `yetty_yfigure` + `yetty_yplatform_core`;
what remote processes and the `demo/yrdawn` examples link) and,
off-emscripten, `yetty_yrdawn_server`
(figure + session + dispatcher, links `yetty_yframework`/`yetty_yrender`/
`webgpu`). `yetty_yterminal` links both; on Android/iOS/tvOS the stub
dispatcher is swapped in because the mobile Dawn prebuilts don't export
every entrypoint. Note the CMake comments still say "yetty_yterm" — the
linking module is `yterminal` today.

The old name "ymux" for remote rendering is dead; it survives only in two
comments in `include/yetty/yrender/render-target.h`.

## Cross-references

- [`../yfigure/README.md`](../yfigure/README.md) — the figure tree and producer sessions the wire rides on.
- [`../yface/README.md`](../yface/README.md) — the OSC envelope encoding.
- [`../yframework/README.md`](../yframework/README.md) — factory registration and the shared GPU context.
- `demo/yrdawn/` — 01_dawn_info … 11_two_canvases, plus `common.c` bring-up helpers.
