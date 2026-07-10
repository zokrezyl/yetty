#!/usr/bin/env python3
"""api.yaml extractor — walks a libc header tree via libclang and emits
a pure data record of what it found. NO comparison, NO classification,
NO syscall numbers. The next pipeline stage consumes one or more
api.yaml files and decides what bridges/translations to emit.

Output sections (see _emit_yaml):
  functions:   name → {header, ret, args:[{name, type_uid}]}
  types:       uid → {kind: struct|union|typedef|builtin|pointer|
                            array|enum|opaque|void, …}
  constants:   name → {value:int, header}
  enums:       [{name?:str, header, members:[{name, value}]}]

Each entry carries a `header:` tag (the source header file the
declaration came from, e.g. `sys/stat.h`). This is the only
provenance we record — the comparison step uses it to know which
generated header should host each symbol on the way out.

Usage: invoked from meson custom_target. Arguments are (-I/-isystem
include paths, -D defines, an entry-point header content). Both
"freestanding sysroot" mode (-nostdinc + explicit -I) and "natural
host search path" mode (no -nostdinc) are supported via --no-stdinc.
"""
from __future__ import annotations

import argparse
import os
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

import yaml

import clang.cindex
from clang.cindex import CursorKind, TypeKind

# libclang.so location: env var wins; otherwise auto-detect on common
# paths. Linux/Debian ships /usr/lib/llvm-N/lib/libclang.so.{1,N};
# Nix puts it under /nix/store/...-clang-N/lib/.
#
# Bonus headache on Nix: libclang.so depends on libstdc++.so.6 which
# isn't in the default loader path on Nix dev shells. We pre-load it
# via ctypes if YOS_LIBSTDCXX_SO is set OR we can find it under
# /nix/store. Without this dance, Config.set_library_file() succeeds
# but clang_createIndex() fails with "libstdc++.so.6 not found".
def _preload_libstdcxx() -> None:
    import ctypes, glob
    p = os.environ.get('YOS_LIBSTDCXX_SO')
    candidates = [p] if p else []
    candidates += sorted(glob.glob('/nix/store/*-gcc-1[3-9]*-lib/lib/libstdc++.so.6'))
    candidates += ['/usr/lib/x86_64-linux-gnu/libstdc++.so.6']
    for c in candidates:
        if c and os.path.isfile(c):
            try:
                ctypes.CDLL(c, mode=ctypes.RTLD_GLOBAL)
                return
            except OSError:
                continue


def _find_libclang():
    p = os.environ.get('YOS_LIBCLANG_SO')
    if p and os.path.isfile(p):
        return p
    import glob
    # The PyPI `libclang` wheel ships its own libclang shared lib
    # bundled at site-packages/clang/native/. Use that first — it's
    # what `uv sync` / `pip install libclang` puts in place and it
    # works cross-platform (Linux .so, macOS .dylib, Windows .dll).
    try:
        import clang.native as _cn
        nat = Path(_cn.__file__).parent
        for cand in ('libclang.dll', 'libclang.so', 'libclang.so.1',
                     'libclang.dylib'):
            f = nat / cand
            if f.is_file():
                return str(f)
    except Exception:
        pass
    # Order matters. Prefer Nix builds because they bundle their own
    # libstdc++/libffi/libLLVM via embedded RPATH, so loading just
    # works on dev shells where the host /usr/lib has libffi.so.8 but
    # libclang.so requires libffi.so.7. /usr/lib is the fallback.
    for pat in (
        '/nix/store/*-clang-*-lib/lib/libclang.so.1',
        '/nix/store/*-clang-*-lib/lib/libclang.so',
        '/usr/lib/llvm-*/lib/libclang.so.1',
        '/usr/lib/x86_64-linux-gnu/libclang*.so*',
    ):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]   # newest by lex order
    return None

_preload_libstdcxx()
_so = _find_libclang()
if _so:
    clang.cindex.Config.set_library_file(_so)


# ─── Source-header tagging ───────────────────────────────────────────

