# yclass — Annotation-Driven Classes, RPC & the Binding Model

`yclass` is yetty's class/object runtime **and** the code generator that feeds
it. You write plain C, annotate the structs and functions that form a class, and
the generator parses those annotations into a model. From that one model it emits
the C glue (accessors, method stubs, vtable registration, RPC skeletons) and a
`model.yaml` — the structured, machine-readable description that drives **language
binding generation**.

One annotated source → one model → many artefacts. The model is the single source
of truth; this is the mechanism yetty uses to generate bindings for other
languages.

- Runtime: `include/yclass/class.h`, `src/yclass/class.c`
- Generator: `src/yclass/gen/codegen.py`
- RPC + transports: `src/yclass/rpc.c`, `transport-*.c`, `include/yclass/transport.h`
- Reference example: `poc/class-object-model/` (yanimal · yvehicle · ytuning)

See [Design Overview](../../../docs/design.md) for where this sits, and
[FFI Generation](../../../docs/ffi-gen.md) for the per-language emitter design that consumes
the model.

---

## 1. The idea in one screen

```c
/* src/yvehicle/vehicle.c — hand-written, annotated */

struct [[clang::annotate("class@yvehicle:vehicle")]] vehicle_data {
    int mileage;
};

[[clang::annotate("override@yvehicle:vehicle:vehicle_describe")]]
static struct str_result vehicle_default_describe(struct object *obj, float distance)
{
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "vehicle describe(%.1f)", distance);
    return YETTY_OK(str, r);
}

#include "vehicle.gen.c"   /* generated accessor body, appended here */
```

Running the generator over this produces:

- a **model** (dumped as `model.yaml`) describing the class `vehicle`, its data
  struct, and its slots;
- the public header `include/yetty/yvehicle/vehicle.h` (the class accessor) and
  `methods.h` (the callable stubs);
- the C bodies (`vehicle.gen.c`, `methods.gen.c`, `rpc.gen.c`) that register the
  class with the runtime and wire each slot to local dispatch or RPC.

You never hand-write a vtable, a dispatch switch, or an RPC stub.

---

## 2. The runtime

`yclass` is a tiny object system with **per-module slot tables** (`class.h`).

### Slots

A method is identified by a **slot** — a packed 32-bit value:

```
bits 31..28  reserved (0; UINT32_MAX = undefined)
bits 27..24  domain_id (1..15) — the owning module
bits 23..0   local index inside that module's slot table
```

Each module ("domain") owns a 0-based local index space. The wire treats a slot
as an opaque 28-bit id that round-trips through this packing, so a method owned by
module A can be overridden by a class in module B without index collisions.

### Classes and objects

```c
struct yetty_yclass_descriptor { const char *name; enum yetty_yclass_type type; size_t data_size; };
struct yetty_yclass_op         { const char *slot_domain; const char *name;
                                 yetty_yclass_method_id_t method_id; yetty_yclass_impl_t impl; };
struct yetty_yclass_object     { const struct yetty_yclass *klass;
                                 struct yetty_yclass_rpc_session *session; }; /* session: NULL=local, set=remote proxy */
struct yetty_yclass_ctx        { struct yetty_yclass_rpc_session *session; }; /* only passed to <class>_create */
```

A class is registered once with `yetty_yclass_register(descriptor, ops, n_ops,
parent, mixins, n_mixins)`. Classes support a **single parent** plus any number of
**mixins**; dispatch walks parent + mixin chains, and works across module
boundaries. Registration and lookup are uthash-backed (O(1) on hit); the server
side installs lazy "accessor lookups" so it can resolve classes it has never
touched.

**Mixin dispatch is flat, not compositional.** At registration the dispatch
table is filled in the order **parent → mixins (declaration order) → leaf
ops**, and each stage *overwrites* any slot the previous stage set. So a mixin
provides a data slice plus **default** slot implementations: a mixin slot
replaces the parent's, a later mixin replaces an earlier one, and the leaf's own
op replaces whatever a mixin left. There is **no automatic chaining** — a mixin
cannot wrap the slot it shadows, and a leaf override does not implicitly run the
mixin's version. `super` walks only the **parent** chain, never the mixin list,
so "call the mixin part" is not expressible through it; if two behaviors must
genuinely compose, invoke them explicitly. Model a mixin as a default-behavior
provider, not a trait that layers onto what is already there.

### Local vs remote — the same call

A method slot takes the object first — there is no `ctx` argument:

```c
RetT slot(struct yetty_yclass_object *obj, /* rest... */);
```

The RPC session is linked onto the object at create time and read back from
`obj->session` (it is **not** on the wire). The stub branches on it:

- **NULL → local:** vtable dispatch via `obj->klass`.
- **set → remote:** translate the class to a remote id, then
  `yetty_yclass_rpc_call(OP_CALL, rid, ...)`.

