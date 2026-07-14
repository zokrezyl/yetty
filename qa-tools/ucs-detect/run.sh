#!/usr/bin/env bash
# Run ucs-detect inside a freshly launched yetty and store the YAML result.
#
# ucs-detect measures the terminal from the inside (it prints test
# characters and reads the cursor position back), so it must run as the
# child process of the yetty instance under test. The YAML artifact is
# the machine-readable score record; the on-screen summary is cosmetic.
#
# Usage:
#   qa-tools/ucs-detect/run.sh [yetty-binary] [output-yaml] [extra ucs-detect args...]
#
# Defaults:
#   yetty-binary : ./build-desktop-ytrace-release/yetty
#   output-yaml  : tmp/ucs-detect-<git-describe>.yaml
#
# Examples:
#   qa-tools/ucs-detect/run.sh
#   qa-tools/ucs-detect/run.sh ./build-desktop-ytrace-release/yetty \
#       tmp/baseline.yaml --limit-category-time 60
set -eu

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

yetty_binary=${1:-./build-desktop-ytrace-release/yetty}
git_version=$(git describe --always --dirty 2>/dev/null || echo unknown)
output_yaml=${2:-tmp/ucs-detect-${git_version}.yaml}
shift $(( $# > 2 ? 2 : $# ))

if [ ! -x "$yetty_binary" ]; then
    echo "error: yetty binary not found or not executable: $yetty_binary" >&2
    exit 1
fi

command -v uvx >/dev/null 2>&1 || {
    echo "error: uvx not found in PATH (needed to run ucs-detect)" >&2
    exit 1
}

mkdir -p tmp
rm -f "$output_yaml"

# ucs-detect probes the terminal on stderr (session stream) and reads
# replies on stdin — neither may be redirected. The trailing marker file
# lets this script distinguish "ucs-detect finished" from "yetty died
# mid-run". TERM is pinned because yetty does not set it for its child,
# and an inherited launcher TERM (e.g. tmux-256color) distorts the
# terminal fingerprint.
done_marker=tmp/ucs-detect-done.marker
rm -f "$done_marker"

inner_command="TERM=xterm-256color uvx ucs-detect \
    --save-yaml '$output_yaml' \
    --set-software-name yetty \
    --set-software-version '$git_version' \
    $*; touch '$done_marker'"

echo "running ucs-detect inside $yetty_binary (version $git_version) ..."
echo "result yaml: $output_yaml"

# Keep yetty's own trace out of the picture unless the caller set it.
YTRACE_DEFAULT_ON=${YTRACE_DEFAULT_ON:-no} \
    "$yetty_binary" -e "bash -c \"$inner_command\"" >tmp/ucs-detect-yetty.log 2>&1

if [ ! -f "$done_marker" ]; then
    echo "error: yetty exited before ucs-detect completed" >&2
    echo "  yetty log:      tmp/ucs-detect-yetty.log" >&2
    echo "  ucs-detect log: tmp/ucs-detect-stderr.log" >&2
    exit 1
fi

if [ ! -s "$output_yaml" ]; then
    echo "error: ucs-detect finished but produced no YAML at $output_yaml" >&2
    echo "  ucs-detect log: tmp/ucs-detect-stderr.log" >&2
    exit 1
fi

echo "done: $output_yaml"
