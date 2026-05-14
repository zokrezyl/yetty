/* QEMU - Start QEMU RISC-V VM */

#include <yetty/yqemu/qemu.h>

#include <yetty/yplatform/socket.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/process.h>
#include <yetty/yplatform/time.h>
#include <yetty/ytrace/ytrace.h>

#ifdef __ANDROID__
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform paths */
#include <yetty/yplatform/paths.h>

/* QEMU tunables, read from <config_dir>/qemu/qemu.cfg if present. */
struct yetty_yqemu_qemu_settings {
    unsigned int memory_mb;
    unsigned int smp;
    char extra_append[128];
};

static void qemu_settings_defaults(struct yetty_yqemu_qemu_settings *s)
{
    s->memory_mb = 256;
    s->smp = 1;
    s->extra_append[0] = '\0';
}

/* Trim ASCII whitespace in-place (lightweight, no locale) */
static char *qemu_trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return s;
}

/* Parse simple "key = value" lines (# for comments). */
static void qemu_settings_load(struct yetty_yqemu_qemu_settings *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = qemu_trim(line);
        if (*p == '\0' || *p == '#') {
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = qemu_trim(p);
        char *val = qemu_trim(eq + 1);

        if (strcmp(key, "memory_mb") == 0) {
            s->memory_mb = (unsigned int)strtoul(val, NULL, 10);
        } else if (strcmp(key, "smp") == 0) {
            s->smp = (unsigned int)strtoul(val, NULL, 10);
        } else if (strcmp(key, "extra_append") == 0) {
            snprintf(s->extra_append, sizeof(s->extra_append), "%s", val);
        }
    }
    fclose(f);
}

/* Ensure a default qemu.cfg exists so users can discover/tune settings. */
static void qemu_settings_ensure_default(const char *path)
{
    if (yetty_yplatform_file_exists(path)) {
        return;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "# yetty --qemu settings\n"
               "# Lines are key = value; '#' starts a comment.\n"
               "memory_mb = 256\n"
               "smp = 1\n"
               "# Appended verbatim to the kernel cmdline (optional):\n"
               "# extra_append =\n");
    fclose(f);
}

