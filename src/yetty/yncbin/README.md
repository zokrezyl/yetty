# yncbin — embedded (incbin) asset container and extractor

`yncbin` is a single translation unit (`incbin-assets.c`) that collects the
asset blobs baked into an executable — shaders, fonts, default config, the
shared RISC-V runtime (`yemu/`), the QEMU binary (`qemu/`) — and extracts
them to the platform data/config directories, decompressing
brotli-compressed entries on the way. The blobs themselves are embedded by
`build-tools/yetty/incbin.cmake` (`.incbin` inline assembly on GCC/Clang; a
host-built generator tool producing C arrays on MSVC and Emscripten,
brotli quality set by `BROTLI_QUALITY`, default 6). Dependencies: brotli
decoder, `ytrace`, `yplatform` fs helpers.

## How it works

- **Registration.** For every embedded prefix the build generates a
  `yetty_<prefix>_manifest.h` exposing `register_<prefix>_assets_c(cb)`.
  `yetty_incbin_assets_create()` includes whichever manifests exist
  (`HAS_DATA_MANIFEST`, `HAS_YCONFIG_MANIFEST`, `HAS_YEMU_MANIFEST`,
  `HAS_QEMU_MANIFEST`) and fans them into one fixed-capacity table
  (`MAX_ASSETS` = 1024) of `{name, data, size, compressed}` entries pointing
  straight into the binary image — nothing is copied.
- **Version markers.** `yetty_incbin_assets_needs_extraction(dir, kind)`
  compares `<dir>/.yetty-assets/version[-<kind>]` against
  `YETTY_BUILD_VERSION` (stamped from the git short hash by
  `yetty_embed_assets` in `build-tools/yetty/platform/shared.cmake`), so a
  new build re-extracts and a reconfigure of the same commit does not. The
  per-kind suffix keeps data/config markers apart on platforms where both
  share one directory (macOS).
- **Extraction.** `extract_with_prefix` strips the component prefix
  (`data/`, `yconfig/`, `yemu/`, `qemu/`), creates parent directories
  (tolerating pre-existing system dirs — an iOS-sandbox quirk), inflates
  brotli entries with the streaming decoder (growable output buffer), and
  skips files already on disk — important for the ~300 MB rootfs image that
  expands from a ~49 MB brotli blob. The QEMU extractor additionally
  chmods the binary 0755.

## API sketch

```c
struct yetty_incbin_assets *assets = yetty_incbin_assets_create();
if (yetty_incbin_assets_needs_extraction(assets, data_dir, "data"))
    yetty_incbin_assets_extract_data_to(assets, data_dir);   /* "data/..."  */
yetty_incbin_assets_extract_config_to(assets, config_dir);   /* "yconfig/..." */
if (yetty_incbin_assets_has_yemu(assets))
    yetty_incbin_assets_extract_yemu_to(assets, data_dir);   /* → <data>/yemu  */
if (yetty_incbin_assets_has_qemu(assets))
    yetty_incbin_assets_extract_qemu_to(assets, data_dir);   /* → <data>/qemu  */
yetty_incbin_assets_destroy(assets);
```

The extract/needs functions return plain `int` (1 = success/needed) — this
predates the repo-wide Result convention. There is no public header; the
functions are defined only in `incbin-assets.c`.

## Status

The TU is still listed in the source set of every platform's main yetty
target and several tools (`tools/yzoo`, `tools/ymaze`, `tools/yaudio`,
`src/yetty/yrich`, `src/yetty/yffi`, …), but **no code currently calls its
API** — the call sites were removed in the yplatform refactoring. The live
extraction path today is the installer stack:
`yetty_yplatform_install_foreach_asset`
(`../yplatform/install/incbin.c` / `winres.c`) driven by
[yinstall](../yinstall/README.md), and `tools/ygreeter` / `tools/yhello`
carry their own private copies of the same extractor pattern
(`embedded-assets.c`). Treat this module as the legacy extractor kept
linked for the standalone-embed build shape; new asset work should go
through yinstall/yplatform-install.

## Layout

| file | role |
|------|------|
| `incbin-assets.c` | manifest registration, version markers, brotli inflate, prefix extraction |

## See also

- [yinstall](../yinstall/README.md) — the installer that owns asset laydown
  today.
- [yplatform](../yplatform/README.md) — `install.h` enumeration interface
  and the per-OS embed backends.
- [yqemu](../yqemu/README.md) — consumes the extracted `yemu/` + `qemu/`
  payloads at run time.
