#!/usr/bin/env python3
"""Emit per-function bridge wrappers from analyse-report.yaml.

Each bridge wrapper is a host-side C function that:

  1. Receives the wasm-ABI representation of the guest's call args
     (i32 for int/long/pointer, i64 for long long, etc.)
  2. Translates any wasm-pointer offset to a host pointer
     (`ctx->memory + offset`)
  3. Widens/narrows scalars where guest and host C types differ in
     width (i.e. mechanical deltas)
  4. Calls the host libc function
  5. Maps return value back to the wasm ABI; remaps errno via
     yos_remap_errno_h2g when the host signalled an error

Scope of THIS iteration:
  - Emit real bridges for `compatible` functions whose arg types are
    "simple" (scalar / void* / pointer-to-builtin / pointer-to-void).
    These cover most basic POSIX surface (read, write, close, dup,
    chmod, getpid, …).
  - Emit `// TODO` stub for everything else (compatible-but-complex,
    mechanical, needs_policy). They return -ENOSYS at runtime so the
    guest gets a clean error if it hits one.

A full converter for `mechanical` (per-field struct walking) is a
follow-on — needs the type renderer to know how to lay out guest
structs in linear memory and walk them field by field.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml


# ─── *at-family functions ────────────────────────────────────────────
#
# These take a `dirfd` as their first int argument, conventionally
# `AT_FDCWD` to mean "interpret the path relative to cwd". Two reasons
# the auto-bridge needs special handling for them:
#
#  - **AT_FDCWD value mismatch.** FreeBSD/Linux use AT_FDCWD = -100;
#    darwin uses -2. The wasm guest is FreeBSD-shaped so it always
#    passes -100; if we forward that straight to a darwin host fstatat
#    it reads -100 as "fd #-100" → EBADF. yos_xlate_dfd (impl/io/io.c)
#    swaps -100 → host AT_FDCWD.
#  - **fd_map translation.** Regular wasm fds are slot numbers into
#    ctx->fd_map and must be looked up to get the host fd. Happens to
#    match on Linux when host_fd==wasm_fd by luck; doesn't on darwin.
#
# Keep this list in sync with the POSIX *at family (man 2 *at).
_AT_FAMILY = frozenset({
    'faccessat', 'fchmodat', 'fchownat', 'fstatat', 'futimesat',
    'linkat', 'mkdirat', 'mkfifoat', 'mknodat', 'openat', 'readlinkat',
    'renameat', 'renameat2', 'symlinkat', 'unlinkat', 'utimensat',
    'name_to_handle_at', 'execveat', 'statx', 'getdents64_at',
})

# *at family functions whose flag arg uses the AT_* constant set
# (AT_SYMLINK_NOFOLLOW, AT_REMOVEDIR, AT_EMPTY_PATH, AT_EACCESS …).
# FreeBSD and Linux/darwin number these differently — e.g. FreeBSD's
# AT_SYMLINK_NOFOLLOW is 0x0200 but Linux's is 0x0100 (its 0x0200 is
# AT_REMOVEDIR), so forwarding the raw value silently corrupts every
# *at call (typical failure mode: EINVAL from fstatat because the
# guest's AT_SYMLINK_NOFOLLOW lands on the host as AT_REMOVEDIR, a
# flag fstatat rejects). Maps fn name → positional index of the flag
# arg in the FreeBSD signature.
#
# Excluded: openat (takes O_* flags, not AT_*), renameat (no flag),
# renameat2 (RENAME_* flags), mkdirat/mkfifoat/mknodat (no flag),
# readlinkat/symlinkat (no flag), futimesat (no flag), execveat (AT_*
# subset overlaps with EMPTY_PATH; handled via AT_FAMILY translator
# already for the dfd, the flag is rare and intentionally untouched
# pending its own delta). statx/getdents64_at: not yet routed through
# auto-bridge here; statx is hand-bridged with its own host path.
_AT_FLAG_ARG = {
    'faccessat': 3, 'fchmodat': 3, 'fchownat': 4, 'fstatat': 3,
    'linkat': 4, 'unlinkat': 2, 'utimensat': 3,
    'name_to_handle_at': 4,
}

# Functions where one or more `const char *` arguments are PATHS
# (filesystem paths the host must resolve against the per-process
# cwd). yos's pthread-per-process fork model can't keep a per-
# process host cwd, so the runtime fakes it: `chdir()` only updates
# ctx->cwd, and every relative path passed in is wrapped at the
# bridge boundary with `yos_path_resolve(ctx, path)` which prepends
# ctx->cwd. Without this, fork+chdir(parent)→fork+chdir(child)
# patterns (runsv, find, fts, …) race on the shared host cwd and
# pick up the wrong base directory.
#
# Map: fn-name → tuple of arg indices that are paths. We use a
# per-fn allowlist instead of "wrap every const char *" because
# many non-path string args exist (setenv/getenv name, signal
# desc, strerror, …) and wrapping THOSE would mangle them.
#
# *at family is intentionally NOT here — its path resolves against
# the dfd (already handled via yos_xlate_dfd at arg 0), not
# ctx->cwd. The codegen leaves those alone.
_PATH_ARG_FNS = {
    # Single-path ops.
    'stat': (0,), 'lstat': (0,), 'access': (0,), 'eaccess': (0,),
    'mkdir': (0,), 'unlink': (0,), 'rmdir': (0,),
    'chmod': (0,), 'lchmod': (0,), 'chown': (0,), 'lchown': (0,),
    'readlink': (0,), 'truncate': (0,), 'mkfifo': (0,), 'mknod': (0,),
    'pathconf': (0,), 'utimes': (0,), 'utime': (0,), 'futimes': (0,),
    'lutimes': (0,), 'statfs': (0,), 'statvfs': (0,),
    'chroot': (0,),
    # Two-path ops.
    'rename': (0, 1), 'link': (0, 1), 'symlink': (0, 1),
    # exec family — argv[0] is a path. yos has its own hand-written
    # execve/execv/execvp in impl/proc.c (see yos_execvp's PATH
    # search), but if codegen ever surfaces a fallback bridge, it
    # should still resolve.
    'execv': (0,), 'execve': (0,),
    # We DON'T list execvp because the PATH search in
    # yos_execvp already handles bare-name lookup; relative-with-
    # slash names (e.g. "./run") fall back to yos_execve which is
    # hand-bridged + resolves itself.
    # opendir is hand-bridged (dir.c), patched separately.
    # open is hand-bridged (vfs.c), patched separately.
    # chdir is hand-bridged (vfs.c), patched separately.
}


# ─── Type-renderer helpers ───────────────────────────────────────────

def _resolve(t: dict, types: dict) -> dict | None:
    """Walk through typedefs (we already forward at extract time, so
    this mostly handles defensive cases)."""
    seen = set()
    while t is not None and t.get('kind') == 'typedef':
        uid = t.get('type_uid')
        if not uid or uid in seen:
            break
        seen.add(uid)
        t = types.get(uid)
    return t


def _is_void_pointer(t: dict, types: dict) -> bool:
    if not t or t.get('kind') != 'pointer':
        return False
    pointee = _resolve(types.get(t.get('pointee_uid')), types)
    return pointee is not None and pointee.get('kind') == 'void'


def _is_const_char_pointer(t: dict, types: dict) -> bool:
    """Detect `const char *` (string args)."""
    if not t or t.get('kind') != 'pointer':
        return False
    pointee = _resolve(types.get(t.get('pointee_uid')), types)
    if not pointee or pointee.get('kind') != 'builtin':
        return False
    name = (pointee.get('name') or '').lower()
    return 'char' in name and t.get('pointee_is_const', False)


def _wasm_type(t: dict, types: dict) -> str:
    """Render a guest type as the wasm-ABI C type the bridge receives.
    Returns None if we can't render it cleanly (caller then falls
    back to a TODO stub)."""
    t = _resolve(t, types)
    if t is None:
        return None
    k = t.get('kind')
    if k == 'void':
        return 'void'
    if k == 'pointer':
        # Every wasm32 pointer is a u32 offset into linear memory.
        return 'uint32_t'
    if k == 'flex_array':
        # `T fds[]` in a function-prototype position decays to `T *` at
        # the call site (C11 6.7.6.3p7). On wasm32 that's the same as
        # any other pointer — a 4-byte linear-memory offset. Treat it
        # as pointer so functions like poll() (whose <poll.h> decl is
        # `int poll(struct pollfd fds[], nfds_t nfds, int timeout)`)
        # can be routed through custom_<area> impls instead of being
        # silently dropped by codegen.
        return 'uint32_t'
    if k == 'builtin':
        size = t.get('size')
        name = (t.get('name') or '')
        # Float types FIRST — they're 4 / 8 bytes too, so the
        # size-based int branches would otherwise swallow them.
        if name == 'float':
            return 'float'
        if name in ('double', 'long double'):
            return 'double'
        if size == 1:
            return 'int8_t' if 'unsigned' not in name and 'char' not in name else 'uint8_t'
        if size == 2:
            return 'uint16_t' if 'unsigned' in name else 'int16_t'
        if size == 4:
            return 'uint32_t' if 'unsigned' in name else 'int32_t'
        if size == 8:
            return 'uint64_t' if 'unsigned' in name else 'int64_t'
        return None
    if k == 'enum':
        return 'int32_t'
    # struct / union by value: not supported in this iteration.
    return None


def _host_type(t: dict, types: dict) -> str | None:
    """Render the host C type as it appears in the libc declaration.
    Used to construct the cast at the host-call site."""
    t = _resolve(t, types)
    if t is None:
        return None
    k = t.get('kind')
    if k == 'void':
        return 'void'
    if k == 'pointer':
        pointee = _resolve(types.get(t.get('pointee_uid')), types)
        if pointee is None:
            return None
        if pointee.get('kind') == 'void':
            return 'void *'
        sub = _host_type(pointee, types)
        if sub is None:
            return None
        return sub + ' *'
    if k == 'builtin':
        # Use the canonical clang spelling — this is what host libc
        # already declared and what we want at the call site.
        return t.get('name') or None
    if k == 'enum':
        return 'int'
    if k in ('struct', 'union'):
        # Struct/union by value isn't bridgeable, but a *pointer* to
        # one is — the recursive caller in the pointer branch needs
        # us to render `struct <name>` so it can append " *".
        n = t.get('name')
        if n:
            return f'{k} {n}'
    return None


def _guest_type(t: dict, types: dict) -> str | None:
    """Render the guest-side C type spelling for the FreeBSD
    declaration in the auto-generated `yos_imports.h`. We use the
    *canonical* spelling (e.g. `int` instead of `pid_t`, `long long`
    instead of `off_t`) — the FreeBSD typedefs themselves are
    already in the FreeBSD headers, and clang merges
    redeclarations canonical-typewise, so canonical spellings are
    enough to attach the import attributes correctly. Returns None
    if the type can't be rendered (struct by value, function-
    pointer, etc.).
    """
    t = _resolve(t, types)
    if t is None:
        return None
    k = t.get('kind')
    if k == 'void':
        return 'void'
    if k == 'pointer':
        pointee = _resolve(types.get(t.get('pointee_uid')), types)
        if pointee is None:
            return None
        # Suppress our `const` qualifier if the pointee already carries
        # one — extract.py records is_const on builtins (so the name
        # may already be the bare type, but the type registry's flag
        # tells us "this is const-qualified"). Either source means
        # "pointer-to-const-T"; emitting both yields the famous
        # `const char const *` warning.
        is_const = bool(t.get('pointee_is_const')) or bool(pointee.get('is_const'))
        prefix = 'const ' if is_const else ''
        if pointee.get('kind') == 'void':
            return f'{prefix}void *'
        sub = _guest_type(pointee, types)
        if sub is None:
            return None
        return f'{prefix}{sub} *'
    if k == 'builtin':
        # Strip the leading 'const ' if clang baked it into the name —
        # the const qualifier is redundantly carried by `is_const` and
        # the surrounding pointer's `pointee_is_const`. Letting the
        # caller add it once avoids `const const char *`.
        name = t.get('name') or ''
        if name.startswith('const '):
            name = name[len('const '):]
        return name or None
    if k == 'enum':
        return 'int'
    return None


# ─── Bridge emitter ──────────────────────────────────────────────────

# Identifier names that collide with locals the bridge body already
# defines (e.g. `ctx` is our `struct yos_exec_ctx *ctx` first arg;
# `errno`/`_r` are touched by the auto-emitted body).
_RESERVED_ARG_NAMES = frozenset({
    'ctx', 'errno', '_r', '_e', '_p', '_hdfd',
    'host_stat_scratch', 'host_statvfs_scratch', 'host_statfs_scratch',
    'host_timespec_scratch', 'host_timeval_scratch', 'host_rlimit_scratch',
    'host_tm_scratch',
})


def _bridge_arg_decl(name: str, idx: int, t: dict, types: dict) -> str | None:
    """Emit the wasm-side function-arg declaration. Returns None on
    types we don't yet render."""
    wt = _wasm_type(t, types)
    if wt is None:
        return None
    # Avoid colliding with reserved locals (e.g. `ctx`). When the guest
    # decl names an arg `ctx` the wrapper sig conflicts with our own
    # `struct yos_exec_ctx *ctx`. Fall back to the positional name.
    if not name or name in _RESERVED_ARG_NAMES:
        name = f'a{idx}'
    return f'{wt} {name}'


