# Shared shell helpers for sudo-free riscv ext4 image production.
#
# Sourced by:
#   build-tools/yemu/yetty-rootfs-riscv/build.sh
#   build-tools/3rdparty/cmake-riscv64/_build.sh
#
# All helpers manipulate ext4 images entirely through e2fsprogs +
# debugfs — no losetup, no mount, no sudo. Callers must `set -Eeuo
# pipefail` themselves; these helpers rely on it.

# e2fsck returns 0=clean, 1=errors corrected, 2=errors corrected (reboot
# advised — irrelevant offline), >=4=real failure. Treat 0/1/2 as success.
e2fsck_ok() {
    local img="$1"
    local rc=0
    e2fsck -fy "$img" >/dev/null || rc=$?
    if [ "$rc" -gt 2 ]; then
        echo "e2fsck failed on $img (rc=$rc)" >&2
        return 1
    fi
    return 0
}

# inject_file IMG SRC_FILE DST_PATH [MODE]
#
# Write a single host file into the ext4 image at DST_PATH, owned by
# uid=0 gid=0 with the given mode (octal, default 755). DST_PATH must
# be absolute. No sudo / mount — pure debugfs.
inject_file() {
    local img="$1" src="$2" dst="$3" mode="${4:-755}"
    local script
    script="$(mktemp -t image-inject-XXXXXX.debugfs)"
    {
        printf 'write %s %s\n' "$src" "$dst"
        printf 'set_inode_field %s mode 0%o\n' "$dst" "$((0100000 + 8#$mode))"
        printf 'set_inode_field %s uid 0\n' "$dst"
        printf 'set_inode_field %s gid 0\n' "$dst"
    } > "$script"
    debugfs -w -f "$script" "$img"
    rm -f "$script"
}

# inject_tree IMG SRC_DIR DST_DIR
#
# Inject everything under SRC_DIR (host path) into IMG at DST_DIR (image
# absolute path). Directories created with host mode-bits (regular-dir
# bit set), files with regular+host mode bits, symlinks copied verbatim.
# All entries owned uid=0 gid=0 regardless of host owner. Pure debugfs.
#
# Whitespace in paths is rejected up-front — debugfs's command parser
# does not quote-handle reliably across versions.
inject_tree() {
    local img="$1" src="$2" dst="$3"

    # Strip trailing slash from dst so callers can pass "/" for "root" and
    # we still produce sane "/path" rather than "//path" entries. (Linux
    # tolerates double slashes; debugfs's command parser is less forgiving.)
    dst="${dst%/}"

    if ( cd "$src" && LC_ALL=C find . -mindepth 1 \( -name '* *' -o -name "*$(printf '\t')*" \) -print -quit ) | grep -q .; then
        echo "inject_tree: $src contains paths with whitespace — unsupported" >&2
        return 1
    fi

    local script log
    script="$(mktemp -t image-inject-XXXXXX.debugfs)"
    log="$(mktemp -t image-inject-XXXXXX.log)"

    {
        # Idempotent prefix-chain mkdir. "File exists" warnings from
        # already-present prefix dirs are filtered out of the log below.
        local prefix="" comp
        local oldifs="$IFS"
        IFS=/
        for comp in $dst; do
            [ -z "$comp" ] && continue
            prefix="$prefix/$comp"
            printf 'mkdir %s\n' "$prefix"
            printf 'set_inode_field %s uid 0\n' "$prefix"
            printf 'set_inode_field %s gid 0\n' "$prefix"
            printf 'set_inode_field %s mode 040755\n' "$prefix"
        done
        IFS="$oldifs"

        ( cd "$src" && LC_ALL=C find . -mindepth 1 -printf '%y\t%m\t%P\n' ) | \
        while IFS=$'\t' read -r kind mode rel; do
            local imgp="$dst/$rel"
            case "$kind" in
                d)
                    printf 'mkdir %s\n' "$imgp"
                    printf 'set_inode_field %s mode 0%o\n' "$imgp" "$((040000 + 8#$mode))"
                    ;;
                f)
                    # `unlink` before `write` so this works as an overlay
                    # over a pristine image — `write` errors out if the
                    # target already exists, and there's no overwrite
                    # flag. unlink on a missing path is harmless (prints
                    # a misleading "No free space in the directory"
                    # message, filtered below).
                    printf 'unlink %s\n' "$imgp"
                    printf 'write %s %s\n' "$src/$rel" "$imgp"
                    printf 'set_inode_field %s mode 0%o\n' "$imgp" "$((0100000 + 8#$mode))"
                    ;;
                l)
                    local target
                    target="$(readlink -- "$src/$rel")"
                    printf 'unlink %s\n' "$imgp"
                    printf 'symlink %s %s\n' "$imgp" "$target"
                    ;;
                *)
                    echo "inject_tree: unsupported entry type '$kind' at $rel" >&2
                    return 1
                    ;;
            esac
            printf 'set_inode_field %s uid 0\n' "$imgp"
            printf 'set_inode_field %s gid 0\n' "$imgp"
        done
    } > "$script"

    if ! debugfs -w -f "$script" "$img" >"$log" 2>&1; then
        echo "inject_tree: debugfs failed:" >&2
        cat "$log" >&2
        rm -f "$script" "$log"
        return 1
    fi
    # debugfs in `-f` mode prints a version banner, a prompt-echoed
    # command line per script line, `Allocated inode: N` after each
    # `write`, and benign "File exists" warnings for the idempotent
    # prefix-chain mkdirs. `unlink` before each `write`/`symlink` emits
    # a misleading "No free space in the directory" message when the
    # target didn't exist — also benign. Anything else is a real error.
    if grep -vE '^debugfs [0-9]|^debugfs: |^$|File exists while making directory|Ext2 directory already exists|^Allocated inode: [0-9]+$|^unlink_file_by_name: No free space in the directory *$' "$log" | grep -q .; then
        echo "inject_tree: unexpected debugfs output:" >&2
        cat "$log" >&2
        rm -f "$script" "$log"
        return 1
    fi
    rm -f "$script" "$log"
}

# fetch URL CACHED_PATH DESCR LOCK_NAME
#
# Download $URL to $CACHED_PATH if not already present. Concurrent
# invocations on the same CACHE_DIR are serialised via flock on
# $CACHE_DIR/.$LOCK_NAME.lock. CACHE_DIR must exist and be exported.
fetch() {
    local url="$1" cache="$2" descr="$3" lock="$4"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> downloading $descr"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.$lock.lock"
        rm -f "$part"
    fi
}

# verify_sha256 FILE EXPECTED_HEX
#
# Fail loudly if the file's sha256 doesn't match. Reproducibility tax.
verify_sha256() {
    local file="$1" expect="$2"
    local got
    got="$(sha256sum "$file" | awk '{print $1}')"
    if [ "$got" != "$expect" ]; then
        echo "sha256 mismatch on $file" >&2
        echo "  expect: $expect" >&2
        echo "  got:    $got" >&2
        return 1
    fi
}
