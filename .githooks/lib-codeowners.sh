#!/usr/bin/env bash
# Helper sourced by pre-commit and pre-push.
# Resolves the CODEOWNERS file for the current repo and exposes:
#   $CODEOWNERS_FILE   — the resolved path (or empty)
#   codeowners_emails  — function: prints allowed email addresses, one per line
#   codeowners_allows  — function: returns 0 iff arg matches an allowed email
#
# CODEOWNERS tokens can be GitHub @handles, @org/team refs, or emails. Only
# email-shaped tokens (contain "@", do not start with "@") are used here —
# we cannot resolve a GitHub @handle to an email at git-hook time.

CODEOWNERS_FILE=""
_root=$(git rev-parse --show-toplevel 2>/dev/null) || return 1
for _p in "$_root/.github/CODEOWNERS" "$_root/CODEOWNERS" "$_root/docs/CODEOWNERS"; do
  if [ -f "$_p" ]; then CODEOWNERS_FILE="$_p"; break; fi
done

codeowners_emails() {
  [ -n "$CODEOWNERS_FILE" ] || return 0
  awk '
    /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
    {
      for (i = 2; i <= NF; i++)
        if ($i ~ /@/ && $i !~ /^@/) print $i
    }
  ' "$CODEOWNERS_FILE" | sort -u
}

codeowners_allows() {
  local needle=$1
  [ -n "$needle" ] || return 1
  codeowners_emails | grep -qxF "$needle"
}
