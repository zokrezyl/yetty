# FFI Binding Generation

Design for the yetty FFI binding pipeline: parse public C headers with libclang,
emit YAML metadata, then generate idiomatic bindings for any target language
(Python, Rust, Go, TypeScript, …).

Status: design draft. Not yet implemented.

## Goals

- One source of truth: the public C headers under `include/yetty/`.
- Downstream languages need **no libclang dependency** — they consume YAML.
- Generated bindings are **idiomatic** per language: `Result<T, E>` in Rust,
  exceptions in Python, multiple returns in Go, etc.
- Ownership, nullability, and out-params are expressed explicitly so bindings
  are **memory-safe by construction**, not by downstream convention.
- Zero runtime cost: annotations are compile-time-only attributes that expand
  to nothing in normal builds.
- Incremental adoption: day-one heuristics cover the common case; explicit
  annotations upgrade precision where it matters.

## Non-goals

- Not a general C parser. Only yetty public headers (`include/yetty/**`) are in
  scope.
- Not a replacement for hand-written high-level wrappers. The output is a thin,
  faithful binding layer. Language-specific ergonomics (async, iterators, etc.)
  belong in a layer above.
- Not ABI-stable across versions (for now). The YAML is regenerated per build.

## Pipeline

Two stages, with YAML as the stable contract between them:

```
  include/yetty/**.h
          │
          ▼
  ┌──────────────────┐
  │  Stage 1:        │   tools/ffi-gen/extract/
  │  libclang        │   (Python, uses clang.cindex)
  │  extractor       │
  └────────┬─────────┘
           │
           ▼
   build/ffi/metadata.yaml   ← reviewable, diffable, versioned
           │
           ▼
  ┌──────────────────┐
  │  Stage 2:        │   tools/ffi-gen/emit/<lang>/
  │  per-language    │   (pure templates, no libclang)
  │  emitters        │
  └────────┬─────────┘
           │
           ├─▶ bindings/python/
           ├─▶ bindings/rust/
           ├─▶ bindings/go/
           └─▶ bindings/ts/
```

Keeping the stages separate is a deliberate tradeoff: one extra artifact
(`metadata.yaml`), but:

- Language emitters stay simple and testable (text-in, text-out).
- YAML diffs in PRs make API changes visible to reviewers.
- CI can fail on accidental API/ABI drift.
- Consumers who want to write their own binding generator in a fourth language
  can read the YAML without touching Clang.

## Stage 1: the libclang extractor

Location: `tools/ffi-gen/extract/` (Python).

### Driver language choice

Python with `clang.cindex`. Rationale:

- Fast iteration on AST-walking logic.
- Runtime cost is irrelevant for a ~hundred-header codebase.
- Wide familiarity — easy for contributors.
- C++ libclang would be more robust but the productivity hit isn't justified.

### Feeding libclang the right flags

libclang needs the same `-I`, `-D`, and target flags used for a real build.
Two options:

1. Parse `build/<preset>/compile_commands.json` and pick an existing TU that
   includes the public umbrella.
2. Synthesise `tools/ffi-gen/ffi_all.c` that `#include`s every public header,
   compile it as a dedicated TU just for extraction.

Option 2 is recommended: it doubles as a "every public header compiles
standalone" smoke test, and it gives the extractor a single, deterministic TU
to walk regardless of build config.

### What to walk in the AST

| Cursor kind | What to emit |
|---|---|
| `StructDecl` / `UnionDecl` | fields, offsets (`clang_Type_getOffsetOf`), size |
| `TypedefDecl` | canonical type, keep typedef name as public name |
| `EnumDecl` | values, underlying integer type |
| `FunctionDecl` | params, return type, attributes |
| `MacroDefinition` | constants only (require `PARSE_DETAILED_PROCESSING_RECORD`, filter to pure-literal macros) |
| Function-pointer typedef | first-class `callback` node in YAML |
| `AnnotateAttr` on any of the above | read the `"yetty:..."` payload |

### Recognising project-specific patterns

The extractor knows about yetty conventions, not just raw C:

- **Result types** (`docs/result.md`). A struct whose name ends in `_result`
  and whose shape matches the `YETTY_RESULT_DECLARE` macro expansion is emitted
  as `kind: result`, not `kind: struct`. The binding generator unwraps it into
  the target language's native fallible-return idiom.
- **Opaque handles**. A type declared in a public header but *defined* only in
  `src/` is marked `kind: opaque`. Bindings expose it as an opaque handle.