def _short_header(path_str: str, root_markers: list[str]) -> str:
    """Trim an absolute path back to the suffix after one of the
    `root_markers` (e.g. 'usr/include/' or 'snapshot/include/').
    Returns '' for paths outside any marker (clang builtins, our
    synthetic top file)."""
    if not path_str:
        return ''
    # Normalise Windows backslashes — the root_markers are written
    # with forward slashes and `path_str` on Windows comes back with
    # backslashes from libclang.
    norm = path_str.replace('\\', '/')
    for marker in root_markers:
        i = norm.find(marker)
        if i >= 0:
            return norm[i + len(marker):]
    return ''


# ─── Type registry ───────────────────────────────────────────────────

class TypeRegistry:
    """Flat, uid-keyed type table. Dedups by canonical (spelling, kind)
    so two clang_types that resolve to the same canonical layout share
    one uid. Recursive structures are safe via placeholder-on-entry."""

    def __init__(self, prefix: str = 't'):
        self.prefix = prefix
        self.types: dict[str, dict] = {}
        self._counter = 0
        self._key_to_uid: dict[tuple, str] = {}

    def _new_uid(self) -> str:
        self._counter += 1
        return f'{self.prefix}_{self._counter}'

    def _key(self, ct):
        c = ct.get_canonical()
        return (c.spelling, int(c.kind.value))

    def register(self, ct, root_markers: list[str]) -> str:
        # Newer libclang (Apple's clang 16 ships kind 32 = _Float16; clang
        # 17 ships kind 39 = BFloat16; kind 40 = Ibm128) exposes TypeKind
        # ids the python clang bindings in the venv don't know about yet
        # — accessing ct.kind then raises ValueError. Treat any unknown
        # kind as an opaque byte blob of the right size; layout-driven
        # comparison still works on it. We just lose the type name.
        try:
            kind = ct.kind
        except ValueError:
            uid = self._new_uid()
            self.types[uid] = {
                'kind': 'opaque',
                'size': ct.get_size(),
                'is_const': ct.is_const_qualified(),
                'header': '',
            }
            return uid

        # Forward through elaborated wrappers without allocating a uid.
        if kind == TypeKind.ELABORATED:
            return self.register(ct.get_named_type(), root_markers)

        # Forward typedef → canonical. Our dedup is keyed on canonical
        # type, so a typedef wrapper would dedup to its own uid and
        # produce `typedef.type_uid == typedef's_own_uid` self-loops.
        # For layout-driven comparison the typedef *name* is
        # irrelevant; the underlying canonical layout is what matters.
        if kind == TypeKind.TYPEDEF:
            return self.register(ct.get_canonical(), root_markers)

        key = self._key(ct)
        if key in self._key_to_uid:
            return self._key_to_uid[key]

        uid = self._new_uid()
        self._key_to_uid[key] = uid
        self.types[uid] = None  # placeholder for cycle safety

        is_const = ct.is_const_qualified()
        info: dict | None = None

        if kind == TypeKind.POINTER:
            pointee = ct.get_pointee()
            info = {
                'kind': 'pointer',
                'is_const': is_const,
                'pointee_uid': self.register(pointee, root_markers),
                'pointee_is_const': pointee.is_const_qualified(),
            }
        elif kind == TypeKind.RECORD:
            decl = ct.get_declaration()
            is_union = decl.kind == CursorKind.UNION_DECL
            name = decl.spelling
            src = _short_header(
                str(decl.location.file) if decl.location and decl.location.file else '',
                root_markers,
            )
            if not name or name.startswith('('):
                info = {'kind': 'opaque', 'size': ct.get_size(),
                        'is_const': is_const, 'header': src}
            else:
                info = {
                    'kind': 'union' if is_union else 'struct',
                    'name': name,
                    'size': ct.get_size(),
                    'alignment': ct.get_align(),
                    'is_const': is_const,
                    'header': src,
                }
                self.types[uid] = info  # publish before recursing into fields
                fields_out = []
                for ch in decl.get_children():
                    if ch.kind == CursorKind.FIELD_DECL:
                        off = ct.get_offset(ch.spelling)
                        if off < 0:
                            continue
                        fields_out.append({
                            'name': ch.spelling,
                            'offset': off // 8,
                            'size': ch.type.get_size(),
                            'type_uid': self.register(ch.type, root_markers),
                        })
                info['fields'] = fields_out
                return uid  # already published with fields
        # TYPEDEF is handled at the top of register() — forwarded to
        # canonical so we never store typedef wrappers.
        elif kind == TypeKind.CONSTANTARRAY:
            info = {
                'kind': 'array',
                'count': ct.element_count,
                'is_const': is_const,
                'element_uid': self.register(ct.element_type, root_markers),
            }
        elif kind == TypeKind.INCOMPLETEARRAY:
            info = {
                'kind': 'flex_array',
                'is_const': is_const,
                'element_uid': self.register(ct.element_type, root_markers),
            }
        elif kind == TypeKind.VOID:
            info = {'kind': 'void', 'is_const': is_const}
        elif kind == TypeKind.ENUM:
            decl = ct.get_declaration()
            src = _short_header(
                str(decl.location.file) if decl.location and decl.location.file else '',
                root_markers,
            )
            info = {
                'kind': 'enum',
                'name': decl.spelling or None,
                'size': ct.get_size(),
                'is_const': is_const,
                'header': src,
            }
        elif kind == TypeKind.FUNCTIONPROTO:
            info = {
                'kind': 'function',
                'is_const': is_const,
                'return_uid': self.register(ct.get_result(), root_markers),
                'arg_uids': [self.register(a, root_markers)
                             for a in ct.argument_types()],
            }
        elif kind in (
            TypeKind.BOOL,
            TypeKind.CHAR_S, TypeKind.CHAR_U, TypeKind.SCHAR, TypeKind.UCHAR,
            TypeKind.SHORT, TypeKind.USHORT,
            TypeKind.INT,   TypeKind.UINT,
            TypeKind.LONG,  TypeKind.ULONG,
            TypeKind.LONGLONG, TypeKind.ULONGLONG,
            TypeKind.FLOAT, TypeKind.DOUBLE, TypeKind.LONGDOUBLE,
            TypeKind.WCHAR,
        ):
            info = {
                'kind': 'builtin',
                'name': ct.spelling,
                'size': ct.get_size(),
                'is_const': is_const,
            }
        else:
            info = {
                'kind': str(kind).rsplit('.', 1)[-1].lower(),
                'spelling': ct.spelling,
                'is_const': is_const,
            }

        self.types[uid] = info
        return uid


