#!/bin/bash
# Show the first divergence for the first K differing files.
cd "$(dirname "$0")/.."
REF=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6/pyparse/pyparse
FREE=./build/full/py-parse-free
CP=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6
K="${1:-15}"
shown=0
while IFS= read -r f; do
  r=$("$REF" "$f" 2>/dev/null) || continue
  fo=$("$FREE" "$f" 2>/dev/null)
  [ "$r" = "$fo" ] && continue
  printf '%s' "$r" > /tmp/_r.txt; printf '%s' "$fo" > /tmp/_f.txt
  off=$(cmp /tmp/_r.txt /tmp/_f.txt 2>/dev/null | grep -oE 'byte [0-9]+' | grep -oE '[0-9]+')
  [ -z "$off" ] && continue
  echo "### ${f#$CP/Lib/} @ $off"
  echo -n "  REF : "; dd if=/tmp/_r.txt bs=1 skip=$((off>30?off-30:0)) count=70 2>/dev/null; echo
  echo -n "  FREE: "; dd if=/tmp/_f.txt bs=1 skip=$((off>30?off-30:0)) count=70 2>/dev/null; echo
  shown=$((shown+1)); [ $shown -ge $K ] && break
done < <(find "$CP/Lib" -name '*.py' | sort)
