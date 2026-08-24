# ydraw client interface — target-API sketches

Sketches of the language surfaces of the ydraw client interface (the
`ydrawlist2` + `ysdf2` yclass modules and their generated bindings,
re-exported to Python as `yetty.ydraw`). Semantics throughout are the
ydraw-list producer model — one drawable list, immediate appends, a
dcs-emit producing the same `YDRAW_BIN` envelope the C tools emit.

Status: ALL FOUR language trees RUN against `libyetty_ffi.so` —
shapes/fonts/text (hello.*), plots incl. buffers and views (plot.* +
the 15-demo yplot corpus), and the complex kinds (yimage/ymesh/
yshadertoy/yvideo, one-shot records). The `hello` envelope is
byte-identical across python, lua, go and typescript. Runners (all
from the repo root — asset paths are repo-root-relative):

- python: `/usr/bin/python3 demo/ffi/ydraw/python/<demo>.py`
  (ctypes bindings under `bindings/python/`).
- lua: `LUA_PATH='bindings/lua/?.lua;;' luajit
  demo/ffi/ydraw/lua/<demo>.lua` (LuaJIT ffi).
- go: `CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi"
  LD_LIBRARY_PATH=<build>/src/yetty/yffi go run
  demo/ffi/ydraw/go/<demo>.go` (cgo, resolved via the root
  `go.work`).
- typescript: `npm install` once in `demo/ffi/ydraw/typescript/`,
  then `node demo/ffi/ydraw/typescript/<demo>.ts` (node ≥ 23
  type-stripping + koffi; `tsc -p demo/ffi/ydraw/typescript`
  typechecks the tree against the generated `ydraw.d.ts`).

Demos are grouped **per language**, one directory each, with identical
content: `python/` is the source of truth; `lua/`, `typescript/` and
`go/` mirror it (the 15 yplot demos are machine-translated from the
python files to prevent drift).

| subdirectory (inside each language) | covers |
|---|---|
| `hello.*` | the core surface: SDF shapes, fonts, text runs |
| `plot.*` | plots as drawables + subplot layout by rect arithmetic |
| `yplot/` | all 15 `demo/scripts/yplot/*.sh` demos reproduced 1:1 (see `python/yplot/README.md` for the DSL → object mapping) |
| `yimage/` | yimage complex records from the PNGs in `demo/assets/yimage/` |
| `yvideo/` | yvideo complex records from the H.264 clips in `demo/assets/yvideo/` |
| `ymesh/` | ymesh complex records from the .glb models in `demo/assets/ymesh/` |
| `yshadertoy/` | yshadertoy complex records from the WGSL files in `demo/assets/yshadertoy/` |

The five complex kinds (`Plot` 0x80000003, `Image` 0x80000004, `Mesh`
0x80000005, `Video` 0x80000006, `Shadertoy` 0x80000007) each pack their
record via the kind's existing wire serializer — no new wire anywhere.

Language conventions: python/lua keep `snake_case` fields; typescript
uses `camelCase` and an options-object constructor argument; go uses
struct literals with `CamelCase` fields (each go file is a standalone
`package main` sketch). In typescript the imported `Function` class
shadows the JS global deliberately, for cross-language parity.
