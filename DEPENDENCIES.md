# Yetty Dependencies

This document lists the third-party components Yetty bundles, links, or
otherwise distributes, together with their licenses.

## Licensing model

- **Yetty's own source** (the original code authored by the Licensor) is
  licensed under the **Business Source License 1.1** — see [LICENSE](LICENSE).
  Non-production use is free; production use requires a commercial license;
  the work converts to GPLv2+ on the Change Date.
- **Third-party components retain their own licenses.** The BSL applies
  ONLY to Yetty's own code. It does **not** relicense, restrict, or place
  a production-use condition on any third-party component. Each component
  listed below is governed by its own license, and you receive it under
  that license. Where a component is permissively licensed (MIT, BSD,
  Zlib, Apache-2.0, ISC, public domain, …), your rights in that component
  come from its own license and are unaffected by Yetty's BSL.

> This file is a good-faith summary to help downstream users. It is not
> legal advice. The authoritative terms for each component are in that
> component's own license text (linked from its upstream project, and for
> vendored components, included in this repository at the path shown).

## Vendored in this repository (`src/`)

These components are checked into this repository in full source form. Their
license texts ship alongside them.

| Component | License | Copyright | License file |
|-----------|---------|-----------|--------------|
| libvterm 0.3.3 | MIT | (c) 2008 Paul Evans | `src/libvterm-0.3.3/LICENSE` |
| TinyEMU | MIT | (c) 2016-2017 Fabrice Bellard | `src/tinyemu/MIT-LICENSE.txt` |

Anyone redistributing these vendored files does so under their MIT terms,
independently of Yetty's BSL.

## Libraries fetched and linked at build time

Pinned versions are tracked under `build-tools/3rdparty/<name>/version`.

| Dependency | Version | License | Usage |
|-----------|---------|---------|-------|
| Dawn | (pinned in build) | BSD-3-Clause | WebGPU backend |
| FreeType | 2.13.2 | FreeType License (BSD-style; or GPLv2 — used under FTL) | Font rasterization |
| GLFW | 3.4 | Zlib | Windowing |
| glfw3webgpu | 8f14534 | MIT / Apache-2.0 | GLFW↔WebGPU surface glue |
| libuv | 1.48.0 | MIT | Async I/O / event loop |
| libco | e18e09d | ISC | Coroutines |
| brotli | 1.1.0 | MIT | Bundled-asset compression |
| incbin | 22061f5 | Unlicense (public domain) | Asset embedding |
| msdfgen | 1.12 | MIT | Font atlas generation |
| ThorVG | 1.0.1 | MIT | SVG / Lottie rendering |
| Dear ImGui | 1.92.5 | MIT | GUI widgets (ymery) |
| tree-sitter | 0.26.5 | MIT | Syntax highlighting |
| tinyxml2 | 10.0.0 | Zlib | XML parsing |
| libyaml | 0.2.5 | MIT | Config parsing |
| yxml | 6650790 | MIT | XML parsing (streaming) |
| zlib (zlib-ng) | 2.2.4 | Zlib License | Compression |
| bzip2 | 1.0.8 | bzip2 License (BSD-like) | Compression |
| lz4 | 1.10.0 | BSD-2-Clause | Wire-stream compression (yface) |
| cdb | 0.0.13 | Public Domain | Constant key/value database (ycdb) |
| libmagic | 5.46 | BSD-2-Clause | File type detection |
| msgpack-c | c-6.1.0 | Boost Software License 1.0 | RPC serialization |
| pdfio | 1.4.0 | Apache-2.0 | PDF parsing (ypdf) |
| libpng | 1.6.43 | libpng License (zlib/libpng) | PNG decode |
| libjpeg-turbo | 3.1.3 | BSD-3-Clause / IJG / Zlib | JPEG decode |
| dav1d | 1.5.0 | BSD-2-Clause | AV1 video decode |
| openh264 | 2.4.1 | BSD-2-Clause (see patent note) | H.264 video decode (yvideo) |
| libopus | 1.5.2 | BSD-3-Clause | Audio decode |
| miniaudio | 0.11.22 | Public Domain (MIT-0 / Unlicense) | Audio playback |
| minimp4 | 4575afb | CC0 (public domain) | MP4 demux |
| lexbor | 3.0.0 | Apache-2.0 | HTML/CSS engine (ylexbor) |
| libcss | 0.9.2 | MIT | CSS engine |
| QuickJS | 0.15.0 | MIT | JavaScript engine (ylexbor) |
| libssh2 | 1.11.1 | BSD-3-Clause | SSH backend (yssh) |
| libcurl | 8.20.0 | curl License (MIT/X11-style) | HTTP transport |
| nghttp2 | 1.67.1 | MIT | HTTP/2 |
| nghttp3 | 1.11.0 | MIT | HTTP/3 |
| ngtcp2 | 1.15.0 | MIT | QUIC |
| OpenSSL | 4.0.0 | Apache-2.0 | TLS |

### H.264 patent note

The openh264 **source** is BSD-2-Clause, but the AVC/H.264 standard it
implements is covered by patents (MPEG-LA pool; Cisco sponsors a binary
distribution to cover royalties). The patent situation is independent of
the source license and of Yetty's BSL.

## RISC-V VM guest images, firmware, and build tooling

These are used by the RISC-V VM console (TinyEMU). They are **guest images
and emulator/build tools — they are not linked into the Yetty binary** and
do not affect the license of Yetty's own code. They carry their own
(sometimes copyleft) licenses as data / standalone tools:

| Component | Version | License | Role |
|-----------|---------|---------|------|
| Linux kernel | 7.0-1 | GPLv2 | RISC-V guest kernel image |
| OpenSBI | 1.4-1 | BSD-2-Clause | RISC-V SBI firmware (guest) |
| Alpine disk image | 3.23.4-riscv64 | Mixed (Alpine base MIT; bundled packages GPL/MIT/BSD/…) | Guest root filesystem |
| QEMU | 11.0.0-rc4-1 | GPLv2 | Emulator / image build tool |

## Optional / off by default

| Component | Version | License | Notes |
|-----------|---------|---------|-------|
| NetSurf | 3.11 | GPLv2 | `ynetsurf` integration — **disabled by default**. Enabling it links GPLv2 code; build with it off to keep distribution permissive. |

## System libraries (dynamically linked, not redistributed)

Provided by the host OS; linked against, not bundled:

- **Fontconfig** (Unix) — MIT — font discovery/fallback
- **Core Text / CoreGraphics** (macOS) — system framework — font discovery
- **DirectWrite / GDI** (Windows) — system component — font discovery

## Summary

- **Desktop & web default builds:** permissive only (MIT, BSD, Zlib,
  Apache-2.0, ISC, Boost, public domain). No copyleft is linked into the
  shipped binary.
- **Copyleft (GPLv2)** appears only in (a) the optional, off-by-default
  NetSurf integration and (b) RISC-V VM guest images/tools that run as
  guests and are not linked into Yetty.
- **Yetty's BSL governs only Yetty's own code** and does not extend to any
  of the components above.
