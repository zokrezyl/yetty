# Vendored libcss (+ libparserutils, libwapcaplet, NetSurf buildsystem)

This is the **in-repo source** of NetSurf's MIT-licensed CSS cascade and its
build dependencies. It is vendored — not fetched from upstream — so the CSS
engine can be **edited directly** instead of being compensated for with a
growing pile of side-table / rewriter workarounds in the ybrowser layer
(calc(), modern selectors, media-query level 4, …).

## Layout

| dir              | upstream                      | pinned version |
|------------------|-------------------------------|----------------|
| `libcss/`        | NetSurf libcss                | 0.9.2          |
| `libparserutils/`| NetSurf libparserutils        | 0.2.5          |
| `libwapcaplet/`  | NetSurf libwapcaplet          | 0.4.3          |
| `buildsystem/`   | NetSurf buildsystem           | 1.10           |

Fetched from `https://download.netsurf-browser.org/libs/releases`. The
per-library `test/`, `docs/`, `examples/` and `.github/` trees are pruned —
only what the static-library build needs is kept.

## Local modifications (why this isn't pristine upstream)

The package `version` (`../version`) carries a `-pN` suffix to mark that the
vendored source is patched. Current deltas from pristine upstream:

- **`libcss/src/select/select.c` — static `empty_bloom` double-free**
  (upstream `f1c3e3d1`, landed after 0.9.2). On a root-element select, any
  mid-selection error freed the static `.bss` `empty_bloom`, poisoning the
  heap freelist and corrupting the interned-string pool minutes later
  (apnews.com crashed). Fix: a `css__get_empty_bloom()` accessor and a
  `node_data->bloom != css__get_empty_bloom()` guard in
  `css__destroy_node_data`. This is `-p1`.

When you change the vendored source, add an entry here and bump the `-pN`
suffix in `../version`.

## How it builds

`../_build.sh` copies these trees into a work dir and drives the NetSurf
make buildsystem (`make install COMPONENT_TYPE=lib-static`) — the buildsystem
handles the build-time codegen (the property-parser generator in
`libcss/src/parse/properties/`; the `select_generator.py` outputs ship
pre-generated). Output is a `lib/*.a` + `include/` tarball.

`build-tools/yetty/libs/libcss.cmake` builds this vendored source **locally**
into the 3rdparty cache the first time (no release download), then consumes
it. To force a rebuild after editing the source, bump `../version` or delete
the cached `libcss-<platform>-<version>.tar.gz`.

Requires `make`, `perl`, a C compiler, and `flex` / `bison` / `gperf` on PATH.
Platforms whose triples the NetSurf GNU-make build can't honour
(windows-MSVC / webasm / android / iOS / tvOS) get an `UNSUPPORTED` placeholder
tarball; ybrowser falls back to its built-in lexbor-CSS path there.