`obj` may be a real object or a `yetty_yclass_proxy` (a `yetty_yclass_object`
header plus a server-side `handle`). Callers can't tell the difference — the same
`struct yetty_yclass_object *` works for both. That is the headline property:
**call sites are identical whether the object lives in this process or across an
RPC transport.**

---

## 3. The annotation schema

Annotations are standard C23 attributes: `[[clang::annotate("<verb>@<domain>:<path>")]]`
(equivalently `__attribute__((annotate(...)))`). They cost nothing at runtime and
disappear in a normal compile; the generator reads them out of clang's AST.

The grammar is always `<verb>@<domain>:<colon-separated path>`:

| Annotation | Goes on | Meaning |
|---|---|---|
| `class@<DOMAIN>:<CLASS>` | data `struct` | declare a regular class backed by this struct |
| `mixin@<DOMAIN>:<CLASS>` | data `struct` | declare a mixin class |
| `parent@<DOMAIN>:<CLASS>` | data `struct` | single parent of the class |
| `uses@<DOMAIN>:<MIXIN>` | data `struct` | include a mixin |
| `override@<DOMAIN>:<CLASS>:<SLOT>` | impl function | implement a same-module slot for the class |
| `override@<DOMAIN>:<CLASS>:<SLOT_DOMAIN>:<SLOT>` | impl function | implement a **cross-module** slot |
| `local@<DOMAIN>:<SLOT>` | any function | mark a slot local — never wire-marshalled |

A class declaration stacks its annotations on the data struct, and each
implementing function carries its `override`:

```c
/* a subclass with a parent and a mixin */
struct [[clang::annotate("class@yvehicle:sportscar")]]
       [[clang::annotate("parent@yvehicle:car")]]
       [[clang::annotate("uses@yvehicle:electric")]] sportscar_data {
    int top_speed;
};

[[clang::annotate("override@yvehicle:sportscar:vehicle_accelerate")]]
static struct yetty_ycore_int_result sportscar_accelerate(struct object *obj, float speed) { ... }
```

### Cross-module inheritance

A class in one module can subclass a class in another and override a slot the
other module *owns* — using the 4-segment `override` form:

```c
/* src/ytuning/tuned_sportscar.c — ytuning subclasses yvehicle */
struct [[clang::annotate("class@ytuning:tuned_sportscar")]]
       [[clang::annotate("parent@yvehicle:sportscar")]] tuned_sportscar_data { int boost_level; };

/* override yvehicle's `vehicle_describe` slot from the ytuning module */
[[clang::annotate("override@ytuning:tuned_sportscar:yvehicle:vehicle_describe")]]
static struct str_result tuned_sportscar_describe(struct object *obj, float distance) { ... }
```

The packed `(domain_id, idx)` slot encoding is exactly what makes this compose
into a working cross-domain vtable.

### The method signature contract

Every slot implementation must match:

```c
RetT slot(struct yetty_yclass_object *obj, <rest...>);
```

`RetT` is a [Result type](../../../docs/result.md). `obj` is fixed; everything after
is the method's own arguments and is what gets marshalled on the wire. The RPC
session is read from `obj->session`, not threaded through as an argument.

---

## 4. The generator

`src/yclass/gen/codegen.py` is a single, dependency-free Python script (run via
`uv`). It is invoked **per module**:

```
./codegen.py <module> <include_base> <module_src_dir> <source.c>...
```

### Pipeline

```
annotated .c sources
        │
        ▼  clang -Xclang -ast-dump=json -fsyntax-only -std=c2x
   clang JSON AST  ── AnnotateAttr nodes ──► verb@domain:path strings
        │
        ▼  build the in-memory model (classes, slots, ops, args, return types)
      MODEL
        │
        ├─► public  include/yetty/<module>/
        │     <class>.h   accessor decl (replaces any hand-written class header)
        │     methods.h   every public method stub in the module
        │     rpc.h       every <class>_create() in the module
        │
        ├─► internal  src/yetty/<module>/
        │     <class>.gen.c   accessor body (#included at the foot of <class>.c)
        │     methods.gen.c   public stub bodies (local/remote branch)
        │     rpc.gen.c       skeletons + lookups + yetty_<module>_register()
        │
        └─► model.yaml        the model dump — the binding contract
```

The clang step uses `-fsyntax-only` and **tolerates semantic errors**: it only
needs the declaration + annotation AST nodes, not a clean compile, so third-party
headers don't have to be resolvable.

### Symbol naming — one name, three uses

Every generated identifier visible at link time is
`yetty_<module>_<localname>`. That same string is:

1. the **C identifier** of the public stub / accessor,
2. the **slot-table key** at runtime,
3. the **wire label** exchanged during RPC handshakes.

One canonical name round-trips through all three, which is why the generator can
emit local dispatch and remote dispatch from the same model entry.

