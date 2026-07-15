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
#       tmp/baseline.yaml --limit-codepoints 300 --limit-graphemes 25
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
output_dir=$(cd "$(dirname "$output_yaml")" && pwd)
output_yaml_abs=$output_dir/$(basename "$output_yaml")
rm -f "$output_yaml_abs"

# ucs-detect probes the terminal on its --stream fd (stderr by default) and
# reads replies on the same tty; that fd is the yetty PTY here and must not
# be redirected. A trailing marker file, touched only after ucs-detect
# returns, tells this script "measurement finished" independently of yetty's
# own exit. TERM is pinned because yetty does not set it for its child, and
# an inherited launcher TERM (e.g. tmux-256color) would distort the terminal
# fingerprint.
done_marker=$repo_root/tmp/ucs-detect-done.marker
yetty_log=$repo_root/tmp/ucs-detect-yetty.log
rm -f "$done_marker"

inner_command="TERM=xterm-256color uvx ucs-detect \
    --save-yaml '$output_yaml_abs' \
    --set-software-name yetty \
    --set-software-version '$git_version' \
    $*; touch '$done_marker'"

echo "running ucs-detect inside $yetty_binary (version $git_version) ..."
echo "result yaml: $output_yaml_abs"

# yetty is launched in the BACKGROUND and torn down by the specific PID we
# capture here — never by name. yetty exits on its own shortly after the -e
# child does, but the marker is the precise "measurement finished" signal
# (independent of teardown timing), and reaping our own PID keeps the harness
# robust if a future regression stalls that teardown again.
YTRACE_DEFAULT_ON=${YTRACE_DEFAULT_ON:-no} \
    "$yetty_binary" -e "bash -c \"$inner_command\"" >"$yetty_log" 2>&1 &
yetty_pid=$!

# Wall-clock ceiling for the measurement itself. Generous — a full unsampled
# language walk is minutes. Override with UCS_DETECT_TIMEOUT for large runs.
measure_timeout=${UCS_DETECT_TIMEOUT:-600}
waited=0
while [ ! -f "$done_marker" ]; do
    if ! kill -0 "$yetty_pid" 2>/dev/null; then
        echo "error: yetty exited before ucs-detect finished (no marker)" >&2
        echo "  yetty log: $yetty_log" >&2
        exit 1
    fi
    if [ "$waited" -ge "$measure_timeout" ]; then
        echo "error: ucs-detect did not finish within ${measure_timeout}s" >&2
        echo "  killing yetty pid $yetty_pid; yetty log: $yetty_log" >&2
        kill "$yetty_pid" 2>/dev/null || true
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

# Measurement done. Give yetty a brief grace to exit on its own, then reap
# the exact PID we started (the shutdown stall means it usually will not).
grace=0
while kill -0 "$yetty_pid" 2>/dev/null; do
    if [ "$grace" -ge 3 ]; then
        kill "$yetty_pid" 2>/dev/null || true
        break
    fi
    sleep 1
    grace=$((grace + 1))
done
wait "$yetty_pid" 2>/dev/null || true

if [ ! -s "$output_yaml_abs" ]; then
    echo "error: ucs-detect finished but produced no YAML at $output_yaml_abs" >&2
    echo "  yetty log: $yetty_log" >&2
    exit 1
fi

echo "done: $output_yaml_abs"
