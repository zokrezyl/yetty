# src/yos/codegen — the whole codegen pipeline.
#
# Single subdir holds all codegen scripts + their inputs (hooks.yaml,
# entry_map.yaml). Outputs go to <build>/src/yos/codegen/.
#
# Pipeline (each stage's output is the next stage's input):
#
#   freebsd-headers-i386 ─┐
#                         ├─→ extract.py ──→ guest-api-i386-freebsd.yaml
#                         │                  + host-api.yaml
#   host-libc-snapshot   ─┘
#                                 │
#                                 ↓
#                            compare.py ──→ compare-report.yaml
#                                 │
#                                 ↓
#                            analyse.py ──→ analyse-report.yaml
#                                 │
#                                 ↓
#                            bridge.py  ──→ yos_bridge.{c,h}
#                            generate.py    yos_imports.h
#                                            yos_remap.{c,h}
#                                            hooks_todo.yaml
#                                            stats.txt
#
# `hooks.yaml` (in this dir, source-tree) drives bridge.py's per-fn
# routing: passthrough / custom_vfs / custom_proc / custom_pthread /
# custom_mem / custom_alloc / custom_sig / struct_convert / variadic /
# stub / runtime_owned. See CLAUDE.md "Codegen strategy" + the file
# header in hooks.yaml itself.

# ─── extract: guest + host API yaml ──────────────────────────────────

# Top-level headers we want extract.py to walk on BOTH sides. Every
# header in this list must exist on the guest (FreeBSD i386) and is
# expected (or harmless) on the host. Mismatches surface as
# guest_only / host_only entries in analyse-report.yaml.
api_top_headers = [
  'stdio.h', 'stdlib.h', 'string.h', 'strings.h',
  'unistd.h', 'fcntl.h', 'errno.h', 'time.h', 'signal.h',
  'pthread.h', 'semaphore.h',
  'math.h', 'ctype.h', 'wchar.h', 'wctype.h',
  'inttypes.h', 'stdint.h', 'stddef.h', 'limits.h',
  'dirent.h', 'dlfcn.h', 'fnmatch.h', 'glob.h', 'iconv.h',
  'locale.h', 'langinfo.h', 'libgen.h',
  'netdb.h', 'ifaddrs.h', 'poll.h',
  'pwd.h', 'grp.h', 'regex.h', 'search.h',
  'setjmp.h', 'assert.h', 'sched.h',
  'termios.h', 'utime.h',
  # readpassphrase(3): BSD libc, declared in <readpassphrase.h>. ssh
  # / sshd / ssh-add prompt for key passphrases through this; without
  # the header in the walk the wasm guest's import declaration never
  # makes it into guest-api.yaml and bridge.py emits no body — the
  # module then fails to load with "unresolved import env.readpassphrase".
  'readpassphrase.h',
]

# Public subdir umbrellas. Listed by *filename*, not by *directory* —
# walking 'sys/' (or 'machine/') drags in kernel-internal headers
# (sys/_lock.h, sys/proc.h, machine/atomic.h, machine/cpufunc.h, …)
# that no userland code would ever #include directly. Those are i386-
# asm-laden and break extraction under -target wasm32 (clang refuses
# '=q', 'iq', 'inb'/'outb' and hits -ferror-limit, after which the
# whole TU is dropped and half the bridges silently vanish).
#
# Userland gets these transitively via the umbrellas: e.g. <sys/stat.h>
# pulls in <machine/_types.h>, <signal.h> pulls in <machine/signal.h>,
# etc. Each umbrella's own header chain knows which feature macros to
# set and which guards to fire; we don't try to second-guess it.
#
# If a missing-bridge surfaces later, add the responsible umbrella here
# (not the implementation header it ends up reaching).
api_top_subdir_headers = [
  # sys/
  'sys/types.h',    'sys/stat.h',     'sys/socket.h',  'sys/mman.h',
  'sys/wait.h',     'sys/select.h',   'sys/time.h',    'sys/times.h',
  'sys/resource.h', 'sys/uio.h',      'sys/un.h',      'sys/ioctl.h',
  'sys/file.h',     'sys/utsname.h',  'sys/param.h',   'sys/statvfs.h',
  'sys/mount.h',    'sys/sysctl.h',   'sys/random.h',  'sys/poll.h',
  'sys/ucontext.h', 'sys/ipc.h',      'sys/sem.h',     'sys/shm.h',
  'sys/msg.h',      'sys/event.h',    'sys/timeb.h',
  # arpa/
  'arpa/inet.h',    'arpa/telnet.h',  'arpa/nameser.h',
  # net/
  'net/if.h',       'net/if_arp.h',   'net/if_dl.h',   'net/route.h',
  'net/ethernet.h',
  # netinet/
  'netinet/in.h',   'netinet/ip.h',   'netinet/tcp.h', 'netinet/udp.h',
  'netinet/ip_icmp.h',
]