def _arg_translation(name: str, idx: int, gt: dict, ht: dict, types: dict) -> tuple[str, str] | None:
    """For one function arg, return (local_setup, host_call_expr).

    local_setup is C lines that prepare a local of the host type;
    host_call_expr is what we pass to the host call.

    Returns None if we can't bridge this arg cleanly.
    """
    gt = _resolve(gt, types)
    ht = _resolve(ht, types)
    var = name or f'a{idx}'
    if gt is None or ht is None:
        return None
    gk, hk = gt.get('kind'), ht.get('kind')

    # void / void — shouldn't appear as an arg, but be defensive.
    if gk == 'void' and hk == 'void':
        return ('', '')

    # pointer args: translate wasm offset to host pointer. We do NOT
    # convert wasm offset 0 to NULL: most input-pointer fns (strlen,
    # strchr, …) crash on NULL, and the guest passing 0 typically
    # means "I have a real pointer to memory[0]" rather than NULL.
    # The handful of fns where NULL passthrough actually matters
    # (strtoimax / strtol with endptr=NULL, posix_spawn etc.) have
    # custom impls in src/yos/impl/.
    if gk == 'pointer' and hk == 'pointer':
        host_ptr_type = _host_type(ht, types) or 'void *'
        setup = f'    {host_ptr_type} {var}_h = ({host_ptr_type})(ctx->memory + {var});'
        return (setup, f'{var}_h')

    # builtin / builtin: width-only conversion via cast at the call.
    if gk == 'builtin' and hk == 'builtin':
        host_t = _host_type(ht, types) or 'int'
        return ('', f'({host_t}){var}')

    # enum → int / vice versa.
    if gk == 'enum' and hk in ('builtin', 'enum'):
        return ('', f'({_host_type(ht, types) or "int"}){var}')
    if hk == 'enum' and gk in ('builtin', 'enum'):
        return ('', f'(int){var}')

    return None


_WASM_TYPE_TO_SIG = {
    'void':     'v',
    'int8_t':   'i', 'uint8_t':  'i',
    'int16_t':  'i', 'uint16_t': 'i',
    'int32_t':  'i', 'uint32_t': 'i',
    'int64_t':  'I', 'uint64_t': 'I',
    'float':    'f',
    'double':   'F',
}


def _sig_char_for_wasm_type(wt: str | None) -> str:
    """Map a rendered wasm-ABI C type to a wasm3 link-signature char.
    Defaults to 'i' (any unknown 32-bit thing — typically a pointer
    we render as uint32_t)."""
    return _WASM_TYPE_TO_SIG.get(wt or '', 'i')


def _emit_bridge(name: str, gf: dict, hf: dict, gtypes: dict, htypes: dict,
                 has_public_header: bool = True) -> tuple[str, str]:
    """Emit (declaration, definition) for one function's bridge. On
    failure returns (decl, stub_def) so the bridge still compiles but
    returns -ENOSYS at runtime."""
    # Wasm-side signature
    arg_decls = ['struct yos_exec_ctx *ctx']
    setups: list[str] = []
    call_args: list[str] = []
    post_writebacks: list[str] = []
    can_emit = has_public_header

    g_args = gf.get('args', [])
    h_args = hf.get('args', [])
    # Arity skew (darwin vs FreeBSD): when guest declares more args
    # than host (pthread_setname_np: FreeBSD takes 2, darwin 1; shm_open:
    # FreeBSD's 3rd arg is fixed, darwin's is variadic) — accept all
    # guest args at the bridge boundary so the m3w wrapper signature
    # still matches, then fall back to the TODO stub for the body.
    arity_skew = len(g_args) != len(h_args)
    if arity_skew:
        can_emit = False
    is_at_family = name in _AT_FAMILY
    at_flag_idx = _AT_FLAG_ARG.get(name)
    path_arg_idxs = _PATH_ARG_FNS.get(name, ())
    for i, (ga, ha) in enumerate(zip(g_args, h_args)):
        gt = gtypes.get(ga['type_uid']);  ht = htypes.get(ha['type_uid'])
        decl = _bridge_arg_decl(ga.get('name'), i, gt, gtypes)
        tr = _arg_translation_full(ga.get('name'), i, gt, ht, gtypes, htypes)
        # *at family: route the first int arg through yos_xlate_dfd so
        # AT_FDCWD (-100 vs -2) and wasm→host fd_map translation work
        # across hosts. See _AT_FAMILY comment above. The auto-bridge's
        # default translation emits `(<host_t>)<var>`; we wrap that
        # whole expression in the helper.
        if (is_at_family and i == 0 and tr is not None
                and gt is not None and ht is not None
                and _resolve(gt, gtypes).get('kind') == 'builtin'
                and _resolve(ht, htypes).get('kind') == 'builtin'):
            inner = tr[1]
            tr = (tr[0], f'yos_xlate_dfd(ctx, (int32_t)({inner}))',
                  tr[2] if len(tr) > 2 else '')
        # *at family: the AT_* flag arg (position varies — see
        # _AT_FLAG_ARG) carries FreeBSD-shape constants that don't match
        # the host's, so translate before passing to host libc.
        if (at_flag_idx is not None and i == at_flag_idx and tr is not None
                and gt is not None and ht is not None
                and _resolve(gt, gtypes).get('kind') == 'builtin'
                and _resolve(ht, htypes).get('kind') == 'builtin'):
            inner = tr[1]
            tr = (tr[0], f'yos_at_flags_fb_to_lx((int)({inner}))',
                  tr[2] if len(tr) > 2 else '')
        # PATH args: route relative paths through yos_path_resolve so
        # they resolve against ctx->cwd (yos's faked per-process cwd —
        # see comment on _PATH_ARG_FNS and impl/io/io.c::yos_path_resolve).
        # The existing translation builds e.g. `(const char *)(ctx->memory
        # + a0)`; we wrap that in yos_path_resolve.
        if (i in path_arg_idxs and tr is not None
                and gt is not None and ht is not None
                and _resolve(gt, gtypes).get('kind') == 'pointer'
                and _resolve(ht, htypes).get('kind') == 'pointer'):
            inner = tr[1]
            tr = (tr[0], f'yos_path_resolve(ctx, {inner})',
                  tr[2] if len(tr) > 2 else '')
        if decl is None or tr is None:
            can_emit = False
            # Don't break — the body falls back to the TODO stub but
            # the wrapper uses the FULL signature (computed from
            # _wasm_sig). If we break here, arg_decls is short and
            # yos_<name>(ctx, a0, a1) is generated while m3w_<name>
            # passes (ctx, a0, a1, a2, a3) → "too many args" compile
            # error. Keep building the decl list with safe i32
            # defaults so the body's signature matches the wrapper.
            decl = decl or f'uint32_t a{i}'
            tr = ('', f'a{i}', '')
        arg_decls.append(decl)
        if tr[0]:
            setups.append(tr[0])
        call_args.append(tr[1])
        if len(tr) >= 3 and tr[2]:
            post_writebacks.append(tr[2])

    # If guest has more args than host, pad arg_decls with dummies
    # so the bridge body's signature matches the m3w wrapper that
    # uses _wasm_sig (computed from gf alone).
    for i in range(len(arg_decls) - 1, len(g_args)):
        arg_decls.append(f'uint32_t a{i}')

    # Variadic: clang's wasm32 ABI adds an implicit `i32 va_list_ptr`
    # at the end of the call. We accept it as an extra unused parameter
    # in the bridge — the variadic args themselves are ignored for now
    # (the body calls host libc with only the fixed args). Functions
    # that genuinely need varargs (printf family) live under
    # `variadic:` in hooks.yaml and have hand-written bodies.
    if gf.get('variadic'):
        arg_decls.append(f'uint32_t _va_ptr')
        # Host libc fns are also variadic at this level; clang's host
        # ABI passes varargs differently but since we don't expand any
        # varargs at the host call here, just don't add to call_args.

    # Return rendering
    g_ret = gtypes.get(gf['ret']);  h_ret = htypes.get(hf['ret'])
    wret  = _wasm_type(g_ret, gtypes)
    hret  = _host_type(h_ret, htypes)
    if wret is None or hret is None:
        can_emit = False

    # Functions that return a pointer need result translation: the
    # host returns a host pointer; the guest expects a wasm offset.
    # The common case is "return aliases first ptr arg" (memset, memcpy,
    # memmove, strcpy, strncpy, strcat, strncat, …) — for those we can
    # just return the first arg's wasm offset unchanged. The next
    # common case is "result is a pointer INTO a string arg" (strchr,
    # strrchr, memchr, strstr) — we compute the offset by subtracting
    # the input buffer's host base. Anything else (strdup-style new
    # allocations) genuinely needs an allocator and stays a stub.
    g_ret_resolved = _resolve(g_ret, gtypes)
    ret_is_ptr = bool(g_ret_resolved and g_ret_resolved.get('kind') == 'pointer')

    # Do the first arg(s) include a pointer? If so, decide which case.
    first_ptr_arg = -1
    for i, ga in enumerate(g_args):
        gt = _resolve(gtypes.get(ga['type_uid']), gtypes)
        if gt and gt.get('kind') == 'pointer':
            first_ptr_arg = i
            break

    # Functions whose return is the first ptr arg unchanged.
    # (Whatever the host returns, we know it's the same address — so
    # we can return the WASM offset of the first ptr arg without any
    # translation arithmetic.)
    RET_IS_DST = {
        'memset',  'memcpy',  'memmove', 'strcpy',  'strncpy',
        'strcat',  'strncat', 'mempcpy', 'stpcpy',  'stpncpy',
        # Wide-char family — same convention with wchar_t.
        'wmemset', 'wmemcpy', 'wmemmove', 'wcscpy',  'wcsncpy',
        'wcscat',  'wcsncat', 'wcpcpy',  'wcpncpy',
    }
    # Functions whose return is a pointer INTO the first ptr arg.
    # (host returns &input[N]; we compute N and add to the wasm
    # offset of the input.)
    RET_OFFSET_INTO_FIRST = {
        'strchr',    'strrchr',   'memchr',  'strstr',
        'strpbrk',   'strcasestr','memrchr', 'strchrnul',
        'memmem',    'memccpy',   'index',   'rindex',
        'basename',
        # Wide-char family.
        'wmemchr',   'wcschr',    'wcsrchr', 'wcsstr',
        'wcspbrk',   'wcstok',
    }
    # Functions that allocate a new buffer + copy the input string.
    # The bridge: call host, read host string length, allocate
    # wasm-side via yos_malloc, copy, free host buffer, return wasm
    # offset. Without this the auto-bridge stubs them with NULL,
    # which makes basically any tool that uses strdup() crash.
    RET_NEW_DUP = {
        'strdup', 'strndup',
    }
    # Wide-char allocate-and-copy.
    RET_NEW_DUP_WIDE = {
        'wcsdup',
    }
    ret_kind = None
    if ret_is_ptr and first_ptr_arg >= 0:
        if name in RET_IS_DST:
            ret_kind = 'first_arg_alias'
        elif name in RET_OFFSET_INTO_FIRST:
            ret_kind = 'offset_into_first'
        elif name in RET_NEW_DUP:
            ret_kind = 'new_dup_str'
        elif name in RET_NEW_DUP_WIDE:
            ret_kind = 'new_dup_wcs'
    if ret_is_ptr and ret_kind is None:
        can_emit = False

    decl = f'{wret or "int32_t"} yos_{name}({", ".join(arg_decls)});'

    if not can_emit:
        # If we can't render the return type, fall back to int32_t —
        # the wasm-side caller treats it as an i32. Better than
        # leaving the function with no return at all.
        eff_wret = wret if (wret and wret != 'void') else 'int32_t'
        # For pointer returns, NULL (0) is far safer than -38 — guest
        # libc treats NULL as "fn returned nothing useful" and most
        # call sites cope (getenv, setlocale, fopen, …); -38 looks like
        # a valid pointer to the guest and crashes the next deref.
        ret_lit = '0' if ret_is_ptr else '(-38)'
        body = (
            f'{eff_wret} yos_{name}({", ".join(arg_decls)}) {{\n'
            f'    /* TODO: complex arg/return types — extend bridge.py to render. */\n'
            f'    (void)ctx;\n'
            f'    return ({eff_wret}){ret_lit};\n'
            f'}}'
        )
        # Make the declaration match.
        decl = f'{eff_wret} yos_{name}({", ".join(arg_decls)});'
        return decl, body

    # Compose the host-call expression with optional return-value
    # narrowing and errno remap.
    call = f'{name}({", ".join(call_args)})'
    if hret == 'void':
        # Guest may declare a non-void return (e.g. darwin's link_addr
        # is `void` but FreeBSD's is `int`). The m3w wrapper that calls
        # us is built from the GUEST signature and assigns *raw_return
        # = yos_<name>(...), so we must follow the guest's wret here
        # and emit an `int yos_...{ host_call; return 0; }` shim when
        # they disagree. Otherwise gcc errors with "operand of type
        # void where arithmetic or pointer type is required".
        if wret and wret != 'void':
            body = (
                f'{wret} yos_{name}({", ".join(arg_decls)}) {{\n'
                + ('    (void)ctx;\n')
                + ('\n'.join(setups) + '\n' if setups else '')
                + f'    {call};\n'
                + ('\n'.join(post_writebacks) + '\n' if post_writebacks else '')
                + f'    return ({wret})0;\n'
                + f'}}'
            )
            return f'{wret} yos_{name}({", ".join(arg_decls)});', body
        body = (
            f'void yos_{name}({", ".join(arg_decls)}) {{\n'
            + ('\n'.join(setups) + '\n' if setups else '')
            + f'    (void)ctx;\n'
            + f'    {call};\n'
            + ('\n'.join(post_writebacks) + '\n' if post_writebacks else '')
            + f'}}'
        )
        return f'void yos_{name}({", ".join(arg_decls)});', body

    body_lines = []
    body_lines.append(f'{wret} yos_{name}({", ".join(arg_decls)}) {{')
    body_lines.append('    (void)ctx;')
    body_lines.extend(setups)
    # NOTE: don't `errno = 0` here. Tools rely on POSIX semantics
    # where successful calls don't clobber a previously-set errno;
    # resetting it confuses callers like FreeBSD ls's fts.c which
    # check errno across multiple calls. Trade-off: tools that
    # intentionally check errno after a "may set errno on success"
    # call (rare — nice/getpriority style) need to set errno=0
    # themselves, per POSIX. The previous version reset errno here
    # and broke `ls -alrt` (fts saw stale errno=22 from an earlier
    # call sequence and bailed with "Invalid argument").
    body_lines.append(f'    {hret} _r = {call};')
    body_lines.extend(post_writebacks)
    if ret_kind == 'first_arg_alias':
        # memset/memcpy/strcpy/...: return is the first ptr arg as
        # passed in. Just return that argument's wasm offset.
        first_arg_var = arg_decls[1 + first_ptr_arg].split()[-1]
        body_lines.append(f'    (void)_r;')
        body_lines.append(f'    return {first_arg_var};')
    elif ret_kind == 'offset_into_first':
        # strchr/memchr/strstr/...: result is a pointer into the first
        # ptr arg's buffer. Return NULL → 0; else compute the offset
        # within the input and add to its wasm offset.
        first_arg_var = arg_decls[1 + first_ptr_arg].split()[-1]
        # Local set up by _arg_translation_full as <name>_h.
        host_first = f'{first_arg_var}_h'
        body_lines.append(f'    if (!_r) return 0;')
        body_lines.append(f'    return (uint32_t)({first_arg_var} + '
                          f'((const char *)_r - (const char *){host_first}));')
    elif ret_kind == 'new_dup_str':
        # strdup/strndup: host returns a freshly malloc'd buffer. We
        # can't hand that pointer back to the wasm guest (different
        # address space). Allocate a wasm-side buffer via yos_malloc,
        # copy the string in, free the host buffer, return wasm offset.
        body_lines.append(f'    if (!_r) return 0;')
        body_lines.append(f'    extern uint32_t yos_malloc(struct yos_exec_ctx *, uint32_t);')
        body_lines.append(f'    extern void free(void *);')
        body_lines.append(f'    size_t _n = strlen((const char *)_r) + 1;')
        body_lines.append(f'    uint32_t _off = yos_malloc(ctx, (uint32_t)_n);')
        body_lines.append(f'    if (!_off) {{ free((void *)_r); return 0; }}')
        body_lines.append(f'    memcpy(ctx->memory + _off, _r, _n);')
        body_lines.append(f'    free((void *)_r);')
        body_lines.append(f'    return _off;')
    elif ret_kind == 'new_dup_wcs':
        # wcsdup: same idea but with wchar_t. Host wchar_t is 4 B
        # (Linux/FreeBSD 64-bit); FreeBSD wasm32 wchar_t is also 4 B.
        # Walk to terminator using the host wchar_t size.
        body_lines.append(f'    if (!_r) return 0;')
        body_lines.append(f'    extern uint32_t yos_malloc(struct yos_exec_ctx *, uint32_t);')
        body_lines.append(f'    extern void free(void *);')
        body_lines.append(f'    size_t _n = 0; while (((const wchar_t *)_r)[_n]) _n++;')
        body_lines.append(f'    _n++;  /* trailing NUL */')
        body_lines.append(f'    uint32_t _bytes = (uint32_t)(_n * sizeof(wchar_t));')
        body_lines.append(f'    uint32_t _off = yos_malloc(ctx, _bytes);')
        body_lines.append(f'    if (!_off) {{ free((void *)_r); return 0; }}')
        body_lines.append(f'    memcpy(ctx->memory + _off, _r, _bytes);')
        body_lines.append(f'    free((void *)_r);')
        body_lines.append(f'    return _off;')
    else:
        # Error reporting: write mapped errno to the per-ctx wasm slot
        # so the FreeBSD `errno` macro (#define errno (*__error()))
        # works in the guest. Use errno-based detection — a negative
        # return is NOT a reliable error indicator (strtoimax(-42)
        # returns -42 with errno=0; sin(-x), etc.). The bridge that
        # called us cleared errno before invoking the host function;
        # we only write the slot if the host actually set one.
        if any(b in (hret or '') for b in ('int', 'long', 'ssize_t', 'off_t', 'pid_t')):
            body_lines.append('    if (errno) {')
            body_lines.append('        extern int yos_remap_errno_h2g(int);')
            body_lines.append('        int _e = yos_remap_errno_h2g(errno);')
            body_lines.append('        if (ctx && ctx->memory && ctx->errno_off)')
            body_lines.append('            *(int *)(ctx->memory + ctx->errno_off) = _e;')
            body_lines.append('    }')
        body_lines.append(f'    return ({wret})_r;')
    body_lines.append('}')
    return decl, '\n'.join(body_lines)


