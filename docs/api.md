# Yetty public API

Status: design proposal for review.

## Purpose

Yetty needs one deliberate, externally supported C API from which FFI bindings
can be generated. The API is not a publication of the existing module headers
and it is not defined by the set of symbols that happen to be linked into a
shared object. It is a facade over Yetty's implementation modules.

The facade uses the existing yclass strategy:

1. API classes and methods are written as annotated C sources.
2. yclass codegen produces the public C declarations, dispatch/RPC glue, and a
   `model.yaml` for each API group.
3. Language generators consume those models.
4. Build targets collect the generated C implementations into the required
   static or shared libraries.

The generated model is the binding contract. The annotated API source is its
human-maintained source of truth.

## Decisions

### API groups live under `src/api`

Every public API group has its own directory directly below `src/api`:

```text
src/
  api/
    yplot/
    yreadme/
    yview/
    ...
```

The group name should normally match the feature users recognize. An API group
does not have to match an implementation library one-to-one.

`src/api` is intentionally separate from `src/yetty`. Code under `src/yetty`
implements Yetty; code under `src/api` defines the supported boundary through
which external consumers use it.

### API symbols have their own namespace

All exported API symbols use:

```text
yetty_api_<group>_<name>
```

Examples:

```c
yetty_api_yplot_plot_create(...);
yetty_api_yplot_set_expression(...);
yetty_api_yreadme_open(...);
```

The `api` component is mandatory. Existing implementation modules already own
names such as `yetty_yplot_*`; reusing those names would cause collisions and
would make it impossible to distinguish supported API entry points from
implementation entry points.

For yclass, the logical domain of a group is `api_<group>`. Thus the `yplot`
API group uses the domain `api_yplot`, and normal yclass naming produces the
required `yetty_api_yplot_*` symbols. The filesystem group name remains
`yplot`; callers should not need to know how the domain is encoded internally.

### Public headers mirror the API groups

Generated public headers live under:

```text
include/yetty/api/<group>/
```

For example:

```c
#include <yetty/api/yplot/plot.h>
#include <yetty/api/yplot/methods.h>
```

No header under `include/yetty/<implementation-module>` becomes part of the
supported API merely because an API implementation includes it. API headers
must expose API-owned types or explicitly approved common ABI types.

## One group

A typical group has the following shape after code generation:

```text
src/api/yplot/
  CMakeLists.txt
  plot.c                 # hand-written annotated facade
  plot.gen.c             # generated class accessor implementation
  methods.gen.c          # generated method stubs
  rpc.gen.c              # generated registration/RPC glue
  model.yaml             # generated binding contract
  README.md              # group semantics and implementation mapping

include/yetty/api/yplot/
  plot.h                 # generated class declaration/accessor
  methods.h              # generated callable methods
  rpc.h                  # generated creation/connection declarations
```

Generated files should carry the usual generated-file banner and must be
reproducible from the annotated sources. Whether generated files are committed
should follow the existing yclass policy.

Conceptually, an annotated source looks like:

```c
struct [[clang::annotate("class@api_yplot:plot")]] plot_data {
    struct yetty_yclass_object *impl;
};

[[clang::annotate("virtual@api_yplot:plot:set_expression")]]
static struct yetty_ycore_void_result
plot_set_expression(struct yetty_yclass_object *obj, const char *expression)
{
    /* Validate the API contract, then delegate to the implementation. */
}
```

This follows the current yclass annotation grammar: the virtual slot is named
`set_expression` inside domain `api_yplot`, while codegen qualifies its public
stub as `yetty_api_yplot_set_expression`. Generator tests for the new source and
header paths must lock down that mapping before the first group is added.

## Facade and implementation boundary

An API group may delegate to one module, several modules, or a smaller client
library:

```text
external caller
      |
      v
yetty_api_yplot_*        public, reviewed contract
      |
      +--> yetty_yplot_* implementation
      +--> yetty_yplot_core / client-side implementation
      +--> yclass local dispatch or RPC
```

The dependency direction is one-way: API groups may depend on implementation
modules; implementation modules must not depend on API groups. This prevents
the facade from becoming a new internal foundation and keeps it replaceable.

