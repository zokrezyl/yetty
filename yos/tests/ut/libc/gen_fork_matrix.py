#!/usr/bin/env python3
"""Generate one wasm test .c per row of fork_matrix.yaml.

Each row becomes test_fork_child_<name>.c with the same fork/waitpid
scaffold and the row's body inlined as the child's work. The
intent is that the matrix grows by appending YAML rows, not by
hand-writing more .c files — which is how the current fork-test
set has stayed at five.

Two invocation modes:

    gen_fork_matrix.py --yaml PATH --list-names
        Print one row name per line to stdout. Used at meson
        configure time to size the test() loop.

    gen_fork_matrix.py --yaml PATH --out-dir DIR
        Emit one test_fork_child_<name>.c per row into DIR.
        Used as a custom_target build step.

Both modes read the same YAML, so meson configure and meson
build see a consistent row list.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.stderr.write(
        "gen_fork_matrix.py: PyYAML not importable. Make sure meson "
        "is invoking us via the project venv (.venv/bin/python3).\n")
    sys.exit(2)


TEMPLATE = """\
/*
 * test_fork_child_{name}.c — GENERATED from fork_matrix.yaml. DO NOT EDIT.
 *
 * Row:         {name}
 * Description: {description}
{notes_block}\
 *
 * The harness: parent forks, child calls child_body(), parent
 * waitpid()s and propagates the child's return code as its own
 * exit code. A row succeeds when child_body() returns 0; any
 * non-zero return surfaces directly as the test's exit code.
 *
 * Expected: exit {expected_exit}{stdout_clause}.
 */

{externs}

/* Wasm function-table padding. The FreeBSD ABI encodes SIG_IGN as
 * sa_handler == 1 and SIG_DFL as sa_handler == 0. yos's sigaction
 * stores wasm function-table indices in the same uint32 slot. If
 * clang's linker happens to place a user-defined handler at table
 * index 1 (small wasm modules typically do — handler count is low),
 * yos can't tell SIG_IGN from "function-at-table[1]" at the bridge
 * boundary.
 *
 * Fix: declare two pad functions with DISTINCT bodies (different
 * static counters) so clang's link-time dedup can't merge them,
 * indirect-call them via a volatile table from _start so wasm-ld
 * can't DCE them, and they reliably land at table indices 1 and 2.
 * Any user-defined function in subsequent .c content then lands at
 * index ≥ 3 — outside the SIG_DFL/SIG_IGN sentinel range. */
static volatile int __yos_pad_marker_a;
static volatile int __yos_pad_marker_b;
__attribute__((used))
static void __yos_fnpad_a(int s) {{ __yos_pad_marker_a += s + 1; }}
__attribute__((used))
static void __yos_fnpad_b(int s) {{ __yos_pad_marker_b -= s + 2; }}
typedef void (*__yos_fnpad_t)(int);
static __yos_fnpad_t volatile __yos_fnpad_arr[2] = {{
    __yos_fnpad_a, __yos_fnpad_b
}};

{prep_globals_block}\
static void emit(const char *s) {{
    write(1, s, (unsigned)strlen(s));
}}

static int child_body(void) {{
{body_indented}
}}

