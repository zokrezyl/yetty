#!/bin/bash
cd "$(dirname "$0")/.."
REF=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6/pyparse/pyparse
FREE=./build/full/py-parse-free
CP=/home/misi/work/my/yetty-yfsvm/tmp/cpython-3.14.6
enc=0; other=0; otherlist=()
while IFS= read -r f; do
  r=$("$REF" "$f" 2>/dev/null) || continue
  [ "$r" = "$("$FREE" "$f" 2>/dev/null)" ] && continue
  rel=${f#$CP/Lib/}
  case "$rel" in
    encodings/*) enc=$((enc+1));;
    *) other=$((other+1)); [ ${#otherlist[@]} -lt 30 ] && otherlist+=("$rel");;
  esac
done < <(find "$CP/Lib" -name '*.py' | sort)
echo "differ in encodings/: $enc"
echo "differ elsewhere: $other"
printf '  %s\n' "${otherlist[@]}"
