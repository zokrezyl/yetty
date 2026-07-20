#!/usr/bin/env bash
# Download the latest yetty release and unpack BOTH its binaries and its source
# tree into host directories that the demo container bind-mounts read-only at
# runtime. Nothing yetty-specific is baked into the image, so shipping a newer
# yetty needs only a re-run of this script — never an image rebuild.
#
#   binaries + data + config -> $YETTY_PREFIX_DIR   (mounted into /usr/local/…)
#   source tree              -> $YETTY_SOURCES_DIR  (mounted at /usr/share/yetty/sources)
#
# The repo publishes several release families (yetty-*, yos-web-*,
# yetty-rootfs-riscv-*) and GitHub's repo-wide "latest release" pointer belongs
# to whichever release published most recently — NOT necessarily a desktop
# yetty one. So "latest" is resolved exactly the way https://yetty.dev/install.sh
# does: list the releases and pick the highest yetty-X.Y.Z. Binaries and sources
# are pinned to the SAME resolved tag so they stay in lockstep.
#
# Usage:
#   ./update-yetty.sh                 # newest yetty-X.Y.Z
#   ./update-yetty.sh yetty-0.2.71    # a specific tag
#   YETTY_PREFIX_DIR=/srv/yetty ./update-yetty.sh
#
# Environment overrides:
#   YETTY_PREFIX_DIR   binaries/data/config prefix (default /var/lib/yetty-demo/prefix).
#                      Must match YETTY_DEMO_PREFIX in yetty-demo-session.sh.
#   YETTY_SOURCES_DIR  source tree dir (default /var/lib/yetty-demo/sources).
#                      Must match YETTY_DEMO_SOURCES in yetty-demo-session.sh.
#   YETTY_REPO         owner/repo to download from (default zokrezyl/yetty).
#   YETTY_VERSION      release tag (default: newest yetty-X.Y.Z). A positional
#                      argument, if given, wins over this.

set -euo pipefail

readonly repo="${YETTY_REPO:-zokrezyl/yetty}"
readonly prefix_dir="${YETTY_PREFIX_DIR:-/var/lib/yetty-demo/prefix}"
readonly sources_dir="${YETTY_SOURCES_DIR:-/var/lib/yetty-demo/sources}"
requested="${1:-${YETTY_VERSION:-latest}}"

log() { printf 'yetty-update: %s\n' "$1" >&2; }
die() { printf 'yetty-update: error: %s\n' "$1" >&2; exit 1; }

command -v tar >/dev/null 2>&1 || die "need tar on PATH"
command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 \
    || die "need curl or wget on PATH"

# Fetch a URL to stdout quietly (small API metadata, not a payload).
fetch_text() {
    local url="$1"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --max-time 30 "$url"
    else
        wget -qO- --timeout=30 "$url"
    fi
}

# Stream a URL to a file, failing loudly on HTTP errors. Quiet so cron /
# systemd-timer logs stay clean; this is a maintenance script, not an installer.
download() {
    local url="$1" out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$out" "$url"
    else
        wget -q -O "$out" "$url"
    fi
}

# Resolve the newest desktop release tag (yetty-X.Y.Z) — the exact selection
# used by the public install.sh. Prints the full tag or fails (prints nothing).
resolve_latest_tag() {
    local releases_json best_version
    releases_json="$(fetch_text "https://api.github.com/repos/${repo}/releases?per_page=100")" || return 1
    best_version="$(printf '%s\n' "$releases_json" \
        | grep -o '"tag_name"[[:space:]]*:[[:space:]]*"yetty-[0-9][0-9.]*"' \
        | sed 's/.*"yetty-\([0-9][0-9.]*\)".*/\1/' \
        | sort -t . -k 1,1n -k 2,2n -k 3,3n \
        | tail -n 1 || true)"
    [ -n "$best_version" ] || return 1
    echo "yetty-${best_version}"
}

tag="$requested"
if [ "$tag" = "latest" ]; then
    tag="$(resolve_latest_tag)" \
        || die "cannot resolve the newest yetty-X.Y.Z tag (GitHub API unreachable or rate-limited)"
    log "newest desktop release is ${tag}"
fi

