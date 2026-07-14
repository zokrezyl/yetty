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
result as `tmp/ucs-detect-<git-describe>.yaml`. A full run is slow (the
language test walks UDHR samples for ~500 languages); for a bounded run pass a
per-category time budget:

```sh
qa-tools/ucs-detect/run.sh ./build-desktop-ytrace-release/yetty \
    tmp/ucs-quick.yaml --limit-category-time 30
```

Any argument after the first two is passed to `ucs-detect` verbatim
(`--test-only wide`, `--limit-errors 100`, ...). `uvx` fetches ucs-detect on
demand; no local install is needed.

## Reading the YAML

Top-level keys of interest:

- `test_results.unicode_wide_version` — highest Unicode version whose wide
  chars measure correctly (the WIDE score).
- `test_results.language_results` — per-language UDHR success rates (LANG).
- `test_results.emoji_zwj` / `emoji_vs16` / `sri` / `ri` — sequence advance
  results (ZWJ, VS-16, flags).
- `terminal` — feature fingerprint (modes 2026/2027, kitty keyboard,
  XTGETTCAP, mouse, bracketed paste, ...).

## Baselines

Baseline YAMLs are recorded on the tracking epic (issue #567) whenever a
sub-issue of the epic lands. Always measure with the same invocation you are
comparing against — sampling limits change the absolute numbers.