# ─── Macro RHS evaluation ────────────────────────────────────────────

def _strip_int_suffix(s: str) -> str:
    i = len(s)
    while i > 0 and s[i - 1] in 'uUlL':
        i -= 1
    return s[:i]


def _parse_int_literal(s: str):
    try:
        return int(_strip_int_suffix(s), 0)
    except ValueError:
        return None


def _try_eval_macro_to_int(cursor):
    """Best-effort evaluation of a MacroDefinition to an integer.
    Handles integer literals (incl. hex/octal, U/L suffix), parens,
    unary - / ~, shifts, and bitwise/arithmetic chains of literals.
    Macro-references-other-macros are NOT chased here — the consumer
    can resolve later from the constants table."""
    toks = [t.spelling for t in cursor.get_tokens()]
    if len(toks) < 2:
        return None
    rhs = toks[1:]

    while len(rhs) >= 2 and rhs[0] == '(' and rhs[-1] == ')':
        rhs = rhs[1:-1]

    if not rhs:
        return None

    if len(rhs) == 1:
        return _parse_int_literal(rhs[0])

    if len(rhs) == 2 and rhs[0] in ('-', '~'):
        v = _parse_int_literal(rhs[1])
        return None if v is None else (-v if rhs[0] == '-' else (~v) & 0xFFFFFFFFFFFFFFFF)

    if len(rhs) == 3 and rhs[1] in ('<<', '>>'):
        a = _parse_int_literal(rhs[0])
        b = _parse_int_literal(rhs[2])
        if a is None or b is None:
            return None
        return (a << b) if rhs[1] == '<<' else (a >> b)

    if all(t in ('|', '&', '^', '+', '-') or _parse_int_literal(t) is not None
           for t in rhs):
        acc = _parse_int_literal(rhs[0])
        if acc is None:
            return None
        i = 1
        while i + 1 < len(rhs):
            op = rhs[i]
            v = _parse_int_literal(rhs[i + 1])
            if v is None:
                return None
            if   op == '|': acc |= v
            elif op == '&': acc &= v
            elif op == '^': acc ^= v
            elif op == '+': acc += v
            elif op == '-': acc -= v
            else: return None
            i += 2
        return acc

    return None


