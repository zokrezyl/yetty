# build-tools/sysroot — wasm sysroot for the guest.
#
# Layout (under <build>/sysroot/):
#   usr/include/         symlinked from build-tools/freebsd/headers-i386/usr/include/
#   include/yos_imports.h (auto-generated, force-included via -include)
#
# A guest .c is compiled with:
#
#   clang -target wasm32-unknown-unknown -nostdlib \
#         --sysroot=<build>/sysroot \
#         -isystem <build>/sysroot/usr/include \
#         -include yos_imports.h \
#         -O2 ...
#
# The -include yos_imports.h forces the auto-generated import-attributed
# redeclarations into every TU. Clang merges them with the FreeBSD-shape
# declarations from the system headers canonical-typewise, so every
# user-code call to a libc fn gets the import attribute and emits an
# `(import "env" "<name>")` in the .wasm.

# Stamp file for the symlink-skeleton — depends on the FreeBSD
# headers so meson rebuilds whenever they regenerate.
sysroot_dir         = meson.project_build_root() / 'sysroot'
sysroot_imports_dir = meson.project_build_root() / 'src' / 'yos' / 'codegen'

if host_machine.system() == 'windows'
  # Windows: Python emitter, no bash needed; mklink /J directory
  # junctions when symlinks are denied (default for unprivileged users
  # without dev-mode).
  sysroot_skel = custom_target(
    'sysroot-skel',
    input            : freebsd_headers_i386,
    output           : 'sysroot.stamp',
    command          : [py, files('skel.py'), '@INPUT@', '@OUTPUT@', sysroot_dir],
    build_by_default : true,
    console          : true,
  )
else
  sysroot_skel = custom_target(
    'sysroot-skel',
    input            : freebsd_headers_i386,
    output           : 'sysroot.stamp',
    command          : [files('skel.sh'), '@INPUT@', '@OUTPUT@', sysroot_dir],
    build_by_default : true,
    console          : true,
  )
endif