void _start(void) {{
    /* Indirect-call the pads so wasm-ld can't drop them and they
     * claim table indices 1 and 2 before any user function. */
    __yos_fnpad_arr[0](0);
    __yos_fnpad_arr[1](0);
{prep_inline_block}\
    int pid = fork();
    if (pid < 0)  {{ emit("HARNESS:fork-failed\\n"); _exit(120); }}
    if (pid == 0) {{ _exit(child_body()); }}

    int st = 0;
    int rpid = waitpid(pid, &st, 0);
    if (rpid != pid) {{ emit("HARNESS:waitpid-failed\\n"); _exit(121); }}

{post_wait_block}\
    /* POSIX status: ((status >> 8) & 0xff) is the child's exit code. */
    _exit((st >> 8) & 0xff);
}}
"""


def load_matrix(yaml_path: Path) -> dict:
    # Explicit utf-8 — Windows Python defaults to cp1252 on file.open().
    with yaml_path.open(encoding='utf-8') as fh:
        m = yaml.safe_load(fh)
    if not isinstance(m, dict) or 'rows' not in m:
        sys.stderr.write(f"{yaml_path}: missing top-level `rows:` list\n")
        sys.exit(2)
    if not isinstance(m['rows'], list):
        sys.stderr.write(f"{yaml_path}: `rows:` must be a list\n")
        sys.exit(2)

    seen = set()
    for row in m['rows']:
        if not isinstance(row, dict):
            sys.stderr.write(f"{yaml_path}: row is not a mapping: {row!r}\n")
            sys.exit(2)
        name = row.get('name')
        if not name or not isinstance(name, str):
            sys.stderr.write(f"{yaml_path}: row missing `name`: {row!r}\n")
            sys.exit(2)
        if not name.replace('_', '').isalnum():
            sys.stderr.write(
                f"{yaml_path}: row name {name!r} must be [A-Za-z0-9_] only\n")
            sys.exit(2)
        if name in seen:
            sys.stderr.write(f"{yaml_path}: duplicate row name {name!r}\n")
            sys.exit(2)
        seen.add(name)
    return m


def render(matrix: dict, row: dict) -> str:
    name           = row['name']
    description    = row.get('description', '').strip().splitlines()[0] \
                       if row.get('description') else ''
    notes          = row.get('notes', '')
    body           = row.get('body', '').rstrip('\n')
    prep_globals   = row.get('prep_globals', '').rstrip('\n')
    prep           = row.get('prep', '').rstrip('\n')
    post_wait      = row.get('post_wait', '').rstrip('\n')
    externs_row    = row.get('externs', []) or []
    externs_global = (matrix.get('prologue', {}) or {}).get('externs', []) or []
    expected_exit  = int(row.get('expected_exit', 0))
    expected_stdout = row.get('expected_stdout', '')

    # Dedup externs while preserving declaration order: global first
    # (fork/waitpid/_exit etc.), then row-specific. Bare equality on
    # the declaration string is enough; rows never partially override.
    seen, externs = set(), []
    for line in list(externs_global) + list(externs_row):
        line = line.rstrip()
        if line in seen:
            continue
        seen.add(line)
        externs.append(line)
    externs_block = '\n'.join(externs)

    # `expected_stdout` becomes a `, stdout contains "X"` clause that
    # run_libc_test.py picks up via its regex. Keep the wording
    # exact — the runner's pattern is `stdout\s+contains\s+"..."`.
    stdout_clause = f', stdout contains "{expected_stdout}"' if expected_stdout else ''

    # Indent the body 4 spaces (function scope). Empty lines stay
    # empty rather than picking up trailing whitespace.
    body_indented = '\n'.join(
        ('    ' + line) if line else '' for line in body.splitlines())

    # Optional notes block — surfaces the YAML `notes:` field at the
    # top of the .c so a failing test points back to its rationale.
    if notes:
        notes_lines = [' * ' + line if line else ' *'
                       for line in notes.rstrip('\n').splitlines()]
        notes_block = ' *\n * Notes:\n' + '\n'.join(notes_lines) + '\n'
    else:
        notes_block = ''

    # `prep_globals:` lands at file scope so both parent prep and
    # child_body can see the declarations. fork() snapshots the wasm
    # linear memory, so any value the parent writes into a file-scope
    # static BEFORE fork is visible to the child afterwards — exactly
    # the channel we want for "compute X in parent, compare to child's".
    prep_globals_block = (prep_globals + '\n\n') if prep_globals else ''

    # `prep:` is plain statements emitted at the top of _start, before
    # the fork() call. Anything needing parent/child handoff state
    # belongs in prep_globals.
    if prep:
        prep_inline = '\n'.join(
            ('    ' + line) if line else '' for line in prep.splitlines())
        prep_inline_block = prep_inline + '\n'
    else:
        prep_inline_block = ''

    # `post_wait:` runs in the parent AFTER waitpid returns, BEFORE
    # the final _exit. Use it to drain pipes the child wrote into and
    # re-emit content on the real stdout so run_libc_test.py's
    # `expected_stdout` substring check fires on parent-visible output.
    # The variable `st` (waitpid's status) is in scope here.
    if post_wait:
        post_wait_indented = '\n'.join(
            ('    ' + line) if line else '' for line in post_wait.splitlines())
        post_wait_block = post_wait_indented + '\n\n'
    else:
        post_wait_block = ''

    return TEMPLATE.format(
        name               = name,
        description        = description,
        notes_block        = notes_block,
        externs            = externs_block,
        body_indented      = body_indented,
        prep_globals_block = prep_globals_block,
        prep_inline_block  = prep_inline_block,
        post_wait_block    = post_wait_block,
        expected_exit      = expected_exit,
        stdout_clause      = stdout_clause,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--yaml',    required=True, type=Path,
                    help='Path to fork_matrix.yaml')
    ap.add_argument('--list-names', action='store_true',
                    help='Print row names (one per line), do not emit files')
    ap.add_argument('--list-xfail', action='store_true',
                    help='Print only the names of rows tagged `xfail: true`')
    ap.add_argument('--out-dir', type=Path,
                    help='Where to write test_fork_child_<name>.c files')
    args = ap.parse_args()

    matrix = load_matrix(args.yaml)
    rows   = matrix['rows']

    if args.list_names:
        for row in rows:
            print(row['name'])
        return 0

    if args.list_xfail:
        for row in rows:
            if row.get('xfail'):
                print(row['name'])
        return 0

    if not args.out_dir:
        sys.stderr.write(
            "gen_fork_matrix.py: need --list-names, --list-xfail or --out-dir\n")
        return 2

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for row in rows:
        out = args.out_dir / f"test_fork_child_{row['name']}.c"
        rendered = render(matrix, row)
        # Skip a write if the content is identical — keeps meson from
        # marking dependents stale on a no-op codegen rerun.
        if out.exists() and out.read_text(encoding='utf-8') == rendered:
            continue
        out.write_text(rendered, encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
