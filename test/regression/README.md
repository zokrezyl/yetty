# Regression reference corpus

Issue #454. Deterministic regression testing against an accepted reference
corpus stored as a GitHub Release asset — never in git history.

## What a scenario is

Each scenario runs `ycat` on a sample input from `demo/assets/`, captures
the OSC wire stream it emits (with `TERM_PROGRAM=yetty`, `LC_ALL=C`, and a
fixed `-w` width, so the output is byte-deterministic), and decodes it with
`decode-ydraw` into a readable text dump of every wire command and SDF
primitive. That decoded dump is the reference artifact: semantic, diffable,
and independent of GPU, display, fonts installed on the host, or driver
versions — so the same corpus is valid on GitHub-hosted runners, the
Woodpecker LAN runner, and developer machines.

Scenarios are defined in `scenarios.json`. The runner is `regression.py`
(Python 3, standard library only).

## Storage model

- **Canonical accepted references:** asset `yetty-regression-corpus-current.zip`
  (plus a `.sha256` sidecar) on the `regression-corpus` GitHub Release
  (a prerelease, so it never shows as the repo's "Latest release").
- **Backup:** exactly one `yetty-regression-corpus-previous-<date>-<commit>.zip`
  asset — the outgoing pack of the last promotion. Older backups are deleted
  at promotion time; nothing accumulates.
- **Normal CI evidence:** compact only — `summary.json` plus truncated
  unified diffs, uploaded as a short-retention workflow artifact. Never a
  full corpus.
- **Nothing under `tmp/regression/` is ever committed.**

The pack layout is `manifest.json` + `scenarios/<id>/decoded.txt`. The
manifest carries a promotion block (commit, reason, issue/PR, timestamp,
scenario filter) and **per-scenario** provenance — after a filtered
promotion different scenarios legitimately carry different commits/reasons.

Do **not** enable release immutability for this repo (or scope such a
ruleset to exclude the `regression-corpus` tag): promotion swaps assets on
an existing release, which immutability forbids.

## Where the check runs

| Lane | Trigger | What it does |
|---|---|---|
| `.woodpecker/linux.yml` step `regression` | every push/PR (LAN CI) | fetch corpus → `check` |
| `.github/workflows/regression.yml` | manual, daily cron, opt-in per push (`YETTY_REGRESSION_ON_PUSH` repo variable) | fetch corpus → build → `check` → evidence artifact on failure |
| `make test-regression` | local | same as above against the local build |

All consumers are read-only. If no corpus has been published yet (fetch
exit 3), the check is skipped with a notice. If the asset is unreachable
(fetch exit 4 — e.g. the short window while a promotion swaps it), the job
fails with a distinct "restart me later" message, never with a fake
regression failure; the built-in fetch retry (4 attempts, 20 s apart)
rides out most swap windows on its own.

## Statuses reported by `check`

| Status | Meaning | Fails the job? |
|---|---|---|
| `PASS` | decoded output matches the reference | no |
| `FAIL` | output differs — evidence diff written | yes |
| `INPUT-CHANGED` | the sample input changed since recording; review + promote | yes |
| `ERROR` | scenario would not execute (tool/input missing, decode error) | yes |
| `NEW` | scenario defined but not in the corpus yet; promote to admit | no |
| `STALE` | reference for a deleted scenario; next promotion drops it | no |

## Promotion — the only write path

`promote-regression-reference` (GitHub Actions, `workflow_dispatch`).
Inputs: `ref`, `scenario_filter`, `reason`, `issue_or_pr`, `dry_run`
(default **true**). The workflow: builds at `ref`, records every selected
scenario **twice** and requires byte-identical output, merges onto the
current pack (unfiltered scenarios keep their references and provenance),
validates (completeness, checksums, non-empty artifacts, metadata),
self-checks against the candidate pack, and only then — when not a dry
run — rotates the backup and swaps the release asset, finishing with a
round-trip fetch + check of what was actually published.

A reference may be promoted only for one of these reasons:

- `intentional-change` — behavior changed by design.
- `bugfix-new-correct-output` — a fix changed output and the new output is correct.
- `oracle-fix` — the old reference was wrong or too weak.
- `normalization-change` — normalization changed without semantic change.

Do **not** promote merely because CI failed once, the diff is small, the
old reference is inconvenient, or nobody wants to debug the failure.

## Local usage

```sh
make test-regression          # fetch the published corpus + check against it
make regression-record        # record a local corpus into tmp/regression/recorded

# check your working tree against a locally recorded corpus:
python3 test/regression/regression.py check \
    --corpus tmp/regression/recorded --evidence tmp/regression/evidence

# other subcommands: list, validate, merge, pack, fetch — see --help
```

## Adding a scenario

Add an entry to `scenarios.json` pointing at a small, committed sample
input (prefer `demo/assets/`). CI reports it as `NEW` (non-failing) until
a maintainer admits it via the promotion workflow. Inputs are
checksummed into the manifest: editing a sample input flips its scenario
to `INPUT-CHANGED` until re-promoted.