# Build the --restrict-to argument vector once per side. Same set on
# host and guest — feature differences (FreeBSD-only umbrellas missing
# on the host) surface as guest_only/host_only entries in
# analyse-report.yaml, which is the intended signal channel.
guest_restrict_args = []
foreach h : api_top_headers + api_top_subdir_headers
  guest_restrict_args += ['--restrict-to', h]
endforeach
host_restrict_args = []
foreach h : api_top_headers + api_top_subdir_headers
  host_restrict_args += ['--restrict-to', h]
endforeach
# Windows: also walk our compat aggregator so codegen sees the
# explicit re-declarations for symbols MSVC hides behind _time64-style
# macros (time, gmtime, mktime, …) and the POSIX functions MSVC
# doesn't ship in any header (clock_gettime, sigaction, pipe, …).
if host_machine.system() == 'windows'
  host_restrict_args += ['--restrict-to', 'posix_extras.h']
endif

guest_api_yaml = custom_target(
  'guest-api-i386-freebsd',
  input            : freebsd_headers_i386,
  output           : 'guest-api-i386-freebsd.yaml',
  command          : [
    py, files('extract.py'),
    '--name',         'guest-i386-freebsd',
    '--no-stdinc',
    '--include-dir',  '@INPUT@/usr/include',
    '--root-marker',  'usr/include/',
    '--target',       'wasm32-unknown-unknown',
    # The FreeBSD-i386 header chain gates arch-specific decls (mcontext_t
    # in <x86/ucontext.h>, register sizes in <machine/_types.h>, etc.)
    # on `__i386__`. -target wasm32 doesn't set it, so without this define
    # mcontext_t never gets typedef'd and <sys/_ucontext.h> fails. Match
    # the WASM_BASE_CFLAGS the Makefile uses for actual guest compilation.
    '--define',       '__i386__=1',
    '--enum',
    guest_restrict_args,
    '--out',          '@OUTPUT@',
  ],
  build_by_default : false,
  console          : true,
)

# Windows / MSVC headers use __declspec(dllimport) + MSVC pragma packs;
# libclang needs -fms-extensions + a windows-msvc target tuple.
host_extra_extract_args = []
if host_machine.system() == 'windows'
  host_extra_extract_args += [
    '--target',  'x86_64-pc-windows-msvc',
    # `=value` form so argparse doesn't misread the leading `-` of the
    # passthrough argument as the start of another flag.
    '--clang-arg=-fms-extensions',
    '--clang-arg=-fms-compatibility',
    '--clang-arg=-fms-compatibility-version=19.50',
    '--define',  '_WIN32=1',
    '--define',  '_M_X64=100',
    '--define',  'NOMINMAX=1',
    '--define',  'WIN32_LEAN_AND_MEAN=1',
    # The yos compat layer adds POSIX functions msvcrt lacks
    # (clock_gettime, pthread_*, dirent, regex, ...). They live in
    # src/yos/platform/windows/compat/ and the runtime provides bodies
    # in compat_libc.c / pthread_win32.c. Add the compat dir as an
    # include path so libclang picks up the declarations and the
    # bridge generator emits direct passthrough wrappers.
    '--include-dir',
        meson.project_source_root() / 'src' / 'yos' / 'platform'
                                    / 'windows' / 'compat',
    '--root-marker', 'compat/',
  ]
endif

host_api_yaml = custom_target(
  'host-api',
  input            : host_libc_snapshot,
  output           : 'host-api.yaml',
  command          : [
    py, files('extract.py'),
    '--name',         'host',
    # Walk every NN-* subdir the snapshot emitted; basenames differ
    # by host (linux: 01-include / 02-include / 03-x86_64-linux-gnu / …,
    # darwin: 01-include … 09-include + 10-Frameworks). The auto-walk
    # picks up apple-sdk's usr/include where string.h / etc. live.
    '--include-root', '@INPUT@/include',
    '--root-marker',  '/usr/include/',
    '--root-marker',  '/usr/local/include/',
    '--root-marker',  'include/',
    '--enum',
    host_restrict_args,
    host_extra_extract_args,
    '--out',          '@OUTPUT@',
  ],
  build_by_default : false,
  console          : true,
)

