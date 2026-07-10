# perf-test runners

Wrappers around `./tools/yos.sh perf-stress` that pin a specific
stress configuration so results are comparable across runs. Each
script writes its full output to `tmp/perf-<name>.log` and prints
the tail summary.

| Script           | Scope                                          |
|------------------|------------------------------------------------|
| `default.sh`     | perf-stress's built-in default (~111 procs, 8 threads). Smoke test. |
| `long.sh`        | The `-l` preset baked into perf-stress (~5× default). |
| `wide-deep.sh`   | Beyond `-l`: ~1600-proc fork tree (4 levels), 902 chaos procs, 64 pthreads, 663 chaos threads, 4 MiB I/O, `-i 10` lock-iter multiplier. |

Each script is `set -euo pipefail` and exits non-zero if perf-stress
itself returns non-zero. The wasm guest prints `perf-stress ok` on a
clean run; absence of that line means a phase failed even if the host
exit code is 0.

## Usage

```sh
./tools/perf-test/default.sh
./tools/perf-test/long.sh
./tools/perf-test/wide-deep.sh
```

All knob meanings (`-f`/`-d`/`-p`/`-e`/`-r`/`-k`/`-T`/`-R`/`-t`/`-i`/`-o`)
are documented by `./tools/yos.sh perf-stress -h`.
