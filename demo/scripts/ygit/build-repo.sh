#!/bin/bash
# build-repo.sh — construct a deliberately complex Git history in a tmp dir so
# the ygit demos have a rich DAG to show off: a main line, a develop line, two
# merged feature branches, a release merge, a hotfix merged back into both
# branches, an UNMERGED feature branch (a dangling lane), several tags, and a
# dirty working tree with staged + unstaged + untracked changes.
#
#   demo/scripts/ygit/build-repo.sh          # → $TMPDIR/ygit-demo-repo
#   YGIT_DEMO_REPO=/path build-repo.sh       # → custom location
#
# Safe to re-run: it wipes and rebuilds the target directory.

set -euo pipefail

REPO="${YGIT_DEMO_REPO:-${TMPDIR:-/tmp}/ygit-demo-repo}"

# Commit timestamps advance from a fixed epoch so the history looks like it was
# written over days, and the newest-first ordering is stable across runs.
BASE_EPOCH=1767258000 # 2026-01-01T09:00:00Z
STEP=0

# Two authors, alternating, so `ygit log` shows a collaborative history.
AUTHORS_NAME=("Ada Lovelace" "Alan Turing")
AUTHORS_MAIL=("ada@example.com" "alan@example.com")

git_commit() {
    local message="$1"
    STEP=$((STEP + 1))
    local when
    when="$(date -u -d "@$((BASE_EPOCH + STEP * 9000))" +"%Y-%m-%dT%H:%M:%S")"
    local slot=$((STEP % 2))
    GIT_AUTHOR_NAME="${AUTHORS_NAME[$slot]}" GIT_AUTHOR_EMAIL="${AUTHORS_MAIL[$slot]}" \
        GIT_COMMITTER_NAME="${AUTHORS_NAME[$slot]}" GIT_COMMITTER_EMAIL="${AUTHORS_MAIL[$slot]}" \
        GIT_AUTHOR_DATE="$when" GIT_COMMITTER_DATE="$when" \
        git -C "$REPO" commit -q -m "$message"
}

# Merge with a real merge commit (never fast-forward), same timestamp scheme.
git_merge() {
    local branch="$1" message="$2"
    STEP=$((STEP + 1))
    local when
    when="$(date -u -d "@$((BASE_EPOCH + STEP * 9000))" +"%Y-%m-%dT%H:%M:%S")"
    local slot=$((STEP % 2))
    GIT_AUTHOR_NAME="${AUTHORS_NAME[$slot]}" GIT_AUTHOR_EMAIL="${AUTHORS_MAIL[$slot]}" \
        GIT_COMMITTER_NAME="${AUTHORS_NAME[$slot]}" GIT_COMMITTER_EMAIL="${AUTHORS_MAIL[$slot]}" \
        GIT_AUTHOR_DATE="$when" GIT_COMMITTER_DATE="$when" \
        git -C "$REPO" merge -q --no-ff -m "$message" "$branch"
}

echo "==> building demo repo at $REPO"
rm -rf "$REPO"
mkdir -p "$REPO"
git -C "$REPO" init -q -b main
git -C "$REPO" config commit.gpgsign false
git -C "$REPO" config tag.gpgsign false

# --- main: project skeleton ---------------------------------------------
mkdir -p "$REPO/src"
cat > "$REPO/README.md" <<'EOF'
# webwidget

A small demo project used to show off ygit's history visualisation.
EOF
cat > "$REPO/src/app.c" <<'EOF'
#include <stdio.h>

int main(void)
{
    printf("webwidget starting\n");
    return 0;
}
EOF
# A visual asset committed to history — the first logo. It is redesigned later,
# so `ygit view v0.1.0:assets/logo.svg` and `ygit view HEAD:assets/logo.svg`
# render two different graphics straight from git.
mkdir -p "$REPO/assets"
cat > "$REPO/assets/logo.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="200" viewBox="0 0 360 200">
  <rect width="360" height="200" rx="18" fill="#0B1014"/>
  <circle cx="90" cy="100" r="56" fill="#6BA892"/>
  <circle cx="90" cy="100" r="28" fill="#0B1014"/>
  <rect x="150" y="64" width="170" height="72" rx="12" fill="#74C5A5"/>
  <text x="166" y="176" font-family="sans-serif" font-size="24" fill="#E0E5E4">webwidget v1</text>
</svg>
EOF
git -C "$REPO" add -A
git_commit "Initial project skeleton"

cat >> "$REPO/src/app.c" <<'EOF'

/* TODO: parse command-line options */
EOF
git -C "$REPO" add -A
git_commit "app: note the options TODO"
git -C "$REPO" tag -a v0.1.0 -m "First tagged prototype"

# --- develop line branches off the tagged prototype ----------------------
git -C "$REPO" checkout -q -b develop
cat > "$REPO/CONTRIBUTING.md" <<'EOF'
# Contributing

Branch off develop, open a PR, keep commits focused.
EOF
mkdir -p "$REPO/docs"
cat > "$REPO/docs/guide.md" <<'EOF'
# webwidget guide

