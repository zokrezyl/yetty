# class-object-model — annotated-C class/object model + RPC (PoC)

A small proof-of-concept for an annotation-driven object model in plain C:
classes, single inheritance, mixins, virtual dispatch, encapsulated data
members, and the *same* call site working locally or over an RPC wire. The
object model is described by `[[clang::annotate(...)]]` attributes on
hand-written `.c` sources; a codegen step (`gen/codegen.py`, run from the
`Makefile`) parses them via the clang JSON AST and emits the glue.

Three independent modules are built and linked side by side to exercise the
model:

| module     | classes                                              | shows |
|------------|------------------------------------------------------|-------|
| `yvehicle` | `vehicle` → `car` → `sportscar` (mixin `electric`); `motorbike` | inheritance, mixin, read-only + private members |
| `yanimal`  | `animal` → `cat`, `animal` → `dog` (mixin `pet`)      | a second, independent domain |
| `ytuning`  | `tuned_sportscar` extends `yvehicle:sportscar`        | cross-module subclass + cross-module data access |

## Build & run

```sh
make            # list targets
make build      # codegen + compile -> ./poc
make run        # build then run
make clean      # remove the binary + every generated file
```

`make` runs codegen first (`uv run gen/codegen.py <module> …`), then compiles.
The demo forks: a server process and a client process talk over a
`socketpair`, so every scenario runs once locally and once over RPC. Tracing
is on by default; output is verbose (redirect to a file).

## Layout

```
gen/codegen.py        the generator (annotated .c -> generated glue)
include/class.h       runtime: object/class/slot model + object_data_offset
include/result.h      Result types + YETTY_* macros
include/rpc.h         RPC wire + session
src/class.c           runtime implementation
src/rpc.c             RPC server loop + client session
src/<module>/*.c      hand-written annotated sources (the authoring surface)
src/main.c            the demo driver
include/<module>/*.h  GENERATED public headers
src/<module>/*.gen.c  GENERATED registration + accessor bodies
src/<module>/*.gen.{c,h}, model.yaml   other generated artefacts
```

Only the non-`.gen` `.c` sources and the runtime headers are hand-written.
Everything under `include/<module>/` and every `*.gen.*` / `model.yaml` is
generated — do not edit it; edit the annotated source and re-run codegen.

---

## The object model

### Objects and classes

An object is just a class pointer followed by the per-class data blocks:

```c
struct object { const struct class *klass; };
```

A `struct class` carries its descriptor (qualified name, regular/mixin,
`data_size`), its `parent`, its `mixins[]`, the per-domain dispatch table, and
the computed `instance_size`. Classes are registered lazily the first time
their accessor (`<domain>_<class>_class_get()`) is called.

### Domains and method slots

Each module is a **domain** with its own 0-based slot index space, so two
modules can use the same local method name without colliding. A `method_slot`
packs the pair:

```
bits 27..24  domain id (1..15)
bits 23..0   local index inside that domain's slot table
```

The same `<domain>_<localname>` string is the slot-table key, the public C
symbol, and the wire label — one canonical name, three uses.

### Dispatch

Every method has a generated public stub. It branches on `ctx->session`:

- **NULL → local**: look up the impl in `obj->klass`'s dispatch table and call
  it directly (a vtable call).
- **set → remote**: translate the local slot to the peer's id and issue an
  `rpc_call`; the server re-enters the *same* public stub with a local ctx so
  the right override runs on the real object.

Caller code is identical either way.

### Inheritance, mixins, instance layout

`class_register()` inherits the parent's dispatch table, then overlays each
mixin's, then the class's own ops. The instance layout is laid out
root → derived, each class's own data immediately followed by that class's
mixin data:

```
[ object header ][ vehicle_data ][ car_data ][ sportscar_data ][ electric_data ]
                  \-- ancestors, root first --/  \-- own --/      \-- mixin --/
```

Because ancestors always precede in the same order, a given class's data block
sits at a fixed offset in *any* instance derived from it. `object_data_offset()`
recovers that offset by replaying the layout for a given most-derived class —
it is the one runtime primitive the data accessors build on.

---

## Annotations (the authoring surface)

Every annotation is `<verb>@<domain>:<path...>`. On the **data struct**:

```c
struct [[clang::annotate("class@yvehicle:sportscar")]]   /* regular class */
       [[clang::annotate("parent@yvehicle:car")]]        /* base class    */
       [[clang::annotate("uses@yvehicle:electric")]]     /* mixin         */
       sportscar_data { ... };
```

`mixin@<DOMAIN>:<CLASS>` marks a mixin instead of `class@`. `parent`/`uses` may
name a **foreign** domain (that is how `ytuning:tuned_sportscar` extends
`yvehicle:sportscar`).

On an **impl function**:

```c
[[clang::annotate("override@yvehicle:sportscar:vehicle_describe")]]        /* same-domain slot */
[[clang::annotate("override@ytuning:tuned_sportscar:yvehicle:vehicle_describe")]]  /* cross-domain slot */
```

A method is *inferred* from the first impl of each slot whose owning domain is
the current module; its C signature is taken from that impl. Cross-domain
overrides target a slot whose public stub already lives in the slot's home
module.

The required method shape is `RetT slot(struct ctx *ctx, struct object *obj, …)`,
and every impl returns a Result.

---

## Data members: encapsulated access (the model's data side)

Data structs are **never** exposed in a header — the struct definition stays
private to its owning `.c`. Other classes reach members only through generated
accessors. Which accessors exist is controlled by annotating each member:

