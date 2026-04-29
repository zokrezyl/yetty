#!/usr/bin/env bash
#
# rerun-failed-jobs.sh — re-trigger failed jobs across all
# build-3rdparty-* workflow runs.
#
# Use case
#   After a tag-push fan-out (`push-3rdparty-tag.sh -f <lib>` × N), some
#   matrix legs flake on transient causes — github cache 5xx, nix store
#   path fetch hiccups, upstream tarball download timeouts. `gh run
#   rerun --failed <id>` only re-runs the FAILED jobs of a single
#   workflow run; this script does the same across every recent
#   build-3rdparty-* run, so one command rescues the whole batch.
#
#   `--failed` is a no-op if a run has no failed jobs, so blanket reruns
#   are safe — runs that are still in_progress / queued / fully green
#   are skipped silently by the gh CLI.
#
# Default behavior
#   * Inspect all `Build 3rdparty — <lib>` workflow runs created within
#     the lookback window (default: last 24h).
#   * For every run with conclusion == "failure", call
#     `gh run rerun <id> --failed`.
#   * Skip in_progress / queued / success runs.
#
# Notes
#   * `gh run rerun --failed` reuses the workflow YAML committed at the
#     run's original SHA, NOT current HEAD — so this script is the
#     right tool for transient flakes, NOT for picking up new workflow
#     edits. For workflow changes, push a new tag.
#   * Reruns are billed against the same workflow run, not a new one.
#
# Usage
#   build-tools/3rdparty/rerun-failed-jobs.sh                 # all libs, last 24h
#   build-tools/3rdparty/rerun-failed-jobs.sh --lib brotli    # one lib
#   build-tools/3rdparty/rerun-failed-jobs.sh --since 2h      # custom lookback
#   build-tools/3rdparty/rerun-failed-jobs.sh --since 2026-04-28T15:00:00Z
#   build-tools/3rdparty/rerun-failed-jobs.sh --dry-run       # report only
#   build-tools/3rdparty/rerun-failed-jobs.sh --repo OWNER/REPO
#
# Options
#   --lib NAME        Only consider runs for this single library.
#   --since SPEC      Lookback cutoff: an RFC3339 timestamp, or a
#                     relative duration like 30m / 2h / 1d (default 24h).
#   --repo OWNER/REPO Target repository (default: zokrezyl/yetty).
#   --limit N         How many recent runs to scan (default 100).
#   --dry-run         Print the list, do not call gh run rerun.
#   -h, --help        Show this help.
#
# Requirements: gh (authenticated), jq, date.

set -euo pipefail

YETTY_REPO="${YETTY_REPO:-zokrezyl/yetty}"
LOOKBACK="24h"
LIB=""
LIMIT=100
DRY_RUN=0

usage() { sed -n '2,/^$/p' "$0" | sed 's/^# \?//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --lib)     LIB="$2"; shift 2 ;;
        --since)   LOOKBACK="$2"; shift 2 ;;
        --repo)    YETTY_REPO="$2"; shift 2 ;;
        --limit)   LIMIT="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

#-----------------------------------------------------------------------------
# Resolve the lookback cutoff into an RFC3339 timestamp the jq filter can
# string-compare against `createdAt`. Accepts:
#   - bare RFC3339 (contains a colon-T-Z shape)            → use as-is
#   - relative duration: 30m, 2h, 1d                       → date arithmetic
#-----------------------------------------------------------------------------
to_cutoff() {
    local spec="$1"
    if [[ "$spec" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T ]]; then
        printf '%s' "$spec"
        return
    fi
    if [[ "$spec" =~ ^([0-9]+)([mhd])$ ]]; then
        local n="${BASH_REMATCH[1]}" unit="${BASH_REMATCH[2]}"
        case "$unit" in
            m) date -u -d "$n minutes ago" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
               || date -u -v-"$n"M +%Y-%m-%dT%H:%M:%SZ ;;
            h) date -u -d "$n hours ago"   +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
               || date -u -v-"$n"H +%Y-%m-%dT%H:%M:%SZ ;;
            d) date -u -d "$n days ago"    +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
               || date -u -v-"$n"d +%Y-%m-%dT%H:%M:%SZ ;;
        esac
        return
    fi
    echo "invalid --since: $spec (use RFC3339 or 30m/2h/1d)" >&2
    exit 2
}

