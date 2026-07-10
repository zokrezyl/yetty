# Snapshot the build host's libc include search path.
#
# This produces a per-host directory whose contents describe what
# `cc /tmp/x.c` would see for `<stdio.h>`, `<sys/stat.h>`, …. The
# extractor walks it as the "host64" ABI reference for the struct
# converter — no use shipping kernel-internal Linux headers when our
# real comparison is against the host libc yos links to.
#
# Output:
#   <build>/build-tools/host-libc/snapshot/
#     manifest.txt          ordered list of include dirs
#     include/01-…/         symlinks to each clang search path
#     .snapshotted          stamp

# `py` is set by the root meson.build (project venv if present).
host_libc_snapshot = custom_target(
  'host-libc-snapshot',
  output           : 'snapshot',
  command          : [
    py, files('snapshot.py'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : false,
  console          : true,
)

alias_target('host-libc-headers', host_libc_snapshot)
