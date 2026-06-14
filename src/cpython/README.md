# yetty/cpython — a libpython-free **subset of Python** (parser / AST emitter)

> **This is a SUBSET of Python, not a Python implementation.** It is *only* the
> CPython 3.14 **parser**: it turns Python source text into an AST (the same
> shape as `ast.dump()`). It does **not** compile, execute, or import anything —
> no bytecode, no interpreter, no standard library, no runtime. It is the front
> end (tokenizer → PEG parser → AST) and nothing else.

**Goal:** extract CPython's PEG parser so it turns Python 3.14 source into an AST
**without linking `libpython`** — a small binary that depends on `libc` only.

**Status: working.** `make` builds `build/full/py-parse-free` — a **~385 KB**
binary that links **only libc** (`ldd` shows no libpython; `nm` shows zero `Py*`
undefined symbols). On the CPython 3.14 standard library it produces
**byte-identical `ast.dump()` output to the reference CPython parser on all 1823
parseable files, with zero crashes** (`make verify`).

## Location & build integration

This lives at `src/cpython/` (sibling of `src/yetty`, `src/libvterm-*`,
`src/tinyemu`). The yetty build compiles it via `CMakeLists.txt` — wired in
`build-tools/yetty/platform/shared.cmake` — producing:

- **`yetty_cpython`** — a static library; public API
  `include/yetty/cpython/cpython.h` (`yetty_cpython_parse_and_dump(...)`). The
  internal runtime headers (which shadow CPython's `<Python.h>`) are kept
  **PRIVATE** to the library and never leak into the rest of the yetty build.
- **`py-parse`** — a standalone `file.py → AST` tool.

`src/cpython/Makefile` is a self-contained dev build of the same sources
(`make`, `make run`, `make verify`, `make gen-ast`).

```
$ make && ./build/full/py-parse-free samples/demo.py
Module(body=[Import(names=[alias(name='os', asname=None)]), ...])
$ ldd build/full/py-parse-free        # libc.so.6 only — no libpython
$ make verify
identical to CPython 3.14 reference: 1823
differ: 0 | parser crash: 0 | reference-unparseable: 28
```

How: the vendored CPython parser/tokenizer compile against our own libpython-free
runtime (`src/runtime/` — a plain-C `PyObject` box, string/number/bytes/error
shims, ~106 C-API functions) and a plain-C AST generated from the ASDL schema
(`src/ast/`). Only two tiny edits to the vendored sources were needed (see
`third_party/cpython/PROVENANCE.md`).

### Known limitations (the unparseable tail)

A handful of stdlib files exercise the full Unicode database, which this PoC does
not embed:
- `\N{NAME}` named-character escapes are left undecoded (needs the ~16k
  name→codepoint table).
- Identifier validity uses a simplified rule (no full XID_Start/XID_Continue
  tables), so some invalid-identifier programs are accepted rather than rejected.
- Source encodings: UTF-8 and Latin-1/ISO-8859-1 are decoded; other declared
  codecs (koi8-r, cp1251, …) fall back to UTF-8 passthrough.

These are Unicode-data completeness gaps, not parser gaps; the reference parser
used for `make verify` (minimal-init, no `unicodedata`) also can't parse most of
them, which is why they show as "reference-unparseable".

### Error reporting (invalid input)

For **valid** Python the AST is byte-exact (the verified case). For **invalid**
Python the parser correctly rejects with a non-zero return and surfaces
tokenizer-level messages (e.g. `'(' was never closed`), but CPython's elaborate
parser-level diagnostics are only partially reproduced: some errors report a
generic "invalid syntax", and a few programs CPython rejects via its "invalid
rules" pass (e.g. `x = *a`) are accepted here. Error-message fidelity and
strict rejection of malformed input are the soft edge; correct ASTs for
well-formed input are the contract.

## Why this is non-trivial (the actual dependency)

CPython's parser is correct and battle-tested, but its C code speaks `PyObject`
everywhere. A symbol-level trace of the parser objects (`parser.c`, the pegen
runtime, the tokenizer/lexer) shows **235 external symbols**. Beyond ~25 trivial
libc calls, they break down as:

| Dependency | Count | What it's for |
|------------|------:|---------------|
| `_PyAST_*` node constructors | 81 | building AST nodes (generated from `Python.asdl`) |
| `PyUnicode*` | 53 | **every identifier and string literal is a `PyUnicode` object** |
| `PyErr*` / `PyExc*` | 66 | syntax errors raised as Python exceptions |
| `PyLong/PyFloat/PyComplex/PyBytes` | ~28 | numeric & bytes literals as Python objects |
| `PyMem*` | 31 | allocation |
| `_PyArena_*` | 2 | the parse arena |
| runtime (`_PyRuntime`, `PyThreadState`, `PyType*`) | ~10 | assumes a live interpreter |

The killer is `PyUnicode`: the moment `pegen.c` calls `PyUnicode_FromStringAndSize`
for a NAME token, that one call transitively requires `unicodeobject.o →
typeobject.o → obmalloc.o → _PyRuntime → gc → codecs`. Linking the parser against
the static `libpython3.14.a` pulls in **176 of 190** core object files for this
reason. There is no configure flag or subset that avoids it — the coupling is in
the source.

The parsing *logic* itself is clean. The coupling is concentrated in five
hand-written files plus the generated AST.

## The plan

1. **[DONE] Plain-C AST.** `tools/gen_ast.py` re-emits `Parser/Python.asdl` as
   plain-C structs + constructors (`src/ast/ast.gen.{h,c}`), matching CPython's
   names (`expr_ty`, `_PyAST_BinOp`, `Name_kind`, `asdl_expr_seq`) but with
   plain-C leaves (`identifier = char*`, `constant` = a tagged struct). The
   `_PyAST_*` constructors carry **zero** `PyObject`. `src/ast/asdl.h` is the
   libpython-free `pycore_asdl.h`.
2. **[DONE] Bump arena.** `src/ast/arena.c` is a ~110-line bump allocator
   implementing `_PyArena_New/Malloc/Free`. No `PyList`, no PyObject tracking.
3. **[NEXT] De-`PyObject` the five files** — `action_helpers.c` (317 sites),
   `pegen.c` (280), `pegen_errors.c` (140), `string_parser.c` (69),
   `tokenizer/helpers.c` (85):
   - identifiers / strings: `PyUnicode_*` → `char*` (slice into the source, or a
     small intern table)
   - numbers: `PyLong/PyFloat_FromString` → keep the literal text as `char*`
     (or parse to `int64`/`double`)
   - errors: `PyErr_*` → a `struct { int line, col; char *msg; }`
   - `PyMem_*` → `malloc`
4. **Result:** parser + tokenizer + our AST, linking against `libc` only.

### Status

| Step | State | Proof |
|------|-------|-------|
| 1–2: AST + arena | **done, libpython-free** | generated by `make gen-ast` |
| 3: parser port (de-`PyObject`) | **done** | `make free` → links libc only |
| Whole parser, libpython-free | **done** | `make verify` → 1823/1823 identical, 0 crashes |

Dev Makefile targets: `make` (build the libc-only binary), `make run` (parse the
sample), `make verify` (diff against a reference CPython 3.14 parser across the
stdlib), `make gen-ast` (regenerate the AST from the ASDL), `make gen-printable`
(regenerate the printable-codepoint table).

## Layout

```
src/cpython/
├── README.md                  # this file (yetty project)
├── LICENSE.md                 # licensing split — READ THIS
├── CMakeLists.txt             # yetty build integration (yetty_cpython + py-parse)
├── Makefile                   # self-contained dev build
├── src/
│   ├── runtime/               # libpython-free runtime (our PyObject box + C-API shims)
│   ├── ast/                   # plain-C AST: generated structs/ctors/dumper + arena
│   ├── cpython_api.c          # public-API wrapper (include/yetty/cpython/cpython.h)
│   └── main_free.c            # the py-parse tool entry point
├── tools/                     # gen_ast.py, gen_printable.py, build/verify scripts
├── samples/                   # demo.py
└── third_party/cpython/       # unmodified CPython 3.14.6 sources (PSF License v2)
    ├── LICENSE                # PSF License Agreement v2
    ├── PROVENANCE.md          # exact origin + the two source edits + regen commands
    ├── Grammar/{python.gram,Tokens}
    ├── Parser/                # PEG parser runtime + tokenizer/lexer + Python.asdl
    └── Tools/peg_generator/   # the PEG parser generator (pure Python)
```

## Regenerating the parser from the grammar

The parser is generated. From `third_party/cpython/`:

```sh
PYTHONPATH=Tools/peg_generator \
  python3 -m pegen -q c Grammar/python.gram Grammar/Tokens -o Parser/parser.c
```

## License

See **[`LICENSE.md`](LICENSE.md)**. In short: `third_party/cpython/**` is under the
**PSF License Agreement v2** (Python Software Foundation); everything under `src/`
is original yetty-project work. The PSF notice in `third_party/cpython/LICENSE`
must not be removed.