### Object creation

The generator emits `yetty_<module>_<class>_create(ctx)`:

- **local** (`ctx->session == NULL`): `yetty_yclass_object_alloc`.
- **remote**: a per-class translate handshake, then `RPC_OP_CREATE`, wrapping the
  returned handle in a proxy that carries the same class accessor.

---

## 5. The RPC layer

The generated `rpc.gen.c` produces **skeletons** (server-side dispatch that
decodes a wire call, looks up the impl, and invokes it) and the
`yetty_<module>_register()` entry point. The transport is pluggable
(`include/yclass/transport.h`):

- `transport-fd.c` — a file-descriptor transport.
- `transport-dcs.c` — DCS-framed transport (rides the terminal's escape-sequence
  channel; see [Layered Rendering](../../../docs/layered-rendering.md)).
- `rpc-dcs-server.c` — the server side over DCS.

Because the slot id is a 28-bit value that fits the RPC header, and the qualified
name round-trips, a client and server built from independent module sets can call
each other as long as they share the wire labels.

This is the foundation under `yrdawn` (remote GPU canvases), `ymgui`, `yfigure`,
and the other yclass-based figure modules — see the
[Architecture & Module Map](../../../docs/architecture.md).

---

## 6. The model dump (`model.yaml`)

`model.yaml` is emitted alongside the C artefacts as the human-readable,
diffable, **canonical description of the module's API**:

```yaml
methods:
  - slot: vehicle_accelerate
    domain: yvehicle
    owning_class: vehicle
    return_type: struct yetty_ycore_int_result
    args:
      - {name: obj,   type: "struct object *"}
      - {name: speed, type: float}
    local: false
classes:
  - name: sportscar
    domain: yvehicle
    accessor: yvehicle_sportscar_class_get
    type: regular
    source_file: src/yvehicle/sportscar.c
    parent: car
    mixins: [electric]
    data: struct sportscar_data
    ops:
      - {slot: vehicle_accelerate, slot_domain: yvehicle, impl: sportscar_accelerate}
```

Each method records its slot, owning class, return type, full argument list, and
whether it is `local` (i.e. not exposed on the wire). Each class records its
accessor, kind, parent, mixins, backing data struct, and the slots it overrides.

This is the bridge to **language bindings**: a binding generator does not need to
re-parse C or understand annotations — it reads `model.yaml` and emits idiomatic
classes/methods for Python, Rust, and so on. The per-language emitter design (a
separate stage that turns the model into `Result<T,E>` in Rust, exceptions in
Python, etc.) is documented in [FFI Generation](../../../docs/ffi-gen.md). yetty's binding
tooling lives under `tools/ffi-codegen/` (parser + per-language emitters), with
generated output committed under `bindings/` (e.g. `bindings/python/`).

> **Note on the two generators.** `yclass`'s codegen turns *annotated class
> sources* into the C object/RPC layer + this model. The FFI pipeline
> ([ffi-gen.md](../../../docs/ffi-gen.md)) turns an API model into per-language bindings. They
> meet at the model: yclass produces it, the FFI emitters consume it.

---

## 7. Build integration

The generator is **not** part of the platform build — its output is committed to
git and compiled as ordinary source. You re-run it only when annotated sources
change:

```bash
make codegen     # runs yclass codegen for all annotated modules
```

Driven from the `Makefile`:

- Modules: `yfigure ygrid ygui ymgui yrdawn yterm`.
- Discovery: every `.c` under `src/yetty/<module>/` (excluding `*.gen.c`) that
  contains a `class@<module>:` or `mixin@<module>:` annotation is fed in as a
  source for that module.
- The clang include roots are derived from the `include/` and `src/` paths
  codegen is passed; the clang step tolerates missing third-party headers.

Platform builds compile what is in the tree and never invoke the generator.

---

## 8. Learning it

`poc/class-object-model/` is the self-contained teaching example. It defines three
modules:

- **yanimal** — `animal` base with `pet`, `cat`, `dog` subclasses.
- **yvehicle** — `vehicle` → `car` → `sportscar`, plus an `electric` mixin and a
  `motorbike`.
- **ytuning** — `tuned_sportscar`, which subclasses `yvehicle:sportscar` from a
  *different* module and overrides a yvehicle-owned slot (the cross-domain proof).

It ships its own copy of `codegen.py`, a `Makefile`, and `main.c` that exercises
local and proxy dispatch — the smallest end-to-end view of the whole system.

## Pointers

- Runtime: `include/yclass/class.h`
- Generator: `src/yclass/gen/codegen.py` (run via `make codegen`)
- RPC / transports: `include/yclass/rpc.h`, `transport.h`; `src/yclass/`
- Reference: `poc/class-object-model/`
- Bindings: [FFI Generation](../../../docs/ffi-gen.md), `tools/ffi-codegen/`, `bindings/`
