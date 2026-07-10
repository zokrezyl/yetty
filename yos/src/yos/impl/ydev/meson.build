# ydev — cross-platform device abstraction (camera/sensors/location)
# + audio via miniaudio.
#
# Builds a small static library `libydev.a`:
#   - core (this dir): vfd ring + self-pipe primitive, perm cache, strerror,
#                      NV12->I420 helper. Platform-agnostic.
#   - audio (this dir, audio_miniaudio.c + miniaudio_impl.c): one cross-
#                      platform audio backend. miniaudio (Unlicense/MIT-0)
#                      dispatches at runtime to CoreAudio on Apple, AAudio
#                      + OpenSL on Android, ALSA / PulseAudio / JACK /
#                      PipeWire on Linux (dlopen, no LGPL link-time deps).
#   - backend (../../platform/{apple,android,linux}): per-host camera,
#                      sensor, location, permission glue. The right
#                      backend is picked by host_os; only that one is
#                      compiled.
#
# Public headers live in include/yos/ydev/. Anyone (including the camera
# server) can link libydev without pulling in wasm3 or the yos runtime.

# Apple backend sources are Objective-C (.m). meson lazily enables the
# language only when a target has .m files — we ask for it here on darwin.
if host_os == 'darwin'
  add_languages('objc', native: false)
endif

ydev_core_sources = files(
  'core.c',
  'perm.c',
  'error.c',
  'convert.c',
  'audio_miniaudio.c',     # cross-platform audio impl, builds everywhere
  'miniaudio_impl.c',      # MA_IMPLEMENTATION TU
)

# Per-host backend source list (no audio — that's miniaudio above).
ydev_backend_sources = []
ydev_backend_args    = []
ydev_backend_deps    = []
ydev_backend_link    = []

if host_os == 'darwin'
  ydev_backend_sources += files(
    '../../platform/apple/camera_av.m',
    '../../platform/apple/perm_apple.m',
    '../../platform/apple/sensor_apple.m',
    '../../platform/apple/location_apple.m',
  )
  ydev_backend_args += ['-fobjc-arc']
  # AVFoundation: needed by camera_av.m for AVCaptureSession + by
  # miniaudio for CoreAudio AudioUnit binding (it goes via
  # AudioToolbox/CoreAudio frameworks too).
  ydev_backend_link += [
    '-framework', 'Foundation',
    '-framework', 'AVFoundation',
    '-framework', 'CoreMedia',
    '-framework', 'CoreVideo',
    '-framework', 'CoreFoundation',
    '-framework', 'CoreAudio',
    '-framework', 'AudioToolbox',
    '-framework', 'CoreLocation',
  ]
  if is_ios_app
    ydev_backend_link += ['-framework', 'CoreMotion', '-framework', 'AVFAudio']
  endif
elif host_os == 'linux'
  ydev_backend_sources += files(
    '../../platform/linux/camera_linux.c',
    '../../platform/linux/sensor_linux.c',
    '../../platform/linux/location_linux.c',
    '../../platform/linux/perm_linux.c',
  )
  # miniaudio loads alsa/pulse/jack/pipewire via dlopen at runtime — no
  # compile-time deps. We just need libdl + libm + libpthread (all in
  # libc on glibc, threads already in yos_common_deps).
  ydev_backend_link += ['-ldl']
elif host_os == 'android'
  ydev_backend_sources += files(
    '../../platform/android/camera_android.c',
    '../../platform/android/sensor_android.c',
    '../../platform/android/location_android.c',
    '../../platform/android/perm_android.c',
  )
  # NDK frameworks for camera/sensor/location/log. miniaudio links
  # AAudio + OpenSL ES dynamically — declare them explicitly so the
  # NDK linker resolves the symbols miniaudio_impl.c references.
  ydev_backend_link += [
    '-lcamera2ndk',
    '-lmediandk',
    '-laaudio',
    '-lOpenSLES',
    '-landroid',
    '-llog',
  ]
elif host_os == 'windows'
  # Initial Windows port: every ydev capability returns UNSUPPORTED.
  # A future iteration may wrap Media Foundation (camera) and the
  # WinRT Windows.Devices.* surface (sensors / location). miniaudio
  # ships its own WASAPI/DSound backends — the audio path works as-is.
  ydev_backend_sources += files(
    '../../platform/windows/camera_windows.c',
    '../../platform/windows/sensor_windows.c',
    '../../platform/windows/location_windows.c',
    '../../platform/windows/perm_windows.c',
  )
else
  error('ydev: unsupported host OS: ' + host_os)
endif

ydev_inc = include_directories(
  '../../..',                              # `<yos/ydev/...>` via ../include
  '../../../../include',
  '../../../../build-tools/miniaudio',     # `miniaudio.h`
  '.',
)

_ydcc = meson.get_compiler('c')
_yd_msvc_like = _ydcc.get_argument_syntax() == 'msvc'

ydev_extra_args = []
ydev_extra_link = []
if host_os == 'windows'
  # Windows SDK / msvcrt have no <poll.h>, no fcntl(), no pipe().
  # yos_winshim.h routes pipe()/fcntl()/close()/read()/write() through
  # Winsock loopback socketpair + WSAPoll so the self-pipe wake
  # primitive ydev uses in core.c / perm.c / audio_miniaudio.c works.
  if _yd_msvc_like
    ydev_extra_args += [
      '/external:I' + meson.current_source_dir() / '../../platform/windows/compat',
      '/external:W0',
      '/FIposix_extras.h',
      '/FIyos_winshim.h',
      '/Zc:preprocessor',
      '/experimental:c11atomics',
      '/D_CRT_SECURE_NO_WARNINGS',
      '/D_CRT_NONSTDC_NO_WARNINGS',
      '/DNOMINMAX',
      '/DWIN32_LEAN_AND_MEAN',
      '/Dstrcasecmp=_stricmp',
      '/Dstrncasecmp=_strnicmp',
      '/wd4100', '/wd4101', '/wd4189', '/wd4244', '/wd4267',
      '/wd4456', '/wd4459', '/wd4477', '/wd4701', '/wd4706', '/wd5045',
    ]
    ydev_extra_link += ['ws2_32.lib']
  else
    ydev_extra_args += [
      '-isystem', meson.current_source_dir() / '../../platform/windows/compat',
      '-include', 'yos_winshim.h',
    ]
    ydev_extra_link += ['-lws2_32']
  endif
endif

ydev_base_c_args = []
ydev_base_objc_args = []
if not _yd_msvc_like
  ydev_base_c_args += ['-D_GNU_SOURCE']
  ydev_base_objc_args += ['-D_GNU_SOURCE']
endif

ydev_lib = static_library(
  'ydev',
  ydev_core_sources,
  ydev_backend_sources,
  include_directories : ydev_inc,
  dependencies        : ydev_backend_deps,
  c_args   : ydev_base_c_args + ydev_backend_args + ydev_extra_args,
  objc_args: ydev_base_objc_args + ydev_backend_args,
  install  : false,
)

ydev_dep = declare_dependency(
  link_with           : ydev_lib,
  include_directories : ydev_inc,
  dependencies        : ydev_backend_deps,
  link_args           : ydev_backend_link + ydev_extra_link,
)