def _arg_translation_full(name, idx, gt, ht, g_types, h_types):
    """Same as _arg_translation but with separate type registries.

    Returns a 3-tuple (setup, host_call_expr, post_writeback). The
    post-writeback is C lines emitted AFTER the host call returns,
    used by the narrow-pointer thunk below to copy a host-wider
    scalar back into a guest-narrower wasm slot. Most args don't
    need any post step and return ''.
    """
    gt = _resolve(gt, g_types)
    ht = _resolve(ht, h_types)
    var = name or f'a{idx}'
    if gt is None or ht is None:
        return None
    gk, hk = gt.get('kind'), ht.get('kind')
    if gk == 'void' and hk == 'void':
        return ('', '', '')
    if gk == 'pointer' and hk == 'pointer':
        # Narrow-pointer thunk: pointer to a builtin scalar where the
        # host writes more bytes than the wasm slot reserves. Classic
        # case: `time(time_t *)` — FreeBSD i386 time_t is __int32_t
        # (4 bytes) but Linux x86_64 time_t is `long` (8 bytes), so
        # the host stores 8 bytes through a 4-byte wasm pointer and
        # smashes the next stack slot. Detected from the per-pointee
        # `size` recorded by extract.py; if guest < host, route
        # through a host-side stack temp and narrow-copy back. We
        # treat any size mismatch as inout: read N=guest_size bytes
        # from wasm into the temp first (so input-only and inout
        # callers see the right starting value), let host write the
        # full host_size, then narrow-cast back unless the pointee is
        # const-qualified (input-only — skip the writeback).
        g_pointee = _resolve(g_types.get(gt.get('pointee_uid')), g_types)
        h_pointee = _resolve(h_types.get(ht.get('pointee_uid')), h_types)
        if (g_pointee and h_pointee
                and g_pointee.get('kind') == 'builtin'
                and h_pointee.get('kind') == 'builtin'
                and g_pointee.get('size') and h_pointee.get('size')
                and g_pointee.get('size') != h_pointee.get('size')):
            guest_t = _guest_type(g_pointee, g_types) or 'int'
            host_t  = _host_type(h_pointee, h_types) or 'int'
            is_const = bool(gt.get('pointee_is_const')) or bool(g_pointee.get('is_const'))
            # NULL passthrough: when the wasm guest passes 0, the
            # libc convention is "no out-param wanted" (time(NULL),
            # wait(NULL), waitpid(..., NULL, 0), …). Without a NULL
            # short-circuit we'd READ from wasm offset 0 (a string
            # literal in .data) AND on the post-call WRITE back 4
            # bytes there — silently corrupting whatever the linker
            # placed at offset 0. nvim hit this on `time(NULL)`
            # called from os_localtime; the canary smash showed up
            # later in os_localtime_r's epilogue once the corrupted
            # data segment was used. Pass NULL straight through to
            # the host, skip both the read and the writeback.
            setup_lines = [
                f'    /* narrow-ptr thunk: guest {guest_t} ({g_pointee.get("size")}B)'
                f' vs host {host_t} ({h_pointee.get("size")}B) — route through'
                f' a host-width temp and narrow-copy back. */',
                f'    {host_t}  {var}_v = ({var}) ? ({host_t})*({guest_t} *)(ctx->memory + {var}) : 0;',
                f'    {host_t} *{var}_h = ({var}) ? &{var}_v : ({host_t} *)0;',
            ]
            setup = '\n'.join(setup_lines)
            post  = ('' if is_const
                     else f'    if ({var}) *({guest_t} *)(ctx->memory + {var}) = ({guest_t}){var}_v;')
            return (setup, f'{var}_h', post)
        host_ptr_type = _host_type(ht, h_types) or 'void *'
        # See _arg_translation comment: don't NULL-translate; custom
        # impls handle the few fns where NULL passthrough matters.
        # The narrow-ptr thunk above DOES NULL-translate (and must,
        # since we'd otherwise read+write wasm offset 0 to satisfy
        # the host's wider scalar). For wide-ptr passthrough, leaving
        # 0 → ctx->memory keeps realpath(_, NULL), getcwd(NULL, _),
        # and friends working — those rely on host libc seeing a
        # non-NULL output that points somewhere it can write to.
        setup = f'    {host_ptr_type} {var}_h = ({host_ptr_type})(ctx->memory + {var});'
        return (setup, f'{var}_h', '')
    if gk == 'builtin' and hk == 'builtin':
        host_t = _host_type(ht, h_types) or 'int'
        return ('', f'({host_t}){var}', '')
    if gk == 'enum' and hk in ('builtin', 'enum'):
        return ('', f'({_host_type(ht, h_types) or "int"}){var}', '')
    if hk == 'enum' and gk in ('builtin', 'enum'):
        return ('', f'(int){var}', '')
    return None


# ─── Top-level emit ──────────────────────────────────────────────────

_BRIDGE_PROLOGUE = '''\
/*
 * Auto-generated by build-tools/api-generate/bridge.py — DO NOT EDIT.
 *
 * Per-function bridge wrappers between the wasm guest's libc imports
 * and the host's libc. yos's runtime links these and binds each
 * `yos_<fn>` to its corresponding wasm import.
 *
 * The struct yos_exec_ctx contains at minimum a `uint8_t *memory`
 * pointing at the wasm linear-memory base — yos host code provides
 * the actual layout.
 */
'''