# Both trees record which tag they hold; skip the whole download when both match.
if [ "$(cat "${prefix_dir}/.yetty-tag" 2>/dev/null || true)" = "$tag" ] \
   && [ "$(cat "${sources_dir}/.yetty-sources-tag" 2>/dev/null || true)" = "$tag" ]; then
    log "binaries and sources already at ${tag}; nothing to do"
    exit 0
fi

# Stage new trees as siblings of the targets so the final swap is a same
# filesystem rename (atomic and instant), not a slow cross-filesystem copy.
mkdir -p "$(dirname "$prefix_dir")" "$(dirname "$sources_dir")"
prefix_new="${prefix_dir}.new.$$"
prefix_old="${prefix_dir}.old.$$"
sources_new="${sources_dir}.new.$$"
sources_old="${sources_dir}.old.$$"
# Scratch space MUST sit on the same (large) filesystem as the install prefix,
# NOT the system /tmp: on many hosts /tmp is a small tmpfs that cannot hold the
# ~1.3 GB installer payload (yinstall alone is that big once unpacked). The
# canonical install.sh honours TMPDIR, so export it here and its download +
# unpack land on the big filesystem too.
workdir="$(mktemp -d "$(dirname "$prefix_dir")/.yetty-update.XXXXXX")"
export TMPDIR="$workdir"
cleanup() {
    rm -rf "$workdir" "$prefix_new" "$prefix_old" "$sources_new" "$sources_old" 2>/dev/null || true
}
trap cleanup EXIT

# --- 1) binaries/data/config, via the canonical installer, pinned to $tag ---
# The public install.sh downloads the desktop asset for this OS/arch and runs
# yinstall, which lays the product down at the XDG paths we point it at. Same
# invocation the image build used to do — just aimed at a host staging dir.
log "installing binaries (${tag}) into ${prefix_dir}"
mkdir -p "$prefix_new"
curl -fsSL https://yetty.dev/install.sh \
    | XDG_BIN_HOME="${prefix_new}/bin" \
      XDG_DATA_HOME="${prefix_new}/share" \
      XDG_CONFIG_HOME="${prefix_new}/etc/xdg" \
      YETTY_VERSION="${tag}" bash

# Drop the RISC-V VM runtime (~470 MB, irrelevant to the CLI demos) and make the
# demo scripts runnable, mirroring what the old Dockerfile did after yinstall.
rm -rf "${prefix_new}/share/yetty/yemu" "${prefix_new}/share/yetty/qemu"
if [ -d "${prefix_new}/share/yetty/demos" ]; then
    find "${prefix_new}/share/yetty/demos" -type f -name '*.sh' -exec chmod 0755 {} + 2>/dev/null || true
fi
[ -x "${prefix_new}/bin/ycat" ] && [ -x "${prefix_new}/bin/yplot" ] \
    || die "installer did not produce ycat/yplot under ${prefix_new}/bin"
printf '%s\n' "$tag" > "${prefix_new}/.yetty-tag"
# The container runs as an unprivileged user (uid 1000); make the whole tree
# world readable + traversable (and keep exec bits) so it can use the mounts.
chmod -R a+rX "$prefix_new"

# --- 2) source tree, from the matching tag archive ---
log "unpacking source tree (${tag}) into ${sources_dir}"
mkdir -p "$sources_new"
archive="${workdir}/${tag}.src.tar.gz"
download "https://github.com/${repo}/archive/refs/tags/${tag}.tar.gz" "$archive"
# The tag archive unpacks under a "yetty-${tag}" top dir; strip it.
tar -xzf "$archive" -C "$sources_new" --strip-components=1
test -f "${sources_new}/CMakeLists.txt" || die "unexpected source archive layout (no top CMakeLists.txt)"
printf '%s\n' "$tag" > "${sources_new}/.yetty-sources-tag"
chmod -R a+rX "$sources_new"

# --- 3) swap both into place (same-filesystem renames) ---
# Sessions that already mounted the old trees keep their view via the bind mount
# until they exit; new sessions pick up the swapped-in trees.
[ -e "$prefix_dir" ] && mv "$prefix_dir" "$prefix_old"
mv "$prefix_new" "$prefix_dir"
[ -e "$sources_dir" ] && mv "$sources_dir" "$sources_old"
mv "$sources_new" "$sources_dir"
rm -rf "$prefix_old" "$sources_old" 2>/dev/null || true

log "yetty updated to ${tag}: binaries in ${prefix_dir}, sources in ${sources_dir}"
