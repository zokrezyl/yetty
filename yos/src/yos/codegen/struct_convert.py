#!/usr/bin/env python3
"""Generate host↔wasm32 struct converters for the yos bridge.

For every host/guest struct pair listed in TRACKED_STRUCTS this emits
two C functions per side:

  cv_<struct>_h2w(uint8_t *w, const struct <name> *h);
  cv_<struct>_w2h(struct <name> *h, const uint8_t *w);

The wasm side is addressed by raw byte offset (the layouts don't match;
fields can be at different offsets, sizes, or absent on one side).
The host side is addressed by C field name through a real `struct
<name>` declaration the C compiler resolves via host headers.

Field matching is by NAME. For each guest field:
  - If a host field of the same name exists with a scalar type, the
    converter copies and casts (widen/narrow as needed).
  - If both sides are nested structs we recognise (timespec inside
    stat, …), the converter calls cv_<inner>_h2w / w2h on the slice.
  - If both sides are byte arrays of comparable length, memcpy with
    a length clamp.
  - Pointer fields and unrecognised types: zero out (h2w) or skip
    (w2h). Most cases that matter (errno, fd, time) are scalar.

The generator also extracts the LIST of host-side struct names that
need a `#include` from the host headers — we emit those into the .c
prelude so `struct stat`, `struct tm`, … are real types we can write
through.

Outputs:
  yos_struct_convert.h   — converter prototypes + cv_<name>_SZ macros
  yos_struct_convert.c   — converter bodies
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml


# Structs we generate converters for. Anything not in this set falls
# back to bridge.py's existing "TODO stub" path. Keeping the set
# explicit avoids generating dead converters for opaque host types
# (FILE, locale_t, jmp_buf, …) where field-by-field copy makes no
# sense anyway.
TRACKED_STRUCTS = (
    'tm',
    'timespec',
    'timeval',
    'itimerspec',
    'itimerval',
    'stat',
    'statfs',
    'rlimit',
    'rusage',
    'sockaddr',
    'sockaddr_in',
    'sockaddr_in6',
    'sockaddr_un',
    'sockaddr_storage',
    'iovec',
    'msghdr',
    'cmsghdr',
    'dirent',
    'pollfd',
    'flock',
    'utsname',
)

# Host headers we include so `struct <name>` is a real type. Keep the
# list tight; one header per group of related structs.
HOST_INCLUDES = (
    '<sys/types.h>',
    '<sys/stat.h>',
    '<sys/statvfs.h>',
    '<sys/time.h>',
    '<sys/resource.h>',
    '<sys/socket.h>',
    '<sys/un.h>',
    '<netinet/in.h>',
    '<sys/uio.h>',
    '<dirent.h>',
    '<poll.h>',
    '<fcntl.h>',
    '<sys/utsname.h>',
    '<time.h>',
    '<string.h>',
)


def _resolve(types: dict, t: dict) -> dict | None:
    """Strip typedefs to the underlying definition."""
    seen = set()
    while t is not None and t.get('kind') == 'typedef':
        uid = t.get('type_uid')
        if not uid or uid in seen:
            break
        seen.add(uid)
        t = types.get(uid)
    return t


def _find_struct(api: dict, name: str):
    """Return the struct typeinfo matching `name`, or None."""
    for uid, t in api.get('types', {}).items():
        if t.get('kind') == 'struct' and t.get('name') == name:
            return t
    return None


def _is_scalar(t: dict) -> bool:
    return t and t.get('kind') in ('builtin', 'enum')


def _is_struct(t: dict) -> bool:
    return t and t.get('kind') == 'struct'


def _is_array(t: dict) -> bool:
    return t and t.get('kind') == 'array'


def _scalar_C_type(field_size: int, is_signed: bool = True) -> str:
    """Pick a fixed-width C type for a scalar of given size."""
    bits = field_size * 8
    pre = 'int' if is_signed else 'uint'
    if bits in (8, 16, 32, 64):
        return f'{pre}{bits}_t'
    return 'int32_t'  # fallback; shouldn't happen for sane structs


def _emit_h2w(name: str, gstruct: dict, hstruct: dict,
              gtypes: dict, htypes: dict) -> str:
    """Emit body of cv_<name>_h2w. Reads host struct by field name,
    writes wasm side at raw byte offsets."""
    lines = [
        f'void cv_{name}_h2w(uint8_t *w, const struct {name} *h)',
        '{',
        f'    if (yos_cv_trace_enabled())',
        f'        yos_cv_trace_h2w("{name}", w, (const void *)h, {gstruct.get("size", 0)});',
        f'    /* Zero the wasm slot first so any guest-only field that',
        f'     * has no host counterpart reads as 0 instead of stale data. */',
        f'    memset(w, 0, {gstruct.get("size", 0)});',
    ]
    h_by_name = {f['name']: f for f in hstruct.get('fields', [])}
    for gf in gstruct.get('fields', []):
        gname  = gf['name']
        goff   = gf['offset']
        gsz    = gf['size']
        gtype  = _resolve(gtypes, gtypes.get(gf['type_uid']))
        if gtype is None:
            lines.append(f'    /* {gname}: guest type unresolved; leave 0 */')
            continue
        hf = h_by_name.get(gname)
        if hf is None:
            lines.append(f'    /* {gname}: no host field; leave 0 */')
            continue

        # Scalar field — cast and copy.
        if _is_scalar(gtype):
            ctype = _scalar_C_type(gsz, is_signed=gtype.get('signed', True))
            lines.append(f'    *({ctype} *)(w + {goff}) = ({ctype})h->{gname};')
            continue

        # Nested struct field — recurse if we have a converter for it.
        if _is_struct(gtype) and gtype.get('name') in TRACKED_STRUCTS:
            inner = gtype.get('name')
            lines.append(f'    cv_{inner}_h2w(w + {goff}, &h->{gname});')
            continue

        # Byte array — memcpy clamped to the smaller side.
        if _is_array(gtype):
            elem = _resolve(gtypes, gtypes.get(gtype.get('element_uid', '')))
            if elem and elem.get('kind') == 'builtin' and elem.get('size', 0) == 1:
                # char/uint8 array — copy bytes, clamped to host's array.
                hsz = hf.get('size', gsz)
                cap = min(gsz, hsz)
                lines.append(f'    memcpy(w + {goff}, h->{gname}, {cap});')
                continue

        # Pointer field — host has a real address, wasm wants an offset.
        # We can't conjure a wasm offset for an arbitrary host pointer,
        # so we just write 0 (NULL). Most callers tolerate (e.g., tm_zone).
        if gtype.get('kind') == 'pointer':
            lines.append(f'    /* {gname}: host pointer, wasm needs offset; skip */')
            continue

        lines.append(f'    /* {gname}: kind={gtype.get("kind")} unsupported; skip */')

    lines.append('}')
    return '\n'.join(lines)


def _emit_w2h(name: str, gstruct: dict, hstruct: dict,
              gtypes: dict, htypes: dict) -> str:
    """Emit body of cv_<name>_w2h. Reads wasm side at raw byte offsets,
    writes host struct by field name."""
    lines = [
        f'void cv_{name}_w2h(struct {name} *h, const uint8_t *w)',
        '{',
        f'    if (yos_cv_trace_enabled())',
        f'        yos_cv_trace_w2h("{name}", (const void *)h, w, {gstruct.get("size", 0)});',
        f'    memset(h, 0, sizeof *h);',
    ]
    h_by_name = {f['name']: f for f in hstruct.get('fields', [])}
    for gf in gstruct.get('fields', []):
        gname  = gf['name']
        goff   = gf['offset']
        gsz    = gf['size']
        gtype  = _resolve(gtypes, gtypes.get(gf['type_uid']))
        if gtype is None:
            continue
        hf = h_by_name.get(gname)
        if hf is None:
            continue

        if _is_scalar(gtype):
            ctype = _scalar_C_type(gsz, is_signed=gtype.get('signed', True))
            # `__typeof__` (gcc/clang extension) works under -std=c11
            # where bare `typeof` doesn't. Cast through it so the
            # field's actual host type silences narrowing warnings.
            lines.append(
                f'    h->{gname} = (__typeof__(h->{gname}))(*({ctype} *)(w + {goff}));'
            )
            continue

        if _is_struct(gtype) and gtype.get('name') in TRACKED_STRUCTS:
            inner = gtype.get('name')
            lines.append(f'    cv_{inner}_w2h(&h->{gname}, w + {goff});')
            continue

        if _is_array(gtype):
            elem = _resolve(gtypes, gtypes.get(gtype.get('element_uid', '')))
            if elem and elem.get('kind') == 'builtin' and elem.get('size', 0) == 1:
                hsz = hf.get('size', gsz)
                cap = min(gsz, hsz)
                lines.append(f'    memcpy(h->{gname}, w + {goff}, {cap});')
                continue

        # Pointer fields: w→h direction. The wasm offset can be
        # translated to a host pointer if we know the ctx — but
        # converters are ctx-less by design. Skip (host pointer stays
        # NULL/zeroed).

    lines.append('}')
    return '\n'.join(lines)


def emit(guest_api: dict, host_api: dict) -> tuple[str, str, list[str]]:
    """Return (header, source, generated_struct_names)."""
    gtypes = guest_api.get('types', {})
    htypes = host_api.get('types',  {})

    decls    = []
    sz_macros = []
    bodies   = []
    generated = []

    for name in TRACKED_STRUCTS:
        gs = _find_struct(guest_api, name)
        hs = _find_struct(host_api,  name)
        if not gs or not hs:
            # Either side missing — skip silently. e.g. some structs
            # like sockaddr_un may not be present in restricted yaml.
            continue
        decls.append(f'void cv_{name}_h2w(uint8_t *w, const struct {name} *h);')
        decls.append(f'void cv_{name}_w2h(struct {name} *h, const uint8_t *w);')
        sz_macros.append(
            f'#define CV_{name.upper()}_GUEST_SZ {gs.get("size", 0)}'
        )
        bodies.append(_emit_h2w(name, gs, hs, gtypes, htypes))
        bodies.append(_emit_w2h(name, gs, hs, gtypes, htypes))
        generated.append(name)

    h_src = (
        '/* Auto-generated by build-tools/api-generate/struct_convert.py.\n'
        ' * DO NOT EDIT — re-run the codegen instead.\n'
        ' *\n'
        ' * One pair of converters per host/guest struct pair we know how to\n'
        ' * walk field-by-field. The wasm side is addressed by raw byte offset\n'
        ' * (the per-field offsets / sizes / orders generally differ from the\n'
        ' * host); the host side is a real `struct <name> *` so the C\n'
        ' * compiler can read/write fields by name. */\n'
        '#ifndef YOS_STRUCT_CONVERT_H\n'
        '#define YOS_STRUCT_CONVERT_H\n\n'
        '#include <stdint.h>\n\n'
        + '\n'.join(f'#include {h}' for h in HOST_INCLUDES)
        + '\n'
        '/* struct statfs lives in different headers per OS. */\n'
        '#ifdef __linux__\n'
        '#  include <sys/vfs.h>\n'
        '#else\n'
        '#  include <sys/mount.h>\n'
        '#endif\n\n'
        '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
        '/* Convert-trace hooks — defined in struct_convert_trace.c.\n'
        ' * Enabled at runtime by YOS_CONVERT_TRACE=1; off by default.\n'
        ' * yos_cv_trace_enabled() is a one-shot env-var probe so the\n'
        ' * `if` in every converter is a single global-memory load when\n'
        ' * tracing is off. */\n'
        'int  yos_cv_trace_enabled(void);\n'
        'void yos_cv_trace_h2w(const char *name, const void *w_off,\n'
        '                     const void *h_ptr, unsigned wasm_size);\n'
        'void yos_cv_trace_w2h(const char *name, const void *h_ptr,\n'
        '                     const void *w_off, unsigned wasm_size);\n\n'
        + '\n'.join(decls)
        + '\n\n'
        + '\n'.join(sz_macros)
        + '\n\n'
        '#ifdef __cplusplus\n}\n#endif\n\n'
        '#endif /* YOS_STRUCT_CONVERT_H */\n'
    )

    c_src = (
        '/* Auto-generated by build-tools/api-generate/struct_convert.py. */\n'
        '#define _GNU_SOURCE\n'
        '#include "yos_struct_convert.h"\n'
        '#include <string.h>\n\n'
        + '\n\n'.join(bodies)
        + '\n'
    )

    return h_src, c_src, generated


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--guest-api', required=True, type=Path)
    p.add_argument('--host-api',  required=True, type=Path)
    p.add_argument('--out-dir',   required=True, type=Path)
    args = p.parse_args()

    with args.guest_api.open() as f: g = yaml.safe_load(f)
    with args.host_api.open()  as f: h = yaml.safe_load(f)

    h_src, c_src, gen = emit(g, h)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / 'yos_struct_convert.h').write_text(h_src)
    (args.out_dir / 'yos_struct_convert.c').write_text(c_src)
    print(f'[struct_convert] generated {len(gen)} struct pairs: {", ".join(gen)}',
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