# ─── Top-level extraction ────────────────────────────────────────────

@dataclass
class ExtractInputs:
    headers: list[str]                # entry headers to #include
    cflags:  list[str]                # passed straight to clang
    root_markers: list[str]           # path-prefix markers for header tagging


def _make_top_file(headers: list[str]) -> str:
    return ''.join(f'#include <{h}>\n' for h in headers)


def extract(inputs: ExtractInputs) -> dict:
    """Run libclang once over a synthetic translation unit that pulls
    in every header in `inputs.headers`. Return the api.yaml dict."""
    src = _make_top_file(inputs.headers)
    with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
        f.write(src)
        tmp = f.name
    try:
        index = clang.cindex.Index.create()
        opts = clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        tu = index.parse(tmp, args=inputs.cflags, options=opts)
        errors = [d for d in tu.diagnostics
                  if d.severity >= clang.cindex.Diagnostic.Error]
        for d in errors:
            print(f'  clang: {d}', file=sys.stderr)
        if errors:
            print(f'[api-extract] {len(errors)} clang error(s) — refusing to '
                  f'emit a partial yaml. After hitting -ferror-limit clang '
                  f'stops parsing, so any header listed AFTER the failure '
                  f'point silently drops every declaration it would have '
                  f'contributed. That used to produce a green build with '
                  f'half the bridges missing. Fix the failing header (most '
                  f'commonly: a kernel-internal arch header pulled in by '
                  f'--enum walking machine/ or x86/ — see _ARCH_ASM_SKIP).',
                  file=sys.stderr)
            sys.exit(1)

        types     = TypeRegistry('t')
        functions: dict[str, dict] = {}
        constants: dict[str, dict] = {}
        enums:     list[dict]      = []

        # darwin SDK ships headers we never want to bridge — kernel
        # internals (`sys/dtrace*`, `sys/kern_*`, `kern/*`), Mach IPC,
        # IOKit, dyld internals. They satisfy `--restrict-to sys/`
        # but their decls aren't libc and including them in the
        # bridge fails (they need `kern/kalloc.h` etc.). Drop here.
        darwin_skip_prefixes = (
            'sys/dtrace', 'sys/kern_', 'sys/_sigtramp_',
            'kern/', 'mach/', 'IOKit/', 'dyld/', 'xpc/',
            'objc/',
            # darwin's <netinet6/in6.h> errors out on direct inclusion
            # (#error "do not include netinet6/in6.h directly"); decls
            # we'd want come transitively through netinet/in.h anyway.
            'netinet6/',
        )
        # Functions that exist as decls in darwin SDK headers but
        # whose symbols aren't actually exported by libSystem (or
        # exist only as deprecation tombstones with absurdly long
        # names like `getdirentries_is_not_available_when_…`). The
        # bridge would compile but the host yos binary fails to link.
        darwin_skip_function_names = {
            'acl_valid_link_np',
            # Apple aliases getdirentries to a long deprecation tombstone
            # symbol via `__asm("_getdirentries_is_not_available…")` when
            # 64-bit inodes is in effect (the modern default). Calling
            # `getdirentries(...)` looks fine at compile time but fails
            # to link. The replacement on darwin is `getdirentries$INODE64`
            # (which doesn't exist) or `readdir`. Just drop the bridge
            # — guests that need it should fall back to readdir() through
            # the regular yos bridge surface instead.
            'getdirentries',
            'profil',
            'unwhiteout',
        }

        for cur in tu.cursor.walk_preorder():
            if cur.location is None or cur.location.file is None:
                continue
            src_tag = _short_header(str(cur.location.file), inputs.root_markers)
            if not src_tag:
                continue
            if any(src_tag.startswith(p) for p in darwin_skip_prefixes):
                continue

            k = cur.kind
            if k == CursorKind.FUNCTION_DECL:
                # Skip static/inline definitions; we want the API surface.
                if cur.is_definition() and cur.storage_class.value not in (0, 2):
                    # storage_class: 0=NONE, 2=EXTERN.
                    continue
                if cur.spelling in functions:
                    continue
                if cur.spelling in darwin_skip_function_names:
                    continue
                args_out = []
                for a in cur.get_arguments():
                    args_out.append({
                        'name': a.spelling or '',
                        'type_uid': types.register(a.type, inputs.root_markers),
                    })
                # is_function_variadic() asserts the type is a function
                # prototype; for K&R-style decls or already-resolved
                # typedefs it isn't, and clang aborts. Guard.
                try:
                    variadic = cur.type.kind == TypeKind.FUNCTIONPROTO and \
                               cur.type.is_function_variadic()
                except (AssertionError, AttributeError):
                    variadic = False
                functions[cur.spelling] = {
                    'header': src_tag,
                    'ret':    types.register(cur.result_type, inputs.root_markers),
                    'args':   args_out,
                    'variadic': variadic,
                }
            elif k == CursorKind.STRUCT_DECL or k == CursorKind.UNION_DECL:
                if cur.spelling and cur.is_definition():
                    types.register(cur.type, inputs.root_markers)
            elif k == CursorKind.TYPEDEF_DECL:
                types.register(cur.type, inputs.root_markers)
            elif k == CursorKind.MACRO_DEFINITION:
                name = cur.spelling
                if not name or (name.startswith('_') and name.endswith('_H')):
                    continue
                v = _try_eval_macro_to_int(cur)
                if v is None or name in constants:
                    continue
                constants[name] = {'value': v, 'header': src_tag}
            elif k == CursorKind.ENUM_DECL:
                members = []
                for ch in cur.get_children():
                    if ch.kind == CursorKind.ENUM_CONSTANT_DECL:
                        members.append({'name': ch.spelling, 'value': ch.enum_value})
                if members:
                    enums.append({
                        'name':    cur.spelling or None,
                        'header':  src_tag,
                        'members': members,
                    })
                # also register the enum's type so functions referencing it resolve
                if cur.is_definition():
                    types.register(cur.type, inputs.root_markers)

        return {
            'functions': functions,
            'types':     types.types,
            'constants': constants,
            'enums':     enums,
        }
    finally:
        os.unlink(tmp)


