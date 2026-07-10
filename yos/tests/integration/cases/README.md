# Shared command case table

One declarative case table, two backends, libvterm as the shared oracle.

Each `*.json` file here holds either a single case object or an array of them.
A **case** describes one command invocation and what a human should see on the
screen after it runs. The same data drives both runners so a case authored once
is asserted on **both**:

- the **native** `yos` binary (the source of truth), and
- the **browser process engine** (`yos_proc.mjs`, cooperative;
  `mt_engine.mjs` for threads).

Both backends run the **same** tool wasm (`<libexec>/<argv[0]>`), capture the
raw terminal stream, and render it through `vterm_grid.c` (libvterm) into a
faithful rows×cols grid — so any grid difference is purely the engine, not a
different build of the tool.

The runner is `src/yos/platform/web/browser-parity-suite.mjs`; run it with
`make test-browser-parity`.

## Case schema

```jsonc
{
  "id": "grep-fixed-match",      // unique, human-readable
  "argv": ["grep", "apple"],     // argv[0] resolves to <libexec>/grep.wasm
  "stdin": "apple\nbanana\napple\n",   // optional; fed to both backends as a pipe
  "cols": 80,                    // optional (default 80) — grid width
  "rows": 24,                    // optional (default 24) — grid height
  "interactive": false,          // optional (default false) — reserved for TUI cases
  "expect": {                    // any subset; ALL present matchers must hold
    "exitCode": 0,               // guest exit code
    "rawStdout": "apple\napple\n",       // exact stdout bytes (CR-normalised)
    "stdoutContains": "apple",           // stdout substring
    "gridContains": ["apple"],           // string | array — rendered grid contains each
    "gridRow": { "row": 0, "equals": "apple" },   // a specific rendered row (equals | contains)
    "goldenGrid": "golden/grep.grid"     // rendered grid equals a committed golden file
  }
}
```

## How a case is classified

The `expect` block is the correctness criterion (the oracle). **Both backends
are always run** and evaluated against it — the native binary is the source of
truth, so it is never skipped just because the browser already passed (a
browser-correct-but-native-broken case is a real native bug, and checking native
only on browser failure would hide it). The two PASS/FAIL results give four
classes:

| browser | native | class          | gates CI? |
|---------|--------|----------------|-----------|
| pass    | pass   | **PASS**       | —         |
| fail    | pass   | **BROWSER-GAP**| **yes**   |
| pass    | fail   | **NATIVE-GAP** | no        |
| fail    | fail   | **BOTH-FAIL**  | no        |

- **BROWSER-GAP** — native works, the browser engine doesn't → a real engine
  bug; the only class that **fails CI**.
- **NATIVE-GAP** — the browser is correct, the native binary is not → a
  native-side bug the browser is already ahead of (e.g. native `grep`/`sed`
  under the rune-locale `<_ctype.h>` hole). Reported with a grid diff; a
  worklist entry, **not** a CI blocker.
- **BOTH-FAIL** — broken on both → a shared guest/libc gap. Worklist, not CI.

CI fails **only on a fresh BROWSER-GAP**, so known-broken-on-native cases
(grep/sed today) don't block the always-on browser-regression net.

### Documenting a pre-existing gap

A case may carry `"knownBrowserGap": "<reason>"` or `"knownNativeGap": "<reason>"`.
A case that would classify as BROWSER-GAP / NATIVE-GAP is still run and reported —
under its own **KNOWN-BROWSER-GAP** / **KNOWN-NATIVE-GAP** heading — but does
**not** gate CI. This is for a documented, pre-existing gap on that one backend:

- `knownBrowserGap` — a known browser-engine bug (native is correct). Mirrors
  `parity_runner.mjs`'s `required:false` KNOWN-GAP entries.
- `knownNativeGap` — a known native bug the browser is already ahead of (e.g.
  `grep`/`sed` today: native's regex character classes never match under the
  rune-locale `<_ctype.h>` hole, while the browser engine matches via JS
  `RegExp`).

Either way: reported, not hidden; a worklist item, not a CI blocker. A
regression on any currently-passing case still fails CI. When the underlying gap
is fixed, drop the flag and the case becomes enforced on that backend.

Seed the correct expected output even for commands that are currently broken on
a backend — they land in NATIVE-GAP / BOTH-FAIL until the gap is fixed, then flip
to PASS automatically, with no test edit.