def _normalised_header(h: str) -> str | None:
    """Snapshot path → libc-style header name. Returns None for headers
    that have no clean public include (linux/ uapi, asm); for glibc
    `bits/<x>.h` returns the corresponding public umbrella so the
    bridge can include it. Without the bits/ mapping, ~150 functions
    that glibc keeps in private bits/ headers (most of <math.h>, all
    of <signal.h>'s sigsetops, much of <stdio.h>, etc.) get flagged
    as "no public header" and stubbed with -ENOSYS — even though the
    public umbrella is perfectly includable. """
    if not h:
        return None
    if '/' in h:
        tail = h.split('/', 1)[1] if h[:3].startswith(('01-', '02-', '03-', '04-')) else h
    else:
        tail = h

    # Reject genuinely-private headers (kernel UAPI, hardware asm,
    # SunRPC). These have no public umbrella we can include.
    if tail.startswith(('asm/', 'asm-generic/', 'linux/',
                        'rpcsvc/', 'rpc/')):
        return None

    # Glibc puts the *declaration* of many libc fns in `bits/<x>.h`
    # which `<x>.h` includes after defining feature macros. Map back
    # to the umbrella so the bridge can `#include` something compilable.
    BITS_TO_UMBRELLA = {
        # math
        'bits/mathcalls.h':                  'math.h',
        'bits/mathcalls-helper-functions.h': 'math.h',
        'bits/mathcalls-narrow.h':           'math.h',
        'bits/math-finite.h':                'math.h',
        'bits/math-vector.h':                'math.h',
        'bits/cmathcalls.h':                 'complex.h',
        # signal
        'bits/sigaction.h':                  'signal.h',
        'bits/sigthread.h':                  'signal.h',
        'bits/sigstack.h':                   'signal.h',
        'bits/signum-arch.h':                'signal.h',
        'bits/signum-generic.h':             'signal.h',
        'bits/sigevent-consts.h':            'signal.h',
        'bits/siginfo-consts.h':             'signal.h',
        'bits/sigcontext.h':                 'signal.h',
        'bits/ss_flags.h':                   'signal.h',
        'bits/signalfd.h':                   'sys/signalfd.h',
        # string / strings
        'bits/string_fortified.h':           'string.h',
        'bits/strings_fortified.h':          'strings.h',
        # stdio
        'bits/stdio.h':                      'stdio.h',
        'bits/stdio2.h':                     'stdio.h',
        'bits/stdio-ldbl.h':                 'stdio.h',
        'bits/stdio_lim.h':                  'stdio.h',
        'bits/printf-ldbl.h':                'stdio.h',
        # stdlib
        'bits/stdlib.h':                     'stdlib.h',
        'bits/stdlib-bsearch.h':             'stdlib.h',
        'bits/stdlib-float.h':               'stdlib.h',
        # wchar / wctype
        'bits/wchar.h':                      'wchar.h',
        'bits/wchar2.h':                     'wchar.h',
        'bits/wchar-ldbl.h':                 'wchar.h',
        'bits/wctype-wchar.h':               'wctype.h',
        # getopt
        'bits/getopt_core.h':                'unistd.h',
        'bits/getopt_ext.h':                 'getopt.h',
        'bits/getopt_posix.h':               'unistd.h',
        # socket / uio / netdb / netinet
        'bits/socket.h':                     'sys/socket.h',
        'bits/socket2.h':                    'sys/socket.h',
        'bits/socket_type.h':                'sys/socket.h',
        'bits/sockaddr.h':                   'sys/socket.h',
        'bits/socketpair.h':                 'sys/socket.h',
        'bits/uio-ext.h':                    'sys/uio.h',
        'bits/uio_lim.h':                    'sys/uio.h',
        'bits/in.h':                         'netinet/in.h',
        'bits/netdb.h':                      'netdb.h',
        # fcntl / poll / select / mman / dirent
        'bits/fcntl-linux.h':                'fcntl.h',
        'bits/fcntl2.h':                     'fcntl.h',
        'bits/fcntl.h':                      'fcntl.h',
        'bits/poll.h':                       'poll.h',
        'bits/poll2.h':                      'poll.h',
        'bits/select.h':                     'sys/select.h',
        'bits/select2.h':                    'sys/select.h',
        'bits/mman-linux.h':                 'sys/mman.h',
        'bits/mman-shared.h':                'sys/mman.h',
        'bits/mman.h':                       'sys/mman.h',
        'bits/mman-map-flags-generic.h':     'sys/mman.h',
        'bits/dirent.h':                     'dirent.h',
        'bits/dirent_ext.h':                 'dirent.h',
        # IPC / SysV
        'bits/ipc.h':                        'sys/ipc.h',
        'bits/ipc-perm.h':                   'sys/ipc.h',
        'bits/sem.h':                        'sys/sem.h',
        'bits/shm.h':                        'sys/shm.h',
        'bits/shmlba.h':                     'sys/shm.h',
        'bits/msq.h':                        'sys/msg.h',
        # Linux-specific event fds (genuinely Linux-only — only
        # bridgeable on Linux hosts; macOS host build will skip
        # them with #ifdef __linux__).
        'bits/inotify.h':                    'sys/inotify.h',
        'bits/eventfd.h':                    'sys/eventfd.h',
        'bits/timerfd.h':                    'sys/timerfd.h',
        'bits/epoll.h':                      'sys/epoll.h',
        # ioctl / dlfcn / sched / resource / sysctl / time
        'bits/ioctls.h':                     'sys/ioctl.h',
        'bits/ioctl-types.h':                'sys/ioctl.h',
        'bits/dlfcn.h':                      'dlfcn.h',
        'bits/sched.h':                      'sched.h',
        'bits/sysctl.h':                     'sys/sysctl.h',
        'bits/resource.h':                   'sys/resource.h',
        'bits/time.h':                       'time.h',
        'bits/timex.h':                      'sys/timex.h',
        'bits/timesize.h':                   'sys/types.h',
        # locale / utmpx / utsname / utime
        'bits/locale.h':                     'locale.h',
        'bits/utmpx.h':                      'utmpx.h',
        'bits/utsname.h':                    'sys/utsname.h',
        'bits/utime.h':                      'utime.h',
        # syslog / error / errno
        'bits/syslog.h':                     'syslog.h',
        'bits/error.h':                      'error.h',
        'bits/errno.h':                      'errno.h',
        # stat / statvfs / statfs
        'bits/stat.h':                       'sys/stat.h',
        'bits/statfs.h':                     'sys/statfs.h',
        'bits/statvfs.h':                    'sys/statvfs.h',
        # termios — multiple shards in glibc
        'bits/termios.h':                    'termios.h',
        'bits/termios-struct.h':             'termios.h',
        'bits/termios-tcflow.h':             'termios.h',
        'bits/termios-c_cc.h':               'termios.h',
        'bits/termios-c_cflag.h':            'termios.h',
        'bits/termios-c_oflag.h':            'termios.h',
        'bits/termios-c_iflag.h':            'termios.h',
        'bits/termios-c_lflag.h':            'termios.h',
        'bits/termios-baud.h':               'termios.h',
        # wait / pthread / semaphore — usually pulled by their umbrella
        'bits/waitflags.h':                  'sys/wait.h',
        'bits/waitstatus.h':                 'sys/wait.h',
        'bits/pthreadtypes.h':               'pthread.h',
        'bits/pthreadtypes-arch.h':          'pthread.h',
        'bits/pthread_stack_min.h':          'pthread.h',
        'bits/struct_mutex.h':               'pthread.h',
        'bits/struct_rwlock.h':              'pthread.h',
        'bits/semaphore.h':                  'semaphore.h',
        # types — bits/types/*.h are type-only, not function decls,
        # but show up in case a fn returns one. Map to <sys/types.h>.
        'bits/types.h':                      'sys/types.h',
        'bits/typesizes.h':                  'sys/types.h',
        'bits/wordsize.h':                   'sys/types.h',
        'bits/timesize.h':                   'sys/types.h',
        'bits/long-double.h':                'sys/types.h',
        'bits/floatn.h':                     'sys/types.h',
        'bits/floatn-common.h':              'sys/types.h',
        'bits/endian.h':                     'endian.h',
        'bits/byteswap.h':                   'byteswap.h',
        # confname / limits / param
        'bits/confname.h':                   'unistd.h',
        'bits/local_lim.h':                  'limits.h',
        'bits/posix1_lim.h':                 'limits.h',
        'bits/posix2_lim.h':                 'limits.h',
        'bits/posix_opt.h':                  'unistd.h',
        'bits/param.h':                      'sys/param.h',
        # cpu-set / rseq / atomic
        'bits/cpu-set.h':                    'sched.h',
        'bits/rseq.h':                       'sys/rseq.h',
        'bits/atomic_wide_counter.h':        'sys/types.h',
        # Misc glue
        'bits/libc-header-start.h':          'features.h',
        'bits/environments.h':               'unistd.h',
        'bits/fp-logb.h':                    'math.h',
        'bits/ptrace-shared.h':              'sys/ptrace.h',
        'bits/syscall.h':                    'sys/syscall.h',
        'bits/thread-shared-types.h':        'pthread.h',
        'bits/platform/features.h':          'features.h',
        'bits/platform/x86.h':               'sys/types.h',
    }
    if tail in BITS_TO_UMBRELLA:
        return BITS_TO_UMBRELLA[tail]
    if tail.startswith('bits/'):
        # Unknown bits/ header — reject conservatively rather than
        # invent an umbrella. Add to the table above when a function
        # we need shows up here.
        return None
    return tail


def _has_public_header(name: str, host_fns: dict) -> bool:
    """True if the host's declaration lives in a header we can include
    in user code. Functions buried inside kernel/uapi/glibc-internal
    headers can't be called by name from a normal .c file."""
    f = host_fns.get(name)
    if not f:
        return False
    return _normalised_header(f.get('header') or '') is not None


def _collect_includes(names, host_fns) -> list[str]:
    """Distinct list of #include paths needed to declare every host
    function we call. Reads `header:` tags written by extract.py.
    Strips the snapshot/path/ prefix so we end up with `<sys/stat.h>`,
    `<stdio.h>`, etc.
    """
    incs: set[str] = set()
    for n in names:
        f = host_fns.get(n)
        if not f:
            continue
        tail = _normalised_header(f.get('header') or '')
        if tail is None:
            continue
        incs.add(tail)
    return sorted(incs)


def _wasm_sig(name: str, gf: dict, gtypes: dict) -> tuple[str, list[str]] | None:
    """Compute the (ret_char, [arg_chars]) wasm3 link signature for a
    function, based on the guest declaration. Returns None if any type
    can't be rendered as a wasm-ABI scalar.

    For variadic functions (open, fcntl, ioctl, …), clang's wasm32 ABI
    adds an implicit `i32` (a pointer to the va_list staging area) to
    the call. We add it to the sig so the bridge matches what the
    wasm guest actually emits at the call site.
    """
    arg_chars: list[str] = []
    for ga in gf.get('args', []):
        wt = _wasm_type(gtypes.get(ga['type_uid']), gtypes)
        if wt is None:
            return None
        arg_chars.append(_sig_char_for_wasm_type(wt))
    if gf.get('variadic'):
        arg_chars.append('i')
    wret = _wasm_type(gtypes.get(gf['ret']), gtypes)
    rchar = _sig_char_for_wasm_type(wret) if wret is not None else 'i'
    return rchar, arg_chars


def _strace_arg_fmt(t: dict, types: dict) -> tuple[str, str]:
    """For a guest arg type, return (printf_fmt, arg_expr_template).
    The expr template has a single `{var}` placeholder that the caller
    fills with the popped wasm arg name (a0, a1, ...). Output mirrors
    strace defaults: strings dereffed and quoted, pointers as 0x..,
    signed/unsigned ints as decimal, floats as %g."""
    t = _resolve(t, types)
    if t is None:
        return ('0x%x', '(unsigned){var}')
    k = t.get('kind')
    if k == 'pointer':
        pointee = _resolve(types.get(t.get('pointee_uid')), types)
        is_const = bool(t.get('pointee_is_const')) or (pointee and bool(pointee.get('is_const')))
        if pointee and pointee.get('kind') == 'builtin' and is_const:
            pname = (pointee.get('name') or '').lower()
            if 'char' in pname and pointee.get('size') == 1:
                return ('%s', 'yos_brg_strarg(ctx, {var})')
        return ('0x%x', '(unsigned){var}')
    if k == 'builtin':
        size = t.get('size')
        nm = (t.get('name') or '').lower()
        if 'float' in nm or 'double' in nm:
            return ('%g', '(double){var}')
        is_unsigned = 'unsigned' in nm or 'uint' in nm
        if size == 8:
            return ('%llu', '(unsigned long long){var}') if is_unsigned \
                else ('%lld', '(long long){var}')
        return ('%u', '(unsigned){var}') if is_unsigned else ('%d', '(int){var}')
    if k == 'enum':
        return ('%d', '(int){var}')
    return ('0x%x', '(unsigned){var}')


