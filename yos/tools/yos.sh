#!/usr/bin/env bash
# yos.sh — thin wrapper that execs the umbrella's yos with libexec on PATH.
#
# Everything you pass is forwarded to yos. Any leading `--*` flags are
# host flags consumed by yos itself; the first non-flag is the wasm
# program (bare libexec names like `zsh`/`nvim` are auto-expanded to
# their absolute umbrella path).
#
#   ./tools/yos.sh --help                            forwarded to yos
#   ./tools/yos.sh zsh                               interactive wasm zsh
#   ./tools/yos.sh zsh -c 'echo hi'                  one-shot zsh
#   ./tools/yos.sh nvim FILE                         libexec tool
#   ./tools/yos.sh --yctl-socket /tmp/yos.sock zsh   yctl daemon + zsh
#   ./tools/yos.sh --server [--log-dir DIR] [--daemon]
#                                                    runit supervisor mode
#
# There is no zsh-by-default — pass `zsh` explicitly if that's what
# you want. A bare `./tools/yos.sh` execs `yos` with no args, which
# prints yos's own usage line.
#
# Builds .#all on first run (nix path-info is read-only — won't build).
set -euo pipefail

cd "$(dirname "$0")/.."
REPO="$PWD"

ALL="$(nix path-info .#all 2>/dev/null || true)"
if [ -z "$ALL" ] || [ ! -e "$ALL" ]; then
    nix build .#all >/dev/null
    ALL="$(nix path-info .#all)"
fi

# ── collect every leading `--*` flag and pass through to yos ─────────
# We also notice --server / --daemon / --log-dir so the script can pick
# the runit default program when no explicit one is given. Everything
# else (--yctl-socket, anything added later) is passed through verbatim
# without the script needing to know about it.
HOST_FLAGS=()
SERVER=0
DAEMON=0
LOG_DIR=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --server)
            SERVER=1; HOST_FLAGS+=("$1"); shift ;;
        --daemon)
            DAEMON=1; SERVER=1; HOST_FLAGS+=("$1"); shift ;;
        --log-dir)
            LOG_DIR="$2"; HOST_FLAGS+=("$1" "$2"); shift 2 ;;
        --log-dir=*)
            LOG_DIR="${1#--log-dir=}"; HOST_FLAGS+=("$1"); shift ;;
        --)
            shift; break ;;
        --*=*)
            HOST_FLAGS+=("$1"); shift ;;
        --*)
            # Two-arg form like `--yctl-socket PATH`. We don't enforce
            # which flags do/don't take a value — yos's own parser is
            # authoritative. If the next token is also a `--*` flag,
            # treat the current one as a bare flag.
            if [ "$#" -ge 2 ] && [ "${2#-}" = "$2" ]; then
                HOST_FLAGS+=("$1" "$2"); shift 2
            else
                HOST_FLAGS+=("$1"); shift
            fi ;;
        *)
            break ;;
    esac
done

if [ "$SERVER" = 1 ]; then
    # Default log dir → repo/runtime/logs. Make sure the flag is in
    # HOST_FLAGS so the yos binary sees it — append if not already.
    [ -n "$LOG_DIR" ] || {
        LOG_DIR="$REPO/runtime/logs"
        HOST_FLAGS+=("--log-dir" "$LOG_DIR")
    }
    mkdir -p "$LOG_DIR"

    SVCDIR="$REPO/runtime/runit"
    if [ ! -d "$SVCDIR" ]; then
        echo "yos.sh --server: $SVCDIR does not exist" >&2
        exit 1
    fi

    # Default program in server mode is runsvdir on the repo's runit
    # service dir, unless the user supplied an explicit one.
    if [ "$#" -eq 0 ]; then
        set -- "$ALL/libexec/runsvdir" -P "$SVCDIR"
    fi
    exec env PATH="$ALL/libexec" \
             LOG_DIR="$LOG_DIR" \
             YOS_LIBEXEC="$ALL/libexec" \
             "$ALL/bin/yos" "${HOST_FLAGS[@]}" "$@"
fi

# ── non-server mode ──────────────────────────────────────────────────
# A bare libexec name (zsh, nvim, …) is rewritten to its absolute
# umbrella path so the wasm loader can find it. Anything starting with
# `/` or `.` passes through. With no program at all we still exec yos —
# yos's own argument parser then handles `--help`, prints usage for a
# bare invocation, etc. The script is a thin pass-through.
if [ "$#" -gt 0 ]; then
    PROG="$1"; shift
    case "$PROG" in
        /*|./*|../*) ;;
        *) [ -e "$ALL/libexec/$PROG" ] && PROG="$ALL/libexec/$PROG" ;;
    esac
    set -- "$PROG" "$@"
fi

exec env PATH="$ALL/libexec" \
         "$ALL/bin/yos" "${HOST_FLAGS[@]}" "$@"
