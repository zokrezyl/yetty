# Provenance — CPython-derived sources

Every file under `third_party/cpython/` is copied **verbatim** (unmodified) from
the CPython source distribution:

- **Project:** CPython — the reference Python interpreter
- **Version:** 3.14.6 (release source tarball)
- **Upstream:** https://github.com/python/cpython (tag `v3.14.6`)
- **Copyright:** © 2001–2025 Python Software Foundation; All Rights Reserved
- **License:** PSF License Agreement, Version 2 — see `LICENSE` in this directory.

## What was copied and why

| Path | Upstream path | Role |
|------|---------------|------|
| `Grammar/python.gram` | `Grammar/python.gram` | The PEG grammar (source of truth for the parser) |
| `Grammar/Tokens` | `Grammar/Tokens` | Token type definitions |
| `Parser/*.c`, `Parser/*.h` | `Parser/` | The PEG parser runtime + tokenizer/lexer |
| `Parser/parser.c` | `Parser/parser.c` | The generated parser (regenerable from the grammar) |
| `Parser/Python.asdl` | `Parser/Python.asdl` | The AST node schema (ASDL) |
| `Parser/asdl.py`, `Parser/asdl_c.py` | `Parser/` | ASDL parser + the C-code emitter for AST nodes |
| `Tools/peg_generator/pegen/` | `Tools/peg_generator/pegen/` | The PEG parser generator (pure Python) |

The relative layout is **identical to upstream** so the regeneration commands work
unchanged (see below).

## What was NOT copied

The CPython runtime headers (`Include/`, `Include/internal/pycore_*.h`) and
`libpython` are intentionally **excluded**. They are exactly the dependency this
PoC exists to remove. The copied `.c` sources still `#include` them today; making
this compile against libc only is the work tracked in the top-level `README.md`.

## Modifications

The libpython-free port keeps the vendored sources almost entirely intact; the
parser/tokenizer compile against our own runtime headers (`../../src/runtime/`,
`../../src/ast/`) instead of `Python.h` + `libpython`. Only two small source
edits were needed, both in error/representation paths, recorded here:

1. **`Parser/pegen.c`** — in the syntax-error helper, dropped the
   `(PySyntaxErrorObject *)exc->metadata = metadata;` write. Our SyntaxError
   model has no metadata slot; that field only enriches f-string error display.

2. **`Parser/string_parser.c`** — in `decode_unicode_with_escapes`, non-ASCII
   bytes are now copied through unchanged instead of being expanded to `\U`
   escapes. Our string store and unicode-escape decoder are UTF-8-byte based, so
   the original `\U`-expansion (which assumed code-point indexing of a decoded
   run) is unnecessary and was overflowing the scratch buffer.

Everything else under `Parser/` is byte-for-byte upstream. `Parser/parser.c` is
regenerated from `Grammar/python.gram` by the bundled generator (see above) and
is identical to upstream.

## Regenerating the generated files

From this directory (`third_party/cpython/`):

```sh
# Regenerate Parser/parser.c from the grammar:
PYTHONPATH=Tools/peg_generator \
  python3 -m pegen -q c Grammar/python.gram Grammar/Tokens -o Parser/parser.c

# Regenerate the C AST node constructors from the ASDL schema:
python3 Parser/asdl_c.py Parser/Python.asdl \
  -H pycore_ast.h -I pycore_ast_state.h -C Python-ast.c
```
