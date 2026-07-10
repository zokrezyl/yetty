# build-tools — meson helpers that produce inputs other parts of the
# build consume. Subdirs by component:
#
#   freebsd/    fetches FreeBSD source + curates a UAPI tree; output
#               is the GUEST sysroot reference (wasm32 i386 view).
#               extract.py walks it to fill the guest_* sections of
#               abi.yaml.
#
#   host-libc/  snapshots the build host's libc include search path
#               (glibc on Linux, libSystem on Darwin, FreeBSD libc on
#               FreeBSD). extract.py walks this as the HOST ABI
#               reference — yos's host runtime calls host libc and
#               must see its struct shapes for the converter.

subdir('freebsd')
subdir('host-libc')
# wasm3 lives under src/wasm3/ (vendored source); its meson.build is
# included from src/meson.build BEFORE subdir('yos').
# mimalloc removed — the guest-facing allocator (impl/alloc.c) now
# keeps all its state IN the wasm linear memory, so there's no host-
# side bookkeeping to clean up on execve and we don't need mimalloc's
# host runtime at all. See the comment at the top of impl/alloc.c.
# Tier-2 sidecar removed — all libc fns are resolved as direct yos
# host bridges (auto-passthrough or hand-written yos_<name>). No
# second wasm3 instance is loaded at runtime.
# wasm sysroot for guest .c compilation. POSIX hosts use skel.sh
# (bash + llvm-ar + ln -s); Windows uses skel.py (Python + llvm-ar +
# mklink /J fallback) — picked inside sysroot/meson.build.
subdir('sysroot')
