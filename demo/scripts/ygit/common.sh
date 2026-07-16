# common.sh — shared setup for the ygit demos. Sourced, not run directly.
#
# Locates the ygit binary and the throwaway demo repository, and offers
# ensure_repo() to (re)build the repo on demand. The demo repo lives under a
# tmp dir so it never touches the yetty working tree.

DEMO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DEMO_DIR/../../.." && pwd)"

# Where the synthetic repo is built. Override with YGIT_DEMO_REPO=/path.
YGIT_DEMO_REPO="${YGIT_DEMO_REPO:-${TMPDIR:-/tmp}/ygit-demo-repo}"
export YGIT_DEMO_REPO

# Resolve the ygit binary: an explicit $YGIT wins, else the first desktop build.
resolve_ygit() {
    if [ -n "${YGIT:-}" ] && [ -x "${YGIT}" ]; then
        echo "${YGIT}"
        return 0
    fi
    for candidate in "$REPO_ROOT"/build-desktop-*/tools/ygit/ygit; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

YGIT_BIN="$(resolve_ygit)" || {
    echo "ygit binary not found. Build it first:" >&2
    echo "  make config-desktop-ytrace-release" >&2
    echo "  cmake --build build-desktop-ytrace-release --target ygit" >&2
    exit 1
}
export YGIT_BIN

# Build the demo repo if it is not already there.
ensure_repo() {
    if [ ! -d "$YGIT_DEMO_REPO/.git" ]; then
        bash "$DEMO_DIR/build-repo.sh"
    fi
}

# Print a section banner (bold cyan), colour only on a terminal.
section() {
    if [ -t 1 ]; then
        printf '\n\033[1;36m=== %s ===\033[0m\n\n' "$*"
    else
        printf '\n=== %s ===\n\n' "$*"
    fi
}

# Echo the (friendly) ygit command line in dim, then run the real binary.
ygit() {
    if [ -t 1 ]; then
        printf '\033[2m$ ygit %s\033[0m\n' "$*"
    else
        printf '$ ygit %s\n' "$*"
    fi
    "$YGIT_BIN" "$@"
}