struct yetty_yplatform_yprocess *yetty_yqemu_qemu_start(uint16_t host_port)
{
    const char *raw_data_dir = yetty_yplatform_get_data_dir();
    const char *raw_config_dir = yetty_yplatform_get_config_dir();

#ifdef _WIN32
    /* Normalize path separators to forward slashes. QEMU's option parsers
     * (-drive, -chardev, -fsdev, ...) use comma-separated `key=value` lists
     * where backslash is the escape character. Paths like
     * `file=C:\Users\foo\...` get mangled because `\U`, `\f`, etc. become
     * escape sequences. We use forward slashes throughout — Windows file
     * APIs accept them, and qemu's parsers are happy. */
    static char data_dir_buf[512];
    static char config_dir_buf[512];
    snprintf(data_dir_buf, sizeof(data_dir_buf), "%s", raw_data_dir);
    snprintf(config_dir_buf, sizeof(config_dir_buf), "%s", raw_config_dir);
    for (char *p = data_dir_buf; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    for (char *p = config_dir_buf; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    const char *data_dir = data_dir_buf;
    const char *config_dir = config_dir_buf;
#else
    const char *data_dir = raw_data_dir;
    const char *config_dir = raw_config_dir;
#endif
    char qemu_bin[512];
    char bios_path[512];
    char kernel_path[512];
    char blk_path[512];
    char share_path[512];
    char qemu_cfg_dir[512];
    char qemu_cfg_path[512];
    char netdev_arg[128];
    char chardev_arg[128];
    char append_arg[384];
    char drive_arg[640];
    char fsdev_arg[640];
    char memory_arg[32];
    char smp_arg[16];
    struct yetty_yqemu_qemu_settings settings;

    /* qemu binary is program-specific; shared runtime lives under yemu/.
     *
     * Android caveat: SELinux denies execute_no_trans on files under the
     * app's writable data dir (untrusted_app context). The only place an
     * app can exec is its nativeLibraryDir. For Android we therefore ship
     * the QEMU binary as libqemu-system-riscv64.so (see android.cmake).
     * Resolve that dir at runtime via dladdr() on a known libyetty.so
     * symbol.
     *
     * On Windows the binary has a .exe suffix. */
#ifdef __ANDROID__
    {
        Dl_info info;
        if (dladdr((void *)yetty_yqemu_qemu_start, &info) && info.dli_fname) {
            char native_dir[512];
            const char *slash;
            snprintf(native_dir, sizeof(native_dir), "%s", info.dli_fname);
            slash = strrchr(native_dir, '/');
            if (slash) {
                native_dir[slash - native_dir] = '\0';
                snprintf(qemu_bin, sizeof(qemu_bin), "%s/libqemu-system-riscv64.so", native_dir);
            } else {
                snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
            }
        } else {
            snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
        }
    }
#elif defined(_WIN32)
    snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64.exe", data_dir);
#else
    snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
#endif
    snprintf(bios_path, sizeof(bios_path), "%s/yemu/opensbi-fw_dynamic.bin", data_dir);
    snprintf(kernel_path, sizeof(kernel_path), "%s/yemu/kernel-riscv64.bin", data_dir);
    /* Unified rootfs image: alpine-extended userland + /yetty/{bin,repo}
     * (cross-compiled riscv64 demos+tools + repo HEAD) in one ext4 image.
     * Replaces the previous two-drive setup (alpine-extended-rootfs.img +
     * yetty-riscv-disk.img). Image is shipped via incbin under
     * yemu/yetty-rootfs-riscv.img.br; runtime extractor decompresses it
     * into <data_dir>/yemu/yetty-rootfs-riscv.img. */
    snprintf(blk_path, sizeof(blk_path), "%s/yemu/yetty-rootfs-riscv.img", data_dir);

    /* User-tunable qemu settings live under <config_dir>/qemu/.
     * <config_dir>/qemu/share/ is exposed to the guest over 9p as
     * mount_tag=hostshare for host<->guest file exchange. */
    snprintf(qemu_cfg_dir, sizeof(qemu_cfg_dir), "%s/qemu", config_dir);
    snprintf(qemu_cfg_path, sizeof(qemu_cfg_path), "%s/qemu.cfg", qemu_cfg_dir);
    snprintf(share_path, sizeof(share_path), "%s/share", qemu_cfg_dir);
    yetty_yplatform_mkdir_p(qemu_cfg_dir);
    yetty_yplatform_mkdir_p(share_path);
    qemu_settings_ensure_default(qemu_cfg_path);
    qemu_settings_defaults(&settings);
    qemu_settings_load(&settings, qemu_cfg_path);

    if (!yetty_yplatform_file_exists(qemu_bin)) {
        yerror("QEMU binary not found: %s", qemu_bin);
        return YPROCESS_INVALID;
    }
    if (!yetty_yplatform_file_exists(bios_path)) {
        yerror("OpenSBI not found: %s", bios_path);
        return YPROCESS_INVALID;
    }
    if (!yetty_yplatform_file_exists(kernel_path)) {
        yerror("Kernel not found: %s", kernel_path);
        return YPROCESS_INVALID;
    }
    if (!yetty_yplatform_file_exists(blk_path)) {
        yerror("Block image not found: %s", blk_path);
        return YPROCESS_INVALID;
    }

    /* Slirp hostfwd: host:HOST_PORT -> guest:23 (busybox telnetd on the
     * extended rootfs). yetty connects to 127.0.0.1:HOST_PORT after the
     * guest finishes booting and telnetd binds — see qemu_wait_ready. */
    snprintf(netdev_arg, sizeof(netdev_arg), "user,id=net0,hostfwd=tcp:127.0.0.1:%u-:23",
             host_port);

    /* Kernel console chardev exposed on 127.0.0.1:2424. yetty's first
     * default tab in --qemu mode connects here via telnet-pty to see
     * the boot log + interact with the in-VM hvc0 console (the slirp
     * telnet path on 2423 is the second tab). server=on, wait=off so
     * QEMU boots immediately and accepts a connection any time;
     * telnet=on wraps the socket in the telnet protocol so the
     * telnet-pty's IAC negotiation matches what the chardev speaks. */
    snprintf(chardev_arg, sizeof(chardev_arg),
             "socket,id=char0,host=127.0.0.1,port=2424,server=on,wait=off,telnet=on");

    if (settings.extra_append[0]) {
        snprintf(append_arg, sizeof(append_arg),
                 "earlycon=sbi console=hvc0 root=/dev/vda rw init=/init %s", settings.extra_append);
    } else {
        snprintf(append_arg, sizeof(append_arg),
                 "earlycon=sbi console=hvc0 root=/dev/vda rw init=/init");
    }

    snprintf(drive_arg, sizeof(drive_arg), "file=%s,if=none,format=raw,id=hd0", blk_path);
    snprintf(fsdev_arg, sizeof(fsdev_arg), "local,id=fsdev0,path=%s,security_model=none",
             share_path);
    snprintf(memory_arg, sizeof(memory_arg), "%u", settings.memory_mb);
    snprintf(smp_arg, sizeof(smp_arg), "%u", settings.smp);

    yinfo("Starting QEMU (telnet hostfwd 127.0.0.1:%u -> guest:23, mem=%uMB smp=%u)",
          host_port, settings.memory_mb, settings.smp);

    const char *argv[40];
    int argc = 0;
    argv[argc++] = qemu_bin;
    argv[argc++] = "-machine";       argv[argc++] = "virt";
    argv[argc++] = "-smp";           argv[argc++] = smp_arg;
    argv[argc++] = "-m";             argv[argc++] = memory_arg;
    argv[argc++] = "-bios";          argv[argc++] = bios_path;
    argv[argc++] = "-kernel";        argv[argc++] = kernel_path;
    argv[argc++] = "-append";        argv[argc++] = append_arg;
    argv[argc++] = "-drive";         argv[argc++] = drive_arg;
    argv[argc++] = "-device";        argv[argc++] = "virtio-blk-device,drive=hd0";
#ifndef _WIN32
    /* virtfs/9p is disabled on the Windows minimal qemu build
     * (--without-default-features in build-tools/3rdparty/qemu/_build.sh —
     * QEMU's virtfs needs POSIX 9p machinery + xattr that don't exist on
     * Windows). Passing -fsdev there makes qemu exit immediately with
     * "fsdev support is disabled". Skip the host-share mount on Windows;
     * the guest still gets virtio-blk + virtio-net + virtio-console. */
    argv[argc++] = "-fsdev";   argv[argc++] = fsdev_arg;
    argv[argc++] = "-device";  argv[argc++] = "virtio-9p-device,fsdev=fsdev0,mount_tag=hostshare";
#endif
    argv[argc++] = "-netdev";  argv[argc++] = netdev_arg;
    argv[argc++] = "-device";  argv[argc++] = "virtio-net-device,netdev=net0";
    /* virtio-console wired to a TCP socket chardev on 127.0.0.1:2424
     * for *debugging only*. yetty does NOT connect here (the broken
     * libuv-tcp ↔ socket-chardev interaction silently kills QEMU on
     * the MSYS2 build). It's a side channel for `telnet 127.0.0.1
     * 2424` so kernel boot log / panic output remains observable. */
    argv[argc++] = "-device";  argv[argc++] = "virtio-serial-device";
    argv[argc++] = "-device";  argv[argc++] = "virtconsole,chardev=char0";
    argv[argc++] = "-chardev"; argv[argc++] = chardev_arg;
    argv[argc++] = "-serial";  argv[argc++] = "none";
    argv[argc++] = "-display"; argv[argc++] = "none";
    /* -no-reboot + -no-shutdown so a guest kernel panic doesn't take
     * the qemu process down silently — keep the chardev open so yetty
     * sees the panic message instead of a blank disconnect. */
    argv[argc++] = "-no-reboot";
    argv[argc++] = "-no-shutdown";
    argv[argc++] = NULL;

    struct yetty_yplatform_yprocess *proc =
        yetty_yplatform_yprocess_spawn(argv, /*detached=*/1, /*stdio_to_null=*/1);
    if (!proc) {
        yerror("Failed to spawn QEMU");
        return YPROCESS_INVALID;
    }

    yinfo("QEMU spawned");
    return proc;
}

void yetty_yqemu_qemu_stop(struct yetty_yplatform_yprocess *proc)
{
    if (!proc) {
        return;
    }
    yetty_yplatform_yprocess_terminate(proc, /*grace_ms=*/100);
}

struct yetty_ycore_void_result yetty_yqemu_qemu_wait_ready(uint16_t port, int timeout_ms)
{
    /* Idempotent — wraps WSAStartup on Windows, no-op on POSIX. */
    if (!yetty_platform_socket_init()) {
        yerror("qemu_wait_ready: socket subsystem init failed");
        return YETTY_ERR(yetty_ycore_void, "qemu_wait_ready: socket subsystem init failed");
    }

    double start = yetty_yplatform_ytime_monotonic_sec();

    while (1) {
        struct yetty_socket_fd_result fd_r = yetty_platform_socket_create_tcp();
        if (fd_r.ok) {
            struct yetty_ycore_void_result cr =
                yetty_yplatform_socket_connect(fd_r.value, "127.0.0.1", port);
            yetty_yplatform_socket_close(fd_r.value);
            if (cr.ok) {
                yinfo("QEMU telnet ready on port %u", port);
                return YETTY_OK_VOID();
            }
            yetty_ycore_error_destroy(cr.error);
        } else {
            yetty_ycore_error_destroy(fd_r.error);
        }

        double elapsed_ms = (yetty_yplatform_ytime_monotonic_sec() - start) * 1000.0;
        if (elapsed_ms >= (double)timeout_ms) {
            yerror("Timeout waiting for QEMU on port %u", port);
            return YETTY_ERR(yetty_ycore_void, "qemu_wait_ready: timeout");
        }

        yetty_yplatform_ytime_sleep_ms(100);
    }
}
