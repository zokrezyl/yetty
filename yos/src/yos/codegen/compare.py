#!/usr/bin/env python3
"""Compare two api.yaml files (guest vs host) by type LAYOUT.

Decides, per function present in both files, whether the argument
shapes line up well enough that yos can pass them through with no
struct conversion. Names don't matter — layout does.

Compatibility rules (recursive, layout-driven):

  pointer  vs  pointer        compatible iff pointees are compatible
                              (the pointers themselves can be 4 or 8
                              bytes — yos translates wasm offsets to
                              host pointers either way).

  builtin  vs  builtin        compatible iff sizes match.

  struct   vs  struct         compatible iff:
                                * total size matches
                                * same number of fields
                                * each field has matching offset, size,
                                  and recursively-compatible type.

  union    vs  union          compatible iff sizes match (we don't
                              try to align all variants — the kernel
                              picks one based on context).

  array    vs  array          compatible iff element count matches AND
                              elements are compatible.

  enum     vs  enum           compatible iff sizes match. (Enum *values*
                              may differ — that's a translation problem,
                              not a layout one.)

  typedef  → unwrap to canonical and recurse.

  void     vs  void           compatible.

  function-pointer types are treated as pointer (we don't recurse into
  function signatures because most callbacks come paired with their
  data; the data layout is the load-bearing piece).

Output (yaml):

  compatible:        [list of function names that pass on every arg]
  mismatches:        { name: [{arg, reason}, …] }
  guest_only:        [present in guest but not host]
  host_only:         [present in host but not guest]
  variadic_skipped:  [variadic functions — comparison is unreliable]

The script does NOT try to map names across systems (e.g. linux's
`epoll_create1` vs freebsd's `kqueue`). That's a manual mapping the
next pipeline stage owns.
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml


# ─── Type helpers ────────────────────────────────────────────────────

def _resolve(t: dict, types: dict) -> dict:
    """Walk through typedef indirections to the canonical type. Returns
    the same dict if t is already canonical."""
    seen = set()
    while t is not None and t.get('kind') == 'typedef':
        uid = t.get('type_uid')
        if uid in seen or uid is None:
            break
        seen.add(uid)
        nxt = types.get(uid)
        if nxt is None:
            break
        t = nxt
    return t


def _summarise(t: dict, types: dict) -> str:
    """One-line human summary of a type, for mismatch reasons."""
    if t is None:
        return '<unknown>'
    t = _resolve(t, types)
    k = t.get('kind', '?')
    if k == 'builtin':
        return f"{t.get('name','?')} (size={t.get('size','?')})"
    if k == 'pointer':
        pointee_uid = t.get('pointee_uid')
        pointee = types.get(pointee_uid) if pointee_uid else None
        return f"pointer → {_summarise(pointee, types)}"
    if k == 'struct':
        return f"struct {t.get('name','?')} (size={t.get('size','?')}, fields={len(t.get('fields',[]))})"
    if k == 'union':
        return f"union {t.get('name','?')} (size={t.get('size','?')})"
    if k == 'array':
        elem = types.get(t.get('element_uid'))
        return f"array[{t.get('count','?')}] of {_summarise(elem, types)}"
    if k == 'enum':
        return f"enum {t.get('name','?')} (size={t.get('size','?')})"
    if k == 'void':
        return 'void'
    if k == 'opaque':
        return f"opaque (size={t.get('size','?')})"
    return f"{k} {t.get('name', t.get('spelling',''))}"


# ─── Compatibility check ─────────────────────────────────────────────
#
# Returns (compatible, delta) per type pair.
#  - compatible=True, delta=None       → byte-for-byte identical layout;
#                                        no conversion needed.
#  - compatible=False, delta=<dict>    → structured description of what
#                                        differs. The shape of `delta`
#                                        is what analyse.py categorises
#                                        into mechanical / needs_policy
#                                        / unsupported.
#
# Delta `op` verbs (extend as new categories are added):
#   widen / narrow            integer or pointer-pointee size change
#   reinterpret_size          size matches but kind differs (e.g.
#                             pointer vs union of equal width)
#   convert_struct            struct same size + same field count, but
#                             one or more fields differ; carries the
#                             list of per-field deltas in `fields`
#   convert_struct_layout     struct sizes/field counts differ — needs
#                             custom (typically hand-written) bridge
#   pointer_descent           both are pointers; carries the pointee
#                             delta in `pointee`
#   array_resize              array length differs
#   array_descent             array same length but element differs
#   union_size_differ         union total size differs
#   enum_size_differ          enum width differs (typed differently)
#   kind_mismatch             totally different type kinds
#   missing_uid               extractor produced a dangling reference

@dataclass
class Mismatch:
    arg: str
    delta: dict


def _types_compatible(g_uid: str, g_types: dict,
                      h_uid: str, h_types: dict,
                      depth: int = 0):
    """Returns (compatible: bool, delta: dict | None)."""
    if depth > 16:
        return False, {'op': 'recursion_limit'}

    g = _resolve(g_types.get(g_uid), g_types)
    h = _resolve(h_types.get(h_uid), h_types)
    if g is None or h is None:
        return False, {
            'op': 'missing_uid',
            'guest_uid': g_uid, 'host_uid': h_uid,
        }

    gk, hk = g.get('kind'), h.get('kind')

    # void / void
    if gk == 'void' and hk == 'void':
        return True, None

    # pointer / pointer — descend into pointees; pointer width itself
    # is yos's responsibility (wasm offset → host VA), so it's never a
    # mismatch.
    if gk == 'pointer' and hk == 'pointer':
        gp = _resolve(g_types.get(g.get('pointee_uid')), g_types)
        hp = _resolve(h_types.get(h.get('pointee_uid')), h_types)
        if (gp and gp.get('kind') == 'void') and (hp and hp.get('kind') == 'void'):
            return True, None
        ok, sub = _types_compatible(g['pointee_uid'], g_types,
                                    h['pointee_uid'], h_types, depth + 1)
        if ok:
            return True, None
        return False, {'op': 'pointer_descent', 'pointee': sub}

    # builtin / builtin
    if gk == 'builtin' and hk == 'builtin':
        gs, hs = g.get('size'), h.get('size')
        if gs == hs:
            return True, None
        op = 'widen' if (gs or 0) < (hs or 0) else 'narrow'
        return False, {
            'op': op,
            'from': {'name': g.get('name'), 'size': gs},
            'to':   {'name': h.get('name'), 'size': hs},
            # Crude signedness guess from the C type spelling. Used by
            # generators to pick sign-extend vs zero-extend.
            'signed_from': 'unsigned' not in (g.get('name') or ''),
            'signed_to':   'unsigned' not in (h.get('name') or ''),
        }

    # struct / struct
    if gk == 'struct' and hk == 'struct':
        gf, hf = g.get('fields', []), h.get('fields', [])
        # Aggregate any per-field deltas.
        if g.get('size') == h.get('size') and len(gf) == len(hf):
            field_deltas = []
            for i, (a, b) in enumerate(zip(gf, hf)):
                if a.get('offset') != b.get('offset'):
                    field_deltas.append({
                        'index': i, 'name': a.get('name'),
                        'op': 'offset_shift',
                        'from': a.get('offset'), 'to': b.get('offset'),
                    })
                    continue
                if a.get('size') != b.get('size'):
                    field_deltas.append({
                        'index': i, 'name': a.get('name'),
                        'op': 'field_size',
                        'from': a.get('size'), 'to': b.get('size'),
                    })
                    continue
                ok, sub = _types_compatible(a['type_uid'], g_types,
                                            b['type_uid'], h_types, depth + 1)
                if not ok:
                    field_deltas.append({
                        'index': i, 'name': a.get('name'),
                        'op': 'field_type',
                        'delta': sub,
                    })
            if not field_deltas:
                return True, None
            return False, {
                'op': 'convert_struct',
                'name': g.get('name'),
                'size': g.get('size'),
                'fields': field_deltas,
            }
        # Different total layout — generator can't auto-emit; flag it.
        return False, {
            'op': 'convert_struct_layout',
            'name': g.get('name'),
            'guest_size': g.get('size'), 'host_size': h.get('size'),
            'guest_field_count': len(gf), 'host_field_count': len(hf),
        }

    # union / union
    if gk == 'union' and hk == 'union':
        if g.get('size') == h.get('size'):
            return True, None
        return False, {
            'op': 'union_size_differ',
            'name': g.get('name'),
            'guest_size': g.get('size'), 'host_size': h.get('size'),
        }

    # array / array
    if gk == 'array' and hk == 'array':
        if g.get('count') != h.get('count'):
            return False, {
                'op': 'array_resize',
                'guest_count': g.get('count'), 'host_count': h.get('count'),
            }
        ok, sub = _types_compatible(g['element_uid'], g_types,
                                    h['element_uid'], h_types, depth + 1)
        if ok:
            return True, None
        return False, {'op': 'array_descent', 'element': sub}

    # enum / enum — size match is enough; values may differ but
    # that's a value-table problem, captured separately.
    if gk == 'enum' and hk == 'enum':
        if g.get('size') == h.get('size'):
            return True, None
        return False, {
            'op': 'enum_size_differ',
            'guest_size': g.get('size'), 'host_size': h.get('size'),
        }

    # opaque slot — be lenient if sizes match.
    if 'opaque' in (gk, hk):
        if g.get('size') == h.get('size'):
            return True, None
        return False, {
            'op': 'opaque_size_differ',
            'guest_size': g.get('size'), 'host_size': h.get('size'),
        }

    # Pointer-vs-non-pointer of the same width is suspicious but
    # technically representable (caller passes a pointer cast as int);
    # flag separately so analyse.py can decide.
    if (gk == 'pointer' and hk in ('builtin', 'union') or
        hk == 'pointer' and gk in ('builtin', 'union')) \
        and g.get('size') == h.get('size'):
        return False, {
            'op': 'reinterpret_size',
            'guest_kind': gk, 'host_kind': hk,
            'size': g.get('size') or h.get('size'),
        }

    return False, {
        'op': 'kind_mismatch',
        'guest_kind': gk, 'host_kind': hk,
        'guest_summary': _summarise(g, g_types),
        'host_summary':  _summarise(h, h_types),
    }


# ─── Function-level comparison ───────────────────────────────────────

def compare(guest: dict, host: dict) -> dict:
    g_fns = guest.get('functions', {})
    h_fns = host.get('functions', {})
    g_types = guest.get('types', {})
    h_types = host.get('types', {})

    common = set(g_fns.keys()) & set(h_fns.keys())
    g_only = sorted(set(g_fns.keys()) - common)
    h_only = sorted(set(h_fns.keys()) - common)

    compatible: list[str] = []
    mismatches: dict[str, list[dict]] = {}
    variadic_skipped: list[str] = []

    for name in sorted(common):
        gf = g_fns[name]
        hf = h_fns[name]
        if gf.get('variadic') or hf.get('variadic'):
            variadic_skipped.append(name)
            continue

        problems: list[dict] = []

        # return type
        ok, delta = _types_compatible(gf['ret'], g_types, hf['ret'], h_types)
        if not ok:
            problems.append({'arg': '<return>', 'delta': delta})

        # arg-by-arg
        ga, ha = gf.get('args', []), hf.get('args', [])
        if len(ga) != len(ha):
            problems.append({'arg': '<arity>',
                             'delta': {'op': 'arity_mismatch',
                                       'guest_argc': len(ga),
                                       'host_argc':  len(ha)}})
        else:
            for i, (a, b) in enumerate(zip(ga, ha)):
                ok, delta = _types_compatible(a['type_uid'], g_types,
                                              b['type_uid'], h_types)
                if not ok:
                    problems.append({
                        'arg':   a.get('name') or f'<arg{i}>',
                        'index': i,
                        'delta': delta,
                    })

        if problems:
            mismatches[name] = problems
        else:
            compatible.append(name)

    return {
        'summary': {
            'guest_total':       len(g_fns),
            'host_total':        len(h_fns),
            'common':            len(common),
            'compatible':        len(compatible),
            'mismatches':        len(mismatches),
            'variadic_skipped':  len(variadic_skipped),
            'guest_only':        len(g_only),
            'host_only':         len(h_only),
        },
        'compatible':        compatible,
        'mismatches':        mismatches,
        'variadic_skipped':  variadic_skipped,
        'guest_only':        g_only,
        'host_only':         h_only,
    }


# ─── CLI ─────────────────────────────────────────────────────────────

def main() -> int:
    p = argparse.ArgumentParser(description='Compare two api.yaml files by type layout.')
    p.add_argument('--guest', required=True, type=Path,
                   help='guest-side api.yaml (e.g. wasm32 FreeBSD)')
    p.add_argument('--host',  required=True, type=Path,
                   help='host-side api.yaml (e.g. build host glibc)')
    p.add_argument('--out',   required=True, type=Path,
                   help='output yaml report')
    p.add_argument('--print-summary', action='store_true',
                   help='also print the summary block to stderr')
    args = p.parse_args()

    with args.guest.open() as f:
        guest = yaml.safe_load(f)
    with args.host.open() as f:
        host = yaml.safe_load(f)

    report = compare(guest, host)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('w') as f:
        yaml.dump(report, f, sort_keys=False, default_flow_style=False, width=120)

    s = report['summary']
    print(f'[api-compare] guest={s["guest_total"]} host={s["host_total"]} common={s["common"]}',
          file=sys.stderr)
    print(f'              compatible={s["compatible"]} mismatches={s["mismatches"]} '
          f'variadic={s["variadic_skipped"]}', file=sys.stderr)
    print(f'              guest_only={s["guest_only"]} host_only={s["host_only"]}',
          file=sys.stderr)
    print(f'              report → {args.out}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
