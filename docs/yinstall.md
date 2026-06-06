# yinstall — the yetty installer

`yinstall` is a single, self-contained executable that **installs the whole
yetty product** onto a machine. It embeds everything yetty ships — the `yetty`
terminal **and all the companion executables** (`ycat`, `yplot`, `ygreeter`,
`ydoc`, `ysheet`, `yslide`, the `demo-*` programs, …), its shaders and fonts,
its default configuration, and the heavy RISC-V VM runtime (`yemu` + `qemu`) —
and on first run it unpacks each piece to the correct location for the
operating system it is running on.

The user downloads one file, runs it, and ends up with a working yetty. There
is no separate archive, no package manager step, no second download. This
replaces shipping one enormous all-in-one `yetty` binary that had grown too
large because every asset was baked directly into the terminal.

Targets: **Linux, macOS, Windows.** (webasm / android / ios / tvos are out of
scope — they have their own packaging.)

**Everything is brotli-compressed** inside the installer — every executable and
every asset — so the download stays as small as the content allows. On
extraction each blob is inflated on the way to disk.

**The desktop `yetty` binary is thin.** Because the installer carries the
assets, the `yetty` binary no longer bakes them in (it drops from ~163 MB to
~33 MB), which also shrinks the copy embedded in the installer. This is
controlled by the CMake option `YETTY_EMBED_ASSETS_IN_YETTY` (default **OFF**
on desktop); set it ON only to build a standalone, self-extracting `yetty`
without the installer. Mobile (ios/android/tvos) and webasm always bundle their
own assets and ignore this option.

---

## The mental model

`yinstall` is a **carrier** that holds a set of **components**. A component is
one logically-distinct thing to install — the yetty executable, the font/shader
bundle, the config, the VM runtime, and so on. Each component knows three
things: *what bytes it is*, *where on disk it belongs*, and *how to put it
there* (decompress? mark executable?).

Installing is just "walk the components, lay each one down, tell the user what
happened." There are two axes of extension, and both are cheap:

- **Adding a new executable** to a component the build already ships (one more
  CLI tool, one more demo) is just listing its build target in the installer's
  CMake. The installer code does not change at all.
- **Adding a whole new component** — a plugin pack, an extra emulator, a sample
  gallery — is one row in the component table plus one embed line in the build.

That extensibility is the whole point of the design.

```
        yinstall  (one executable)
        ┌─────────────────────────────────────────────────────────────┐
        │  embedded payload (incbin / Win32 RT_RCDATA)                  │
        │   bin/      → yetty, ycat, yplot, ygreeter, demo-ygui, …      │
        │   data/     → shaders, fonts                                  │
        │   yconfig/  → config.yaml, defaults, temu cfgs                │
        │   yemu/     → RISC-V kernel + rootfs (brotli)                 │
        │   qemu/     → qemu-system-riscv64                             │
        └─────────────────────────────────────────────────────────────┘
                              │  run
                              ▼
        resolve per-OS destinations ──► unpack each component ──► log
                              │
                              ▼
        ~/.local/bin/{yetty,ycat,…}, ~/.local/share/yetty/…, ~/.config/yetty/…
```

Every executable yetty ships — the terminal, the companion CLIs, and the
`demo-*` programs alike — installs into the single `BIN` destination. The set is
**curated at build time**, not hardcoded in the installer: the yetty build
produces dozens of binaries, many of them internal codegen/dev tools
(`gen-error`, `yetty-msdf-gen`, `gpu-info`, the `*-bench` programs) that must
**not** be installed. The installer's CMake lists exactly the user-facing
targets to embed; everything else is simply never staged.

---

## Where the code lives

| Path | Role |
|------|------|
| `tools/yinstall/` | The thin executable: `main.c` + `CMakeLists.txt`. Declares which components are embedded and runs the install. |
| `src/yetty/yinstall/` | The module/library (`yetty_yinstall_*`). All the cross-platform logic: the component model, destination resolution, decompression, the install loop, the log. No `#ifdef`. |
| `src/yetty/yplatform/install/incbin.c` | Backend for Linux/macOS — reads embedded bytes out of `.incbin` symbols. |
| `src/yetty/yplatform/install/winres.c` | Backend for Windows — reads embedded bytes out of `RT_RCDATA` resources. |
| `include/yetty/yplatform/install.h` | The one interface the two backends implement and the module consumes. |

The split follows the standard yetty platform-isolation rule: **only the two
backend files know how bytes are physically embedded.** The module and the tool
are completely platform-independent — they ask the backend to enumerate the
embedded blobs and never touch a platform `#ifdef`.

---

## Components

A component is described by a small, declarative record. The installer holds a
table of these (a `static const` table local to the function that returns it —
no file-scope state), iterates it, and installs each one.

A component descriptor carries:

