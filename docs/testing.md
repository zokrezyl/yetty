# Testing

This document is the single source of truth for how yetty is tested: the local
and CI commands, the test layers, the label policy, the shared harness, and the
rules for adding new tests.

## 1. Why this exists

yetty has real tests, but the test *system* is not yet coherent. The problems
this strategy fixes:

- **The Makefile test entrypoint is stale.** The `test-*` targets build a
  `yetty_tests` target and run `./<build>/test/ut/yetty_tests`
  (`Makefile:282-283` and siblings). Neither the target nor the binary exists;
  the real tests are individual executables registered with CTest.
- **CI does not run tests.** `.woodpecker/linux.yml` builds yetty but never runs
  the suite, so tests can silently break.
- **Tests are flag/platform dependent and not protected by the public
  Makefile/CI contract.** Linux config enables them
  (`build-tools/yetty/platform/linux/variables.cmake:4` forces
  `YETTY_ENABLE_FEATURE_TESTS ON`), but the default is `OFF`
  (`build-tools/yetty/platform/variables-defaults.cmake:142`), so other
  platforms build no tests, and CI never runs the ones Linux does build.
- **`assert()`-based tests are dead in Release.** `CMAKE_BUILD_TYPE=Release`
  (`Makefile:112`) implies `-DNDEBUG`, which compiles `assert()` out. The
  `test/ut/ygui/{object,layout,figure}-test.c` tests use only `assert()`, so in
  the recommended Release build they pass while checking nothing.
- **No shared harness and duplicated CMake.** Three assertion styles across the
  tree; per-directory `enable_testing()`, shim sources, include dirs, and link
  rules are copy-pasted.
- **Coverage is upside-down.** The most-reused, most-critical modules (`ycore`,
  `yclass`, `yconfig`, `ydraw-core`, `ywire`/`yface`, `yvterm`) have little or
  no coverage, while some downstream figure modules do.

The first goal is a reliable platform: one obvious command, one deterministic
fast lane, consistent labels and timeouts, no network in the default gate, no
stale aggregate target, no repeated boilerplate. Coverage then expands module by
module without re-introducing chaos.

## 2. Commands

Developer- and CI-facing targets:

| Command | Meaning |
|---|---|
| `make test-fast` | Build test targets if needed, run the deterministic gate: no GPU, no display, no network, no slow tests. This is the default. |
| `make test-all` | All local tests except live-network ones. |
| `make test-network` | Explicitly run `network`-labeled tests. |
| `make test-wpt` | The ybrowser WPT geometry suite. |
| `make test-asan` | The sanitizer lane. |

`make test-fast` is defined as build-if-needed plus:

```sh
ctest --test-dir build-desktop-ytrace-release --output-on-failure \
      -LE 'network|gpu|display|slow'
```

The ASAN lane additionally excludes `asan-skip`:

```sh
ctest --test-dir build-desktop-ytrace-asan --output-on-failure \
      -LE 'network|gpu|display|slow|asan-skip'
```

These replace the stale `yetty_tests` Makefile targets. A green `make test-fast`
under Release must be meaningful — which is why the harness never compiles out
(see §5).

## 3. Test layers

Every test belongs to exactly one layer.

- **L0 — plumbing / harness.** The harness header, the CMake helper, the
  Makefile `ctest` entrypoints, the CI test step, labels/timeouts. Not tests
  themselves; the foundation that makes the rest trustworthy.
- **L1 — pure logic.** No GPU, display, network, or event loop. `ycore`
  result/error helpers, `yclass` dispatch, `yconfig`, `ydraw-core` command
  builders/parsers, `ywire`/`yface` framing, `yvterm` parser pieces, `yrich`
  document/selection/YAML where pure.
- **L2 — headless component.** Real modules through in-process seams:
  `yfigure`/`ygrid`/`ygui` with a NULL context/framework, and terminal core via
  the memory-PTY seam (§7).
- **L3 — golden / oracle.** Deterministic serializable artifacts:
  drawable-list byte dumps, `yfigure`/`ygrid` YAML dumps, ybrowser box geometry,
  `ypdf` text positions, parser/IR dumps for chart/circuit/music/diagram/
  markdown/SVG, `yclass` generated-model snapshots. Screenshots are **not** the
  first assertion layer — cover the deterministic intermediate layer first.
- **L4 — render / E2E.** GPU render-diff / tile-diff, a launched `yetty`
  process, and `yctl`-driven smoke tests. Fewer, slower, higher value; nightly
  or manual.

## 4. Label policy

Labels drive selection and gating. The guiding rule: **label the exception, not
the norm**, so the gate stays correct-by-construction and the label set does not
rot.

