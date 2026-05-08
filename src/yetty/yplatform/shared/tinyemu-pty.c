/* TinyEMU PTY - RISC-V VM as PTY backend
 *
 * Provides a PTY interface backed by a RISC-V Linux VM running in TinyEMU.
 * Used on platforms that don't support fork/exec (iOS) or when --virtual flag is passed.
 *
 * License: MIT (TinyEMU), GPL v2 (embedded Linux kernel)
 */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* TinyEMU headers */
#include <tinyemu/cutils.h>
#include <tinyemu/iomem.h>
#include <tinyemu/virtio.h>
#include <tinyemu/machine.h>
#include <tinyemu/fs.h>
#ifdef CONFIG_SLIRP
#include <tinyemu/slirp/libslirp.h>
#endif

/* TinyEMU PTY implementation */
struct yetty_yplatform_tinyemu_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    /* os_input_pipe: terminal writes [1], VM reads [0] - keyboard from OS */
    int os_input_pipe[2];

    /* pty_pipe: VM writes [1], terminal polls [0] - console output */
    int pty_pipe[2];

    /* VM state */
    VirtMachine *vm;
    pthread_t vm_thread;
    int running;
    uint32_t cols;
    uint32_t rows;

    /* Config path */
    char *config_path;
};

/* External: get data/config directories (platform-specific) */
extern const char *yetty_yplatform_get_data_dir(void);
extern const char *yetty_yplatform_get_config_dir(void);

/* Forward declarations */
static struct yetty_ycore_void_result tinyemu_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result tinyemu_pty_read(struct yetty_platform_pty *self, char *buf,
                                                       size_t max_len);
static struct yetty_ycore_size_result tinyemu_pty_write(struct yetty_platform_pty *self,
                                                        const char *data, size_t len);
