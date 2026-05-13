#!/usr/bin/env bash
#
# Install the upstream Emscripten SDK into a user-local directory.
#
# Mirrors the official quick-start (https://emscripten.org/docs/getting_started/downloads.html):
#   git clone emsdk -> ./emsdk install <ver> -> ./emsdk activate <ver>
#
# Defaults:
#   EMSDK_DIR     = $HOME/.local/emsdk
#   EMSDK_VERSION = 4.0.23
#
# Why pin 4.0.23: matches the version `pkgs.emscripten` ships in our nix
# `.#web` shell (emcc 4.0.23-git + bundled clang 21.1.8) — the
# historically-validated combo. `latest` currently pulls emscripten 5.0.x
# with an unreleased clang 23 dev snapshot whose codegen breaks the
# produced wasm at runtime even when the build succeeds. Bump in lockstep
# with nixpkgs' emscripten.
#
# Usage:
#   build-tools/install-emscripten.sh
#   EMSDK_DIR=$HOME/emsdk EMSDK_VERSION=4.0.23 build-tools/install-emscripten.sh
#
# After install, source the env script in your shell rc:
#   source $EMSDK_DIR/emsdk_env.sh
#
# Or invoke the yetty webasm targets with EMSDK pointing at the install:
#   make EMSDK=$EMSDK_DIR build-webasm-ytrace-release

set -euo pipefail

EMSDK_DIR="${EMSDK_DIR:-$HOME/.local/emsdk}"
EMSDK_VERSION="${EMSDK_VERSION:-4.0.23}"
EMSDK_REPO="https://github.com/emscripten-core/emsdk.git"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()    { printf '%b[INFO]%b %s\n'    "$BLUE"   "$NC" "$*"; }
warn()    { printf '%b[WARN]%b %s\n'    "$YELLOW" "$NC" "$*"; }
error()   { printf '%b[ERROR]%b %s\n'   "$RED"    "$NC" "$*" >&2; }
success() { printf '%b[OK]%b %s\n'      "$GREEN"  "$NC" "$*"; }

check_prereqs() {
    local missing=()
    command -v git    >/dev/null 2>&1 || missing+=(git)
    command -v python3 >/dev/null 2>&1 || missing+=(python3)
    command -v curl   >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || missing+=("curl or wget")

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing prerequisites: ${missing[*]}"
        error "Install with:  sudo apt-get install -y ${missing[*]}"
        exit 1
    fi
}

clone_or_update() {
    mkdir -p "$(dirname "$EMSDK_DIR")"

    if [ -d "$EMSDK_DIR/.git" ]; then
        info "emsdk repo already at $EMSDK_DIR — fetching updates"
        git -C "$EMSDK_DIR" fetch --tags origin
        git -C "$EMSDK_DIR" pull --ff-only
    elif [ -e "$EMSDK_DIR" ]; then
        error "$EMSDK_DIR exists but is not a git checkout — refusing to overwrite"
        exit 1
    else
        info "Cloning emsdk into $EMSDK_DIR"
        git clone --depth 1 "$EMSDK_REPO" "$EMSDK_DIR"
    fi
}

install_and_activate() {
    info "Installing Emscripten ($EMSDK_VERSION) — this can take a few minutes"
    ( cd "$EMSDK_DIR" && ./emsdk install "$EMSDK_VERSION" )

    info "Activating Emscripten ($EMSDK_VERSION)"
    ( cd "$EMSDK_DIR" && ./emsdk activate "$EMSDK_VERSION" )
}

print_next_steps() {
    local emcc="$EMSDK_DIR/upstream/emscripten/emcc"
    if [ -x "$emcc" ]; then
        success "Installed: $("$emcc" --version | head -1)"
    else
        success "Installed at $EMSDK_DIR"
    fi

    cat <<EOF

Next steps
----------
  # Activate in your shell:
  source "$EMSDK_DIR/emsdk_env.sh"

  # Or invoke the yetty webasm targets directly:
  make EMSDK="$EMSDK_DIR" build-webasm-ytrace-release

EOF
}

main() {
    info "EMSDK_DIR      = $EMSDK_DIR"
    info "EMSDK_VERSION  = $EMSDK_VERSION"
    check_prereqs
    clone_or_update
    install_and_activate
    print_next_steps
}

main "$@"