- **Exactly one layer label per test:** `unit`, `contract`, `golden`,
  `integration`, `render`, `e2e`.
- **Environment labels only when exceptional:** `gpu`, `display`, `network`,
  `slow`, `asan-skip`. A test with none of these is by definition fast and
  asan-safe — do **not** add `headless` or `asan-safe` labels; the default
  condition is not labeled.
- **Domain labels, optional, for selection only** (never for gating): `browser`,
  `wpt`, `terminal`, `wire`, `rpc`.

Gates:

```sh
# fast lane (Release) and default developer run
ctest -LE 'network|gpu|display|slow'

# asan lane
ctest -LE 'network|gpu|display|slow|asan-skip'
```

## 5. The C harness: `test/support/ytest.h`

A single assertion vocabulary for all C tests. `assert()` is banned in tests.

Requirements:

- **Never compiles out under `NDEBUG`** — checks are always live, including in
  Release.
- Two families:
  - `YTEST_CHECK*` — record the failure and **continue**, so one run surfaces
    every failure.
  - `YTEST_REQUIRE*` — record the failure and **abort the current test path**
    when continuing would be unsafe (e.g. a NULL about to be dereferenced).
- Variants for equality, string equality, float tolerance, null / non-null, and
  yetty's typed result style (`ok` / `error`).
- Every failure prints `file:line` and enough context for
  `ctest --output-on-failure`.
- Each executable prints a final `N checks, M failed` line and returns:
  - `0` — pass
  - `1` — fail
  - `77` — skip / unsupported environment

The `77` skip code is only meaningful if CTest is told about it; the CMake
helper wires `SKIP_RETURN_CODE` (§6).

Sketch:

```c
#include "ytest.h"   /* test/support is on the include path via yetty_add_c_test() */

static void test_base64_roundtrip(struct ytest *t)
{
    struct yetty_ycore_buffer_result enc = yetty_ycore_base64_encode("hi", 2);
    YTEST_REQUIRE_OK(t, enc);
    char out[8];
    size_t n = yetty_ycore_base64_decode((const char *)enc.value.data,
                                         enc.value.len, out, sizeof out);
    YTEST_CHECK_EQ_SIZE(t, n, 2);
    YTEST_CHECK_MEM_EQ(t, out, "hi", 2);
}

int main(void)
{
    struct ytest t = ytest_begin("ycore_base64");
    test_base64_roundtrip(&t);
    return ytest_end(&t);   /* prints summary, returns 0/1/77 */
}
```

The three `test/ut/ygui` tests are migrated off `assert()` first — they are the
current highest-risk false-green tests.

## 6. The CMake helper: `test/cmake/YettyTest.cmake`

One function, `yetty_add_c_test()`, owns all test-registration boilerplate. The
generic name is deliberate — the layer is expressed by labels, not by the
function name.

```cmake
yetty_add_c_test(
    NAME    ygrid_wire
    SOURCES ygrid-wire-test.c
    LIBS    yetty_ygrid yetty_ydraw_factory yetty_ydraw_core yetty_ysdf yetty_yfigure
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
```

The helper owns: executable naming, include directories (including
`test/support`, so tests use `#include "ytest.h"`), the common trace / platform
shim sources, `add_executable`, `target_link_libraries`, `add_test`, and
always:

```cmake
set_tests_properties(${NAME} PROPERTIES
    LABELS           "${LABELS}"
    TIMEOUT          ${TIMEOUT}
    SKIP_RETURN_CODE 77)
```

Non-C helper tests (Python drivers, WPT runner) must set the same properties and
preserve the `77` skip convention manually.

A single top-level `test/CMakeLists.txt` includes the helper and adds the test
subdirectories, replacing the hand-maintained list in
`build-tools/yetty/platform/shared.cmake` and the repeated per-directory
`enable_testing()`.

## 7. Terminal tests without a process: the memory-PTY seam

Escape-sequence, scrollback, alt-screen, resize, selection, and OSC/DCS
figure-routing regressions belong in **L2**, not L4 — they do not need a real
process, window, or GPU. Use the in-process memory-PTY pair:

- API: `yetty_yplatform_memory_pty_pair_create()`
- header: `include/yetty/yplatform/pty.h`
- implementation: `src/yetty/ypty/memory-pty.c`

Pattern:

1. Create a memory-PTY pair.
2. Write a deterministic byte stream into one endpoint.
3. Drive the terminal / `yface` / `ywire` / `yvterm` path.
4. Assert on the resulting grid / model / figure-routing state.

Reserve L4 E2E for cases that genuinely need a real process, window, or GPU.

## 8. Naming conventions

