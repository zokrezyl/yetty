# qa-tools

Small, focused Python wrappers around C static-analysis, vulnerability
scanning, and formatting tools. Each script does one thing; the
orchestrator (`qa-overview.py`) runs them all and prints a per-section
report.

All logs and reports go under `tmp/qa/` at the repo root. All scripts
have a PEP 723 `uv run --script` shebang — run them directly.

## Layout

```
qa-tools/
├── _common.py                shared helpers (file discovery, logging)
├── qa-overview.py            orchestrator + report
├── analysis/                 read-only checks (never mutate source)
│   ├── check-format.py             clang-format dry run
│   ├── check-clang-tidy.py         clang-tidy
│   ├── check-cppcheck.py           cppcheck
│   ├── check-scan-build.py         Clang Static Analyzer
│   ├── check-osv-scanner.py        OSV / lockfile CVE scan
│   ├── check-trivy.py              Aqua Trivy fs scan
│   ├── check-grype.py              Anchore Grype dir scan
│   ├── result-checker/             LibTooling: enforces Result-type return rule
│   ├── yclass/                     LibTooling: flags hand-rolled object/slice pointer arithmetic
│   ├── out-param/                  LibTooling: flags small values returned as a cluster of scalar pointer out-params
│   └── naming-convention/          LibTooling: naming-convention diagnostics
└── refactoring/              tools that *modify* the codebase
    ├── code-format/
    │   └── apply-format.py         clang-format -i (in place)
    ├── naming-convention/          LibTooling: applies naming-convention fixes
    └── replace                     existing search-replace helper
```

The LibTooling checkers (`result-checker`, `yclass`, `out-param`,
`naming-convention`) are C++ clang tools, built as part of the CMake project
when `YETTY_ENABLE_TOOL_QA` is on. Each has a `run-*.sh` that intersects the
source tree with `build-*/compile_commands.json` and invokes the built binary.

## Scope

By default the source-level checks (format, tidy, cppcheck) only look at
our own code:

- `src/yetty/`
- `src/yrender-utils/`
- `include/yetty/`

Third-party directories (`src/libvterm-0.3.3/`, `src/tinyemu/`) are always
excluded. Limit the scope for a single run with `QA_PATHS`:

```sh
QA_PATHS="src/yetty/ygui" qa-tools/analysis/check-format.py
```

The vulnerability scanners (osv-scanner, trivy, grype) run over the whole
repo with `build-*/`, `tmp/`, and vendored deps excluded.

## Requirements

| Check         | Tool needed                                               |
|---------------|------------------------------------------------------------|
| format        | `clang-format` (v9 works; v14+ recommended)                |
| clang-tidy    | `clang-tidy` v14+, plus a `compile_commands.json` from a build |
| cppcheck      | `cppcheck`                                                 |
| scan-build    | `scan-build` (clang-tools-<ver>) + `cmake`                 |
| osv-scanner   | `osv-scanner` (Go binary; see install instructions in script) |
| trivy         | `trivy`                                                    |
| grype         | `grype`                                                    |

`uv` runs the scripts; install once with your package manager of choice.

## Typical workflow

```sh
# Normalize whitespace across the tree (one-shot).
qa-tools/analysis/check-format.py             # report only
qa-tools/refactoring/code-format/apply-format.py   # rewrite in place

# Build once so clang-tidy / scan-build have a compile DB.
make build-desktop-ytrace-release

# Full report.
qa-tools/qa-overview.py
qa-tools/qa-overview.py --skip scan-build      # skip slow checks
QA_PATHS="src/yetty/ygui" qa-tools/qa-overview.py  # narrow scope
```

Exit codes are consistent across scripts:

- `0` — clean
- `1` — check ran, reported issues
- `2` — check could not run (missing tool, missing compile DB, etc.)

## Adding a new check

1. Drop a new `check-<thing>.py` into `qa-tools/analysis/` with the same
   PEP 723 shebang the others use.
2. `from _common import ...` for paths, logging, file discovery.
3. Write detailed output under `_common.TMP_DIR`; return a `CheckResult`
   from a top-level `check_<thing>()` function.
4. Wire it into `qa-overview.py`: add a `_load(...)` line and a `run_one`
   call alongside the others.