CUTOFF="$(to_cutoff "$LOOKBACK")"

#-----------------------------------------------------------------------------
# Pull recent runs and select failed Build-3rdparty ones inside window.
# We filter on the *workflow run* conclusion, not the per-job state, because
# `gh run rerun --failed` operates at the run level and is a no-op if the
# run is still going. Including in_progress runs would just spam noise.
#-----------------------------------------------------------------------------
filter_jq=$(cat <<'JQ'
map(select(.name | startswith("Build 3rdparty —")))
| map(select(.createdAt > $cutoff))
| (if $lib != "" then map(select(.name == "Build 3rdparty — " + $lib)) else . end)
| map(select(.conclusion == "failure"))
| sort_by(.createdAt)
| .[]
| "\(.databaseId)\t\(.name | sub("Build 3rdparty — "; ""))\t\(.createdAt)"
JQ
)

mapfile -t HITS < <(
    gh run list --repo "$YETTY_REPO" --limit "$LIMIT" \
        --json databaseId,name,status,conclusion,createdAt 2>/dev/null \
      | jq -r --arg cutoff "$CUTOFF" --arg lib "$LIB" "$filter_jq"
)

if [ "${#HITS[@]}" -eq 0 ]; then
    echo "no failed Build 3rdparty runs since $CUTOFF (repo=$YETTY_REPO, lib='${LIB:-*}')"
    exit 0
fi

printf 'Failed runs since %s (repo=%s, lib=%s):\n' \
    "$CUTOFF" "$YETTY_REPO" "${LIB:-*}"
printf '%s\n' "${HITS[@]}" | column -t -s$'\t' -N "RUN ID,LIB,CREATED AT"
echo

# Extract the matrix-target slug from a job name. Job names look like
# "build-darwin-arm64 (tvos-arm64)" — we want the parenthesized half.
# For non-matrix jobs (e.g. "resolve") we fall back to the full name.
job_target() {
    local jname="$1" t
    t="$(printf '%s' "$jname" | sed -nE 's/.*\(([^)]+)\).*/\1/p')"
    printf '%s' "${t:-$jname}"
}

ok=0
fail=0
for line in "${HITS[@]}"; do
    rid=${line%%	*}
    rest=${line#*	}
    name=${rest%%	*}

    # Resolve which jobs were the failures for this run, so we can show
    # the user which targets a `--failed` rerun is going to re-attempt.
    failed_targets=$(gh run view "$rid" --repo "$YETTY_REPO" --json jobs 2>/dev/null \
        | jq -r '.jobs[]
                 | select(.conclusion == "failure")
                 | .name' 2>/dev/null \
        | while IFS= read -r jn; do
            [ -n "$jn" ] && printf '%s\n' "$(job_target "$jn")"
        done | paste -sd ' ' -)
    failed_targets="${failed_targets:-(unknown)}"

    if [ "$DRY_RUN" = 1 ]; then
        printf '[dry-run] would rerun: %-15s (run=%s) failed-targets: %s\n' \
            "$name" "$rid" "$failed_targets"
        ok=$((ok + 1))
        continue
    fi
    if gh run rerun "$rid" --repo "$YETTY_REPO" --failed >/dev/null 2>&1; then
        printf 'rerun OK:   %-15s (run=%s) targets: %s\n' \
            "$name" "$rid" "$failed_targets"
        ok=$((ok + 1))
    else
        printf 'rerun FAIL: %-15s (run=%s) targets: %s\n' \
            "$name" "$rid" "$failed_targets"
        fail=$((fail + 1))
    fi
done

echo
echo "summary: rerun-ok=$ok rerun-fail=$fail total=${#HITS[@]}"
exit 0
