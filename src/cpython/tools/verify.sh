#!/bin/bash
# Compare the libpython-free parser's AST dump against the reference CPython
# 3.14 parser across a set of files. Usage: verify.sh [N]
cd "$(dirname "$0")/.."
REF=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6/pyparse/pyparse
FREE=./build/full/py-parse-free
CP=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6
N="${1:-100000}"

identical=0; differ=0; reffail=0; freefail=0
difflist=()
while IFS= read -r f; do
  rout=$("$REF" "$f" 2>/dev/null); rc=$?
  if [ $rc -ne 0 ]; then reffail=$((reffail+1)); continue; fi
  fout=$("$FREE" "$f" 2>/dev/null); fc=$?
  if [ $fc -ne 0 ]; then freefail=$((freefail+1)); difflist+=("CRASH ${f#$CP/Lib/}"); continue; fi
  if [ "$rout" = "$fout" ]; then
    identical=$((identical+1))
  else
    differ=$((differ+1))
    [ ${#difflist[@]} -lt 8 ] && difflist+=("DIFF ${f#$CP/Lib/}")
  fi
done < <(find "$CP/Lib" -name '*.py' | sort | head -n "$N")

echo "identical to CPython 3.14 reference: $identical"
echo "differ: $differ | parser crash: $freefail | reference-unparseable: $reffail"
if [ ${#difflist[@]} -gt 0 ]; then printf '  %s\n' "${difflist[@]}"; fi
