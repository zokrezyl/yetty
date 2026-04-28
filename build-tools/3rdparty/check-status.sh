#!/usr/bin/env bash
#
# check-status.sh — report the migration / publication status of every
# library under build-tools/3rdparty/.
#
# For each <lib> dir, the script reports:
#   - version file content
#   - .noarch flag
#   - presence of build.sh / _build.sh
#   - presence of .github/workflows/build-3rdparty-<lib>.yml
#   - matrix targets declared by that workflow
#   - presence of build-tools/cmake/<lib>.cmake or
#     build-tools/cmake/libs/<lib>.cmake stub that calls
#     yetty_3rdparty_fetch(<lib>)
#   - existence of the lib-<lib>-<ver> GitHub release
#   - per-target presence of the expected tarball asset on that release
#
# Tracks issue #70 (3rdparty: migrate remaining libs to pre-built tarballs).
#
# Requirements: bash, gh (authenticated), awk, sort.
# Defaults to repo zokrezyl/yetty; override with YETTY_REPO=owner/repo.
# Pass --no-remote to skip the gh release queries (offline mode).
#
# Exit code: 0 always. Use the table to drive decisions.

set -euo pipefail

#-----------------------------------------------------------------------------
# Resolve repo paths and options.
#-----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
THIRDPARTY_DIR="$REPO_ROOT/build-tools/3rdparty"
WORKFLOWS_DIR="$REPO_ROOT/.github/workflows"
CMAKE_DIR="$REPO_ROOT/build-tools/cmake"

YETTY_REPO="${YETTY_REPO:-zokrezyl/yetty}"
NO_REMOTE=0
ONLY=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [--no-remote] [--lib NAME] [--repo OWNER/REPO]

  --no-remote          Skip GitHub release queries (offline).
  --lib NAME           Only check this library.
  --repo OWNER/REPO    Override target repo (default: $YETTY_REPO).
  -h, --help           Show this help.

Environment:
  YETTY_REPO           Same as --repo.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --no-remote) NO_REMOTE=1; shift ;;
        --lib) ONLY="$2"; shift 2 ;;
        --repo) YETTY_REPO="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

#-----------------------------------------------------------------------------
# Color helpers — only enabled on a terminal.
#-----------------------------------------------------------------------------
if [ -t 1 ]; then
    C_RESET=$'\033[0m'
    C_BOLD=$'\033[1m'
    C_DIM=$'\033[2m'
    C_RED=$'\033[31m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_CYAN=$'\033[36m'
else
    C_RESET=""; C_BOLD=""; C_DIM=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_CYAN=""
fi

ok()   { printf "%s%s%s" "$C_GREEN"  "$1" "$C_RESET"; }
miss() { printf "%s%s%s" "$C_RED"    "$1" "$C_RESET"; }
warn() { printf "%s%s%s" "$C_YELLOW" "$1" "$C_RESET"; }
dim()  { printf "%s%s%s" "$C_DIM"    "$1" "$C_RESET"; }

#-----------------------------------------------------------------------------
# Workflow matrix parsing — returns the union of target slugs declared under
# any `target:` matrix key in the workflow file, in canonical order.
#
# We don't parse YAML proper. We just look for `- <slug>` bullets that follow
# a `target:` line and that match the known platform vocabulary. That keeps
# the script dependency-free while still catching multi-block matrices.
#-----------------------------------------------------------------------------
KNOWN_PLATFORMS=(
    linux-x86_64
    linux-aarch64
    android-arm64-v8a
    android-x86_64
    webasm
    macos-arm64
    macos-x86_64
    ios-arm64
    ios-x86_64
    tvos-arm64
    tvos-x86_64
    windows-x86_64
)

extract_workflow_targets() {
    local wf="$1"
    [ -f "$wf" ] || return 0
    # Workflows declare matrix targets in three ways. Handle all three.
    #   1) target:\n      - foo\n      - bar
    #   2) target: [foo, bar, baz]
    #   3) matrix: { target: [foo, bar] }
    awk '
        {
            line = $0
            # Inline arrays — there may be more than one per line in the
            # `matrix: { target: [...] }` form, though we have not seen it.
            while (match(line, /target:[[:space:]]*\[[^]]*\]/)) {
                chunk = substr(line, RSTART, RLENGTH)
                sub(/^.*\[/, "", chunk)
                sub(/\].*$/, "", chunk)
                n = split(chunk, parts, /[[:space:],]+/)
                for (i = 1; i <= n; i++) if (parts[i] != "") print parts[i]
                line = substr(line, RSTART + RLENGTH)
            }
        }
        # Multi-line block form.
        /^[[:space:]]+target:[[:space:]]*$/ { in_t = 1; next }
        in_t {
            if ($0 ~ /^[[:space:]]+-[[:space:]]+[A-Za-z0-9._-]+[[:space:]]*$/) {
                gsub(/^[[:space:]]+-[[:space:]]+/, "")
                gsub(/[[:space:]]+$/, "")
                print
                next
            }
            if ($0 ~ /^[[:space:]]*$/) next
            in_t = 0
        }
    ' "$wf" | sort -u
}