def _emit_m3_wrapper(name: str, ret_char: str, arg_chars: list[str],
                     gf: dict | None = None, gtypes: dict | None = None) -> str:
    """Emit the wasm3 raw-function wrapper that pops args off the m3
    stack, calls yos_<name>, pushes the return, and traces the call
    in a single strace-style ytrace line."""
    pop_type = {'i': 'uint32_t', 'I': 'uint64_t', 'f': 'float', 'F': 'double'}

    lines = [
        f'static const void *m3w_{name}('
        'IM3Runtime runtime, IM3ImportContext _ctx, '
        'uint64_t *_sp, void *_mem)',
        '{',
        '    (void)_ctx; (void)_mem;',
        '    struct yos_exec_ctx *ctx = '
        '(struct yos_exec_ctx *)m3_GetUserData(runtime);',
        '    extern const char *yos_brg_last_call;',
        '    extern void yos_brg_record(const char *);',
        f'    yos_brg_last_call = "{name}";',
        f'    yos_brg_record("{name}");',
    ]
    if ret_char != 'v':
        lines.append(f'    {pop_type[ret_char]} *raw_return = '
                     f'({pop_type[ret_char]}*)(_sp++);')

    arg_names: list[str] = []
    for i, c in enumerate(arg_chars):
        an = f'a{i}'
        lines.append(f'    {pop_type[c]} {an} = '
                     f'*({pop_type[c]}*)(_sp++);')
        arg_names.append(an)
    lines.append('    extern void yos_brg_record_args(uint64_t,uint64_t,uint64_t,uint64_t);')
    a_arr = []
    for i in range(4):
        if i < len(arg_names):
            a_arr.append(f'(uint64_t)a{i}')
        else:
            a_arr.append('0')
    lines.append('    yos_brg_record_args(' + ', '.join(a_arr) + ');')

    # Reset host errno before the call so the post-call value cleanly
    # signals whether the host libc routine touched it. The bridge body
    # itself does NOT reset (POSIX-conformant: success doesn't clobber
    # a previously-set errno from the *guest's* point of view, which
    # reads the per-ctx slot, not host errno). The m3w trace, however,
    # wants the per-call signal — so we wrap the body in a clean
    # host-errno window here. Doesn't affect guest-visible errno
    # because the guest never reads host errno directly.
    lines.append('    errno = 0;')

    call = f'yos_{name}(ctx{("," if arg_names else "")} '
    call += ', '.join(arg_names) + ')'
    if ret_char == 'v':
        lines.append(f'    {call};')
    else:
        lines.append(f'    *raw_return = ({pop_type[ret_char]}){call};')
    lines.append('    int _trace_errno = errno;  /* captured for the ytrace line below */')

    # ── strace-style trace line — one ytrace point per bridge ────────
    # Build the format string and the arg-expression list from the
    # guest signature. The variadic-tail va_list pointer (added to
    # arg_chars when gf->variadic) has no guest-side type entry, so
    # we render it as a bare "..." placeholder in the format and skip
    # any expression for it.
    g_args = (gf or {}).get('args', []) if gf else []
    arg_fmt_parts: list[str] = []
    arg_exprs: list[str] = []
    for i, an in enumerate(arg_names):
        if gf and gtypes is not None and i < len(g_args):
            ga = g_args[i]
            gt = gtypes.get(ga.get('type_uid'))
            fmt, expr_tpl = _strace_arg_fmt(gt, gtypes)
            arg_fmt_parts.append(fmt)
            arg_exprs.append(expr_tpl.format(var=an))
        else:
            # Variadic-tail va_list, or no guest info available.
            arg_fmt_parts.append('...')
    if ret_char == 'v':
        fmt_str = f'{name}({", ".join(arg_fmt_parts)})'
        if arg_exprs:
            lines.append(f'    ytrace("{fmt_str}", {", ".join(arg_exprs)});')
        else:
            lines.append(f'    ytrace("{fmt_str}");')
    else:
        # Return-value formatting. Pointer-returning bridges (wret
        # uint32_t for fns like getenv/strdup) render as 0x%x; integer
        # returns go through yos_brg_retstr which adds the ENNN suffix
        # on negative-with-errno.
        if ret_char == 'i':
            ret_expr = (
                'yos_brg_retstr((long long)(int32_t)*raw_return, _trace_errno)')
            ret_fmt = '%s'
        elif ret_char == 'I':
            ret_expr = (
                'yos_brg_retstr((long long)*raw_return, _trace_errno)')
            ret_fmt = '%s'
        else:  # float / double — no errno semantics
            ret_expr = '(double)*raw_return'
            ret_fmt = '%g'
        fmt_str = f'{name}({", ".join(arg_fmt_parts)}) = {ret_fmt}'
        arg_exprs.append(ret_expr)
        lines.append(f'    ytrace("{fmt_str}", {", ".join(arg_exprs)});')

    lines.append('    return 0;  /* m3Err_none */')
    lines.append('}')
    return '\n'.join(lines)


def _emit_guest_imports_h(decls: list[tuple[str, str, list[str]]]) -> str:
    """Emit the guest-side supplemental header `yos_imports.h`.

    Each entry decorates a FreeBSD libc function with import
    attributes so clang -target wasm32 emits an `(import "env"
    "<name>")` for it. The C type spelling is canonical (FreeBSD
    typedefs live in FreeBSD's own headers — the redeclaration
    here merges with them). Only "compatible" passthrough
    functions are emitted: their FreeBSD wasm32 ABI is layout-
    identical to the host libc ABI, so no shape conversion runs
    in the bridge.
    """
    head = (
        '/*\n'
        ' * yos_imports.h — auto-generated by build-tools/api-generate/\n'
        ' *                bridge.py. DO NOT EDIT.\n'
        ' *\n'
        ' * One redeclaration per FreeBSD libc function whose ABI matches\n'
        ' * the host libc bit-for-bit ("compatible" in the analyser).\n'
        ' * `__attribute__((import_module("env"), import_name("X")))`\n'
        ' * tells clang -target wasm32 to emit the call as a wasm import.\n'
        ' * Include this header AFTER FreeBSD\'s normal headers; clang\n'
        ' * merges the redeclarations canonical-typewise and applies the\n'
        ' * import attributes to every call site.\n'
        ' */\n'
        '#ifndef YOS_IMPORTS_H\n'
        '#define YOS_IMPORTS_H\n\n'
        '#define YOS_IMP \\\n'
        '    __attribute__((import_module("env"), import_name(__func_name)))\n'
        '\n'
        '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
    )
    body: list[str] = []
    for name, ret_t, arg_ts in decls:
        # Per-decl import_name attribute (no macro indirection — keep the
        # generated source greppable).
        attr = (f'__attribute__((import_module("env"), '
                f'import_name("{name}")))')
        sig_args = ', '.join(arg_ts) if arg_ts else 'void'
        body.append(f'{attr}\n{ret_t} {name}({sig_args});')
    tail = (
        '\n\n#ifdef __cplusplus\n}\n#endif\n'
        '#endif /* YOS_IMPORTS_H */\n'
    )
    return head + '\n\n'.join(body) + tail


def _emit_link_imports(sigs: dict[str, tuple[str, list[str]]]) -> str:
    """Emit yos_link_imports(IM3Module) which calls m3_LinkRawFunction
    for every bridge. Tolerates "function not found" — guests don't
    have to import every libc symbol."""
    head = [
        '/* Hooked up by src/yos/yos-main.c. m3Err_functionLookupFailed',
        ' * means the wasm module simply didn\'t import this symbol —',
        ' * not a fatal condition for our usage. */',
        '/* m3Err_functionLookupFailed is declared by wasm3.h (already extern). */',
        '',
        'int yos_brg_link_imports(IM3Module mod)',
        '{',
        '    M3Result r = NULL;',
    ]
    body = []
    for name, (rchar, args) in sorted(sigs.items()):
        sig = f'{rchar}({"".join(args)})'
        body.append(
            f'    r = m3_LinkRawFunction(mod, "env", "{name}", '
            f'"{sig}", &m3w_{name});\n'
            f'    if (r && r != m3Err_functionLookupFailed) {{\n'
            f'        fprintf(stderr, "yos: link {name}: %s\\n", r);\n'
            f'        n_failed++;\n'
            f'    }}'
        )
    # Sig-mismatch on a single fn shouldn't take out the whole link
    # pass — skip it (the unresolved-stub catches it later) and keep
    # going. Returning 0 lets the wasm load even when a few bridges
    # don't bind. `n_failed` is just a tally for the "yos:" log.
    tail = [
        '    if (n_failed) fprintf(stderr, '
            '"yos: %d bridge(s) failed to link (see lines above)\\n", '
            'n_failed);',
        '    return 0;',
        '}',
    ]
    head_with_count = head + ['    int n_failed = 0;']
    return '\n'.join(head_with_count + body + tail)


