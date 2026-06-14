# Licensing of `src/cpython/`

This directory contains code under **two different licenses**. Read this before
copying anything out of here.

## 1. `third_party/cpython/` — Python Software Foundation License v2

Everything under `third_party/cpython/` is derived from **CPython 3.14.6** and is
licensed under the **PSF License Agreement, Version 2**.

- Copyright © 2001–2025 Python Software Foundation; All Rights Reserved.
- Full license text: [`third_party/cpython/LICENSE`](third_party/cpython/LICENSE)
- Provenance and per-file origin: [`third_party/cpython/PROVENANCE.md`](third_party/cpython/PROVENANCE.md)

The PSF License is a permissive, GPL-compatible license. It requires that the
copyright notice and license text be retained (they are, in
`third_party/cpython/LICENSE`). Derivative works must state the changes made; any
modifications we make to these files are tracked in `PROVENANCE.md`.

## 2. `src/` and everything else — yetty project

All other files in this directory (the libpython-free runtime, the AST shims, the
build glue, this document, and `README.md`) are **original work, part of the yetty
project**, and are governed by the yetty project's license terms — *not* the PSF
License.

These files are a *new* work that links against / is generated from the
PSF-licensed grammar and ASDL. The PSF License permits this; the resulting
combination must continue to ship the PSF notice for the CPython-derived portion,
which is why `third_party/cpython/LICENSE` must never be removed.

## Summary

| Path | License | Copyright holder |
|------|---------|------------------|
| `third_party/cpython/**` | PSF License Agreement v2 | Python Software Foundation |
| `src/**`, `README.md`, `LICENSE.md` | yetty project terms | yetty project |