```c
struct [[clang::annotate("class@yvehicle:vehicle")]] vehicle_data {
    [[clang::annotate("property")]]    int mileage;   /* read + write */
    [[clang::annotate("property")]]    int speed;     /* read + write */
    int fuel_level;                                   /* private — no accessor */
};
```

| annotation         | generates                         | external access |
|--------------------|-----------------------------------|-----------------|
| `property`         | `_get` **and** `_set`             | read + write    |
| `property:ro`      | `_get` only                       | read-only       |
| `property:wo`      | `_set` only                       | write-only      |
| *(none)*           | nothing                           | private — owner only |

### The opaque data handle

For each class, codegen emits a handle that returns a pointer to the class's
own data block, wrapped in a Result:

```c
struct vehicle_data;                                            /* forward decl only */
YETTY_YRESULT_DECLARE(yvehicle_vehicle_data_ptr, struct vehicle_data *);
struct yvehicle_vehicle_data_ptr_result yvehicle_vehicle_data_get(struct object *obj);
```

The struct tag is **incomplete** everywhere except the owning `.c`, so the
returned pointer is opaque to everyone else — they can hold it but cannot
dereference it. The owning class, where the struct is complete, uses the handle
to reach *all* of its own members (including private and read-only ones).

### Member getters / setters

Exposed members get typed accessors; both return a Result so a bad object
surfaces as an error instead of a fabricated value:

```c
struct yetty_ycore_int_result  yvehicle_vehicle_mileage_get(struct object *obj);
struct yetty_ycore_void_result yvehicle_vehicle_mileage_set(struct object *obj, int value);
```

Each resolves the block through the owning class's handle (so it works whatever
the most-derived class of `obj` is) and propagates failure.

### Access rules

- **Your own members** → one `<domain>_<class>_data_get(obj)`, check it once,
  then touch `.value->member` directly. One call covers every own member; it
  does not scale with field count.
- **Another class's members** (a parent's, a mixin's, a mixin host's) → the
  per-member `_get` / `_set`. You never see that struct.
- **Private members** → only the owning class can touch them.
- **Read-only / write-only** → the missing direction is simply not generated,
  so an external write to a `:ro` member fails to compile; the owner still has
  full access through the handle.

Example — `sportscar_describe` reaching four data blocks of one object:

```c
struct yvehicle_sportscar_data_ptr_result self = yvehicle_sportscar_data_get(obj); /* own: top_speed, turbo_engaged */
YETTY_RETURN_IF_ERR(str, self, "sportscar_describe: data block");
struct yetty_ycore_int_result speed   = yvehicle_vehicle_speed_get(obj);            /* parent  */
struct yetty_ycore_int_result mileage = yvehicle_vehicle_mileage_get(obj);          /* parent  */
struct yetty_ycore_int_result doors   = yvehicle_car_doors_get(obj);                /* parent  */
struct yetty_ycore_int_result battery = yvehicle_electric_battery_percent_get(obj); /* mixin   */
/* ... build the string from self.value->top_speed, speed.value, ... */
```

### What the demo proves

- **Private member**: `vehicle.fuel_level` has no accessor in any header; only
  `vehicle`'s own code reads it. The plain `vehicle` instance prints it
  (`fuel=96`); subclasses cannot reach it.
- **Read-only member**: `sportscar.top_speed` is `property:ro` —
  `yvehicle_sportscar_top_speed_get` exists, `..._set` is generated nowhere.
- **Cross-module data access**: `ytuning:tuned_sportscar` reads and writes
  `yvehicle`'s members purely through the accessors, including reading the
  read-only `top_speed` it cannot set.
- **Mixin ↔ host**: the `electric` mixin's brake reads/writes the host
  `vehicle`'s `speed` through vehicle's setter.
- All of the above runs identically **local and over RPC**.

---

## Error handling

Every fallible function returns a `*_result` (see `include/result.h`). Callers
check `YETTY_IS_ERR` and either propagate with `YETTY_RETURN_IF_ERR` (which
preserves the heap-linked `cause` chain) or absorb at a boundary — `main.c`
absorbs and prints. The data accessors above are part of this: a `_data_get`
on a wrong object returns an error whose cause chain points back through
`object_data_offset`.

---

## Generated symbol naming

| symbol | meaning |
|---|---|
| `<domain>_<class>_class_get()` / `_mixin_get()` | lazy class registration / accessor |
| `<domain>_<class>_create(ctx)`                  | factory (local alloc or remote proxy) |
| `<domain>_<slot>(ctx, obj, …)`                  | public method stub (local dispatch or RPC) |
| `<domain>_<class>_data_get(obj)`                | opaque data-block handle (Result) |
| `<domain>_<class>_<field>_get(obj)`             | member getter (value Result) |
| `<domain>_<class>_<field>_set(obj, value)`      | member setter (void Result) |
| `struct <domain>_<class>_data_ptr_result`       | Result type wrapping the block pointer |

---

## Codegen notes

- One `gen/codegen.py` invocation per module emits that module's public
  headers, `methods.gen.{h,c}`, per-class `*.gen.c`, `rpc.gen.{h,c}`, and
  `model.yaml`.
- The generator only needs declarations from the AST, so it tolerates a
  function *body* that references a not-yet-generated accessor: clang still
  emits a complete JSON AST on such errors, which is the bootstrap case on a
  clean tree (`make clean && make build`).
- A compile-time check guards every override: each `*.gen.c` assigns the impl
  to a function-pointer typedef of the slot's signature, so a mismatched
  override is a hard compile error.
</content>