- **Vtable polymorphism**. A struct whose first field is a pointer to a struct
  of function pointers is tagged `kind: interface`. OO target languages can
  emit an abstract base class.
- **Naming hierarchy** (`yetty_<module>_<thing>_<action>`). Used both to group
  functions by owning type and to drive heuristics (see below).

## Annotations

Annotations are the bridge between "what C happens to allow" and "what the
target language needs to know to do the right thing." They are attached
**directly on the declaration** in the public header, inline with the thing
they describe.

### The macros

New header: `include/yetty/ycore/ffi-annotations.h`.

```c
#ifndef YETTY_YCORE_FFI_ANNOTATIONS_H
#define YETTY_YCORE_FFI_ANNOTATIONS_H

#if defined(__clang__) || defined(__GNUC__)
#  define YETTY_ANNOTATE(s) __attribute__((annotate(s)))
#else
#  define YETTY_ANNOTATE(s)
#endif

/* Parameter roles */
#define YETTY_OUT            YETTY_ANNOTATE("yetty:out")
#define YETTY_INOUT          YETTY_ANNOTATE("yetty:inout")
#define YETTY_ARRAY(len)     YETTY_ANNOTATE("yetty:array:" #len)

/* Ownership */
#define YETTY_OWNED          YETTY_ANNOTATE("yetty:owned")
#define YETTY_BORROWED       YETTY_ANNOTATE("yetty:borrowed")
#define YETTY_CONSUMES       YETTY_ANNOTATE("yetty:consumes")
#define YETTY_RETURNS_OWNED  YETTY_ANNOTATE("yetty:returns_owned")

/* Nullability and strings */
#define YETTY_NULLABLE       YETTY_ANNOTATE("yetty:nullable")
#define YETTY_NONNULL        YETTY_ANNOTATE("yetty:nonnull")
#define YETTY_CSTRING        YETTY_ANNOTATE("yetty:cstring")

/* Callback lifetime */
#define YETTY_CB_CALL_ONLY   YETTY_ANNOTATE("yetty:cb_call_only")
#define YETTY_CB_RETAINED    YETTY_ANNOTATE("yetty:cb_retained")

#endif
```

In a normal compile these expand to zero code. In the extractor, they surface
as `CXCursor_AnnotateAttr` children of the annotated declaration.

### Where annotations go

Inline with the declaration, on the parameter / function / field / typedef
they describe.

Example — `include/yetty/yterm/terminal.h`:

```c
#include <yetty/ycore/ffi-annotations.h>

YETTY_RETURNS_OWNED
struct yetty_term_terminal_ptr_result
yetty_term_terminal_create(const struct yetty_term_config *config YETTY_BORROWED);

void
yetty_term_terminal_destroy(struct yetty_term_terminal *term YETTY_CONSUMES);

struct yetty_core_void_result
yetty_term_terminal_write(struct yetty_term_terminal *term,
                          const char *data YETTY_ARRAY(len),
                          size_t len);

struct yetty_core_void_result
yetty_term_terminal_get_size(struct yetty_term_terminal *term,
                             uint32_t *cols YETTY_OUT,
                             uint32_t *rows YETTY_OUT);
```

### Annotation placement rules

- On a **parameter**: describes that parameter (in/out/array/nullable/owning).
- On a **function**: describes the return value or the function as a whole
  (`YETTY_RETURNS_OWNED`).
- On a **struct field**: describes that field.
- On a **typedef**: applies wherever the type is used.

### What each annotation changes in the generated binding

| C signature | Without annotation | With annotation |
|---|---|---|
| `uint32_t *cols` | Rust: `*mut u32`, user handles it | `YETTY_OUT` → function returns the value in a tuple |
| `const char *data, size_t len` | Two params, caller passes len | `YETTY_ARRAY(len)` → one `&[u8]` / `bytes` / `[]byte` param |
| `const char *name` | `*const c_char` | `YETTY_CSTRING` → `&str` / `str`, NUL handled |
| Returned `struct foo *` | Opaque pointer, unclear lifetime | `YETTY_RETURNS_OWNED` → RAII wrapper / `Drop` / finalizer |
| `yetty_x_destroy(struct x *)` | Public function | `YETTY_CONSUMES` → becomes `Drop` / `__del__`; destroy hidden from public API |
| Callback param `struct cb *cb` | Raw pointer | `YETTY_CB_RETAINED` → binding keeps closure alive until a counterpart "unregister" |

