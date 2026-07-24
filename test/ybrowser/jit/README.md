# QuickJS baseline-JIT test + measurement harness

Design: `tmp/qjs-sljit-jit.md`. Engine: `src/quickjs/quickjs-jit.h`,
`quickjs-jit-compile.c` (SLJIT backend), `src/quickjs/sljit/` (vendored).

## Compiler tests (Stages 1–5)

- `jit-selftest.c` — ~75 cases (constants, control flow, locals/args,
  arithmetic, property/array/method/closure/constructor, exceptions),
  each run in interpreter / eager / baseline mode and checked against
  the expected result, so the JIT can never silently diverge. Includes
  the infinite-loop interruptibility check.
- `jit-stress.c` — differential (interpreter vs eager JIT must stringify
  identically over a program corpus) plus N create/eval/destroy cycles
  for lifetime/leak coverage.
- `run.sh [build-dir]` — builds and runs both, plain (linked against the
  built `libqjs.a`) and under ASan+UBSan (direct-source), each under a
  120s timeout so a hang/non-interruptible regression fails loudly.
  End-to-end differential: run the WPT suite with the JIT on —
  `YBROWSER_JS_JIT=eager python3 test/ybrowser/wpt/run.py` (must stay
  115/115). Enable the JIT at runtime with `YBROWSER_JS_JIT=baseline|eager`.

## Acceptance gates (experimental enablement)

| Gate | Status |
|---|---|
| No WPT/differential regression vs interpreter | met (eager + baseline 115/115) |
| No ASan/UBSan/leak failure under forced-eager stress | met (run.sh sanitized) |
| Unsupported functions run correctly interpreted (mixed tier) | met |
| Infinite loops remain interruptible | met (backedge safe-points) |
| Native code freed at fn/runtime destroy; counted vs limits | met (per-runtime + thread-wide caps) |
| Executable memory follows W^X | met (SLJIT PROT dual-map allocator) |
| Disable at build (`QJS_ENABLE_JIT=OFF`) and runtime (mode off) | met |

test262 is not wired into the yetty build; the QuickJS-NG upstream
test suites under `src/quickjs/tests/` remain runnable via the upstream
build for the forced-eager test262 gate.

---

# Stage 0 JIT-opportunity measurement harness

Support for Stage 0 of the QuickJS baseline-JIT plan (design:
`tmp/qjs-sljit-jit.md`; engine side: `src/quickjs/quickjs-jit.h`).

## Pieces

- `workloads.tsv` — the pre-registered gate workload set (six live pages;
  YouTube watch is the sole 15% priority exception) plus the three
  deterministic sampler-validation fixtures. FROZEN at first data
  collection; post-hoc edits require an explicit design decision.
- `analyze.py metrics DUMP.tsv [DUMP.tsv.child …] [--wall-seconds W]` —
  computes the registered metrics from the profiler dump(s):
  `eligible_hot_share`, `stage3_page_opportunity`,
  `stage4_load_page_opportunity`, `page_opportunity`, with Amdahl
  bounds, plus the separately reported native-callee share. Thresholds
  via `--n-call` / `--n-backedge`. The run FAILS (exit 2) when
  unclassified (UNKNOWN-category) eligible-hot time exceeds
  `--max-unknown-share` (registered tolerance: 2%).
- `analyze.py repeat D1 D2 …` — exact-counter repeatability check across
  runs of a deterministic fixture (calls/backedges must be identical;
  sample columns vary by design).

Sample categories (set per dispatched opcode against the stage-3
lowering table, refined by helper wrappers): dispatch, prop_load,
prop_write, call, string, vm, native, unknown. Only `dispatch` enters
the stage3 numerator; `dispatch + prop_load` the stage4 numerator;
everything except `native` the ceiling. The profile is thread-wide:
iframe child runtimes contribute, each dumping rows to
`<out>.<runtime>` side files at teardown — pass every file to
`metrics` (rows are drained on dump; nothing double-counts).

## Producing a dump

```sh
YBROWSER_JS_PROFILE=1 YBROWSER_JS_PROFILE_OUT=tmp/js-profile.tsv \
  ./build-desktop-ytrace-release/tools/ybrowser/ybrowser \
  --once --dump-boxes -w 1280 <url-or-file>
```

`YBROWSER_PROFILE=1` (the load-timeline profiler) also enables the JS
profiler; `YBROWSER_JS_PROFILE_HZ` overrides the 1 kHz default. One
profile per thread: iframe child runtimes are not sampled. The dump is
written at document teardown.

## Lifetime and bias tooling

- `lifetime-check.py` — runs the committed fixtures under `fixtures/`:
  tombstone preservation for functions freed before teardown
  (40 dynamic hot functions, exact counters), tombstone-vs-live metric
  equality, and the multi-runtime iframe teardown protocol including
  side-file discovery. Must PASS before campaign data is admissible.
- `bias-gate.py <url-or-file>` — the paired profiled/unprofiled
  overhead gate (interleaved reps, 2% p50 / 5% p95, 10k-sample floor).
  Run it per registered workload on a QUIET machine before the
  campaign. Known ceiling: a pathological pure-bytecode loop (JS ≈
  whole page) measures ~12% p50 overhead from the per-op category tap;
  representative pages sit far lower. If a registered workload fails,
  the redesign path is sampler-side attribution (handler maps the
  current pc to a pre-classified bytecode range, zero mutator cost) —
  not a lower timer rate.

## Gate (pre-registered)

Proceed beyond Stage 0 only if `stage4_load_page_opportunity` ≥ 10% at
the median of the gate set, or ≥ 15% for YouTube watch. Sampling bias
gate: paired profiled/unprofiled runs within 2% p50 / 5% p95, ≥ 10k JS
CPU samples per workload, bootstrapped 95% CI half-width ≤ 1pp.

A dump carrying `# incomplete 1` (tombstone lost to allocation failure)
is INVALID for any go/no-go purpose; `analyze.py` hard-fails on it.

## Campaign protocol (quiet machine, per frozen workload)

1. Interleaved paired profiled/unprofiled trials with the warmup
   policy; retain raw per-pair wall times.
2. Enforce ≤ 2% p50 / ≤ 5% p95 overhead and the ≥ 10k-sample floor.
   Loaded or noisy runs are INVALID — neither passes nor failures.
3. If any registered workload fails the bias gate under valid
   conditions, redesign mutator-side attribution (sampler-side
   pc→category is the committed fallback) and repeat the gate before
   collecting any opportunity data.
4. Only after every bias gate passes: collect opportunity samples,
   require the ≤ 1pp bootstrapped 95% CI half-width, then compute the
   frozen median/priority verdict.

Record with the results: the engine bytecode fingerprint, this
manifest's hash, profiler rate, raw timing logs, dump file hashes, and
the exact analyzer command — the gate must be reproducible.
