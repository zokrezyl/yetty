#!/bin/sh
# Build tmux-oracle against the pinned tmux object files in tmp/tmux.
#
# The pin (tmp/tmux, commit d5afb67) must already be built (`./configure &&
# make` inside it). We take every object except tmux.o's `main`, which is
# renamed away with objcopy so the oracle provides its own entry point. The
# compile flags mirror the pin's own Makefile (DEFS + AM_CPPFLAGS extracted
# at build time), so oracle.c sees the exact config.h environment the
# objects were built with.
set -e

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
tmux_dir="$repo_root/tmp/tmux"
out_dir="$repo_root/tmp/tmux-oracle-build"

if [ ! -f "$tmux_dir/tmux.o" ]; then
	echo "tmux-oracle: pinned tmux not built at $tmux_dir" >&2
	exit 1
fi

mkdir -p "$out_dir"

# tmux.o without its main (globals and helpers stay linkable).
objcopy --redefine-sym main=tmux_unused_main "$tmux_dir/tmux.o" \
	"$out_dir/tmux-nomain.o"

# The pin's own compile environment.
defs=$(sed -n 's/^DEFS = //p' "$tmux_dir/Makefile")
cppflags=$(sed -n 's/^AM_CPPFLAGS = //p' "$tmux_dir/Makefile" | sed 's/\\$//')
libs=$(sed -n 's/^LIBS = //p' "$tmux_dir/Makefile")

# Every object except tmux.o (replaced by the nomain copy) and the compat
# objects that carry their own strong definitions already linked into the
# main set.
objects=$(ls "$tmux_dir"/*.o | grep -v '/tmux\.o$')
compat_objects=$(ls "$tmux_dir"/compat/*.o 2>/dev/null || true)

# The pin was built with the nix gcc recorded in its config.log; the profile
# `cc` may have moved to a different glibc since, so extract and reuse the
# SAME wrapper (falls back to cc when the store path is gone).
pin_cc=$(sed -n 's/^PATH: \(.*gcc-wrapper[^/]*\/bin\)\/$/\1\/gcc/p' \
	"$tmux_dir/config.log" | head -1)
[ -x "$pin_cc" ] || pin_cc=cc

# DEFS carries shell-escaped quoting (\"tmux\") from automake — it must be
# evaluated by the shell, hence the eval.
# shellcheck disable=SC2086
eval "$pin_cc" -O2 -g -o \"$out_dir/tmux-oracle\" \
	\"$script_dir/oracle.c\" \
	-I\"$tmux_dir\" $defs $cppflags \
	\"$out_dir/tmux-nomain.o\" $objects $compat_objects \
	$libs -lutil

echo "built: $out_dir/tmux-oracle"
