# 3rdparty — pre-built 3rd-party libraries

Mirrors the `build-tools/assets/` group, but for **3rd-party libraries
that yetty links against**. Each library is built once per
`(target-platform, lib-version)` on GitHub Actions and published as a
tarball attached to a `lib-<libname>-<version>` release; downstream yetty
builds fetch the tarball instead of rebuilding from source on every CI
run / developer machine.

## Per-library model

Every library lives in `build-tools/3rdparty/<libname>/` with three files:

```
build-tools/3rdparty/openh264/
├── version       — single source of truth: "2.4.1"
├── build.sh      — nix-shell wrapper, picks .#3rdparty-<target>
└── _build.sh     — actual build, reads ./version, builds for $TARGET_PLATFORM
```

The `version` file value is used for **both**:
- the upstream tag fetched by `_build.sh` (e.g. `v2.4.1` for openh264)
- the tarball name and release tag

There is **no** global 3rdparty version. Bumping one lib triggers only
that lib's matrix.

### Wrapper producers (linux, opensbi, alpine-disk, alpine-extended-disk)

For producers that wrap *something else* (a kernel build, a firmware
build, a rootfs install) instead of being the upstream themselves, the
`version` file encodes the upstream version with a packaging revision
suffix:

| producer             | format                                | example              |
|----------------------|---------------------------------------|----------------------|
| linux                | `<kernel>-<rev>`                      | `7.0-1`              |
| opensbi              | `<opensbi>-<rev>`                     | `1.4-1`              |
| alpine-disk          | `<alpine-release>-<arch>-<rev>`       | `3.23.4-riscv64-1`   |
| alpine-extended-disk | `<alpine-release>-<arch>-<rev>`       | `3.23.4-riscv64-1`   |

The `_build.sh` parses the upstream component out of the version string
to drive the source fetch — single source of truth for both the upstream
that's actually compiled AND the `lib-<name>-<version>` release tag.
Bump `<rev>` for packaging-only changes (config tweaks, bundled-rootfs
bumps); bump the upstream component when moving to a new upstream
release.

## Workflow per library

`.github/workflows/build-3rdparty-<libname>.yml`, triggered by tags
`lib-<libname>-*`. Only that one library's matrix rebuilds when the tag
fires. `lib-*` tags are excluded from `cmake-multi-platform.yml` so
pushing a 3rdparty tag does not also kick the main yetty CI.

## Tarball layout (consumer contract)

```
<libname>-<target>-<libver>.tar.gz
└── lib/lib<libname>.a   (or lib<libname>.lib on windows)
└── include/...
```

Attached to release `lib-<libname>-<libver>`. Consumed by
`build-tools/cmake/3rdparty-fetch.cmake` via
`yetty_3rdparty_fetch(<libname>)`, which downloads, extracts to
`${CMAKE_BINARY_DIR}/3rdparty/<libname>/`, and exposes the directory
back to the caller `*.cmake`.

## Target platforms

Same set as `assets/qemu/` (link targets only — no `tvos-arm64`):

| target              | runner            | nix shell                    |
|---------------------|-------------------|------------------------------|
| `linux-x86_64`      | ubuntu-latest     | `.#3rdparty-linux-x86_64`    |
| `linux-aarch64`     | ubuntu-latest     | `.#3rdparty-linux-aarch64`   |
| `linux-riscv64`     | ubuntu-latest     | `.#3rdparty-linux-riscv64`   |
| `android-arm64-v8a` | ubuntu-latest     | `.#3rdparty-android-arm64-v8a`|
| `android-x86_64`    | ubuntu-latest     | `.#3rdparty-android-x86_64`  |
| `webasm`            | ubuntu-latest     | `.#3rdparty-webasm`          |
| `macos-arm64`       | macos-latest      | `.#3rdparty-macos-arm64`     |
| `ios-arm64`         | macos-latest      | `.#3rdparty-ios-arm64`       |
| `macos-x86_64`      | macos-15-intel    | `.#3rdparty-macos-x86_64`    |
| `ios-x86_64`        | macos-15-intel    | `.#3rdparty-ios-x86_64`      |
| `windows-x86_64`    | (deferred)        | (no nix — PowerShell + MSVC) |

## Releasing a library version

1. Bump (or create) `build-tools/3rdparty/<libname>/version`.
2. Commit and push the change.
3. `build-tools/push-3rdparty-tag.sh <libname>` — reads the version file,
   creates and pushes `lib-<libname>-<version>` tag.
4. Workflow fires; tarballs end up on the release of the same name.
5. Yetty's `build-tools/cmake/<libname>.cmake` reads the same `version`
   file at configure time and downloads the matching tarball.

## Tier list

Tier 1 = always-from-source heavyweights, biggest CI wins.
Tier 2 = moderate, worth bundling for cross-platform pain.
Tier 3 = header-only / trivial — keep as CPM source, no pre-build.
Tier 0 = already pre-built (Dawn).

| Tier | Library          | Build system       | Status |
|------|------------------|--------------------|--------|
| 0    | dawn             | (pre-built)        | done — `Dawn.cmake` |
| 1    | **openh264**     | GNU make           | **migrated to per-lib 3rdparty** |
| 1    | dav1d            | meson + ninja      | todo   |
| 1    | openssl          | CMake (janbar)     | todo   |
| 1    | freetype-bundle  | CMake (5 libs)     | todo — zlib + libpng + brotli + bzip2 + freetype |
| 1    | libmagic         | autotools          | todo   |
| 1    | thorvg           | CMake              | todo (carries our patches) |
| 1    | libjpeg-turbo    | CMake + NASM       | todo   |
| 1    | libssh2          | CMake              | todo (chains on openssl/zlib) |
| 1    | libcurl          | CMake / autotools  | todo (currently system-only) |
| 2    | libuv, vterm, wasm3, tree-sitter (+grammars), pdfio, tinyxml2, yaml-cpp, libyaml | CMake | todo |
| 3    | args, glm, stb, msgpack-cxx, libco, incbin, lz4, spdlog, ytrace, msdfgen, minimp4, miniaudio, imgui | (header-only / single-file) | skip — keep CPM |

## Adding a new library

1. `mkdir build-tools/3rdparty/<lib>` with:
   - `version`   — upstream version, e.g. `1.5.0`
   - `build.sh`  — copy/adapt `openh264/build.sh`
   - `_build.sh` — copy/adapt `openh264/_build.sh`
2. Add `.github/workflows/build-3rdparty-<lib>.yml` (copy/adapt
   `build-3rdparty-openh264.yml`, change tag pattern).
3. Add deps to the right `3rdparty-<target>` shell in `flake.nix` if the
   existing tooling doesn't cover them.
4. Replace `build-tools/cmake/<lib>.cmake` with a download-and-import-
   static stub via `yetty_3rdparty_fetch(<lib>)`.
5. `build-tools/push-3rdparty-tag.sh <lib>` to cut the first release.