static struct yetty_ycore_void_result tinyemu_pty_resize(struct yetty_platform_pty *self,
                                                         uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result tinyemu_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *tinyemu_pty_pipe_source(
    struct yetty_platform_pty *self);

/* Ops table */
static const struct yetty_platform_pty_ops tinyemu_pty_ops = {
    .destroy = tinyemu_pty_destroy,
    .read = tinyemu_pty_read,
    .write = tinyemu_pty_write,
    .resize = tinyemu_pty_resize,
    .stop = tinyemu_pty_stop,
    .pipe_source = tinyemu_pty_pipe_source,
};

/* Global PTY pointer for console callbacks */
static struct yetty_yplatform_tinyemu_pty *g_pty = NULL;

/* Console write callback - VM outputs data to pty_pipe */
static void tinyemu_console_write(void *opaque, const uint8_t *buf, int len)
{
    struct yetty_yplatform_tinyemu_pty *pty = g_pty;
    (void)opaque;
    if (!pty || len <= 0) {
        return;
    }

    ssize_t written = write(pty->pty_pipe[1], buf, len);
    if (written != len) {
        ywarn("tinyemu_console_write: wrote %zd of %d bytes", written, len);
    }
}

/* Console read callback - VM reads input from os_input_pipe */
static int tinyemu_console_read(void *opaque, uint8_t *buf, int len)
{
    struct yetty_yplatform_tinyemu_pty *pty = g_pty;
    (void)opaque;
    if (!pty || len <= 0) {
        return 0;
    }

    int ret = read(pty->os_input_pipe[0], buf, len);
    if (ret > 0) {
        ydebug("tinyemu_console_read: VM read %d bytes input", ret);
    }
    return (ret > 0) ? ret : 0;
}

/* Block device implementation (same as temu.c) */
typedef enum {
    YETTY_YPLATFORM_BF_MODE_RO,
    YETTY_YPLATFORM_BF_MODE_RW,
    YETTY_YPLATFORM_BF_MODE_SNAPSHOT,
} BlockDeviceModeEnum;

typedef struct {
    FILE *f;
    int64_t nb_sectors;
    BlockDeviceModeEnum mode;
    uint8_t **sector_table;
#if defined(__EMSCRIPTEN__)
    /* Wasm-only: the rootfs lives in MEMFS, and every fseek/fread call
     * crosses the wasm↔JS boundary (≫1µs each). For workloads like
     * `find /` that issue thousands of small reads, this dominates.
     * Pre-read the whole image into a native buffer at init so reads
     * become memcpy. ~700 MB on the extended rootfs — fits inside our
     * fixed 2 GiB SharedArrayBuffer with room to spare. */
    uint8_t *raw_data;
    int64_t raw_size;
#endif
} BlockDeviceFile;

static int64_t bf_get_sector_count(BlockDevice *bs)
{
    BlockDeviceFile *bf = bs->opaque;
    return bf->nb_sectors;
}

static int bf_read_async(BlockDevice *bs, uint64_t sector_num, uint8_t *buf, int n,
                         BlockDeviceCompletionFunc *cb, void *opaque)
{
    BlockDeviceFile *bf = bs->opaque;
#if defined(__EMSCRIPTEN__)
    /* Native-buffer fast path. Snapshot writes are still tracked in
     * sector_table; reads prefer overrides, otherwise memcpy from the
     * pre-loaded image. No JS boundary crossings on the read path. */
    if (bf->raw_data) {
        if (bf->mode == YETTY_YPLATFORM_BF_MODE_SNAPSHOT) {
            for (int i = 0; i < n; i++) {
                if (bf->sector_table[sector_num]) {
                    memcpy(buf, bf->sector_table[sector_num], 512);
                } else {
                    memcpy(buf, bf->raw_data + sector_num * 512, 512);
                }
                sector_num++;
                buf += 512;
            }
        } else {
            memcpy(buf, bf->raw_data + sector_num * 512, (size_t)n * 512);
        }
        return 0;
    }
#endif
    if (bf->mode == YETTY_YPLATFORM_BF_MODE_SNAPSHOT) {
        for (int i = 0; i < n; i++) {
            if (!bf->sector_table[sector_num]) {
                fseek(bf->f, sector_num * 512, SEEK_SET);
                fread(buf, 1, 512, bf->f);
            } else {
                memcpy(buf, bf->sector_table[sector_num], 512);
            }
            sector_num++;
            buf += 512;
        }
    } else {
        fseek(bf->f, sector_num * 512, SEEK_SET);
        fread(buf, 1, n * 512, bf->f);
    }
    return 0;
}

static int bf_write_async(BlockDevice *bs, uint64_t sector_num, const uint8_t *buf, int n,
                          BlockDeviceCompletionFunc *cb, void *opaque)
{
    BlockDeviceFile *bf = bs->opaque;
    switch (bf->mode) {
    case YETTY_YPLATFORM_BF_MODE_RO:
        return -1;
    case YETTY_YPLATFORM_BF_MODE_RW:
        fseek(bf->f, sector_num * 512, SEEK_SET);
        fwrite(buf, 1, n * 512, bf->f);
        return 0;
    case YETTY_YPLATFORM_BF_MODE_SNAPSHOT:
        for (int i = 0; i < n; i++) {
            if (!bf->sector_table[sector_num]) {
                bf->sector_table[sector_num] = malloc(512);
            }
            memcpy(bf->sector_table[sector_num], buf, 512);
            sector_num++;
            buf += 512;
        }
        return 0;
    }
    return -1;
}

static BlockDevice *block_device_init(const char *filename, BlockDeviceModeEnum mode)
{
    BlockDevice *bs;
    BlockDeviceFile *bf;
    int64_t file_size;
    FILE *f;

    f = fopen(filename, mode == YETTY_YPLATFORM_BF_MODE_RW ? "r+b" : "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    file_size = ftello(f);

    bs = mallocz(sizeof(*bs));
    bf = mallocz(sizeof(*bf));

    bf->mode = mode;
    bf->nb_sectors = file_size / 512;
    bf->f = f;

    if (mode == YETTY_YPLATFORM_BF_MODE_SNAPSHOT) {
        bf->sector_table = mallocz(sizeof(bf->sector_table[0]) * bf->nb_sectors);
    }

#if defined(__EMSCRIPTEN__)
    /* Slurp the whole image into a native buffer so per-sector reads
     * never touch MEMFS. file_size is bounded by what we extracted (the
     * 700 MiB extended rootfs, fits in our 2 GiB heap). On failure we
     * leave raw_data NULL and fall back to fseek/fread. */
    if (file_size > 0) {
        bf->raw_data = malloc((size_t)file_size);
        if (bf->raw_data) {
            fseek(f, 0, SEEK_SET);
            size_t got = fread(bf->raw_data, 1, (size_t)file_size, f);
            if (got != (size_t)file_size) {
                yerror("block_device_init: short read on %s (%zu/%lld)",
                       filename, got, (long long)file_size);
                free(bf->raw_data);
                bf->raw_data = NULL;
            } else {
                bf->raw_size = file_size;
                yinfo("block_device_init: cached %s (%lld bytes) in native buffer",
                      filename, (long long)file_size);
            }
        } else {
            ywarn("block_device_init: malloc(%lld) failed; falling back to MEMFS reads",
                  (long long)file_size);
        }
    }
#endif

    bs->opaque = bf;
    bs->get_sector_count = bf_get_sector_count;
    bs->read_async = bf_read_async;
    bs->write_async = bf_write_async;
    return bs;
}

#ifdef CONFIG_SLIRP
/* SLIRP networking */
static Slirp *slirp_state;

static void slirp_write_packet(EthernetDevice *net, const uint8_t *buf, int len)
{
    Slirp *slirp = net->opaque;
    slirp_input(slirp, buf, len);
}

int slirp_can_output(void *opaque)
{
    EthernetDevice *net = opaque;
    return net->device_can_write_packet(net);
}

void slirp_output(void *opaque, const uint8_t *pkt, int pkt_len)
{
    EthernetDevice *net = opaque;
    net->device_write_packet(net, pkt, pkt_len);
}

static void slirp_select_fill1(EthernetDevice *net, int *pfd_max, fd_set *rfds, fd_set *wfds,
                               fd_set *efds, int *pdelay)
{
    Slirp *slirp = net->opaque;
    slirp_select_fill(slirp, pfd_max, rfds, wfds, efds);
}

static void slirp_select_poll1(EthernetDevice *net, fd_set *rfds, fd_set *wfds, fd_set *efds,
                               int select_ret)
{
    Slirp *slirp = net->opaque;
    slirp_select_poll(slirp, rfds, wfds, efds, (select_ret <= 0));
}

static EthernetDevice *slirp_open(const VMEthEntry *e)
{
    EthernetDevice *net;
    struct in_addr net_addr = {.s_addr = htonl(0x0a000200)};
    struct in_addr mask = {.s_addr = htonl(0xffffff00)};
    struct in_addr host = {.s_addr = htonl(0x0a000202)};
    struct in_addr dhcp = {.s_addr = htonl(0x0a00020f)};
    struct in_addr dns = {.s_addr = htonl(0x0a000203)};

    if (slirp_state) {
        return NULL;
    }

    net = mallocz(sizeof(*net));
    slirp_state = slirp_init(0, net_addr, mask, host, NULL, "", NULL, dhcp, dns, net);

    /* Apply config-driven port forwards (qemu-style "tcp:HOST-:GUEST"). On
     * the yetty side this typically exposes the in-guest telnetd at
     * 127.0.0.1:2323 so a separate yetty --telnet 2323 (or any telnet
     * client) can attach to a real pty inside the VM, while the primary
     * yetty terminal continues to talk to hvc0 over the in-process pipe. */
    if (e) {
        struct in_addr any = {.s_addr = htonl(INADDR_ANY)};
        struct in_addr guest_any = {.s_addr = 0};
        for (int j = 0; j < e->hostfwd_count; j++) {
            const VMEthHostFwd *hf = &e->hostfwd[j];
            if (slirp_add_hostfwd(slirp_state, hf->is_udp ? 1 : 0, any,
                                  hf->host_port, guest_any, hf->guest_port) < 0) {
                yerror("slirp: failed to add hostfwd %s:%d -> guest:%d (port busy?)",
                       hf->is_udp ? "udp" : "tcp", hf->host_port, hf->guest_port);
                /* Non-fatal: VM still boots, the forward just isn't active. */
            } else {
                yinfo("slirp: hostfwd %s 0.0.0.0:%d -> 10.0.2.15:%d",
                      hf->is_udp ? "udp" : "tcp", hf->host_port, hf->guest_port);
            }
        }
    }

    net->mac_addr[0] = 0x02;
    net->mac_addr[1] = 0x00;
    net->mac_addr[2] = 0x00;
    net->mac_addr[3] = 0x00;
    net->mac_addr[4] = 0x00;
    net->mac_addr[5] = 0x01;
    net->opaque = slirp_state;
    net->write_packet = slirp_write_packet;
    net->select_fill = slirp_select_fill1;
    net->select_poll = slirp_select_poll1;

    return net;
}
#endif

/* VM run loop */
/* On wasm each RISC-V cycle is 5–20× slower than native (the interpreter
 * runs through wasm). 500k cycles per batch becomes 50–100ms, which caps
 * keystroke latency at one batch. Drop the batch size and the select
 * timeout so the loop polls os_input_pipe[0] more often. */
#if defined(__EMSCRIPTEN__)
#define MAX_EXEC_CYCLE 50000
#define MAX_SLEEP_TIME 2
#else
#define MAX_EXEC_CYCLE 500000
#define MAX_SLEEP_TIME 10
#endif

static void vm_run_once(struct yetty_yplatform_tinyemu_pty *pty)
{
    VirtMachine *m = pty->vm;
    fd_set rfds, wfds, efds;
    int fd_max, ret, delay;
    struct timeval tv;

    delay = virt_machine_get_sleep_duration(m, MAX_SLEEP_TIME);

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    fd_max = -1;

    /* Add os_input_pipe[0] to select for keyboard input */
    if (m->console_dev && virtio_console_can_write_data(m->console_dev)) {
        FD_SET(pty->os_input_pipe[0], &rfds);
        if (pty->os_input_pipe[0] > fd_max) {
            fd_max = pty->os_input_pipe[0];
        }
    }

    if (m->net) {
        m->net->select_fill(m->net, &fd_max, &rfds, &wfds, &efds, &delay);
    }

    tv.tv_sec = delay / 1000;
    tv.tv_usec = (delay % 1000) * 1000;
    ret = select(fd_max + 1, &rfds, &wfds, &efds, &tv);

    /* Feed keyboard input to VM */
    if (ret > 0 && m->console_dev && FD_ISSET(pty->os_input_pipe[0], &rfds)) {
        uint8_t buf[128];
        int len = virtio_console_get_write_len(m->console_dev);
        if (len > (int)sizeof(buf)) {
            len = sizeof(buf);
        }
        int n = read(pty->os_input_pipe[0], buf, len);
        if (n > 0) {
            virtio_console_write_data(m->console_dev, buf, n);
        }
    }

    if (m->net) {
        m->net->select_poll(m->net, &rfds, &wfds, &efds, ret);
    }

    virt_machine_interp(m, MAX_EXEC_CYCLE);
}

/* VM thread function */
static void *vm_thread_func(void *arg)
{
    struct yetty_yplatform_tinyemu_pty *pty = arg;

    yinfo("vm_thread_func: VM thread started");

    struct timespec start_ts;
    int redrive_done = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    while (pty->running && pty->vm) {
        vm_run_once(pty);

        /* The first virtio_console_resize_event we issue from
         * tinyemu_pty_resize can race with the kernel's virtio_console
         * probe — kernel reads cols/rows from config_space on probe,
         * and may not always re-read on a config_change interrupt. So
         * after the kernel has had time to probe the device (~3s on
         * most boards), we re-fire the resize event. By then the
         * kernel's config_changed handler is registered and will pick
         * up the new size, propagate it to the tty, and SIGWINCH the
         * shell — which resizes vi/htop/etc. correctly. */
        if (!redrive_done && pty->cols > 0 && pty->rows > 0 && pty->vm &&
            ((VirtMachine *)pty->vm)->console_dev) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms =
                (now.tv_sec - start_ts.tv_sec) * 1000 + (now.tv_nsec - start_ts.tv_nsec) / 1000000;
            if (elapsed_ms >= 3000) {
                virtio_console_resize_event(((VirtMachine *)pty->vm)->console_dev, (int)pty->cols,
                                            (int)pty->rows);
                yinfo("tinyemu: re-fired resize %ux%u %ldms after VM start", pty->cols, pty->rows,
                      elapsed_ms);
                redrive_done = 1;
            }
        }
    }

    yinfo("vm_thread_func: VM thread exiting");
    return NULL;
}

/* Initialize VM */
static int init_vm(struct yetty_yplatform_tinyemu_pty *pty)
{
    VirtMachineParams p_s, *p = &p_s;

    g_pty = pty;

    virt_machine_set_defaults(p);
    yinfo("init_vm: loading cfg %s", pty->config_path);
    virt_machine_load_config_file(p, pty->config_path, NULL, NULL);
    yinfo("init_vm: cfg loaded — drive_count=%d fs_count=%d eth_count=%d",
          p->drive_count, p->fs_count, p->eth_count);

    /* Initialize block devices */
    for (int i = 0; i < p->drive_count; i++) {
        char *fname = get_file_path(p->cfg_filename, p->tab_drive[i].filename);
        yinfo("init_vm: opening drive%d %s", i, fname);
        BlockDevice *drive = block_device_init(fname, YETTY_YPLATFORM_BF_MODE_SNAPSHOT);
        if (!drive) {
            yerror("init_vm: drive%d open failed: %s", i, fname);
            free(fname);
            virt_machine_free_config(p);
            return -1;
        }
        free(fname);
        p->tab_drive[i].block_dev = drive;
    }

    /* Initialize 9p filesystems */
    for (int i = 0; i < p->fs_count; i++) {
        char *fname = get_file_path(p->cfg_filename, p->tab_fs[i].filename);
        FSDevice *fs = fs_disk_init(fname);
        if (!fs) {
            yerror("init_vm: fs%d init failed: %s", i, fname);
            free(fname);
            virt_machine_free_config(p);
            return -1;
        }
        free(fname);
        p->tab_fs[i].fs_dev = fs;
    }

    /* Initialize network */
    for (int i = 0; i < p->eth_count; i++) {
#ifdef CONFIG_SLIRP
        if (!strcmp(p->tab_eth[i].driver, "user")) {
            p->tab_eth[i].net = slirp_open(&p->tab_eth[i]);
            if (!p->tab_eth[i].net) {
                yerror("init_vm: slirp_open failed for eth%d", i);
            }
        }
#endif
    }

    /* Setup console */
    CharacterDevice *console = mallocz(sizeof(*console));
    console->write_data = tinyemu_console_write;
    console->read_data = tinyemu_console_read;
    p->console = console;
    p->rtc_real_time = TRUE;

    yinfo("init_vm: calling virt_machine_init");
    pty->vm = virt_machine_init(p);
    if (!pty->vm) {
        yerror("init_vm: virt_machine_init returned NULL");
        virt_machine_free_config(p);
        return -1;
    }
    yinfo("init_vm: VM created");

    virt_machine_free_config(p);

    if (pty->vm->net) {
        pty->vm->net->device_set_carrier(pty->vm->net, TRUE);
    }

    /* If a resize was already requested before the VM came up, push the
     * stored cols/rows now so the kernel boots with the right size. */
    if (pty->cols > 0 && pty->rows > 0 && pty->vm->console_dev) {
        virtio_console_resize_event(pty->vm->console_dev, (int)pty->cols, (int)pty->rows);
    }

    return 0;
}

/* PTY implementation */

static struct yetty_ycore_void_result tinyemu_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);
    struct yetty_ycore_void_result stop_r = tinyemu_pty_stop(self);

    if (pty->os_input_pipe[0] >= 0) {
        close(pty->os_input_pipe[0]);
    }
    if (pty->os_input_pipe[1] >= 0) {
        close(pty->os_input_pipe[1]);
    }
    if (pty->pty_pipe[0] >= 0) {
        close(pty->pty_pipe[0]);
    }
    if (pty->pty_pipe[1] >= 0) {
        close(pty->pty_pipe[1]);
    }

    free(pty->config_path);
    if (g_pty == pty) {
        g_pty = NULL;
    }
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "tinyemu_pty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result tinyemu_pty_read(struct yetty_platform_pty *self, char *buf,
                                                       size_t max_len)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);

    if (!pty->running || max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Read from pty_pipe[0] - VM output */
    ssize_t n = read(pty->pty_pipe[0], buf, max_len);
    if (n < 0) {
        n = 0;
    }

    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result tinyemu_pty_write(struct yetty_platform_pty *self,
                                                        const char *data, size_t len)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);

    if (!pty->running || len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Write to os_input_pipe[1] - keyboard input to VM */
    ssize_t n = write(pty->os_input_pipe[1], data, len);
    if (n < 0) {
        n = 0;
    }

    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_void_result tinyemu_pty_resize(struct yetty_platform_pty *self,
                                                         uint32_t cols, uint32_t rows)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);
    pty->cols = cols;
    pty->rows = rows;
    /* Push the new size into the guest via virtio-console.
     * virtio_console_resize_event() updates the device's config space
     * and notifies the guest; the kernel's hvc driver picks this up and
     * issues SIGWINCH on the controlling tty so vi/htop/etc. redraw at
     * the right size.
     *
     * If the VM hasn't been created yet, the latest pty->cols/rows we
     * just stored will be applied at vm_thread_func startup (initial
     * push from there). */
    if (pty->vm && ((VirtMachine *)pty->vm)->console_dev) {
        virtio_console_resize_event(((VirtMachine *)pty->vm)->console_dev, (int)cols, (int)rows);
        yinfo("tinyemu: resized console to %ux%u (live)", cols, rows);
    } else {
        yinfo("tinyemu: deferred resize to %ux%u (vm not ready)", cols, rows);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tinyemu_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);

    if (!pty->running) {
        return YETTY_OK_VOID();
    }

    pty->running = 0;

    if (pty->vm_thread) {
        pthread_join(pty->vm_thread, NULL);
        pty->vm_thread = 0;
    }

    if (pty->vm) {
        virt_machine_end(pty->vm);
        pty->vm = NULL;
    }

    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *tinyemu_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_yplatform_tinyemu_pty *pty = container_of(self, struct yetty_yplatform_tinyemu_pty, base);
    return &pty->pipe_source;
}