- Source: `test/ut/<module>/<component>-test.c`.
- CTest name: `<module>_<component>` (e.g. `ygrid_wire`, `ycore_base64`).
- One assertion vocabulary (`ytest.h`); `assert()` is not used in tests.

## 9. CI lanes

**Per-PR (Woodpecker Linux), two runs:**

```sh
make build-desktop-ytrace-release
make test-fast                     # ctest -LE 'network|gpu|display|slow'
```

```sh
# ASAN lane
make test-asan                     # ctest -LE 'network|gpu|display|slow|asan-skip'
```

There is no separate per-PR Debug ctest lane: once the harness is NDEBUG-safe,
Release tests are meaningful, and the ASAN lane provides the non-optimized
diagnostic path better than a plain Debug lane would.

**Nightly / manual:**

- ybrowser network render sanity (`network`)
- broad WPT geometry sweep (`wpt`)
- GPU render-diff / tile-diff (`gpu`, `render`)
- `yctl`-driven E2E (`e2e`)

**Other platforms (Windows/macOS/webasm):** at minimum configure + build; then
run the headless C tests that do not depend on unavailable platform pieces.
Migrate more headless tests into these lanes over time.

## 10. Rules for new tests

- Every feature or bug fix adds at least one test at the lowest layer that
  covers it: a pure unit test, a wire/protocol contract test, a parser/IR or
  drawable-list or geometry golden, a local-vs-RPC equivalence test, or a
  targeted E2E regression.
- No `assert()` in tests — use `ytest.h`.
- No new live-network dependency in default (gated) tests.
- No test requires a real display unless labeled `display`, or a GPU unless
  labeled `gpu`.
- No reliance on wall-clock timing without an explicit timeout and the
  appropriate label.

## 11. Module coverage plan

Expand in this order (highest structural risk first):

1. **yclass** — object alloc/free, data-slice alignment, inheritance/mixin
   dispatch, super calls, local-vs-RPC-proxy equivalence, malformed RPC
   payloads, unknown handles, generated-skeleton error paths.
2. **ywire / yface** — partial frames, multiple frames per buffer, malformed
   OSC/DCS envelopes, base64 and LZ4 payloads, reset/recovery after bad input,
   large-payload chunking.
3. **ydraw-core** — command builder/parser roundtrips, groups, delete, clear,
   invalid lengths, truncated buffers, stable textual dumps for goldens.
4. **yfigure / ygrid** — child create/delete/clear, rect update, z-order, hit
   testing, dirty propagation, body forwarding, local-vs-RPC dispatch; ygrid
   spatial buckets, tombstone reuse, group-hierarchy changes, dump stability.
5. **ygui** — hbox/vbox sizing, padding/margin/gap, flex grow/shrink,
   align/justify combinations, nested layouts, click/hover/focus state machines,
   figure emission for common widgets.
6. **yvterm / yterminal** (via §7) — byte ingestion, ANSI colors, cursor
   movement, scrollback, alt screen, resize, selection, DCS/OSC routing to
   yfigure/ywire, malformed-escape recovery.
7. **Parser / renderer modules** (`ychart`, `ycircuit`, `ymusic`, `ydiagram`,
   `ymarkdown`, `ysvg`, `ypdf`) — parse valid minimal input, parse
   representative complex input, reject malformed input, deterministic IR,
   deterministic ydraw output; one or two render smoke tests only after the
   lower layer is stable.
8. **ybrowser / ylexbor** — keep the geometry-based WPT approach; integrate
   `test/ybrowser/wpt/run.py` into CTest with `browser`/`wpt` labels, separate
   expected-failing from required-green fixtures, keep network render under
   `network`.

## 12. Roadmap

**Phase 0 — plumbing (the unlock).** After this, "green" is meaningful.

- [x] Add `test/support/ytest.h`.
- [x] Replace `assert()` in the three `test/ut/ygui` tests.
- [x] Add `test/cmake/YettyTest.cmake` with `yetty_add_c_test()`.
- [x] Migrate all `test/ut/*` to the helper (labels + timeouts land here, one
      touch per registration).
- [x] Replace the stale `yetty_tests` Makefile targets with `ctest`; add
      `make test-fast` (and `test-all` / `test-network` / `test-wpt` /
      `test-asan`).
- [x] Update `.woodpecker/linux.yml` to run `make test-fast`, plus the ASAN
      lane.
- [x] Ensure the network test is labeled `network` and excluded by default.
- [x] This document lands with Phase 0.

Fast-follow: migrate `test/ybrowser/ut` and `test/integration/ylexbor` to the
helper/label conventions.

**Then, coverage** in the §11 order: yclass → ywire/yface → ydraw-core →
yfigure/ygrid → ygui → yvterm/yterminal → parser/render modules → render/E2E.
