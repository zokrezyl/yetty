# ydummy — role-split codegen pilot

`ydummy` pilots the yclass codegen **role-split layout** (diagnosis in
`tmp/yrpc-issues.md`, converged design + final layout correction in
`tmp/cdx-to-cld.md` / `tmp/cld-to-cdx.md`). It is generated with
`YCLASS_SPLIT_ydummy := 1` (root `Makefile`); fleet modules keep their
legacy byte-identical output until rollout.

## The model

**The class is pure contract + state.** A shader is a piece of TEXT, a
rect is four floats, time is a float. No GPU code, GPU type or GPU include
exists anywhere in this module.

```
constructor              lifecycle slot — never remotely callable (no skel)
set_shader(buffer)       the WGSL text
set_rect(4×float)        placement
set_time(float) oneway@  animation clock
destroy                  teardown
+ exposed read accessors: shader_text/shader_length/shader_generation/rect/time
```

**The object API does not encode RPC.** Consumers call typed methods on an
object; whether dispatch is in-process or marshalled is decided by the
object (`session == NULL` or not), never by the API surface.

**Rendering the state is the embedding program's business.** The
standalone server demo owns its WebGPU renderer outright and observes the
canvas through the exposed accessors; a hosting yetty renders the same
state with its own machinery.

## Layout — raw source, generated API, generated impl

The directory is the role boundary (no filename suffixes):

| location | role | consumer |
|---|---|---|
| `src/yetty/ydummy/canvas.c` | hand-written annotated source (the ONLY hand-written file) | — |
| `include/yetty/api/ydummy/canvas.h` | generated object-API header | every consumer |
| `src/yetty/gen/api/ydummy/canvas.c` | generated typed API stubs (no strong refs to the impl) | `yetty_api_ydummy` |
| `src/yetty/gen/impl/ydummy/canvas.c` | generated impl glue: accessor, shims, constructor stub, skels, factory, registration hook — `#include`d at the foot of `canvas.c` | `yetty_ydummy` |
| `model.yaml` | binding contract | FFI |

Build ownership: `yetty_api_ydummy` compiles the API TU (links ycore +
yclass only); `yetty_ydummy` compiles `canvas.c` and links
`yetty_api_ydummy`. There is no module-level `rpc.gen.c` — the impl glue
owns `yetty_ydummy_register()`.

Split-mode codegen semantics (inert for non-split modules): remote slots
resolve by canonical qualified name (`ensure_remote_id_by_name` — a pure
client needs no slot table, no class metadata); the factory is local-only
(remote consumers wrap a `GET_ROOT`/`CREATE` handle via
`yetty_yclass_object_proxy_create`); the constructor gets no skel.

## The demos (`demo/ydummy/`)

- **`client/` → `ydummy-client`** — pure wire client: includes the object
  API header, links `yetty_api_ydummy` + the yclass runtime, connects over
  an inherited fd, wraps the server's root handle in a proxy, ships shader
  text + rect + time. No GPU include path, no impl, no skels, no
  registration, no slot table.
- **`server/` → `ydummy-server`** — creates the canvas locally
  (constructor runs there), publishes it as RPC root, spawns the client
  over a socketpair, serves, then renders the resulting state with its own
  renderer and verifies the readback.

## Acceptance

- `ydummy-client` compiles with no webgpu include path.
- `nm` on `ydummy-client`: zero `wgpu*`, zero `*_skel`, zero `*_class_get`,
  zero impl symbols, zero constructor stub.
- `ydummy-server <path-to-ydummy-client> tmp/ydummy.ppm` exits 0 with
  `PASS`: readback non-uniform inside the rect, clear outside, red channel
  oscillating (the client's rings — the server's built-in default gradient
  is monotonic, so a monotonic image means the wire bytes never arrived).

## Regenerating

`make _cg1-ydummy` regenerates only this module. New codegen behavior is
proven here before any fleet-wide rollout. Driven so far: the role-split
emission (`YCLASS_SPLIT`), name-keyed remote resolution in the runtime,
and the constructor-off-the-wire rule.