/* Create TinyEMU PTY */
struct yetty_yplatform_pty_result yetty_yplatform_tinyemu_pty_create(struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_tinyemu_pty *pty;

    pty = malloc(sizeof(struct yetty_yplatform_tinyemu_pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate tinyemu pty");
    }

    memset(pty, 0, sizeof(*pty));
    pty->base.ops = &tinyemu_pty_ops;
    pty->os_input_pipe[0] = -1;
    pty->os_input_pipe[1] = -1;
    pty->pty_pipe[0] = -1;
    pty->pty_pipe[1] = -1;
    pty->cols = 80;
    pty->rows = 24;

    /* Create os_input_pipe: terminal writes [1], VM reads [0] */
    if (pipe(pty->os_input_pipe) < 0) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to create os_input_pipe");
    }

    /* Create pty_pipe: VM writes [1], terminal reads [0] */
    if (pipe(pty->pty_pipe) < 0) {
        close(pty->os_input_pipe[0]);
        close(pty->os_input_pipe[1]);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to create pty_pipe");
    }

    /* Set non-blocking */
    fcntl(pty->os_input_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(pty->os_input_pipe[1], F_SETFL, O_NONBLOCK);
    fcntl(pty->pty_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(pty->pty_pipe[1], F_SETFL, O_NONBLOCK);

    /* Terminal polls pty_pipe[0] for VM output */
    pty->pipe_source.abstract = pty->pty_pipe[0];

    /* The cfg is shipped via incbin (assets/yemu/temu/yetty-temu-extended.cfg)
     * and dropped at <config_dir>/temu/yetty-temu-extended.cfg by
     * extract-assets at startup. Path references inside the cfg use
     * $YETTY_RUNTIME_DIR which the platform layer exports (see glfw-main.c). */
    {
        const char *config_dir = yetty_yplatform_get_config_dir();
        char cfg_path[512];
        snprintf(cfg_path, sizeof(cfg_path),
                 "%s/temu/yetty-temu-extended.cfg", config_dir);
        if (access(cfg_path, F_OK) != 0) {
            close(pty->os_input_pipe[0]);
            close(pty->os_input_pipe[1]);
            close(pty->pty_pipe[0]);
            close(pty->pty_pipe[1]);
            free(pty);
            yerror("tinyemu: missing cfg at %s — extract-assets did not run?",
                   cfg_path);
            return YETTY_ERR(yetty_yplatform_pty,
                             "missing temu cfg under config dir");
        }
        pty->config_path = strdup(cfg_path);
    }

    /* Initialize VM */
    if (init_vm(pty) < 0) {
        tinyemu_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty, "failed to initialize VM");
    }

    /* Start VM thread */
    pty->running = 1;
    if (pthread_create(&pty->vm_thread, NULL, vm_thread_func, pty) != 0) {
        tinyemu_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty, "failed to start VM thread");
    }

    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}