| Field | Meaning |
|-------|---------|
| `name` | Human-readable label shown in the log, e.g. `"RISC-V VM runtime"`. |
| `prefix` | The embed prefix that selects this component's bytes, e.g. `"yemu/"`. Every embedded asset whose name starts with this prefix belongs to this component. |
| `destination` | Which install root the files land under: `BIN`, `DATA`, or `CONFIG` (resolved per-OS, see below). |
| `subdir` | Optional path appended under the destination, e.g. `"yemu"` → `<data>/yemu/`. Empty means the destination root. |
| `executable` | If set, every file extracted for this component gets its exec bit set (the application binaries, the demos, the `qemu` binary). |
| `description` | One-line summary for the log, e.g. `"RISC-V kernel, firmware and root filesystem"`. |

Whether a given file is brotli-compressed is **not** a component property — it
travels with each embedded asset individually (the build stamps it), so a
component can freely mix compressed and uncompressed files. In practice the
build compresses everything (executables included), and the installer
decompresses transparently on extraction.

### The built-in components

| Component | prefix | destination | subdir | exec | contents |
|-----------|--------|-------------|--------|------|----------|
| Executables | `bin/` | `BIN` | — | yes | `yetty`, companion CLIs (`ycat`, `yplot`, `ygreeter`, `ydoc`, `ysheet`, `yslide`, …) and the `demo-*` programs |
| Shaders & fonts | `data/` | `DATA` | — | no | WGSL shaders, TTF + MSDF fonts |
| Default config | `yconfig/` | `CONFIG` | — | no | `config.yaml`, `defaults.yaml`, temu cfgs |
| RISC-V VM runtime | `yemu/` | `DATA` | `yemu` | no | kernel, OpenSBI firmware, root filesystem |
| QEMU emulator | `qemu/` | `DATA` | `qemu` | yes | `qemu-system-riscv64` |

This table *is* the contract between the installer and everything it ships.
Read top to bottom, it is also exactly the order things are installed and
logged.

The Executables row describes *where the binaries go* (all into `BIN`); the
actual list of binaries is decided by the build (see *Adding a new executable*
below).

---

## Install destinations per OS

The three destination roots resolve through `yplatform/paths` (the same
resolver yetty itself uses), extended with a `BIN` root for the executables:

| Destination | Linux | macOS | Windows |
|-------------|-------|-------|---------|
| `BIN` (all executables) | `~/.local/bin` | `~/.local/bin` | `%LOCALAPPDATA%\Programs\yetty` |
| `DATA` (shaders, fonts, VM runtime) | `~/.local/share/yetty` | `~/Library/Application Support/yetty` | `%LOCALAPPDATA%\yetty\data` |
| `CONFIG` (config files) | `~/.config/yetty` | `~/Library/Application Support/yetty` | `%APPDATA%\yetty` |

`yinstall` honours the same environment overrides yetty respects
(`XDG_*` on Linux, etc.). If `BIN` is not already on the user's `PATH`, the
installer says so explicitly at the end rather than silently leaving a binary
the shell can't find.

---

## The install flow

1. **Identify yourself.** Print the installer banner and the build version
   (the git short hash baked in at build time).
2. **Resolve destinations.** Compute the `BIN` / `DATA` / `CONFIG` roots for
   this OS, expanding `~` and environment overrides.
3. **Enumerate the payload.** Ask the platform backend
   (`yetty_yplatform_install_foreach_asset`) to list every embedded blob.
4. **For each component, in table order:**
   - Select the blobs whose name starts with the component's `prefix`.
   - Strip the prefix, prepend `destination` + `subdir` → the on-disk path.
   - Create parent directories as needed.
   - **Skip if already current** (see *Upgrades* below) — and say so in the log.
   - Otherwise decompress (if the blob is brotli) and write the file.
   - Apply the exec bit if the component is `executable`.
   - Tally bytes and file count for the log line.
5. **Stamp the version marker** so the next run can detect "already installed".
6. **Summarise.** Print the per-component log lines, the totals, and any
   PATH advice.

