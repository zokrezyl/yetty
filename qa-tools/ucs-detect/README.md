# ucs-detect harness

[ucs-detect](https://ucs-detect.readthedocs.io/) scores a terminal's Unicode
conformance by printing test characters and reading the cursor position back
(CPR, `DSR 6`). It must run *inside* the yetty instance under test.

## Run

```sh
make build-desktop-ytrace-release          # or any desktop build
qa-tools/ucs-detect/run.sh
```

This launches yetty, runs `ucs-detect` as its child via `-e`, and stores the
result as `tmp/ucs-detect-<git-describe>.yaml`. The default full run is the
canonical measurement and completes in a few seconds — no sampling flags are
needed. This is the invocation every sub-issue of the epic re-measures against.

For a narrower run, pass ucs-detect flags after the two positional arguments:

```sh
qa-tools/ucs-detect/run.sh ./build-desktop-ytrace-release/yetty \
    tmp/ucs-wide.yaml --test-only wide
```

Any argument after the first two is forwarded to `ucs-detect` verbatim
(`--test-only lang`, `--limit-graphemes 25`, `--limit-codepoints 800`, ...).
`uvx` fetches ucs-detect on demand; no local install is needed.

### How the runner behaves

- ucs-detect probes the terminal on its `--stream` fd (stderr by default) and
  reads the replies on the same tty. That fd is the yetty PTY, so it is **not**
  redirected — do not add `2>file` around the inner command or the probe reads
  "Not a terminal".
- yetty does not reliably exit when its `-e` child finishes (a shutdown-path
  stall after the child closes the PTY). The runner therefore launches yetty in
  the background, waits for a `tmp/ucs-detect-done.marker` that ucs-detect
  touches on completion, and then reaps the **specific** yetty PID it started.
  The measurement time is unaffected by the stall.
- `UCS_DETECT_TIMEOUT` (seconds, default 600) caps how long the runner waits
  for the marker before giving up and killing its yetty instance.

## Reading the YAML

The scores live under `test_results`. Each dimension is a map keyed by Unicode
version; each version carries `n_total`, `n_errors`, `pct_success`, and a list
of the failing samples.

- `test_results.unicode_wide_results` — wide-character widths (the WIDE score).
- `test_results.narrow_results` — narrow-character widths.
- `test_results.emoji_zwj_results` — ZWJ emoji sequences (should advance 2).
- `test_results.emoji_vs16_results` / `emoji_vs15_results` — variation-selector
  presentation width (VS-16 emoji, VS-15 text).
- `test_results.ri_results` / `sri_results` — regional-indicator flag pairs and
  single regional indicators.
- `test_results.sfz_results` — skin-tone / ZWJ family sequences.
- `test_results.language_results` — per-language UDHR grapheme success rates
  (the LANG score).
- `terminal_results` — the feature fingerprint: `modes` (2026 synchronized
  output, 2027 grapheme clustering, 2004 bracketed paste, 1006 SGR mouse, ...),
  `decrqss`, `sixel`, `osc52_clipboard`, `kitty_*`, and the
  `xtgettcap-bad-screenleak` / `xtversion-bad-screenleak` correctness flags.

A quick headline summary can be produced with pyyaml:

```sh
uvx --with pyyaml python - tmp/ucs-detect-*.yaml <<'PY'
import sys, yaml
d = yaml.safe_load(open(sys.argv[1])); tr = d['test_results']
def agg(b):
    t = sum(v.get('n_total',0) for v in (b or {}).values())
    e = sum(v.get('n_errors',0) for v in (b or {}).values())
    return t, e, (100.0*(t-e)/t if t else None)
for k in ['unicode_wide_results','emoji_zwj_results','emoji_vs16_results',
          'emoji_vs15_results','ri_results','sri_results','sfz_results']:
    t,e,p = agg(tr.get(k)); print(f"{k:<24} n={t:<5} err={e:<5} pct={p}")
PY
```

## Baselines

The recorded baseline (per-dimension scores + the feature fingerprint) lives as
a comment on the tracking epic, issue #567; refresh it there whenever a
sub-issue lands. Always compare against a run made with the **same invocation**
— sampling flags change the absolute counts.