This separation permits implementation refactoring without an API rename. For
example, an existing feature can be split into GPU/server and client/core
libraries while `yetty_api_<group>_*` remains unchanged. Names such as
`yplot-core` describe implementation linkage and deployment, not the public
contract.

API objects should wrap or own implementation objects rather than exposing
their layouts. A field containing an implementation pointer is private class
data and must not appear as a language-visible data property in `model.yaml`.

### Delegation must scale

The facade creates a second intentional surface, but it must not create a large
second body of hand-maintained behavior. Most API methods will be pure
delegations after unwrapping the API object. Yclass codegen should support a
declarative pass-through mode that names the implementation method and emits
the type-checked delegation thunk. The generated thunk must still preserve the
API domain, Result chain, ownership rules, and local/remote behavior.

Hand-written method bodies remain appropriate where the API contract validates
or normalizes input, translates types or errors, manages a different lifetime,
combines implementation calls, or deliberately changes semantics. The model
must distinguish generated pass-throughs from custom adapters so review can
detect accidental behavior in a nominally thin facade.

## yclass requirements

Each API group is a first-class yclass domain and is registered through the
same mechanism as other yclass modules. Numeric domain IDs and slot indices are
runtime/wire details, not public API identifiers; qualified names are the
durable identity used to resolve independently built peers.

API methods use normal yclass behavior:

- the same generated call site supports a local object or a remote proxy;
- fallible calls return Yetty Result types;
- methods that cannot cross a transport are explicitly annotated `local@`;
- fire-and-forget behavior is explicit rather than inferred;
- creation, destruction, inheritance, and mixins follow yclass semantics;
- `model.yaml` is the only input to language binding generators.

The API must not claim remote support for a signature that yclass cannot
marshal. Pointer-rich implementation types, callbacks, borrowed internal
objects, and platform handles require an API-level representation or a local
method.

## ABI design rules

The API is more exposed and longer-lived than implementation headers. Every
group must therefore follow these rules:

- Prefer opaque yclass objects and API-owned value types.
- Do not expose implementation struct layouts, vtables, or private enums.
- Use fixed-width integers where width is part of the contract.
- Pass byte strings and arbitrary buffers with an explicit length.
- State whether text is UTF-8 and whether embedded NUL bytes are allowed.
- Make ownership of objects, buffers, and returned strings explicit.
- Define callback lifetime, calling thread, reentrancy, and cancellation.
- Avoid passing evolvable structs by value. Extensible input structs include a
  size and version, and callers zero fields they do not use.
- Do not expose compiler-, libc-, window-system-, or GPU-specific types unless
  that dependency is an intentional property of the API group.
- Destruction is explicit and safe for every successfully created object.
- Result errors remain valid for a documented lifetime and are translated by
  language bindings consistently.

An API group is accepted only when its generated model contains enough
ownership and type information for bindings to use it without guessing.

## Libraries and packaging

Source and symbol organization must not depend on the final library filename.
Each group should first build as a target named along the lines of:

```text
yetty_api_yplot
yetty_api_yreadme
```

The targets may be static/object libraries internally. A product build can
then package them as:

- one aggregate shared library;
- several feature shared libraries;
- statically linked API groups; or
- an FFI-oriented aggregate such as the current `libyetty_ffi.so`.

The exported C names remain identical in every packaging arrangement. Bindings
must resolve capabilities by API/feature metadata, not by assuming that every
group is present in one particular filename.

The existing `yffi` target is an aggregation mechanism. It may collect the new
API group targets, but it does not define their API and should not force API
sources to live under `src/yetty/yffi`.

## Version and feature discovery

The API needs a small base group before feature groups are considered stable.
It should provide, at minimum:

```c
uint32_t yetty_api_abi_version(void);
const char *yetty_api_version(void);
bool yetty_api_has_group(const char *group, uint32_t min_version);
```

Each group needs its own monotonically increasing contract version in generated
metadata. The project version, aggregate-library ABI version, group contract
version, and yclass wire compatibility are related but distinct values and
must not be represented by one version string.