# Find the cmake stub for a lib, if any. The stub may live as
# build-tools/cmake/<Lib>.cmake or build-tools/cmake/libs/<lib>.cmake.
find_cmake_stub() {
    local lib="$1"
    local matches
    matches=$(grep -lE "yetty_3rdparty_fetch\\(\\s*${lib}\\b" \
        "$CMAKE_DIR"/*.cmake "$CMAKE_DIR"/libs/*.cmake 2>/dev/null | head -1 || true)
    if [ -n "$matches" ]; then
        printf "%s" "${matches#"$REPO_ROOT/"}"
    fi
}

# Cached release-asset listing per tag: avoids re-querying gh for the same
# release if multiple checks need it.
declare -A RELEASE_ASSETS_CACHE
declare -A RELEASE_EXISTS_CACHE

fetch_release_assets() {
    local tag="$1"
    if [ "$NO_REMOTE" = 1 ]; then
        RELEASE_EXISTS_CACHE[$tag]="skip"
        RELEASE_ASSETS_CACHE[$tag]=""
        return 0
    fi
    if [ -n "${RELEASE_EXISTS_CACHE[$tag]:-}" ]; then
        return 0
    fi
    local out
    if out=$(gh release view "$tag" --repo "$YETTY_REPO" \
            --json assets --jq '.assets[].name' 2>/dev/null); then
        RELEASE_EXISTS_CACHE[$tag]="yes"
        RELEASE_ASSETS_CACHE[$tag]="$out"
    else
        RELEASE_EXISTS_CACHE[$tag]="no"
        RELEASE_ASSETS_CACHE[$tag]=""
    fi
}

#-----------------------------------------------------------------------------
# Per-lib check.
#-----------------------------------------------------------------------------

# Counters for the trailing summary.
TOTAL=0
COMPLETE=0
PARTIAL=0
NO_RELEASE=0
NO_WORKFLOW=0
NO_STUB=0

print_lib_status() {
    local lib="$1"
    local lib_dir="$THIRDPARTY_DIR/$lib"

    TOTAL=$((TOTAL + 1))

    local version=""
    if [ -f "$lib_dir/version" ]; then
        version="$(tr -d '[:space:]' < "$lib_dir/version")"
    fi

    local noarch=0
    [ -f "$lib_dir/.noarch" ] && noarch=1

    local has_build=0 has_under=0
    [ -f "$lib_dir/build.sh"  ] && has_build=1
    [ -f "$lib_dir/_build.sh" ] && has_under=1

    local wf="$WORKFLOWS_DIR/build-3rdparty-$lib.yml"
    local has_wf=0
    [ -f "$wf" ] && has_wf=1

    local stub
    stub="$(find_cmake_stub "$lib")"

    local -a targets=()
    if [ "$noarch" = 1 ]; then
        targets=(noarch)
    elif [ "$has_wf" = 1 ]; then
        while IFS= read -r t; do
            [ -n "$t" ] && targets+=("$t")
        done < <(extract_workflow_targets "$wf")
    fi

    local tag=""
    [ -n "$version" ] && tag="lib-$lib-$version"

    if [ -n "$tag" ] && [ "$has_wf" = 1 ]; then
        fetch_release_assets "$tag"
    fi
    local release_state="${RELEASE_EXISTS_CACHE[$tag]:-na}"
    local assets="${RELEASE_ASSETS_CACHE[$tag]:-}"

    # Header line per lib.
    printf "%s%-22s%s  v=%-30s  " \
        "$C_BOLD" "$lib" "$C_RESET" "${version:-?}"

    local files_part=""
    if [ "$has_build" = 1 ] && [ "$has_under" = 1 ]; then
        files_part="$(ok files)"
    else
        files_part="$(miss files)"
    fi

    local wf_part
    if [ "$has_wf" = 1 ]; then
        wf_part="$(ok wf)"
    else
        wf_part="$(miss wf)"
        NO_WORKFLOW=$((NO_WORKFLOW + 1))
    fi

    local stub_part
    if [ -n "$stub" ]; then
        stub_part="$(ok stub)"
    else
        stub_part="$(warn nostub)"
        NO_STUB=$((NO_STUB + 1))
    fi

    local rel_part
    case "$release_state" in
        yes)  rel_part="$(ok release)" ;;
        no)   rel_part="$(miss release)" ;;
        skip) rel_part="$(dim "release?")" ;;
        *)    rel_part="$(dim "release?")" ;;
    esac

    local noarch_part=""
    [ "$noarch" = 1 ] && noarch_part=" $(dim '[noarch]')"

    printf "%s  %s  %s  %s%s\n" \
        "$files_part" "$wf_part" "$stub_part" "$rel_part" "$noarch_part"

    # Per-target asset breakdown.
    if [ "$release_state" = "skip" ]; then
        printf "    %s\n" "$(dim '(skipped: --no-remote)')"
    elif [ -z "$tag" ]; then
        printf "    %s\n" "$(miss 'no version file')"
    elif [ "$has_wf" = 0 ]; then
        printf "    %s\n" "$(miss 'no workflow')"
    elif [ ${#targets[@]} -eq 0 ]; then
        printf "    %s\n" "$(warn 'no targets parsed from workflow (no .noarch and no matrix)')"
    else
        local missing=0 present=0
        local line="    "
        for t in "${targets[@]}"; do
            local fname
            if [ "$t" = "noarch" ]; then
                fname="$lib-$version.tar.gz"
            else
                fname="$lib-$t-$version.tar.gz"
            fi
            if [ "$release_state" = "yes" ] \
                    && printf "%s\n" "$assets" | grep -qx "$fname"; then
                line="$line$(ok "$t") "
                present=$((present + 1))
            else
                line="$line$(miss "$t") "
                missing=$((missing + 1))
            fi
        done
        printf "%s\n" "$line"

        if [ "$release_state" != "yes" ]; then
            NO_RELEASE=$((NO_RELEASE + 1))
        elif [ "$missing" -eq 0 ]; then
            COMPLETE=$((COMPLETE + 1))
        else
            PARTIAL=$((PARTIAL + 1))
        fi

        # Surface any release assets we did not predict — a sign the
        # workflow matrix changed since the version file was bumped, or
        # that an old asset is hanging around.
        if [ "$release_state" = "yes" ]; then
            local stray=""
            while IFS= read -r a; do
                [ -n "$a" ] || continue
                local expected=0
                for t in "${targets[@]}"; do
                    local efname
                    if [ "$t" = "noarch" ]; then
                        efname="$lib-$version.tar.gz"
                    else
                        efname="$lib-$t-$version.tar.gz"
                    fi
                    if [ "$a" = "$efname" ]; then
                        expected=1
                        break
                    fi
                done
                if [ "$expected" = 0 ]; then
                    stray="$stray      stray: $a"$'\n'
                fi
            done <<< "$assets"
            if [ -n "$stray" ]; then
                printf "%s%s%s" "$C_YELLOW" "$stray" "$C_RESET"
            fi
        fi
    fi
}

#-----------------------------------------------------------------------------
# Main.
#-----------------------------------------------------------------------------
if ! command -v gh >/dev/null 2>&1 && [ "$NO_REMOTE" != 1 ]; then
    echo "warning: gh not found — falling back to --no-remote" >&2
    NO_REMOTE=1
fi

printf "%srepo:%s %s   %s3rdparty:%s %s\n\n" \
    "$C_BOLD" "$C_RESET" "$YETTY_REPO" \
    "$C_BOLD" "$C_RESET" "${THIRDPARTY_DIR#"$REPO_ROOT/"}"

mapfile -t LIBS < <(
    for d in "$THIRDPARTY_DIR"/*/; do
        [ -d "$d" ] || continue
        basename "$d"
    done | sort
)

for lib in "${LIBS[@]}"; do
    if [ -n "$ONLY" ] && [ "$lib" != "$ONLY" ]; then
        continue
    fi
    print_lib_status "$lib"
done

if [ -z "$ONLY" ]; then
    printf "\n%ssummary%s  total=%d  complete=%d  partial=%d  no-release=%d  no-workflow=%d  no-stub=%d\n" \
        "$C_BOLD" "$C_RESET" \
        "$TOTAL" "$COMPLETE" "$PARTIAL" "$NO_RELEASE" "$NO_WORKFLOW" "$NO_STUB"
fi