def _emit_struct_convert_body(name: str, gf: dict, hf: dict,
                              g_types: dict, h_types: dict,
                              meta: dict,
                              decls: list, defs: list) -> bool:
    """Emit a bridge body that uses cv_<type>_h2w / cv_<type>_w2h
    converters to marshal a struct arg whose layout differs between
    host and wasm32. Returns True on success.

    `meta` shape (from hooks.yaml struct_convert section):
        {type: 'stat',     out: 'statbuf'}        # OUT-only
        {type: 'rlimit',   in:  'rlim'}           # IN-only
        {type: 'timespec', in:  'rqtp', out: 'rmtp'}  # IN-OUT
    """
    sc_type = meta.get('type')
    in_arg  = meta.get('in')
    out_arg = meta.get('out')
    if not sc_type:
        return False

    # Render the wasm-ABI parameter list.
    wargs = []
    for i, ga in enumerate(gf.get('args', [])):
        gt = g_types.get(ga['type_uid'])
        d = _bridge_arg_decl(ga.get('name'), i, gt, g_types)
        if d is None:
            return False
        wargs.append(d)

    wret = _wasm_type(g_types.get(gf['ret']), g_types) or 'int32_t'
    sig  = (f'{wret} yos_{name}(struct yos_exec_ctx *ctx'
            f'{(", " + ", ".join(wargs)) if wargs else ""})')
    decls.append(sig + ';')

    # Find the pointer-to-<sc_type> arg index. Our yaml extractor
    # often loses parameter names (most decls become {name:''}), so
    # name-based matching from hooks.yaml is unreliable. Match by
    # POINTEE TYPE instead: there's almost always exactly one
    # pointer-to-`sc_type` argument; that's the in/out slot.
    struct_arg_idx = -1
    for i, ga in enumerate(gf.get('args', [])):
        gt = _resolve(g_types.get(ga['type_uid']), g_types)
        if gt and gt.get('kind') == 'pointer':
            pointee = _resolve(g_types.get(gt.get('pointee_uid')), g_types)
            if pointee and pointee.get('kind') == 'struct' \
                    and pointee.get('name') == sc_type:
                struct_arg_idx = i
                break

    # Build the host-call argument list. For each guest arg:
    #   - if it's the in/out struct arg: use &host_scratch
    #   - else: pass the wasm value (with cast if needed)
    call_args = []
    struct_wname = None
    extra_setups: list[str] = []
    extra_posts: list[str] = []
    is_at_family = name in _AT_FAMILY
    at_flag_idx = _AT_FLAG_ARG.get(name)
    path_arg_idxs = _PATH_ARG_FNS.get(name, ())
    for i, ga in enumerate(gf.get('args', [])):
        wname = wargs[i].split()[-1]
        if i == struct_arg_idx:
            call_args.append(f'&host_{sc_type}_scratch')
            struct_wname = wname
            continue
        gt = g_types.get(ga['type_uid'])
        ht = h_types.get(hf['args'][i].get('type_uid')) if i < len(hf.get('args', [])) else None
        host_t = _host_type(ht, h_types) if ht else None
        # Reuse the narrow-pointer thunk (pointer-to-builtin where the
        # host scalar is wider than the guest scalar — e.g. `const
        # time_t *` where wasm time_t is 4B and host time_t is 8B).
        # Without this, the host reads/writes 8 bytes through a
        # 4-byte wasm pointer and either pulls junk high bits in
        # (input) or smashes the next stack slot (output).
        tr = _arg_translation_full(ga.get('name'), i, gt, ht, g_types, h_types)
        if tr is not None and tr[0] and tr[1] != wname and 'narrow-ptr thunk' in (tr[0] or ''):
            # Indent the setup so it lines up inside the function body.
            for line in tr[0].splitlines():
                extra_setups.append(line if line.startswith('    ') else '    ' + line)
            call_args.append(tr[1])
            if tr[2]:
                extra_posts.append(tr[2] if tr[2].startswith('    ') else '    ' + tr[2])
            continue
        if ht and ht.get('kind') == 'pointer':
            # Translate wasm offset to host pointer for non-struct
            # pointer args (e.g. `const char *path` for stat). NULL
            # passthrough is mandatory here: `gettimeofday(_, NULL)`
            # is a normal POSIX call and glibc writes 8 bytes through
            # a non-NULL `tz` (the obsolete `struct timezone` slot).
            # Translating wasm 0 to `ctx->memory + 0` makes glibc
            # smash the start of linear memory — surfaced as a stack
            # canary trip in nvim's logger function. struct_convert
            # is a curated table (see hooks.yaml), so adding NULL
            # passthrough here is safe: every fn in that table
            # accepts NULL for its non-struct pointer args (path
            # args are required-non-NULL by the libc spec, but we
            # don't second-guess buggy callers).
            ht_str = host_t or 'void *'
            if i in path_arg_idxs:
                # PATH arg: prepend ctx->cwd if relative. See
                # comment on _PATH_ARG_FNS at top of this file.
                call_args.append(
                    f'({wname}) ? ({ht_str})yos_path_resolve(ctx, '
                    f'(const char *)(ctx->memory + {wname})) : ({ht_str})0'
                )
            else:
                call_args.append(
                    f'({wname}) ? ({ht_str})(ctx->memory + {wname}) : ({ht_str})0'
                )
        elif host_t:
            if is_at_family and i == 0:
                # *at family: translate dfd via yos_xlate_dfd — see the
                # _AT_FAMILY comment near the top of this file.
                call_args.append(
                    f'yos_xlate_dfd(ctx, (int32_t)({host_t}){wname})')
            elif at_flag_idx is not None and i == at_flag_idx:
                # *at family: translate FreeBSD-shape AT_* flag bits to
                # the host's. See _AT_FLAG_ARG comment.
                call_args.append(
                    f'({host_t})yos_at_flags_fb_to_lx((int)({host_t}){wname})')
            else:
                call_args.append(f'({host_t}){wname}')
        else:
            call_args.append(wname)

    # Compose the body.
    body_lines = [
        f'{sig} {{',
        '    extern int errno;',
        f'    struct {sc_type} host_{sc_type}_scratch;',
    ]
    if struct_arg_idx < 0:
        # Couldn't find the struct arg — bail out, caller falls
        # through to passthrough/TODO stub.
        return False

    if in_arg:
        body_lines.append(
            f'    cv_{sc_type}_w2h(&host_{sc_type}_scratch, '
            f'(const uint8_t *)(ctx->memory + {struct_wname}));'
        )
    else:
        # OUT-only: zero scratch in case host fn returns early.
        body_lines.append(
            f'    memset(&host_{sc_type}_scratch, 0, sizeof host_{sc_type}_scratch);'
        )

    # Narrow-pointer thunks for non-struct args (e.g. localtime_r's
    # `const time_t *` where wasm time_t is 4B but host is 8B).
    body_lines.extend(extra_setups)

    body_lines.append('    errno = 0;')
    hret = _host_type(h_types.get(hf['ret']), h_types) or 'int'
    # Detect when the host return is a pointer (e.g. localtime_r,
    # gmtime_r return `struct tm *`). For those, success is "non-NULL
    # return pointing at our scratch", and the wasm guest expects the
    # offset of its own out-struct slot — NOT a meaningless cast of
    # the host pointer to int. Truncating an 8-byte pointer to a
    # 4-byte int also corrupts adjacent memory if the caller
    # interprets it as a pointer.
    h_ret_resolved = _resolve(h_types.get(hf['ret']), h_types)
    ret_is_ptr = bool(h_ret_resolved and h_ret_resolved.get('kind') == 'pointer')
    call = f'{name}({", ".join(call_args)})'
    body_lines.append(f'    {hret} _r = {call};')

    if out_arg:
        # Copy back on success. For void-returning fns (rare) always
        # copy. For pointer-returning fns: success is `_r != NULL`.
        # For int-returning, only on _r >= 0 — failure paths mustn't
        # trample the guest's slot.
        if hret == 'void':
            cond = ''
        elif ret_is_ptr:
            cond = '    if (_r)\n    '
        else:
            cond = '    if (_r >= 0)\n    '
        body_lines.append(
            f'{cond}    cv_{sc_type}_h2w((uint8_t *)(ctx->memory + {struct_wname}), '
            f'&host_{sc_type}_scratch);'
        )

    # Narrow-pointer post-writebacks (e.g. inout time_t *). Most
    # struct_convert IN-only thunks emit nothing here, but keep the
    # plumbing symmetric.
    body_lines.extend(extra_posts)

    # Errno mapping. Only meaningful for int-returning fns; pointer-
    # returning fns signal failure with NULL and may set errno too,
    # but we still want to copy that across.
    if any(b in (hret or '') for b in ('int', 'long', 'ssize_t', 'off_t', 'pid_t')) \
            or ret_is_ptr:
        body_lines.append('    if (errno) {')
        body_lines.append('        extern int yos_remap_errno_h2g(int);')
        body_lines.append('        int _e = yos_remap_errno_h2g(errno);')
        body_lines.append('        if (ctx && ctx->memory && ctx->errno_off)')
        body_lines.append('            *(int *)(ctx->memory + ctx->errno_off) = _e;')
        body_lines.append('    }')

    if wret == 'void':
        body_lines.append('    (void)_r;')
    elif ret_is_ptr and out_arg:
        # Pointer-returning struct_convert fns: convention is "return
        # the out-arg buffer on success, NULL on failure". Hand the
        # guest its own out-buffer offset back so `r == &tm` holds.
        body_lines.append(f'    return _r ? {struct_wname} : 0u;')
    elif ret_is_ptr:
        # Pointer return with no out-arg buffer to alias to: bail
        # out, no faithful translation possible without an allocator.
        body_lines.append(f'    return 0u;')
    else:
        body_lines.append(f'    return ({wret})_r;')
    body_lines.append('}')
    defs.append('\n'.join(body_lines))
    return True