alias_target('api', guest_api_yaml, host_api_yaml)

# ─── compare + analyse ──────────────────────────────────────────────

api_compare_report = custom_target(
  'api-compare-report',
  input            : [guest_api_yaml, host_api_yaml],
  output           : 'compare-report.yaml',
  command          : [
    py, files('compare.py'),
    '--guest', '@INPUT0@',
    '--host',  '@INPUT1@',
    '--out',   '@OUTPUT@',
  ],
  build_by_default : false,
  console          : true,
)

api_analyse_report = custom_target(
  'api-analyse-report',
  input            : [api_compare_report, guest_api_yaml, host_api_yaml],
  output           : 'analyse-report.yaml',
  command          : [
    py, files('analyse.py'),
    '--report',    '@INPUT0@',
    '--guest-api', '@INPUT1@',
    '--host-api',  '@INPUT2@',
    '--out',       '@OUTPUT@',
  ],
  build_by_default : false,
  console          : true,
)

alias_target('api-compare', api_compare_report)
alias_target('api-analyse', api_analyse_report)

# ─── generate: remap + hooks_todo + stats ───────────────────────────

api_generate_outputs = custom_target(
  'api-generate-outputs',
  input            : api_analyse_report,
  output           : ['yos_remap.h', 'yos_remap.c', 'hooks_todo.yaml', 'stats.txt'],
  command          : [
    py, files('generate.py'),
    '--analyse', '@INPUT@',
    '--out-dir', '@OUTDIR@',
  ],
  build_by_default : false,
  console          : true,
)

# ─── bridge: yos_bridge.{c,h} + yos_imports.h ───────────────────────

api_bridge_outputs = custom_target(
  'api-bridge-outputs',
  input            : [api_analyse_report, guest_api_yaml, host_api_yaml],
  output           : ['yos_bridge.h', 'yos_bridge.c', 'yos_imports.h',
                      # Auto-isolation outputs (driven by
                      # build-tools/libbridge/policies/libc.yaml):
                      'yos_autoglobals.h',
                      'blocked-by-policy.txt'],
  command          : [
    py, files('bridge.py'),
    '--analyse',   '@INPUT0@',
    '--guest-api', '@INPUT1@',
    '--host-api',  '@INPUT2@',
    '--hooks',     files('hooks.yaml'),
    # libc globals policy — forces refusal stubs for fns whose
    # `unbridge:` entries name them. See build-tools/libbridge/
    # README.md and policies/libc.yaml.
    '--globals-policy',
        files(meson.project_source_root() /
              'build-tools' / 'libbridge' / 'policies' / 'libc.yaml'),
    '--out-dir',   '@OUTDIR@',
  ],
  build_by_default : false,
  console          : true,
)

# ─── struct_convert: per-field host↔wasm32 layout copiers ──────────
#
# Many bridge fns take a struct arg whose host and wasm32 layouts
# differ in size, field offset, or field type. struct_convert.py
# walks the extracted struct definitions and emits two copiers per
# struct (cv_<name>_h2w / cv_<name>_w2h). The bridge bodies for
# affected fns then allocate a host scratch struct, call host libc,
# and h2w-copy back.
api_struct_convert_outputs = custom_target(
  'api-struct-convert',
  input            : [guest_api_yaml, host_api_yaml],
  output           : ['yos_struct_convert.h', 'yos_struct_convert.c'],
  command          : [
    py, files('struct_convert.py'),
    '--guest-api', '@INPUT0@',
    '--host-api',  '@INPUT1@',
    '--out-dir',   '@OUTDIR@',
  ],
  build_by_default : false,
  console          : true,
)

alias_target('api-generate', api_generate_outputs, api_bridge_outputs)
alias_target('api-bridge',   api_bridge_outputs)

# ─── compile generated bridges into a static lib ────────────────────

# We need `yos/types.h` (struct yos_exec_ctx) on the include path —
# yos-private convention `-Isrc` resolves "yos/types.h" to src/yos/.
# From this dir (src/yos/codegen) we reach up two levels to `src/`.
yos_types_inc = include_directories('../../..' / 'src')
yos_inc       = include_directories('../../..' / 'include')

yos_impl_inc  = include_directories('..')