# ─── CLI ─────────────────────────────────────────────────────────────

def _gather_headers(include_dir: Path, restrict_to: list[str] | None) -> list[str]:
    """Walk an include directory, return relative .h paths suitable for
    `#include <...>`. Optionally restrict to listed prefixes (e.g.
    ['sys/', 'stdio.h']) so we don't pull driver UAPI on some hosts."""
    out = []
    for h in sorted(include_dir.rglob('*.h')):
        rel = h.relative_to(include_dir).as_posix()
        if restrict_to is not None:
            if not any(rel == p or rel.startswith(p) for p in restrict_to):
                continue
        out.append(rel)
    return out


def main() -> int:
    p = argparse.ArgumentParser(
        description='Walk a libc header tree and emit api.yaml.')
    p.add_argument('--name', required=True,
                   help='label for this extraction, e.g. "guest-i386-freebsd" or "host"')
    p.add_argument('--include-dir', action='append', default=[],
                   help='one or more sysroot include dirs (passed as -isystem to clang)')
    p.add_argument('--include-root', action='append', default=[],
                   help='auto-discover NN-* subdirs (host-libc/snapshot.py output) '
                        'inside this dir and add each as an include path. Repeatable. '
                        'Lets the meson rule stay portable across hosts where the '
                        'snapshot dir basenames differ.')
    p.add_argument('--no-stdinc', action='store_true',
                   help='pass -nostdinc (use ONLY the --include-dirs given). '
                        'For freestanding-sysroot extraction.')
    p.add_argument('--define', action='append', default=[],
                   help='extra -Dfoo=bar passed to clang')
    p.add_argument('--clang-arg', action='append', default=[],
                   help='extra flag passed straight to clang (e.g. '
                        '-fms-extensions for MSVC-flavoured Windows SDK '
                        'headers). Repeatable.')
    p.add_argument('--target', default=None,
                   help='clang -target triple (e.g. wasm32-unknown-unknown, '
                        'i386-unknown-freebsd, x86_64-linux-gnu)')
    p.add_argument('--root-marker', action='append', default=[],
                   help='path-prefix marker for header tagging; the suffix '
                        'after this marker is used as the source-header label.')
    p.add_argument('--enum', action='store_true',
                   help='enumerate every .h under each include dir as an entry; '
                        'otherwise pass --header explicitly.')
    p.add_argument('--header', action='append', default=[],
                   help='explicit entry header (e.g. stdio.h, sys/stat.h). '
                        'Repeatable. Mutually exclusive with --enum.')
    p.add_argument('--restrict-to', action='append', default=None,
                   help='when --enum is set, restrict to these prefixes '
                        '(e.g. sys/ stdio.h). Repeatable.')
    p.add_argument('--out', required=True,
                   help='output yaml file')
    args = p.parse_args()

    include_dirs = [Path(d) for d in args.include_dir]
    # Walk each --include-root and pick up NN-* subdirs in numeric order.
    # Ignores Frameworks/ entries (darwin) — those are -F not -I.
    # Also seeds --root-marker with each subdir's basename so the
    # source-header labels emitted by extract are stable cross-host
    # (e.g. `string.h`, not `09-include/string.h`).
    import re
    for root in args.include_root:
        root_p = Path(root)
        if not root_p.is_dir():
            continue
        subs = sorted(p for p in root_p.iterdir()
                      if p.is_dir() and re.match(r'^\d+-', p.name)
                      and 'Frameworks' not in p.name)
        include_dirs.extend(subs)
        # Prepend (more specific) so they match before the generic
        # `include/` marker the meson rule already passes — otherwise
        # `find('include/')` wins and the suffix is `NN-include/x.h`.
        for sub in subs:
            args.root_marker.insert(0, sub.name + '/')
    cflags = ['-fsyntax-only']
    if args.no_stdinc:
        cflags.append('-nostdinc')
    if args.target:
        cflags += ['-target', args.target]
    for d in include_dirs:
        cflags += ['-isystem', str(d)]
    cflags += [f'-D{d}' for d in args.define]
    cflags += list(args.clang_arg)

    if args.enum:
        if args.header:
            p.error('--enum and --header are mutually exclusive')
        headers: list[str] = []
        for d in include_dirs:
            headers += _gather_headers(d, args.restrict_to)
        # de-dup while preserving order
        seen = set(); uniq = []
        for h in headers:
            if h not in seen:
                seen.add(h); uniq.append(h)
        headers = uniq
    else:
        headers = args.header
    if not headers:
        p.error('no headers — pass --enum or one or more --header')

    inputs = ExtractInputs(
        headers=headers,
        cflags=cflags,
        root_markers=args.root_marker or ['usr/include/'],
    )

    print(f'[api-extract] {args.name}: {len(headers)} entry headers, '
          f'{len(include_dirs)} include dirs', file=sys.stderr)

    data = extract(inputs)
    data['_meta'] = {
        'name':    args.name,
        'target':  args.target or '',
        'cflags':  cflags,
        'headers': headers,
    }

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, 'w') as f:
        yaml.dump(data, f, sort_keys=False, default_flow_style=False, width=120)

    print(f'[api-extract] {args.name}: '
          f'{len(data["functions"])} functions, '
          f'{len(data["types"])} types, '
          f'{len(data["constants"])} constants, '
          f'{len(data["enums"])} enums '
          f'-> {args.out}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