## Default reasoning for unannotated code

Not every header needs to be annotated on day one. The extractor applies a
layered heuristic, from strongest signal to weakest. Every inference is logged
to `build/ffi/inferences.txt` so humans can review and annotate what the
heuristics got wrong.

### Layer 1 — type-driven (unambiguous)

| Seen | Default | Confidence |
|---|---|---|
| Return is `*_result` | Fallible; unwrap `ok_type` | Certain |
| Return is `void` | Not fallible | Certain |
| `const T *` param | Input, borrowed | Certain |
| Scalar / struct by value | Input, copied | Certain |

### Layer 2 — naming-driven

yetty has strict `yetty_<module>_<thing>_<action>` naming. That is leverage.

| Pattern | Inferred |
|---|---|
| `*_create`, `*_new`, `*_open` | Returns **owned** |
| `*_destroy`, `*_close`, `*_free` | First param is **consumed** (becomes `Drop`/`__del__`) |
| `*_get_<field>` with trailing pointer params | Those pointer params are **out** |
| `*_set_<field>` | Pointer params are **in** |
| `yetty_X_*` function, first param `struct yetty_X *` | Receiver (method on `X`) |
| `yetty_X_*` function, first param `const struct yetty_X *` | Const receiver |

### Layer 3 — structural (pointer + length pairs)

A non-const pointer immediately followed by a length-ish integer:

```c
foo(..., const char *data, size_t len, ...)
foo(..., float *points, size_t n_points, ...)
```

is treated as an **array pair**. Length param names that trigger: `len`,
`length`, `size`, `n`, `n_*`, `*_count`, `count`, `num`, `num_*`.

### Layer 4 — non-const pointer with no other signal

The genuinely ambiguous case. Default ladder:

1. Param name starts with `out_` → out-param.
2. Function name contains `get` and the param is last → out-param.
3. Function name contains `write` / `read` → buffer (array).
4. Otherwise → in-param, mutable borrow (the conservative choice).

**Never silently infer `owned` or `consumes` for a parameter.** Ownership
errors cause use-after-free / leaks; role errors at worst cause a target-
language compile error, which is recoverable.

### Layer 5 — return values without a result wrapper

| Return | Default |
|---|---|
| `T *` from `_create` / `_new` / `_open` | Owned |
| `T *` from anything else | **Borrowed** (getter-like) |
| `const T *` | Borrowed, read-only |
| `const char *` | Borrowed C-string |

### Conflict resolution

When heuristics disagree, the strongest signal wins (Layer 2 > Layer 3 >
Layer 4). The inference is logged with its provenance:

```
yterm/terminal.h:42  yetty_term_terminal_write
    param `data`:  array(len)   [Layer 3: ptr+size_t pair]
    param `len`:   array_length [Layer 3: follows ptr]
ypaint/layer.h:17  yetty_ypaint_get_bounds
    param `bounds`: out         [Layer 2: `get_*` + last param]
```

### Strictness knob

CLI flag on the extractor:

- `--strict-annotations=off` (default) — heuristics fill gaps, warnings logged.
- `--strict-annotations=warn` — every heuristic use emits a build warning.
- `--strict-annotations=error` — unannotated ambiguous cases fail the build.

Migration path: start at `off`, raise to `warn` once coverage is reasonable,
aim for `error` on new headers in the long term.

## YAML schema

Versioned. A first sketch — keep it stable once downstream emitters land.