The install is **idempotent**: running `yinstall` twice does no work the second
time and says nothing was needed. Deleting the data dir (or a single
component's subdir) and re-running reinstalls exactly that.

---

## The install log

The log is a first-class feature, not an afterthought. The user must be able to
see *what* is being installed and *where* — every destination path is shown in
full, so nothing lands on their machine invisibly.

Example of a fresh install:

```
yetty installer · version a1b2c3d

Installing to this machine:

  Executables          →  ~/.local/bin/               199.0 MB  (13 files, unpacked from 67.3 MB)
  Shaders & fonts      →  ~/.local/share/yetty/       167.6 MB  (69 files, unpacked from 35.6 MB)
  Default config       →  ~/.config/yetty/            22.5 KB   (4 files, unpacked from 6.8 KB)
  RISC-V VM runtime    →  ~/.local/share/yetty/yemu/  417.2 MB  (5 files, unpacked from 87.8 MB)
  QEMU emulator        →  ~/.local/share/yetty/qemu/  6.6 MB    (1 files, unpacked from 2.0 MB)

Installed 5 components · 790.5 MB written.
yetty is ready -- run:  ~/.local/bin/yetty

Note: ~/.local/bin is not on your PATH. Add it to run `yetty` directly.
```

The carried (compressed) bytes are what matters for the download: the example
above is a ~193 MB installer that expands to ~790 MB on disk. A `--verbose` run
additionally lists each binary as it lands
(`~/.local/bin/ycat`, `~/.local/bin/yplot`, …) so the user can see the full
inventory, not just the per-component summary.

And a re-run, where nothing changed:

```
yetty installer · version a1b2c3d

Already installed (version a1b2c3d):

  executables          →  ~/.local/bin/                         up to date
  shaders & fonts      →  ~/.local/share/yetty/                 up to date
  default config       →  ~/.config/yetty/                      up to date
  RISC-V VM runtime    →  ~/.local/share/yetty/yemu/            up to date
  QEMU emulator        →  ~/.local/share/yetty/qemu/            up to date

Nothing to do · yetty is ready.
```

Log conventions:
- One line per component, always showing the **full destination path**.
- Sizes are human-readable; brotli components show `decompressed from <packed>`
  so the user understands the on-disk cost.
- Per-component status (`installed`, `up to date`, `updated`) is explicit.
- Output stays quiet on success and loud on anything skipped or failed — a
  failed component names the file and the reason and stops the install with a
  non-zero exit.

---

## Upgrades and re-installs

Each install writes a version marker (`<data>/.yinstall/version`) holding the
build version. On the next run:

- **Marker matches the build** → everything is reported `up to date` and no
  bytes are written.
- **Marker missing or different** → the installer reinstalls. Per-file, it
  still skips any file already present with the right size, so an upgrade only
  rewrites what actually changed.

The marker is private to `yinstall` (it does not share state with yetty's own
runtime asset markers), so the two never confuse each other when they look at
the same data directory.

---

## Adding a new executable (the common case)

Most additions are "I wrote a new tool/demo, ship it." Because the binary set is
curated in the build, this never touches installer code:

1. In `tools/yinstall/CMakeLists.txt`, add the build target to the list staged
   into the `bin/` prefix. The staging step copies the built binary — e.g.
   `$<TARGET_FILE:ycat>` — into the embed directory. A demo is no different from
   any other tool here; it is just another target in the same list.
2. Rebuild. The new binary is embedded, installed into `BIN`, gets its exec bit,
   and shows up in the inventory.

The list is intentionally an explicit allow-list, not a glob over every build
target, so internal codegen/dev tools never get shipped by accident.

## Adding a new component (the recipe)

This is the part the design optimises for when a *new kind* of thing needs to be
installed. To ship a new component:

1. **Stage the files.** In `tools/yinstall/CMakeLists.txt`, copy the files into
   a staging directory under a new prefix, then embed them:

   ```
   incbin_add_directory(yinstall "<prefix>" "<staging-dir>" "*" <COMPRESS>)
   ```

   Pass `TRUE` for `COMPRESS` to brotli-pack large files, `FALSE` to embed
   as-is (already-compressed images, small text).

2. **Declare the component.** Add one row to the component table in
   `src/yetty/yinstall/`:

   ```
   { "Sample gallery", "samples/", DEST_DATA, "samples", /*exec*/ false,
     "Example plots, images and documents" },
   ```

   Choose the destination (`BIN` / `DATA` / `CONFIG`), an optional subdir, and
   whether the files need an exec bit.

3. **Build.** That's it. The platform backends enumerate the new blobs
   automatically, the install loop picks them up by prefix, and the log gains a
   new line. No backend changes, no new extraction code, no platform `#ifdef`.

If a future component needs a genuinely new *destination root* (e.g. a system
service directory), add it to `yplatform/paths` once and reference it by name
in the component table — the rest of the installer is agnostic to how many
roots exist.

---

## Design invariants

- **One interface for embedding.** Everything the installer reads comes through
  `yetty_yplatform_install_foreach_asset`. The module never knows whether it is
  running off `.incbin` symbols or `RT_RCDATA`.
- **Declarative components.** Behaviour lives in data (the component table), not
  in branchy code. New components are data edits.
- **No file-scope state.** The component table is a `static const` local; any
  per-install state lives in a context struct passed by value/pointer.
- **Result-returning.** Every fallible step returns a Result and propagates the
  error chain; the installer surfaces the first failure with the offending file
  and a non-zero exit.
- **Idempotent and transparent.** Safe to re-run; every destination is printed;
  nothing is written silently.