def emit_bridge(analyse: dict, guest_api: dict, host_api: dict,
                hooks: dict[str, str] | None = None,
                sc_meta: dict[str, dict] | None = None) -> tuple[str, str, dict, str]:
    hooks = hooks or {}
    g_fns, h_fns = guest_api.get('functions', {}), host_api.get('functions', {})
    g_types, h_types = guest_api.get('types', {}), host_api.get('types', {})

    # `compatible` (types match exactly) and `mechanical` (types differ
    # only in safe widen/narrow/scalar casts) are both auto-bridgeable
    # by _emit_bridge — the cast at the host call site handles the
    # mechanical deltas. needs_policy / unsupported / variadics still
    # have to fall through as stubs.
    candidates = (
        list(analyse.get('compatible') or [])
        + list((analyse.get('mechanical') or {}).keys())
    )
    stub_only  = (
        list((analyse.get('needs_policy') or {}).keys())
        + list((analyse.get('unsupported') or {}).keys())
        + list(analyse.get('variadic_skipped') or [])
        # guest_only — function exists in the FreeBSD guest header set
        # but the host libc doesn't expose it (e.g. sched_getparam on
        # darwin). Without a bridge the wasm import is unresolved and
        # any call traps the guest. Emit an ENOSYS stub so the call
        # returns -1/errno=ENOSYS instead — sane libc behaviour and
        # callers that probe for feature support get a clean signal.
        + list(analyse.get('guest_only') or [])
    )

    # Anything in hooks.yaml that is NOT already in the analyse-report
    # (e.g. fork/getpid/pthread_create are needs_policy on size, but
    # we still want bridges for them via custom_proc/custom_pthread).
    # Add them to candidates so emit_bridge processes them.
    hooked_only = [n for n in hooks
                   if n not in candidates and n not in stub_only]
    stub_only += hooked_only

    decls: list[str] = []
    defs:  list[str] = []
    sigs:  dict[str, tuple[str, list[str]]] = {}  # name -> (ret_char, arg_chars)
    guest_decls: list[tuple[str, str, list[str]]] = []
    counts = {
        'passthrough_real': 0, 'passthrough_stub': 0,
        'custom_routed':    0,
        'enosys_stub':      0,
        'variadic_skipped': 0, 'runtime_skipped': 0,
        'skipped':          0, 'guest_decls':     0,
    }

    def _wargs_decl(gf):
        """Render the wasm-ABI parameter declarations for a function.
        Returns (list_of_decls, ok).  Variadic functions get an extra
        `uint32_t _va_ptr` to match clang's wasm32 ABI lowering."""
        out = []
        for i, ga in enumerate(gf.get('args', [])):
            gt = g_types.get(ga['type_uid'])
            d = _bridge_arg_decl(ga.get('name'), i, gt, g_types)
            if d is None:
                return None, False
            out.append(d)
        if gf.get('variadic'):
            out.append('uint32_t _va_ptr')
        return out, True

    for name in sorted(set(candidates) | set(stub_only)):
        category = hooks.get(name, 'passthrough')

        # ── Skipped entirely: codegen emits no body, no wrapper, no
        # link entry. Hand-written elsewhere. ─────────────────────────
        if category == 'runtime_owned':
            counts['runtime_skipped'] += 1
            continue
        if category == 'variadic':
            counts['variadic_skipped'] += 1
            continue

        gf, hf = g_fns.get(name), h_fns.get(name)
        if not gf:
            counts['skipped'] += 1
            continue
        # Host fn isn't strictly required for routed (custom_*) bridges,
        # but is required for passthrough bodies that call host libc.
        # If the host doesn't have it (FreeBSD-only fn on a darwin host,
        # e.g. sched_getparam, pthread_attr_get_np), reroute to ENOSYS
        # stub so the wasm import resolves to *something* and the guest
        # gets a clean errno rather than trapping with "unresolved
        # import". Without this every host-API gap becomes a hard
        # abort the moment the guest happens to call that fn.
        if category == 'passthrough' and not hf:
            category = 'stub'

        wsig = _wasm_sig(name, gf, g_types)
        if wsig is None:
            counts['skipped'] += 1
            continue

        # Always emit a guest-side import-decorated decl so apps can
        # call the function and clang turns it into an env import —
        # except for variadic / runtime_owned (already handled above).
        ret_spelling = _guest_type(g_types.get(gf['ret']), g_types)
        arg_spellings = []
        guest_ok = ret_spelling is not None
        for ga in gf.get('args', []):
            spell = _guest_type(g_types.get(ga['type_uid']), g_types)
            if spell is None:
                guest_ok = False
                break
            arg_spellings.append(spell)
        if guest_ok:
            guest_decls.append((name, ret_spelling, arg_spellings))
            counts['guest_decls'] += 1

        # ── Custom routing — bridge.py emits ONLY a forward decl + the
        # m3w wrapper. The yos_<name>(ctx, ...) body is in
        # src/yos/impl/<area>.c. ─────────────────────────────────────
        if category.startswith('custom_'):
            wargs, ok = _wargs_decl(gf)
            if not ok:
                counts['skipped'] += 1
                continue
            wret = _wasm_type(g_types.get(gf['ret']), g_types) or 'int32_t'
            decl = (f'extern {wret} yos_{name}(struct yos_exec_ctx *ctx'
                    f'{(", " + ", ".join(wargs)) if wargs else ""});')
            decls.append(decl)
            counts['custom_routed'] += 1
            sigs[name] = wsig
            continue

        # ── Auto save/restore wrapper: bridge.py emits the entire
        # per-ctx safety dance around a host libc call. No human
        # input needed beyond declaring the global in the policy. ─────
        if category == 'auto_save_restore':
            wargs, ok = _wargs_decl(gf)
            if not ok:
                counts['skipped'] += 1
                continue
            wret = _wasm_type(g_types.get(gf['ret']), g_types) or 'int32_t'
            hret = _host_type(h_types.get((hf or {}).get('ret', '')), h_types) \
                   if hf else None
            host_call_args = []  # we don't translate args here — that
            # IS the work the regular bridge does. For auto_save_restore
            # we forward args POSITIONALLY using their wasm-side names
            # because the legacy hand bridges that touch _res etc. take
            # only scalars + char* args; the few pointer args are
            # already wasm offsets, and the wrapped fn here is what the
            # WASM GUEST already calls (so its host-libc signature
            # accepts wasm-shaped args too via auto-bridge). In other
            # words: this category is for fns whose normal passthrough
            # body would be the right thing IF the global isolation
            # were honoured — we wrap the passthrough body in a save/
            # restore frame keyed by per-ctx state.
            sig = (f'{wret} yos_{name}(struct yos_exec_ctx *ctx'
                   f'{(", " + ", ".join(wargs)) if wargs else ""})')
            decls.append(sig + ';')
            auto_g = (sc_meta.get('__auto_fns__') or {}).get(name, [])
            auto_meta_all = sc_meta.get('__auto_globals__') or {}
            # Build save/restore lines per touched global.
            saves, applies, writebacks, restores = [], [], [], []
            lock_names = []
            for gname in auto_g:
                gmeta = auto_meta_all.get(gname)
                if not gmeta:
                    continue
                # `extern T G;` to reach the host global.
                # For struct types, the size may be large — memcpy via
                # local snapshot rather than scalar copy.
                t = gmeta['type']
                saves.append(f'        {t} __saved_{gname} = {gname};')
                applies.append(f'        {gname} = ctx->autoglobals.{gname};')
                writebacks.append(f'        ctx->autoglobals.{gname} = {gname};')
                restores.append(f'        {gname} = __saved_{gname};')
                lock_names.append(gname)
            # One mutex shared by ALL autoglobals — keeps total lock
            # count fixed at 1 across yos's lifetime regardless of how
            # many globals the policy auto-isolates. Contention is
            # acceptable: these fns are low-frequency.
            wargs_callthrough = ', '.join(a.split()[-1] for a in wargs) if wargs else ''
            host_arg_casts = ''
            if hf:
                # Same cast logic as the passthrough path; reuse
                # _arg_translation_full to get scalar widen/narrow
                # right.
                tr_setups = []
                tr_calls = []
                for i, (ga, ha) in enumerate(zip(gf.get('args', []),
                                                 hf.get('args', []))):
                    gt = g_types.get(ga['type_uid'])
                    ht = h_types.get(ha['type_uid'])
                    tr = _arg_translation_full(ga.get('name'), i,
                                               gt, ht, g_types, h_types)
                    if tr is None:
                        tr = ('', f'a{i}', '')
                    if tr[0]:
                        tr_setups.append(tr[0])
                    tr_calls.append(tr[1])
                host_arg_casts = ', '.join(tr_calls)
                tr_setups_block = '\n'.join(tr_setups)
            else:
                tr_setups_block = ''
                host_arg_casts = wargs_callthrough
            ret_decl = '' if wret == 'void' else f'        {wret} _rc;'
            ret_assign = '' if wret == 'void' else '_rc = '
            ret_return = '' if wret == 'void' else '        return _rc;'
            host_call = f'{name}({host_arg_casts})'
            body = (
                f'{sig} {{\n'
                f'    /* AUTO save/restore wrapper. Touches: {", ".join(lock_names)}.\n'
                f'     * Emitted by bridge.py from policies/libc.yaml\n'
                f'     * `leaks.*.auto_save_restore`. */\n'
                f'    extern pthread_mutex_t yos_autoglobals_lock;\n'
                f'    (void)ctx;\n'
                + ''.join(f'    (void){a.split()[-1]};\n' for a in wargs)
                + (tr_setups_block + '\n' if tr_setups_block else '')
                + f'    pthread_mutex_lock(&yos_autoglobals_lock);\n'
                f'    {{\n'
                + '\n'.join(saves) + '\n'
                + '\n'.join(applies) + '\n'
                + (f'{ret_decl}\n' if ret_decl else '')
                + f'        {ret_assign}{host_call};\n'
                + '\n'.join(writebacks) + '\n'
                + '\n'.join(restores) + '\n'
                + f'    pthread_mutex_unlock(&yos_autoglobals_lock);\n'
                + (f'{ret_return}\n' if ret_return else '')
                + '    }\n'
                + '}\n'
            )
            defs.append(body)
            counts['custom_routed'] += 1  # accounting bucket
            sigs[name] = wsig
            continue

        # ── Policy refusal: a loud ENOSYS body naming the leak. ──────
        # Triggered when the policy file's `leaks.*.unbridge:` lists this
        # function. We use the same body shape as `stub` but write a
        # comment naming the global(s) the function would corrupt, and
        # emit a one-line stderr message at runtime so a guest's first
        # call surfaces the refusal instead of returning -1 silently.
        if category == 'policy_refused':
            wargs, ok = _wargs_decl(gf)
            if not ok:
                counts['skipped'] += 1
                continue
            wret = _wasm_type(g_types.get(gf['ret']), g_types) or 'int32_t'
            g_ret_resolved2 = _resolve(g_types.get(gf['ret']), g_types)
            is_ptr = (g_ret_resolved2 and
                      g_ret_resolved2.get('kind') == 'pointer')
            refused_globals = (sc_meta.get('__refused_globals__') or {}).get(name, [])
            globals_str = ', '.join(refused_globals) if refused_globals else '?'
            sig = (f'{wret} yos_{name}(struct yos_exec_ctx *ctx'
                   f'{(", " + ", ".join(wargs)) if wargs else ""})')
            decls.append(sig + ';')
            if wret == 'void':
                body_extra = ''
            elif is_ptr:
                body_extra = f'    return ({wret})0;\n'
            else:
                body_extra = (
                    '    extern int yos_remap_errno_h2g(int);\n'
                    '    if (ctx && ctx->memory && ctx->errno_off)\n'
                    '        *(int *)(ctx->memory + ctx->errno_off) =\n'
                    '            yos_remap_errno_h2g(38 /* ENOSYS */);\n'
                    f'    return ({wret})-1;\n'
                )
            defs.append(
                f'{sig} {{\n'
                f'    /* {name}: refused by globals policy. Touches: '
                f'{globals_str}.\n'
                f'     * See build-tools/libbridge/policies/libc.yaml. To unblock,\n'
                f'     * promote the global(s) to bridged_per_ctx + write the\n'
                f'     * per-ctx impl. */\n'
                f'    (void)ctx;\n'
                + ''.join(f'    (void){a.split()[-1]};\n' for a in wargs)
                + f'    static int _warned = 0;\n'
                  f'    if (!_warned) {{ _warned = 1; '
                  f'fprintf(stderr, "yos: {name} refused by globals policy '
                  f'(touches {globals_str})\\n"); }}\n'
                + body_extra
                + f'}}'
            )
            counts['enosys_stub'] += 1
            sigs[name] = wsig
            continue

        # ── Hand-marked stub: -ENOSYS body. ───────────────────────────
        if category == 'stub':
            wargs, ok = _wargs_decl(gf)
            if not ok:
                counts['skipped'] += 1
                continue
            wret = _wasm_type(g_types.get(gf['ret']), g_types) or 'int32_t'
            # Stub return value:
            #   pointer  → NULL (caller dereferences; -38 looks like a
            #             plausible address and crashes the next deref)
            #   integer  → -1, with errno set to ENOSYS in the per-ctx
            #             slot (POSIX libc convention — many callers
            #             test `rc == -1` rather than `rc < 0`; libuv's
            #             `if (kqueue() == -1)` was the canary)
            #   void     → no return statement
            g_ret_resolved2 = _resolve(g_types.get(gf['ret']), g_types)
            is_ptr = (g_ret_resolved2 and
                      g_ret_resolved2.get('kind') == 'pointer')
            sig  = (f'{wret} yos_{name}(struct yos_exec_ctx *ctx'
                    f'{(", " + ", ".join(wargs)) if wargs else ""})')
            decls.append(sig + ';')
            if wret == 'void':
                body_extra = ''
            elif is_ptr:
                body_extra = f'    return ({wret})0;\n'
            else:
                body_extra = (
                    '    extern int yos_remap_errno_h2g(int);\n'
                    '    if (ctx && ctx->memory && ctx->errno_off)\n'
                    '        *(int *)(ctx->memory + ctx->errno_off) =\n'
                    '            yos_remap_errno_h2g(38 /* ENOSYS */);\n'
                    f'    return ({wret})-1;\n'
                )
            defs.append(
                f'{sig} {{\n'
                f'    /* {name}: hooks.yaml -> stub (Linux-only or unportable). */\n'
                f'    (void)ctx;\n'
                + ''.join(f'    (void){a.split()[-1]};\n' for a in wargs)
                + body_extra
                + f'}}'
            )
            counts['enosys_stub'] += 1
            sigs[name] = wsig
            continue

        # ── struct_convert: bridge body uses cv_<name>_h2w / w2h to
        # marshal the differing host/wasm32 layouts. Driven by the
        # struct_convert: section of hooks.yaml — each entry names the
        # wasm32 struct (`type:`), and which arg is `in:` / `out:`.
        # We allocate a host scratch struct, w2h the input, call host
        # libc, h2w the output. ────────────────────────────────────────
        if category == 'struct_convert' and sc_meta and name in sc_meta:
            meta = sc_meta[name]
            if hf and _emit_struct_convert_body(
                    name, gf, hf, g_types, h_types, meta,
                    decls, defs):
                counts['struct_convert'] = counts.get('struct_convert', 0) + 1
                sigs[name] = wsig
                continue
            # If the smart body couldn't be emitted (signature edge
            # case), fall through to passthrough — caller still gets
            # SOME bridge, possibly with the layout bug, but at
            # least the build doesn't break.

        # ── Passthrough: existing emitter writes a yos_<name>(ctx, ...)
        # body that calls host libc. ─────────────────────────────────
        if not hf:
            counts['skipped'] += 1
            continue
        pub = _has_public_header(name, h_fns)
        decl, body = _emit_bridge(name, gf, hf, g_types, h_types,
                                  has_public_header=pub)
        decls.append(decl)
        defs.append(body)
        if 'TODO' in body:
            counts['passthrough_stub'] += 1
        else:
            counts['passthrough_real'] += 1
        sigs[name] = wsig

    # Collect the host-libc headers we need to include so the bridge
    # bodies can call the real functions by name. Pulled from the
    # `header:` tags extract.py recorded.
    host_call_names = list(analyse.get('compatible') or []) + stub_only
    host_includes = _collect_includes(host_call_names, h_fns)
    # Plus a few canonical headers our bodies always reach for.
    for fixed in ('errno.h', 'stdint.h', 'unistd.h', 'stdlib.h',
                  'stdio.h', 'string.h', 'time.h', 'signal.h',
                  'sys/types.h'):
        if fixed not in host_includes:
            host_includes.append(fixed)
    host_includes.sort()

    # Per-function wasm3 raw wrappers + the linker.
    wrappers = [_emit_m3_wrapper(name, rchar, args,
                                 gf=g_fns.get(name), gtypes=g_types)
                for name, (rchar, args) in sorted(sigs.items())]
    linker = _emit_link_imports(sigs)

    h = (_BRIDGE_PROLOGUE
         + '#ifndef YOS_BRIDGE_H\n#define YOS_BRIDGE_H\n\n#include <stdint.h>\n'
           '\n/* The full struct is defined in src/yos/yos-types.h. Bridges\n'
           ' * only ever touch ctx->wasm_memory, so the forward decl plus\n'
           ' * that field via the public typedef is enough at the call\n'
           ' * site. Linking pulls in the real definition. */\n'
           'struct yos_exec_ctx;\n\n'
           '/* Forward decl of wasm3 module handle; full type comes from wasm3.h. */\n'
           'struct M3Module;\n'
           'typedef struct M3Module *IM3Module;\n\n'
           '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
           '/* Bind every yos_<fn> bridge as a wasm import in module `env`.\n'
           ' * Tolerates missing imports; returns -1 only on a real wasm3\n'
           ' * link error. Renamed from yos_link_imports to avoid clashing\n'
           ' * with the runtime\'s own linker in main.c. */\n'
           'int yos_brg_link_imports(IM3Module mod);\n\n'
         + '\n'.join(decls)
         + '\n\n#ifdef __cplusplus\n}\n#endif\n'
           '#endif /* YOS_BRIDGE_H */\n')

    include_block = '\n'.join(f'#include <{p}>' for p in host_includes)
    # Headers declared by auto_save_restore globals (resolv.h for _res, …)
    auto_globals_meta = sc_meta.get('__auto_globals__') or {}
    auto_includes = sorted({g.get('include')
                            for g in auto_globals_meta.values()
                            if g.get('include')})
    auto_include_block = '\n'.join(f'#include {p}' for p in auto_includes)
    # Macro forms of the global names (e.g. resolv.h `#define _res
    # (*__res_state())`) would expand at every use site below and the
    # save/restore wrapper would assign to a function-call rvalue.
    # Undef the macro, then re-extern the storage so we still reach the
    # actual host symbol.
    auto_undefs = '\n'.join(f'#undef {g}' for g in sorted(auto_globals_meta))
    auto_externs = '\n'.join(
        f'extern {gm["type"]} {g};'
        for g, gm in sorted(auto_globals_meta.items()))
    c = (_BRIDGE_PROLOGUE
         + '#include "yos_bridge.h"\n'
         + '#include "yos/types.h"  /* full struct yos_exec_ctx for ctx->memory */\n'
         + '#include "wasm3.h"     /* m3ApiRawFunction, m3_LinkRawFunction, ... */\n'
         + '#include "yos_struct_convert.h" /* cv_<name>_h2w / w2h */\n'
         + '#include <yos/ytrace/ytrace.h>  /* ytrace() — strace-style per-call line */\n'
         + '#include <pthread.h> /* yos_autoglobals_lock for auto_save_restore */\n'
         + include_block + '\n'
         + (auto_include_block + '\n' if auto_include_block else '')
         + (('/* Auto-globals: disarm macro forms, re-extern raw storage. */\n'
             + auto_undefs + '\n' + auto_externs + '\n\n')
            if auto_globals_meta else '')
         + 'extern int yos_remap_errno_h2g(int);\n'
         + 'extern int yos_xlate_dfd(struct yos_exec_ctx *, int32_t);\n'
         + 'extern int yos_at_flags_fb_to_lx(int);\n'
         + 'extern const char *yos_path_resolve(struct yos_exec_ctx *, const char *);\n'
         + 'extern const char *yos_brg_strarg(struct yos_exec_ctx *, uint32_t);\n'
         + 'extern const char *yos_brg_retstr(long long ret, int host_errno);\n'
         + '/* Single mutex protecting every auto_save_restore wrapper.\n'
         + ' * Low-frequency fns (getopt/tzset/dns/locale); one lock fine. */\n'
         + 'pthread_mutex_t yos_autoglobals_lock = PTHREAD_MUTEX_INITIALIZER;\n\n'
         + '/* ---- bridge bodies (call host libc) ---- */\n\n'
         + '\n\n'.join(defs)
         + '\n\n/* ---- wasm3 raw-function wrappers ---- */\n\n'
         + '\n\n'.join(wrappers)
         + '\n\n/* ---- import linker ---- */\n\n'
         + linker
         + '\n')

    guest_h = _emit_guest_imports_h(sorted(guest_decls,
                                          key=lambda x: x[0]))
    return h, c, counts, guest_h


