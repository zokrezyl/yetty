/* iOS TinyEMU PTY - RISC-V VM as PTY backend */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytelnet/telnet-pty.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
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

/* External platform-paths from platform-paths.m. Assets live under data_dir
 * (extracted from incbin by yetty_yplatform_extract_assets at startup); the
 * VM cfg is auto-generated under config_dir/temu/ — same model as Linux
 * desktop (src/yetty/yplatform/shared/tinyemu-pty.c). */
extern const char *yetty_yplatform_get_config_dir(void);
extern const char *yetty_yplatform_get_data_dir(void);

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
    if (!pty || len <= 0) {
        return;
    }

    write(pty->pty_pipe[1], buf, len);
}

/* Console read callback - VM reads input from os_input_pipe */
static int tinyemu_console_read(void *opaque, uint8_t *buf, int len)
{
    struct yetty_yplatform_tinyemu_pty *pty = g_pty;
    if (!pty || len <= 0) {
        return 0;
    }

    int ret = read(pty->os_input_pipe[0], buf, len);
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

static EthernetDevice *slirp_open(void)
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
#define MAX_EXEC_CYCLE 500000
#define MAX_SLEEP_TIME 10

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

    while (pty->running && pty->vm) {
        vm_run_once(pty);
    }

    return NULL;
}