```yaml
schema_version: 1
module: yterm
source_headers:
  - include/yetty/yterm/terminal.h
  - include/yetty/yterm/config.h

types:
  - kind: opaque
    name: yetty_term_terminal

  - kind: struct
    name: yetty_term_config
    size: 48
    align: 8
    fields:
      - {name: cols,  type: uint32, offset: 0}
      - {name: rows,  type: uint32, offset: 4}
      - {name: title, type: {ptr: char, const: true}, offset: 8, cstring: true}

  - kind: enum
    name: yetty_term_cursor_style
    underlying: int
    values:
      - {name: YETTY_TERM_CURSOR_BLOCK,     value: 0}
      - {name: YETTY_TERM_CURSOR_UNDERLINE, value: 1}

  - kind: result
    name: yetty_term_terminal_ptr_result
    ok_type: {ptr: yetty_term_terminal, ownership: owned}

  - kind: callback
    name: yetty_term_on_resize_cb
    returns: void
    params:
      - {name: term, type: {ptr: yetty_term_terminal}, role: in}
      - {name: cols, type: uint32, role: in}
      - {name: rows, type: uint32, role: in}

functions:
  - name: yetty_term_terminal_create
    header: include/yetty/yterm/terminal.h
    returns: yetty_term_terminal_ptr_result
    fallible: true
    params:
      - name: config
        type: {ptr: yetty_term_config, const: true}
        role: in

  - name: yetty_term_terminal_destroy
    returns: void
    params:
      - {name: term, type: {ptr: yetty_term_terminal}, role: consumes}

  - name: yetty_term_terminal_write
    returns: yetty_core_void_result
    fallible: true
    params:
      - {name: term, type: {ptr: yetty_term_terminal}, role: in}
      - name: data
        type: {ptr: char, const: true}
        role: array
        length_param: len
      - {name: len, type: size_t, role: array_length_of, of: data}

constants:
  - {name: YETTY_TERM_MAX_COLS, type: uint32, value: 1024}
```

## Stage 2: per-language emitters

Location: `tools/ffi-gen/emit/<lang>/`. Pure template work — no libclang.

Each emitter reads `metadata.yaml` and produces:

- A low-level `sys` / `raw` layer that mirrors the C ABI 1:1.
- A thin safe wrapper that uses role / ownership info from the YAML to expose
  idiomatic types.

Initial targets (priority order):

1. **Python** — CFFI or ctypes. Fastest to validate the pipeline end-to-end.
2. **Rust** — `bindgen`-style `-sys` crate + a thin safe crate.
3. **Go** — cgo wrapper.
4. **TypeScript** — via WebAssembly build, interfacing with `webasm` build
   output.

## Build integration

New CMake target `ffi-metadata`:

1. Configures a release build (needed for `compile_commands.json`).
2. Runs `tools/ffi-gen/extract/extract.py`.
3. Writes `build/ffi/metadata.yaml`.

Per-language targets `ffi-python`, `ffi-rust`, etc. depend on `ffi-metadata`.

CI gate: `ffi-metadata` regenerates YAML, diff against the committed copy must
be empty — any API change is a visible, reviewable diff.

## Suggestions and open questions

### Start small

Don't annotate everything at once. Ship day-one with:

- The five most-used public headers annotated.
- Heuristics (`--strict-annotations=off`) covering the rest.
- Python as the only emitter.

Expand from there. Each binding target surfaces ambiguities the previous one
didn't care about.

### Four annotations are enough to start

Of the macros listed above, the four with the highest payoff are:

- `YETTY_OUT`
- `YETTY_ARRAY(len)`
- `YETTY_RETURNS_OWNED`
- `YETTY_CONSUMES`

Everything else can wait until a binding target asks for it.

### Favor soundness over ergonomics when guessing

Asymmetric failure cost drives the defaults:

- Wrong "owned" assumption → double-free, crash in someone else's process.
- Wrong "borrowed" assumption → leak, visible in RSS / valgrind.

Prefer borrowed when in doubt. The annotation upgrades ergonomics where it
matters.

### Round-trip validation

Worth considering: emit a synthetic C source file from the YAML that
references every declared symbol with its declared type, and compile it. If
the YAML and the headers disagree, the compile fails — free drift detection.

### Open questions

- **Enum-as-bitflags**: how to distinguish a bitfield enum (`YETTY_FLAG_A |
  YETTY_FLAG_B`) from a normal one? An annotation on the enum declaration, or
  a naming convention (`*_flags`)?
- **Non-literal `#define` constants**: `#define X (1 << 3)` — worth evaluating
  with a constant folder, or always require `static const` / `enum`?
- **Variadic functions** (`...`): probably just mark as un-bindable for
  non-trivial targets; most languages require per-call knowledge.
- **Struct layout across targets**: `size_t`, padding, `long` size differ
  between platforms. YAML should record the **target triple** used during
  extraction; cross-platform bindings may need per-triple YAML.
- **Forward-declared structs in public headers whose bodies live in other
  public headers**: include ordering for the `ffi_all.c` umbrella TU needs
  care.

## References

- `docs/result.md` — the Result type pattern; drives the `kind: result` YAML
  node.
- `docs/c-coding-style.md` — naming conventions the extractor relies on for
  heuristics.
- `docs/design.md` — overall architecture context.
