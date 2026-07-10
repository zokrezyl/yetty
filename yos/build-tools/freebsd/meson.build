# Fetch FreeBSD source, pinned by version + sha256 from meson options.
#
# Output layout (under <meson-build-dir>/build-tools/freebsd/):
#   src.txz             the verified tarball (cached; re-runs are no-ops)
#   src/                extracted FreeBSD source tree
#   src/.extracted      stamp file so meson can chain dependencies
#
# Subsequent steps (make-includes-i386, make-includes-amd64, …) will
# consume `src/` as their input. They live in sibling meson.build
# files added later — this file is just the fetch layer.

# `py` is set by the root meson.build (project venv if present).
freebsd_version    = get_option('freebsd_version')
freebsd_url_tpl    = get_option('freebsd_src_url')
freebsd_sha256     = get_option('freebsd_src_sha256')

# Substitute @VERSION@ in the URL template.
freebsd_url = freebsd_url_tpl.replace('@VERSION@', freebsd_version)

# `sha256:` empty in options means "first-fetch, print observed hash".
# Once the user copies that hash into meson_options.txt the build
# becomes reproducible.
fetch_args = [
  py, files('fetch.py'),
  '--version', freebsd_version,
  '--url',     freebsd_url,
  '--out',     '@OUTDIR@',
]
if freebsd_sha256 != ''
  fetch_args += ['--sha256', freebsd_sha256]
endif

# Tarball cache target. Idempotent: skips download if the tarball is
# already on disk and the sha256 matches (or no hash was pinned).
freebsd_src_txz = custom_target(
  'freebsd-src-txz',
  output           : 'src.txz',
  command          : fetch_args + ['--mode', 'download'],
  build_by_default : false,
  console          : true,
)

# Extracted source tree. Depends on the tarball; produces the
# .extracted stamp so downstream targets can list it as input.
freebsd_src = custom_target(
  'freebsd-src',
  input            : freebsd_src_txz,
  output           : 'src',
  command          : [
    py, files('fetch.py'),
    '--mode',    'extract',
    '--archive', '@INPUT@',
    '--out',     '@OUTDIR@',
  ],
  build_by_default : false,
  console          : true,
)

# Convenience alias: `ninja freebsd-src` from the build dir grabs both.
alias_target('freebsd-src-all', freebsd_src)


# ─── Guest UAPI tree (i386 only) ─────────────────────────────────────
#
# Curates a clean usr/include/ + usr/share/syscalls/ from the
# extracted FreeBSD source as the GUEST sysroot reference (wasm32).
# Output:
#   <build>/build-tools/freebsd/headers-i386/usr/include/
#   <build>/build-tools/freebsd/headers-i386/usr/share/syscalls/
#
# We do NOT generate a freebsd-headers-amd64 here — that would only be
# the right "host" ABI reference if the build host runs FreeBSD. The
# host reference comes from build-tools/host-libc/ instead, which
# snapshots whatever libc headers the build host's clang sees.

freebsd_headers_i386 = custom_target(
  'freebsd-headers-i386',
  input            : freebsd_src,
  output           : 'headers-i386',
  command          : [
    py, files('install_includes.py'),
    '--src-root', '@INPUT@/usr/src',
    '--out-root', '@OUTPUT@',
    '--arch',     'i386',
  ],
  build_by_default : false,
  console          : true,
)

alias_target('freebsd-headers', freebsd_headers_i386)