**webwidget** is a small demo project, grown one feature branch at a time.
It has three parts:

- `login` — authenticate a user
- `search` — find things in a haystack
- `export` — write results out as CSV

## Building

Nothing to build yet — see `ygit log` for how it came together.

> Tip: `ygit view HEAD:docs/guide.md` *renders* this file inside yetty;
> `git show HEAD:docs/guide.md` just prints the Markdown source.
EOF
git -C "$REPO" add -A
git_commit "docs: add contributing + guide"

# --- feature/login: merged into develop ---------------------------------
git -C "$REPO" checkout -q -b feature/login develop
cat > "$REPO/src/login.c" <<'EOF'
#include "login.h"

int login(const char *user, const char *password)
{
    (void)password;
    return user != NULL;
}
EOF
git -C "$REPO" add -A
git_commit "login: skeleton authenticator"
cat > "$REPO/src/login.h" <<'EOF'
#ifndef LOGIN_H
#define LOGIN_H
int login(const char *user, const char *password);
#endif
EOF
git -C "$REPO" add -A
git_commit "login: public header"
git -C "$REPO" checkout -q develop
git_merge feature/login "Merge feature/login into develop"

# --- feature/search: merged into develop --------------------------------
git -C "$REPO" checkout -q -b feature/search develop
cat > "$REPO/src/search.c" <<'EOF'
#include <string.h>

int search(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}
EOF
git -C "$REPO" add -A
git_commit "search: substring matcher"
cat >> "$REPO/src/search.c" <<'EOF'

/* case-insensitive variant to follow */
EOF
git -C "$REPO" add -A
git_commit "search: note case-insensitive follow-up"
git -C "$REPO" checkout -q develop
git_merge feature/search "Merge feature/search into develop"

# --- release: merge develop into main, tag v1.0.0 -----------------------
git -C "$REPO" checkout -q main
git_merge develop "Release 1.0.0: merge develop"
git -C "$REPO" tag -a v1.0.0 -m "First public release"

# --- hotfix off the release, merged back into main and develop ----------
git -C "$REPO" checkout -q -b hotfix/crash main
# Fix a crash by null-checking in app.c.
cat > "$REPO/src/app.c" <<'EOF'
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 1 || argv == NULL) {
        return 1;
    }
    printf("webwidget starting\n");
    return 0;
}
EOF
git -C "$REPO" add -A
git_commit "hotfix: guard against null argv"
git -C "$REPO" checkout -q main
git_merge hotfix/crash "Merge hotfix/crash into main"
git -C "$REPO" tag -a v1.0.1 -m "Crash hotfix"
# Carry the hotfix back into develop so the lines reconverge.
git -C "$REPO" checkout -q develop
git_merge main "Merge main (hotfix) back into develop"

# --- feature/export: left UNMERGED (a dangling lane) --------------------
git -C "$REPO" checkout -q -b feature/export develop
cat > "$REPO/src/export.c" <<'EOF'
#include <stdio.h>

int export_csv(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        return -1;
    }
    fclose(out);
    return 0;
}
EOF
git -C "$REPO" add -A
git_commit "export: CSV writer skeleton"
cat >> "$REPO/src/export.c" <<'EOF'

/* TODO: JSON export */
EOF
git -C "$REPO" add -A
git_commit "export: note the JSON follow-up"

# --- redesign the logo on main (so HEAD differs from v0.1.0) -------------
git -C "$REPO" checkout -q main
cat > "$REPO/assets/logo.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="200" viewBox="0 0 360 200">
  <rect width="360" height="200" rx="18" fill="#0B1014"/>
  <polygon points="180,26 322,110 180,194 38,110" fill="#5A8979"/>
  <polygon points="180,58 280,116 180,174 80,116" fill="#74C5A5"/>
  <circle cx="180" cy="116" r="24" fill="#0B1014"/>
  <text x="120" y="123" font-family="sans-serif" font-size="22" fill="#E0E5E4">webwidget</text>
</svg>
EOF
git -C "$REPO" add -A
git_commit "brand: refresh the logo for 1.0"

# --- land on main with a dirty working tree -----------------------------

# Staged change: extend the README (in the index).
cat >> "$REPO/README.md" <<'EOF'

## Status

1.0.1 shipped. See `ygit log` for the story.
EOF
git -C "$REPO" add README.md

# Unstaged change: tweak a tracked source file (worktree only).
cat >> "$REPO/src/app.c" <<'EOF'

/* release build: strip debug logging */
EOF

# Untracked file.
cat > "$REPO/NOTES.txt" <<'EOF'
Scratch notes — not yet tracked.
EOF

echo "==> demo repo ready:"
echo "    $REPO"
echo "    $(git -C "$REPO" rev-list --all --count) commits, \
$(git -C "$REPO" tag | wc -l | tr -d ' ') tags, \
$(git -C "$REPO" branch | wc -l | tr -d ' ') branches"
