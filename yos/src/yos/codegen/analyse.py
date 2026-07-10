#!/usr/bin/env python3
"""Categorise the structured deltas from compare-report.yaml.

Reads:
  - compare-report.yaml          (output of compare.py — raw deltas)
  - guest-api*.yaml, host-api.yaml (for the constants tables)

Emits a single yaml with these top-level sections:

  summary:        counts per bucket
  mechanical:     functions whose every arg/return delta is one of:
                    - pointer_descent that bottoms out in mechanical
                    - widen / narrow on builtins
                    - convert_struct (per-field deltas all mechanical)
                    - array_resize / array_descent
                    - opaque_size_differ (size match → already
                      compatible, this category only fires when sizes
                      genuinely differ so we mark it needs_policy)
                  → an auto-emitter can produce a converter from the
                    delta tree alone.

  needs_policy:   functions that need a human decision:
                    - convert_struct_layout (size or field-count diff)
                    - kind_mismatch
                    - reinterpret_size (pointer ↔ scalar of equal width)
                    - union_size_differ
                    - enum_size_differ
                    - arity_mismatch
                  → the next pipeline stage's hooks.yaml lists these
                    with chosen translators.

  unsupported:    deltas the system can't bridge today, e.g.
                    - missing_uid (extractor bug)
                    - recursion_limit (cyclic types)

  constants_remap: dict of name → {guest_value, host_value, header}
                  for symbols whose names match in both api.yamls but
                  whose integer values differ. This is the table the
                  runtime needs for errno_guest_to_host(int) and
                  similar O_*/MAP_*/SIG* remaps.

The intent: hand `mechanical:` to the converter generator, hand
`needs_policy:` to humans (small, curated list), and hand
`constants_remap:` to a lookup-table generator.
"""
from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

import yaml


# Verbs that an auto-emitter can handle directly.
_MECHANICAL_OPS = frozenset({
    'widen',
    'narrow',
    'pointer_descent',
    'convert_struct',
    'array_resize',
    'array_descent',
})

# Verbs that need a human-curated decision.
_NEEDS_POLICY_OPS = frozenset({
    'convert_struct_layout',
    'kind_mismatch',
    'reinterpret_size',
    'union_size_differ',
    'enum_size_differ',
    'opaque_size_differ',
    'arity_mismatch',
})

# Verbs that reflect bugs / give up.
_UNSUPPORTED_OPS = frozenset({
    'missing_uid',
    'recursion_limit',
})


def _classify_delta(delta: dict) -> str:
    """Return one of 'mechanical' / 'needs_policy' / 'unsupported'.

    `pointer_descent` / `array_descent` / `convert_struct` are
    container deltas — recurse into their sub-delta(s) and require
    EVERY child to be mechanical for the whole thing to remain
    mechanical. One bad apple downgrades to needs_policy."""
    op = delta.get('op')
    if op in _UNSUPPORTED_OPS:
        return 'unsupported'

    if op == 'pointer_descent':
        return _classify_delta(delta.get('pointee') or {'op': 'missing_uid'})

    if op == 'array_descent':
        return _classify_delta(delta.get('element') or {'op': 'missing_uid'})

    if op == 'convert_struct':
        verdicts = set()
        for fd in delta.get('fields', []):
            fop = fd.get('op')
            if fop in ('offset_shift', 'field_size'):
                # These are dimensional struct deformations; mechanical
                # only if the rest of the struct's fields resolve.
                verdicts.add('needs_policy')
                continue
            if fop == 'field_type':
                verdicts.add(_classify_delta(fd.get('delta') or {'op': 'missing_uid'}))
                continue
            verdicts.add('needs_policy')
        if 'unsupported' in verdicts:
            return 'unsupported'
        if 'needs_policy' in verdicts:
            return 'needs_policy'
        return 'mechanical'

    if op in _MECHANICAL_OPS:
        return 'mechanical'
    if op in _NEEDS_POLICY_OPS:
        return 'needs_policy'
    return 'unsupported'