def _load_hooks(path: Path | None) -> tuple[dict[str, str], dict[str, dict]]:
    """Read hooks.yaml. Returns (cat_map, struct_convert_meta).

      cat_map[name] = category string ('passthrough' for anything not
      listed). struct_convert_meta[name] = {'in': str?, 'out': str?,
      'type': str}.
    """
    if path is None:
        return {}, {}
    raw = yaml.safe_load(path.read_text()) or {}
    cat_map: dict[str, str] = {}
    list_cats = ('custom_proc', 'custom_pthread', 'custom_vfs',
                 'custom_mem', 'custom_alloc', 'custom_sig',
                 'variadic', 'stub', 'runtime_owned')
    for cat in list_cats:
        for name in raw.get(cat) or []:
            cat_map[name] = cat
    sc_meta = raw.get('struct_convert') or {}
    for name in sc_meta:
        cat_map[name] = 'struct_convert'
    return cat_map, sc_meta


def _load_globals_policy(path: Path | None) -> dict:
    """Read build-tools/libbridge/policies/<lib>.yaml.

    Returns a dict shaped:
      {
        'fn_to_globals': {fn_name: [global_name, ...]},
            # for the refusal mechanism — every fn touching a leak
            # global gets refused unless an auto_save_restore (below)
            # or hand impl handles it.

        'auto_globals': {global_name: {type, init, include?, fns: [...]}},
            # leak entries that opt into auto save/restore. bridge.py
            # emits the per-ctx struct field AND the wrapper for each
            # listed fn — those fns get category 'auto_save_restore'
            # instead of 'policy_refused'.

        'auto_fns': {fn_name: [global_name, ...]},
            # inverse: fn → list of auto-isolated globals it touches.
            # A fn may touch multiple auto globals; the wrapper saves/
            # restores all of them.
      }

    Returns empty structure when path is None.
    """
    empty = {'fn_to_globals': {}, 'auto_globals': {}, 'auto_fns': {}}
    if path is None or not path.exists():
        return empty
    raw = yaml.safe_load(path.read_text()) or {}
    classes = raw.get('classes') or {}
    leaks_entries = classes.get('leaks') or []
    fn_to_globals: dict[str, list[str]] = {}
    auto_globals: dict[str, dict] = {}
    auto_fns: dict[str, list[str]] = {}
    for entry in leaks_entries:
        if not isinstance(entry, dict):
            continue
        global_name = entry.get('name')
        if not global_name:
            continue
        unbridge = entry.get('unbridge') or []
        for fn in unbridge:
            fn_to_globals.setdefault(fn, []).append(global_name)
        # auto_save_restore opts the global out of plain refusal:
        # bridge.py will emit per-ctx storage + wrapper for each fn.
        auto = entry.get('auto_save_restore')
        if auto and isinstance(auto, dict):
            auto_globals[global_name] = {
                'type':    auto.get('type', 'int'),
                'init':    auto.get('init', '0'),
                'include': auto.get('include'),  # optional header
                'fns':     list(unbridge),
            }
            for fn in unbridge:
                auto_fns.setdefault(fn, []).append(global_name)
    return {
        'fn_to_globals': fn_to_globals,
        'auto_globals':  auto_globals,
        'auto_fns':      auto_fns,
    }


def main() -> int:
    p = argparse.ArgumentParser(description='Emit yos_bridge.{h,c} from analyse-report.yaml.')
    p.add_argument('--analyse',   required=True, type=Path)
    p.add_argument('--guest-api', required=True, type=Path)
    p.add_argument('--host-api',  required=True, type=Path)
    p.add_argument('--hooks',     required=False, type=Path)
    p.add_argument('--globals-policy', required=False, type=Path,
                   help='build-tools/libbridge/policies/<lib>.yaml — '
                        'functions in `leaks.*.unbridge:` get policy_refused')
    p.add_argument('--out-dir',   required=True, type=Path)
    args = p.parse_args()

    with args.analyse.open()   as f: analyse   = yaml.safe_load(f)
    with args.guest_api.open() as f: guest_api = yaml.safe_load(f)
    with args.host_api.open()  as f: host_api  = yaml.safe_load(f)
    hooks, sc_meta = _load_hooks(args.hooks)

    # Merge in the globals-policy: refusals override `passthrough` and
    # `stub` (those would silently leak) to `policy_refused`. Auto-
    # save/restore fns override to `auto_save_restore` (handled at the
    # emit_bridge layer below). Do NOT override `runtime_owned` /
    # `custom_*` / `variadic` / `struct_convert` — those mean "hand-
    # bound elsewhere by the human, who has presumably already
    # arranged for per-ctx safety." Re-routing those would duplicate
    # symbols.
    policy = _load_globals_policy(args.globals_policy)
    refused      = policy['fn_to_globals']
    auto_globals = policy['auto_globals']
    auto_fns     = policy['auto_fns']
    if refused or auto_globals:
        skipped_handled, refused_count, auto_count = 0, 0, 0
        for fn, globals_touched in refused.items():
            prior = hooks.get(fn, 'passthrough')
            if prior in ('runtime_owned', 'variadic', 'struct_convert') \
                    or prior.startswith('custom_'):
                skipped_handled += 1
                continue
            if fn in auto_fns:
                hooks[fn] = 'auto_save_restore'
                auto_count += 1
            else:
                hooks[fn] = 'policy_refused'
                refused_count += 1
            if prior not in ('policy_refused', 'auto_save_restore', 'passthrough'):
                print(f'[bridge] policy override: {fn}: {prior} → '
                      f'{hooks[fn]} (touches: {", ".join(globals_touched)})',
                      file=sys.stderr)
        # Stash globals-touched table so emit_bridge can write it into
        # the refusal stub comment.
        for fn, gs in refused.items():
            sc_meta.setdefault('__refused_globals__', {})[fn] = gs
        # Stash auto-globals + per-fn refused-globals for emit_bridge.
        sc_meta.setdefault('__refused_globals__', {}).update(refused)
        sc_meta['__auto_globals__'] = auto_globals
        sc_meta['__auto_fns__']     = auto_fns

        report_path = args.out_dir / 'blocked-by-policy.txt'
        args.out_dir.mkdir(parents=True, exist_ok=True)
        with report_path.open('w') as rf:
            rf.write('# Functions named in build-tools/libbridge/policies/libc.yaml\n'
                     '# `leaks.*.unbridge:`. To unblock: add an auto_save_restore\n'
                     '# block to the global, or write a per-ctx impl and promote\n'
                     '# to bridged_per_ctx.\n\n')
            rf.write('# === REFUSED at codegen (was passthrough/stub) ===\n')
            for fn in sorted(refused):
                if hooks.get(fn) == 'policy_refused':
                    rf.write(f'{fn}\ttouches: {", ".join(refused[fn])}\n')
            rf.write('\n# === AUTO save/restore wrappers emitted ===\n')
            for fn in sorted(refused):
                if hooks.get(fn) == 'auto_save_restore':
                    rf.write(f'{fn}\tauto_touches: {", ".join(refused[fn])}\n')
            rf.write('\n# === ALREADY hand-handled (runtime_owned/custom_*) ===\n')
            for fn in sorted(refused):
                if hooks.get(fn) not in ('policy_refused', 'auto_save_restore'):
                    rf.write(f'{fn}\thandled_via: {hooks.get(fn, "?")}\n')
        print(f'[bridge] {refused_count} fns refused, {auto_count} fns auto-wrapped, '
              f'{skipped_handled} already hand-handled → {report_path}',
              file=sys.stderr)

        # Generate the autoglobals header: per-ctx field declarations
        # for each auto global. Sourced into the host build via a
        # top-level #include in types.h, exposing ctx->autoglobals.<G>.
        ah = '/* AUTOGENERATED by src/yos/codegen/bridge.py — DO NOT EDIT.\n'
        ah += ' * Per-ctx storage for libc globals classified leaks+auto_save_restore\n'
        ah += ' * in build-tools/libbridge/policies/libc.yaml. */\n'
        ah += '#ifndef YOS_AUTOGLOBALS_H\n#define YOS_AUTOGLOBALS_H\n\n'
        includes = sorted({g['include'] for g in auto_globals.values()
                           if g.get('include')})
        for inc in includes:
            ah += f'#include {inc}\n'
        # Many libc globals are exposed via macros that resolve to
        # something other than the bare name (e.g. resolv.h's
        # `_res` is `(*__res_state)`). Undef each macro before we
        # declare the field — the macro form is irrelevant to us
        # because we explicitly extern the underlying storage in the
        # generated wrapper.
        ah += '\n/* Disarm macro forms of the global names that would\n'
        ah += ' * collide with our field declarations below. */\n'
        for gname in sorted(auto_globals):
            ah += f'#undef {gname}\n'
        ah += '\nstruct yos_autoglobals {\n'
        if not auto_globals:
            # C requires structs to have at least one member (MSVC errors;
            # gcc accepts as an extension). Emit a token padding byte when
            # the build has no libc autoglobals to track (e.g. Windows
            # builds without libpython / liblua linked).
            ah += '    char _yos_autoglobals_pad;\n'
        else:
            for gname, gmeta in sorted(auto_globals.items()):
                ah += f'    /* {gname}: {gmeta["type"]} */\n'
                ah += f'    {gmeta["type"]} {gname};\n'
        ah += '};\n\n'
        # Default initialiser snippet, callable from libc-init.c.
        # memset for struct types (initialiser-list to a struct via cast
        # isn't C99-legal for all clang configurations); init expr is
        # treated as a scalar literal for scalar types and ignored for
        # struct types (we zero-init structs).
        ah += '#include <string.h>\n'
        ah += 'static inline void yos_autoglobals_init(struct yos_autoglobals *a) {\n'
        for gname, gmeta in sorted(auto_globals.items()):
            t = gmeta['type'].strip()
            if t.startswith('struct ') or t.startswith('union '):
                ah += f'    memset(&a->{gname}, 0, sizeof(a->{gname}));\n'
            else:
                ah += f'    a->{gname} = ({t}){gmeta["init"]};\n'
        ah += '}\n\n'
        ah += '#endif\n'
        (args.out_dir / 'yos_autoglobals.h').write_text(ah)
        print(f'[bridge] generated yos_autoglobals.h '
              f'({len(auto_globals)} globals, {len(auto_fns)} wrapped fns)',
              file=sys.stderr)

    h, c, counts, guest_h = emit_bridge(analyse, guest_api, host_api, hooks, sc_meta)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / 'yos_bridge.h').write_text(h)
    (args.out_dir / 'yos_bridge.c').write_text(c)
    (args.out_dir / 'yos_imports.h').write_text(guest_h)

    print(
        f'[bridge] passthrough={counts["passthrough_real"]}  '
        f'passthrough-stub={counts["passthrough_stub"]}  '
        f'custom-routed={counts["custom_routed"]}  '
        f'enosys-stub={counts["enosys_stub"]}  '
        f'variadic-skipped={counts["variadic_skipped"]}  '
        f'runtime-skipped={counts["runtime_skipped"]}  '
        f'skipped={counts["skipped"]}  '
        f'guest-imports-h={counts["guest_decls"]}',
        file=sys.stderr)
    print(f'         wrote yos_bridge.h, yos_bridge.c, yos_imports.h → {args.out_dir}',
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