/* Initialize VM */
static int init_vm(struct yetty_yplatform_tinyemu_pty *pty)
{
    VirtMachineParams p_s, *p = &p_s;

    g_pty = pty;

    virt_machine_set_defaults(p);
    virt_machine_load_config_file(p, pty->config_path, NULL, NULL);

    /* Initialize block devices */
    for (int i = 0; i < p->drive_count; i++) {
        char *fname = get_file_path(p->cfg_filename, p->tab_drive[i].filename);
        BlockDevice *drive = block_device_init(fname, YETTY_YPLATFORM_BF_MODE_SNAPSHOT);
        free(fname);
        if (!drive) {
            virt_machine_free_config(p);
            return -1;
        }
        p->tab_drive[i].block_dev = drive;
    }

    /* Initialize network. CONFIG_SLIRP must be defined for the eth0 in our
     * cfg to actually get a backing device — otherwise virtio_net_init in
     * tinyemu derefs a NULL EthernetDevice and we SIGSEGV. The cmake target
     * for ios/tvos passes CONFIG_SLIRP for this file. */
    for (int i = 0; i < p->eth_count; i++) {
#ifdef CONFIG_SLIRP
        if (!strcmp(p->tab_eth[i].driver, "user")) {
            p->tab_eth[i].net = slirp_open();
        }
#endif
    }

    /* Setup console */
    CharacterDevice *console = mallocz(sizeof(*console));
    console->write_data = tinyemu_console_write;
    console->read_data = tinyemu_console_read;
    p->console = console;
    p->rtc_real_time = TRUE;

    pty->vm = virt_machine_init(p);
    if (!pty->vm) {
        virt_machine_free_config(p);
        return -1;
    }

    virt_machine_free_config(p);

    if (pty->vm->net) {
        pty->vm->net->device_set_carrier(pty->vm->net, TRUE);
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
    /* TODO: Send resize to VM via virtio-console if supported */
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
static struct yetty_yplatform_pty_result tinyemu_pty_create(struct yetty_yconfig_config *config)
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

    /* Resolve VM config path under <config_dir>/temu/root-riscv64.cfg.
     * Kernel/opensbi/alpine-rootfs.img were extracted from incbin to
     * <data_dir>/yemu/ by yetty_yplatform_extract_assets() at startup. If
     * the cfg does not exist yet, emit it with absolute paths so tinyemu's
     * get_file_path() resolves them as-is. Mirrors shared/tinyemu-pty.c. */
    {
        const char *config_dir = yetty_yplatform_get_config_dir();
        const char *data_dir = yetty_yplatform_get_data_dir();
        char cfg_dir[512];
        char cfg_path[512];
        int cfg_ready = 1;

        snprintf(cfg_dir, sizeof(cfg_dir), "%s/temu", config_dir);
        snprintf(cfg_path, sizeof(cfg_path), "%s/root-riscv64.cfg", cfg_dir);

        /* mkdir -p <config_dir>/temu */
        {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s", cfg_dir);
            for (char *p = tmp + 1; *p && cfg_ready; p++) {
                if (*p == '/') {
                    *p = 0;
                    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                        cfg_ready = 0;
                    }
                    *p = '/';
                }
            }
            if (cfg_ready && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                cfg_ready = 0;
            }
        }

        /* Always overwrite the cfg: data_dir is a sandbox-relative path
         * that can change between installs (iOS rotates the container UUID).
         * Caching the cfg means a stale path from a previous install
         * survives — and load_file(opensbi-fw_jump.elf) then perror+exits.
         * Cheap to re-emit; just do it. */
        if (cfg_ready) {
            (void)unlink(cfg_path);
            FILE *f = fopen(cfg_path, "w");
            if (f) {
                fprintf(f,
                        "/* TinyEMU VM Configuration (auto-generated; edit to customize) */\n"
                        "{\n"
                        "    version: 1,\n"
                        "    machine: \"riscv64\",\n"
                        "    memory_size: 256,\n"
                        "    bios: \"%s/yemu/opensbi-fw_jump.elf\",\n"
                        "    kernel: \"%s/yemu/kernel-riscv64.bin\",\n"
                        "    cmdline: \"earlycon=sbi console=hvc0 root=/dev/vda rootfstype=ext4 rw "
                        "init=/init\",\n"
                        "    drive0: { file: \"%s/yemu/alpine-rootfs.img\" },\n"
                        /* eth0 is required even though we don't use the network: without
                     * a virtio-net device the kernel wedges at PLIC init and never
                     * reaches console=hvc0 switch. Same shape as the shared cfg in
                     * src/yetty/yplatform/shared/tinyemu-pty.c. */
                        "    eth0: { driver: \"user\" }\n"
                        "}\n",
                        data_dir, data_dir, data_dir);
                fclose(f);
                yinfo("tinyemu: wrote default cfg to %s", cfg_path);
            } else {
                cfg_ready = 0;
            }
        }

        if (!cfg_ready) {
            close(pty->os_input_pipe[0]);
            close(pty->os_input_pipe[1]);
            close(pty->pty_pipe[0]);
            close(pty->pty_pipe[1]);
            free(pty);
            return YETTY_ERR(yetty_yplatform_pty, "failed to prepare temu cfg under config dir");
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
    struct yetty_yplatform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_yplatform_tinyemu_pty_factory *factory = container_of(self, struct yetty_yplatform_tinyemu_pty_factory, base);

    /* --telnet: connect as a pure telnet client to an already-running
     * server (e.g. the companion YettyQemu.app's qemu virtio-console
     * chardev listening on 127.0.0.1:2323). No fork/exec, no qemu
     * spawning — yetty just opens the TCP socket. */
    if (factory->config &&
        factory->config->ops->get_bool(factory->config,
                                       YETTY_YCONFIG_KEY_TELNET, 0)) {
        const char *host = factory->config->ops->get_string(
            factory->config, YETTY_YCONFIG_KEY_TELNET_HOST, "127.0.0.1");
        int port = factory->config->ops->get_int(
            factory->config, YETTY_YCONFIG_KEY_TELNET_PORT, 0);
        if (port <= 0 || port > 65535) {
            return YETTY_ERR(yetty_yplatform_pty,
                             "--telnet requires telnet/port (1..65535)");
        }
        return yetty_ytelnet_telnet_pty_create(host, (uint16_t)port, event_loop);
    }

    /* Fallback: TinyEMU (the original iOS / tvOS default). */
    (void)event_loop;
    return tinyemu_pty_create(factory->config);
}

static const struct yetty_yplatform_pty_factory_ops tinyemu_pty_factory_ops = {
    .destroy = tinyemu_pty_factory_destroy,
    .create_pty = tinyemu_pty_factory_create_pty,
};

/* Factory creation - the public API */
struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    struct yetty_yplatform_tinyemu_pty_factory *factory;

    (void)os_specific;

    factory = malloc(sizeof(struct yetty_yplatform_tinyemu_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate tinyemu pty factory");
    }

    factory->base.ops = &tinyemu_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}
