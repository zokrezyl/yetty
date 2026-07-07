# symbol-graph — cross-module symbol-use analyzer

Answers one question per module: which symbols in `include/yetty/<mod>/`
are actually consumed by other modules, and which symbols in
`src/yetty/<mod>/` are consumed from outside despite being private?

After acting on the report, `include/yetty/` becomes precisely the
inter-module API — the prerequisite for scoping FFI generation.

## Running

```sh
# Needs a compile_commands.json (make build-desktop-ytrace-release).
qa-tools/analysis/symbol-graph/symbol-graph.py            # scan + report
qa-tools/analysis/symbol-graph/symbol-graph.py scan       # extraction only
qa-tools/analysis/symbol-graph/symbol-graph.py report     # re-aggregate
qa-tools/analysis/symbol-graph/symbol-graph.py scan src/yetty/yplot  # narrow
```

`QA_BUILD_DIR` selects the build dir; `QA_PATHS` narrows the scan scope
like the other qa scripts. The scan takes a few minutes for the full
tree (one libclang parse per TU, parallel across cores).

## Universe

Every TU in the compile database under `src/yetty/`, `tools/`, `test/`,
and `demo/`. The latter three are consumer-only namespaces
(`tools/<name>`, …): they count as external users but never get their
own report. Third-party trees and build-dir generated TUs are excluded.
`*.gen.{c,h}` files are attributed to their owning module by path.

## What is tracked, and what counts as a use

| Kind | Tracked | Counts as use |
|---|---|---|
| function | external linkage anywhere; `static` only in headers (inline API) | call or address taken |
| type | named `struct`/`union`/`enum` definitions | **by-value** only: field access, `sizeof`, value instantiation (var / param / return / field) |
| constant | `enum` members, file-scope `const` variables (non-`static` in .c) | any reference |

Cross-TU matching follows linkage. Functions join by name — the linker
makes the name one entity program-wide, which also catches consumers
that hand-write an `extern` instead of including the owning header.
Types and constants have no linkage (an unrelated local `struct opts`
in some tool is a different type), so their uses only count when they
resolve to the owner's actual declaration site.

A bare `struct foo *` parameter/return/field/local is deliberately NOT
a use of `struct foo` — pointer-only mentions never pull a definition
in. Field access through the pointer is. Anonymous structs/unions
attribute to their nearest named enclosing record.

Out of scope by design: macros, typedefs (banned in this codebase),
globals (banned), non-default platform paths (only the configured
build's TUs are seen).

Known imprecision: `sizeof(struct foo *)` counts as a use of
`struct foo` (rare); symbols referenced only from macro bodies
attribute to the expansion site's module.

## Outputs (under `tmp/qa/symbol-graph/`)

`tus/<rel-tu-path>.yaml` — one record per TU:

```yaml
tu: src/yetty/yplot/yplot.c
module: yplot
defines:
  functions:
  - {sym: yetty_yplot_render, decl: 'include/yetty/yplot/yplot.h:74'}
uses:
  functions:
  - {sym: yetty_yexpr_parse_plot, decl: 'include/yetty/yexpr/yexpr.h:184'}
```

`report/<module>.yaml` — the classification:

- `private_candidates` — declared under `include/yetty/<mod>/`, zero
  external users → safe to move into `src/yetty/<mod>/`.
  `internal_use: false` additionally means no user at all (dead?).
- `leaks` — declared under `src/yetty/<mod>/`, used externally
  (`used_by` lists the consumers) → promote to `include/yetty/<mod>/`.
- `public_used` — the genuine inter-module API, with `used_by`.

`report/summary.yaml` — per-module counts, plus `conflicts`: the same
symbol declared in files of two or more REAL modules (consumer-side
redeclarations never count) — a blurred module boundary. Conflicted
symbols are excluded from the per-module classification.

Exit codes match the other qa scripts: 0 clean, 1 findings,
2 could not run.