def _classify_function(problems: list[dict]) -> str:
    """Combine per-arg verdicts into a function-level one."""
    verdicts = {_classify_delta(p['delta']) for p in problems}
    if 'unsupported' in verdicts:
        return 'unsupported'
    if 'needs_policy' in verdicts:
        return 'needs_policy'
    return 'mechanical'


def _constants_remap(guest: dict, host: dict) -> dict:
    """Find (name, name) pairs in constants whose values disagree.
    Returns a dict suitable for emitting a value-translation table."""
    g = guest.get('constants', {}) or {}
    h = host.get('constants', {}) or {}
    out = {}
    for name in sorted(set(g) & set(h)):
        gv, hv = g[name].get('value'), h[name].get('value')
        if gv == hv:
            continue
        out[name] = {
            'guest_value': gv,
            'host_value':  hv,
            'guest_header': g[name].get('header'),
            'host_header':  h[name].get('header'),
        }
    return out


def analyse(report: dict, guest_api: dict, host_api: dict) -> dict:
    mechanical:    dict[str, list[dict]] = {}
    needs_policy:  dict[str, list[dict]] = {}
    unsupported:   dict[str, list[dict]] = {}

    # Histogram of root delta verbs across all problems — useful for
    # sanity-checking what's actually showing up.
    op_hist = Counter()

    for fname, problems in (report.get('mismatches') or {}).items():
        verdict = _classify_function(problems)
        bucket = {
            'mechanical':   mechanical,
            'needs_policy': needs_policy,
            'unsupported':  unsupported,
        }[verdict]
        bucket[fname] = problems
        for p in problems:
            op_hist[(p.get('delta') or {}).get('op', '?')] += 1

    constants_remap = _constants_remap(guest_api, host_api)

    return {
        'summary': {
            'mechanical':       len(mechanical),
            'needs_policy':     len(needs_policy),
            'unsupported':      len(unsupported),
            'compatible':       len(report.get('compatible') or []),
            'guest_only':       len(report.get('guest_only') or []),
            'host_only':        len(report.get('host_only') or []),
            'variadic_skipped': len(report.get('variadic_skipped') or []),
            'constants_remap':  len(constants_remap),
            'delta_op_histogram': dict(op_hist.most_common()),
        },
        'compatible':       report.get('compatible') or [],
        'mechanical':       mechanical,
        'needs_policy':     needs_policy,
        'unsupported':      unsupported,
        'guest_only':       report.get('guest_only') or [],
        'host_only':        report.get('host_only') or [],
        'variadic_skipped': report.get('variadic_skipped') or [],
        'constants_remap':  constants_remap,
    }


def main() -> int:
    p = argparse.ArgumentParser(description='Categorise compare-report.yaml deltas.')
    p.add_argument('--report',    required=True, type=Path,
                   help='compare-report.yaml from compare.py')
    p.add_argument('--guest-api', required=True, type=Path,
                   help='guest-side api.yaml (for constants table)')
    p.add_argument('--host-api',  required=True, type=Path,
                   help='host-side api.yaml (for constants table)')
    p.add_argument('--out',       required=True, type=Path,
                   help='output yaml')
    args = p.parse_args()

    with args.report.open()    as f: report    = yaml.safe_load(f)
    with args.guest_api.open() as f: guest_api = yaml.safe_load(f)
    with args.host_api.open()  as f: host_api  = yaml.safe_load(f)

    out = analyse(report, guest_api, host_api)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('w') as f:
        yaml.dump(out, f, sort_keys=False, default_flow_style=False, width=120)

    s = out['summary']
    print(f'[api-analyse] mechanical={s["mechanical"]}  '
          f'needs_policy={s["needs_policy"]}  '
          f'unsupported={s["unsupported"]}', file=sys.stderr)
    print(f'              compatible={s["compatible"]}  '
          f'constants_remap={s["constants_remap"]}', file=sys.stderr)
    print(f'              report → {args.out}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
