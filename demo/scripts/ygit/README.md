# ygit demo — what git can't show you

These scripts build a deliberately gnarly Git repository in a tmp directory and
run [`ygit`](../../../tools/ygit/README.md) against it. Two headline scenes show
what git can't:

- **`view.sh`** — `ygit view <rev>:<path>` pulls a file's blob from history and
  renders it inline: an SVG is *drawn*, Markdown is *formatted*, source is
  *syntax-highlighted*. `git show HEAD:assets/logo.svg` prints XML; `ygit view`
  draws the logo. The repo redesigns its logo partway through history, so
  `v0.1.0` vs `HEAD` shows two different graphics straight from git.
- **`diff.sh`** — `ygit diff <rev>` renders a commit's diff. A changed **asset**
  is drawn as a visual **before / after** (the old logo above the new one, as
  figures — not XML); a changed **source** file is a unified diff with the code
  *syntax-highlighted by its own grammar* and the changed lines banded, past
  git's line-level red/green.

Run them inside yetty to see the figures:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/view.sh   # rendered from history
./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/diff.sh   # drawn before/after
./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/all.sh    # everything
```

The `graph` view is the other yetty-only piece — inside yetty the commit DAG is
**drawn as a GPU figure** (coloured lanes, circle nodes, ringed merges), not
`git log --graph` ASCII. `graph.sh` uses `--all`, so every branch — including the
unmerged `feature/export` lane — shows in one picture.

## What the synthetic repo contains

`build-repo.sh` constructs (under `$TMPDIR/ygit-demo-repo` by default):

- a **main** line and a **develop** line;
- two feature branches (`feature/login`, `feature/search`) **merged** into
  develop with real merge commits;
- a **release** merge of develop into main, tagged `v1.0.0`;
- a **hotfix** branch merged into main (`v1.0.1`) and then merged **back** into
  develop, so the lines reconverge;
- an **unmerged** `feature/export` branch — a lane that never lands;
- tags `v0.1.0`, `v1.0.0`, `v1.0.1`;
- a **dirty working tree**: a staged README edit, an unstaged `src/app.c` edit,
  and an untracked `NOTES.txt`.

## Running

Build `ygit` once (`cmake --build build-desktop-ytrace-release --target ygit`),
then either run the scripts in a plain shell or inside yetty:

```sh
demo/scripts/ygit/all.sh
# or, to render inside yetty:
./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/all.sh
```

The scripts locate the `ygit` binary automatically (override with `YGIT=/path`).

| script          | shows |
|-----------------|-------|
| **`view.sh`**   | **rendered files from history** — SVG drawn, Markdown formatted, source highlighted (the reason to switch) |
| **`diff.sh`**   | **the diff git can't draw** — assets as a before/after figure, source as a syntax-highlighted patch |
| `graph.sh`      | the full commit **DAG** drawn as a GPU figure (`--all`: every branch + the unmerged lane) |
| `all.sh`        | rebuild the repo, then run every scene below (`DEMO_PAUSE=<s>` between) |
| `build-repo.sh` | just (re)create the demo repo |
| `log.sh`        | commit list with tag/branch decoration, authors, dates |
| `status.sh`     | branch, upstream, and the staged/unstaged/untracked split |
| `branches.sh`   | local branches with tip hash + subject |
| `show.sh`       | a tag- and merge-commit inspection with per-file `+/-` counts |

## What to look for

- In **`graph`**, the merge commits fan a second lane out and back in, and
  `feature/export` sits in its own lane — the columns are computed by
  `src/yetty/ygit/commit-graph.c`, not by `git`.
- In **`status`**, the two porcelain columns (index vs. worktree) light up
  independently for the staged README, the unstaged source edit, and the
  untracked file.
- In **`show`**, a tag resolves to its commit and a merge commit's file list is
  its diff against the first parent.