Feature discovery is necessary because builds may omit groups or package them
in separate libraries.

## Build and generation integration

The top-level build adds `src/api` after the implementation targets needed by
enabled API groups. `src/api/CMakeLists.txt` selects groups using feature
options and adds their subdirectories.

Yclass already accepts a source directory independently of the module's
location, so `src/api/<group>` needs no new source-relocation mechanism. The
current generator does, however, use one `<module>` argument for several roles:
it validates the annotation domain, chooses the generated header directory,
and names the module registration function. The API needs those roles split:

- logical domain/register stem: `api_<group>`;
- public-header path stem: `api/<group>`;
- source/output directory: `src/api/<group>`.

For `yplot`, this produces `yetty_api_yplot_register()`, headers under
`include/yetty/api/yplot`, and generated sources plus `model.yaml` under
`src/api/yplot`. This is a narrow generator-interface change, not an open-ended
new codegen subsystem. Slashes must never be treated as part of a yclass domain
or C symbol.

Binding generators must discover models under both the legacy implementation
tree and the API tree during migration. Their output roots and import names
must keep implementation-domain bindings separate from supported API bindings;
two models must never silently overwrite the same generated module or package
entry. Supported bindings ultimately default to API models. Any remaining
implementation bindings are explicitly marked unstable/legacy.

The build must also enforce a controlled export surface. Shared libraries use
hidden visibility by default and export only `yetty_api_*` plus any explicitly
required runtime entry points. Whole-archiving implementation libraries must
not make their symbols part of the supported ABI.

## Migration

Migration is incremental and does not require renaming implementation modules.

1. Define separate stable-API and legacy-implementation binding namespaces so
   both kinds of model can coexist without output or import collisions.
2. Split the yclass logical-domain/register stem from its public-header path,
   and add the API base/version group.
3. Implement one reference feature with an existing client/core and server/RPC
   split, such as `yview` or `yplot`.
4. Prove API-facade-over-RPC: an API proxy must successfully dispatch through
   the facade to an implementation proxy, including creation, calls, errors,
   and destruction. This double indirection is a hard gate for the design.
5. Generate bindings from the API model and test C, Python, and Lua consumers.
6. Add ABI/export checks before expanding the surface.
7. Inventory the remaining features as thin pass-through facades versus custom
   adapters, then wrap them one group at a time.
8. Deprecate direct bindings to implementation-domain models only after an API
   replacement exists.

Existing `yetty_<module>_*` symbols remain implementation interfaces during
the migration. Compatibility is promised only for the documented
`yetty_api_*` surface once its stability policy is adopted.

## Required checks for every group

CI should verify:

- yclass codegen is reproducible and leaves no diff;
- generated public headers compile as C and C++;
- one generated umbrella translation unit includes every enabled public API
  header and compiles cleanly;
- the generated model passes schema validation;
- generated bindings can load and call a smoke-test object;
- local and remote/proxy paths have matching observable behavior where remote
  use is supported;
- the reference group passes the API-proxy-over-implementation-proxy test;
- ownership and destruction paths pass sanitizer tests;
- exported-symbol lists contain only approved symbols;
- model and ABI diffs are reported explicitly in review; and
- disabled feature groups are reported accurately by capability discovery.

## Review questions

The first implementation should resolve these points before the API is called
stable:

1. What is the exact compatibility policy for group models, C ABI, and yclass
   wire names?
2. Are domain IDs centrally allocated in source control, generated from a
   registry, or negotiated entirely by qualified name?
3. Which Result error representation is safe across shared-library and FFI
   boundaries, and who owns error text?
4. How are API annotations for ownership, nullability, arrays, callbacks, and
   asynchronous completion represented in `model.yaml`?
5. Which minimal runtime symbols, in addition to `yetty_api_*`, must an
   aggregate library export for generated yclass dispatch?
6. After migration, should implementation-domain models remain available as
   explicitly unstable bindings, or should public binding distributions contain
   API models only?

These are review gates, not reasons to weaken the namespace or directory
decisions above.
