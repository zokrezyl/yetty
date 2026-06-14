#!/bin/bash
# Build the libpython-free parser: runtime + AST + vendored parser sources.
# Compiles each file, reports failures, then links. Run from the PoC root.
set -u
cd "$(dirname "$0")/.."

VEN=third_party/cpython/Parser
RT=src/runtime
AST=src/ast
B=build/full
mkdir -p "$B"

INC=(-I"$RT" -I"$AST" -I"$VEN")
CF=(-O1 -std=gnu11 -DPy_BUILD_CORE -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-unused-function "${INC[@]}")

RUNTIME=("$RT"/object.c "$RT"/unicode.c "$RT"/numbers.c "$RT"/bytes.c "$RT"/errors.c "$RT"/mem.c
         "$AST"/arena.c "$AST"/ast.gen.c "$AST"/ast_dump.gen.c "$AST"/pyp_dump.c "$AST"/printable.gen.c)

# Vendored parser sources (no myreadline).
PARSER=("$VEN"/token.c "$VEN"/pegen.c "$VEN"/pegen_errors.c "$VEN"/action_helpers.c
        "$VEN"/string_parser.c "$VEN"/peg_api.c "$VEN"/parser.c
        "$VEN"/lexer/buffer.c "$VEN"/lexer/lexer.c "$VEN"/lexer/state.c
        "$VEN"/tokenizer/string_tokenizer.c "$VEN"/tokenizer/utf8_tokenizer.c
        "$VEN"/tokenizer/file_tokenizer.c "$VEN"/tokenizer/helpers.c)

DRIVER=(src/main_free.c)

LOG="$B/build.log"
: > "$LOG"
objs=()
ok=0; fail=0; failed=()
for f in "${RUNTIME[@]}" "${PARSER[@]}" "${DRIVER[@]}"; do
  [ -f "$f" ] || continue
  o="$B/$(echo "$f" | tr '/' '_').o"
  if cc "${CF[@]}" -c "$f" -o "$o" 2>>"$LOG"; then
    objs+=("$o"); ok=$((ok+1))
  else
    fail=$((fail+1)); failed+=("$f")
  fi
done

echo "compiled ok=$ok fail=$fail"
if [ "$fail" -gt 0 ]; then
  printf 'FAILED: %s\n' "${failed[@]}"
  echo "--- first errors ---"
  grep -E "error:" "$LOG" | head -30
  exit 1
fi

if cc "${objs[@]}" -o "$B/py-parse-free" 2>>"$LOG"; then
  echo "LINK OK -> $B/py-parse-free"
  echo "--- undefined Py* symbols (must be none) ---"
  nm --undefined-only "$B/py-parse-free" | awk '{print $NF}' | grep -E '^_?Py' && echo "FAIL: libpython refs" || echo "OK: zero libpython symbols"
else
  echo "LINK FAILED"
  grep -E "undefined reference|error:" "$LOG" | sort -u | head -40
  exit 1
fi
