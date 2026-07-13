# yqemu — QEMU RISC-V VM spawner

`yqemu` launches an external `qemu-system-riscv64` process that boots a
RISC-V Linux guest (Alpine-based rootfs) and makes it reachable over local
TCP, so yetty can attach terminal tabs to it as telnet clients. It backs the
`--qemu` session mode selected by the PTY factories. One translation unit;
depends on `ycore`, `ytrace`, and yplatform (process spawn, sockets, paths,
fs, time).

This is the *out-of-process* sibling of the in-process/WebASM TinyEMU VM
(`../ypty/temu-pty.c`, `--temu`): QEMU is a real child process with full
system emulation, TinyEMU runs inside yetty itself.

## What `yetty_yqemu_qemu_start(host_port)` assembles

- **Binary**: `<data_dir>/qemu/qemu-system-riscv64` (`.exe` on Windows). On
  Android SELinux forbids exec from the writable data dir, so the binary
  ships as `libqemu-system-riscv64.so` in the app's `nativeLibraryDir`,
  resolved at runtime via `dladdr()`. The minimal QEMU build itself is
  produced at configure time by `build-tools/yetty/qemu.cmake` and embedded
  into the installer payload.
- **Boot chain**: OpenSBI (`<data_dir>/yemu/opensbi-fw_dynamic.bin`) +
  kernel (`yemu/kernel-riscv64.bin`) + one unified ext4 rootfs image
  (`yemu/yetty-rootfs-riscv.img`: alpine-extended userland plus
  `/yetty/{bin,repo}` with cross-compiled riscv64 demos/tools), attached as
  virtio-blk. Shipped brotli-compressed inside the binary and extracted to
  `<data_dir>/yemu/` at install time.
- **Networking**: slirp user netdev with
  `hostfwd=tcp:127.0.0.1:<host_port>-:23` — the guest runs busybox telnetd
  on tcp/23; yetty connects as an ordinary telnet client. The canonical
  port is `QEMU_TELNET_PORT` (2423, defined in `qemu.h`).
- **Console**: the guest's `hvc0` virtio-console is exposed as a
  `telnet=on` socket chardev on 127.0.0.1:2424 (`server=on,wait=off`), so
  boot log and kernel panics stay observable. `-no-reboot -no-shutdown`
  keeps the chardev open after a panic instead of a silent disconnect.
- **Host share**: `<config_dir>/qemu/share/` is exported to the guest over
  virtio-9p as `mount_tag=hostshare` — skipped on Windows, where the
  minimal QEMU build has no virtfs.
- **Tunables**: `<config_dir>/qemu/qemu.cfg` (`memory_mb`, `smp`,
  `extra_append` — plain `key = value` lines); a commented default file is
  written on first run. Defaults: 256 MB, 1 CPU.

The process is spawned detached with stdio to null via
`yetty_yplatform_yprocess_spawn`.

## Public API

```c
struct yetty_yplatform_yprocess *yetty_yqemu_qemu_start(uint16_t host_port);
void yetty_yqemu_qemu_stop(struct yetty_yplatform_yprocess *proc);
struct yetty_ycore_void_result yetty_yqemu_qemu_wait_ready(uint16_t port, int timeout_ms);
```

`wait_ready` polls a TCP connect against 127.0.0.1:`port` every 100 ms until
it succeeds or the timeout expires — used both for the console chardev
(2424) and the slirp telnet (2423). The `--temu` path reuses it for
TinyEMU's own slirp port.

## Layout

| file | role |
|------|------|
| `qemu.c` | settings load, path resolution, argv assembly, spawn/stop/wait |
| `include/yetty/yqemu/qemu.h` | API + `QEMU_TELNET_PORT` |

Built as `yetty_qemu` behind `YETTY_ENABLE_LIB_QEMU`
(`src/yetty/CMakeLists.txt`).

## Consumers

- **Unix PTY factory** (`../yplatform/pty-factory/default.c`) — in `--qemu`
  mode yetty opens two default tabs: tab 1 telnets to the console chardev
  (2424), tab 2 (and every later pane) telnets to the slirp-forwarded
  guest telnetd (2423). The factory owns the QEMU process lifetime.
- **Windows PTY factory** (`../ypty/conpty.c`) — same spawn, but connects
  only via the slirp telnet port: on the MSYS2 QEMU build, a libuv TCP
  client attached to a `socket,server=on` chardev silently kills QEMU, so
  the chardev stays a debugging side channel there.

## See also

- [ypty](../ypty/README.md) — the PTY backends, including the TinyEMU
  in-process VM this module parallels.
- [ytelnet](../ytelnet/README.md) — the telnet PTY used to attach to the
  guest.
- [yplatform](../yplatform/README.md) — process/socket/paths primitives and
  the PTY factory.