/* Factory implementation */

struct yetty_yplatform_tinyemu_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
};

static void tinyemu_pty_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct yetty_yplatform_tinyemu_pty_factory *factory = container_of(self, struct yetty_yplatform_tinyemu_pty_factory, base);
    free(factory);
}

static struct yetty_yplatform_pty_result tinyemu_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_yplatform_tinyemu_pty_factory *factory = container_of(self, struct yetty_yplatform_tinyemu_pty_factory, base);
    (void)event_loop;
    return yetty_yplatform_tinyemu_pty_create(factory->config);
}

static const struct yetty_yplatform_pty_factory_ops tinyemu_pty_factory_ops = {
    .destroy = tinyemu_pty_factory_destroy,
    .create_pty = tinyemu_pty_factory_create_pty,
};

/* Factory creation - called when --virtual flag is set */
struct yetty_yplatform_pty_factory_result yetty_yplatform_tinyemu_pty_factory_create(struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_tinyemu_pty_factory *factory;

    factory = malloc(sizeof(struct yetty_yplatform_tinyemu_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate tinyemu pty factory");
    }

    factory->base.ops = &tinyemu_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}

#if defined(__EMSCRIPTEN__)
/* On the WebAssembly target the only pty implementation is the embedded
 * tinyemu RISC-V VM, so the public yetty_yplatform_pty_factory_create
 * symbol is just an alias of yetty_yplatform_tinyemu_pty_factory_create
 * (mirrors the iOS variant). */
struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    (void)os_specific;
    return yetty_yplatform_tinyemu_pty_factory_create(config);
}
#endif