# iOS sim: redirect the 6 libc symbols Apple marks `unavailable` in
# the iPhoneSimulator SDK (system, drem, finite, gamma, significand,
# clock_settime) to dlsym-backed forwarders. The codegen-emitted
# bridges call those names directly, so we `-include` a shim header
# only when building the bridge TU, and link in ios-unblock.c. See
# build-tools/ios/ios-unblock.{h,c}.
_brcc = meson.get_compiler('c')
_br_gnu_like  = _brcc.get_id() in ['gcc', 'clang', 'clang-cl']
_br_msvc_like = _brcc.get_argument_syntax() == 'msvc'

api_bridge_extra_sources = []
api_bridge_extra_c_args  = []
if get_option('ios_unblock')
  ios_unblock_hdr = meson.project_source_root() / 'build-tools' / 'ios' / 'ios-unblock.h'
  ios_unblock_src = files(meson.project_source_root() / 'build-tools' / 'ios' / 'ios-unblock.c')
  api_bridge_extra_c_args += ['-include', ios_unblock_hdr]
  api_bridge_extra_sources += ios_unblock_src
endif
if get_option('tvos_unblock')
  tvos_unblock_hdr = meson.project_source_root() / 'build-tools' / 'tvos' / 'tvos-unblock.h'
  tvos_unblock_src = files(meson.project_source_root() / 'build-tools' / 'tvos' / 'tvos-unblock.c')
  api_bridge_extra_c_args += ['-include', tvos_unblock_hdr]
  api_bridge_extra_sources += tvos_unblock_src
endif

api_bridge_c_args = []
if _br_gnu_like
  api_bridge_c_args += [
    '-D_GNU_SOURCE',
    '-Wno-deprecated-declarations',
    '-Wno-implicit-function-declaration',
    '-Wno-incompatible-pointer-types',
    '-Wno-discarded-qualifiers',
    '-Wno-pointer-sign',
    '-Wno-int-conversion',
    '-Wno-unused-parameter',
    '-Wno-unused-variable',
  ]
endif
if _br_msvc_like
  api_bridge_c_args += [
    '/Zc:preprocessor',
    '/experimental:c11atomics',
    '/D_CRT_SECURE_NO_WARNINGS',
    '/D_CRT_NONSTDC_NO_WARNINGS',
    '/DNOMINMAX',
    '/DWIN32_LEAN_AND_MEAN',
    '/Dstrcasecmp=_stricmp',
    '/Dstrncasecmp=_strnicmp',
    '/external:I' + meson.current_source_dir() / '..' / 'platform' / 'windows' / 'compat',
    '/external:W0',
    '/FIposix_extras.h',
    '/wd4013',  # implicit function declaration
    '/wd4047',  # incompatible pointer types
    '/wd4090',  # discarded const qualifier
    '/wd4133',  # incompatible pointer
    '/wd4100', '/wd4101', '/wd4189',
    '/wd4244', '/wd4267', '/wd4477',
    '/wd4456', '/wd4459', '/wd4701',
    '/wd4706', '/wd4996',
    '/wd5045',
  ]
endif
api_bridge_c_args += api_bridge_extra_c_args

api_bridge_lib = static_library(
  'yos_api_bridge',
  api_bridge_outputs[1],         # yos_bridge.c
  api_generate_outputs[1],       # yos_remap.c
  api_struct_convert_outputs[1], # yos_struct_convert.c
  api_bridge_extra_sources,
  include_directories : [yos_types_inc, yos_inc, yos_impl_inc],
  dependencies        : [wasm3_dep],
  c_args : api_bridge_c_args,
  install : false,
)

# link_whole so every bridge symbol lands in the binary even when
# yos_brg_link_imports() is the only direct caller (the linker
# otherwise drops the m3w_<name> wrappers as unreferenced).
api_bridge_dep = declare_dependency(
  link_whole          : api_bridge_lib,
  include_directories : [
    include_directories('.'),
    yos_types_inc,
    yos_inc,
  ],
  # Force codegen to RUN before any consumer compiles. yos's types.h
  # #includes yos_autoglobals.h which doesn't exist until bridge.py
  # has run; without this `sources:` entry, the impl/*.c files start
  # compiling before the header is generated and fail with "not found".
  # The three .h outputs are the headers consumers can include; .c
  # outputs are already gated via link_whole on api_bridge_lib.
  sources             : [
    api_bridge_outputs[0],   # yos_bridge.h
    api_bridge_outputs[2],   # yos_imports.h
    api_bridge_outputs[3],   # yos_autoglobals.h
  ],
)
