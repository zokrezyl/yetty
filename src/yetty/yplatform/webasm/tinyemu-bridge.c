/* Standalone TinyEMU wasm — entry point for tinyemu.wasm.
 *
 * This file is the ONLY C entry point of the iframe wasm module. It runs
 * a single-threaded RISC-V Linux VM in the iframe's wasm context and
 * shuttles console bytes to/from the parent yetty.wasm via window
 * postMessage. The parent side lives in src/yetty/yplatform/webasm/
 * iframe-pty.c (struct yetty_yplatform_iframe_pty).
 *
 * Why this exists: the previous in-process design ran TinyEMU on a
 * pthread of yetty.wasm. Every libc syscall the VM thread issued went
 * through emscripten's pthread shim (Atomics.notify / proxying), which
 * cost ~µs per call and dominated runtime. Splitting TinyEMU into its
 * own iframe lets it drop -pthread entirely and use plain MEMFS / libc
 * with no cross-thread coordination.
 *
 * Wire protocol (mirrors iframe-pty.c, term-bridge.js):
 *   parent → iframe : term-input  / term-resize       (postMessage)
 *   iframe → parent : term-output                     (postMessage)
 *
 * JS-callable C functions (declared with EMSCRIPTEN_KEEPALIVE):
 *   tinyemu_bridge_input(ptr, len)        — bytes from the user's keyboard
 *   tinyemu_bridge_resize(cols, rows)     — geometry change
 *
 * C-callable JS hook:
 *   window.__yettyTinyemuPostOutput(ptr, len)
 *     Posts a 'term-output' message to window.parent. Defined by
 *     tinyemu-iframe.html before tinyemu.js loads.
 */

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinyemu/cutils.h>
#include <tinyemu/iomem.h>
#include <tinyemu/virtio.h>
#include <tinyemu/machine.h>
#include <tinyemu/fs.h>
#ifdef CONFIG_SLIRP
#include <tinyemu/slirp/libslirp.h>
#include <tinyemu/slirp/chr-backend.h>
#endif

/* ---- Console input ring ---- */

#define INPUT_RING_CAPACITY (64 * 1024)

struct input_ring {
    uint8_t buf[INPUT_RING_CAPACITY];
    size_t head;  /* write index */
    size_t tail;  /* read index */
    size_t size;  /* bytes in flight */
};

static struct input_ring g_input;
static VirtMachine *g_vm = NULL;
static int g_running = 0;
static int g_pending_cols = 0;
static int g_pending_rows = 0;

static size_t input_ring_push(const uint8_t *data, size_t len)
{
    size_t avail = INPUT_RING_CAPACITY - g_input.size;
    size_t n = (len < avail) ? len : avail;
    for (size_t i = 0; i < n; i++) {
        g_input.buf[g_input.head] = data[i];
        g_input.head = (g_input.head + 1) % INPUT_RING_CAPACITY;
    }
    g_input.size += n;
    return n;
}

static size_t input_ring_pop(uint8_t *out, size_t max_len)
{
    size_t n = (max_len < g_input.size) ? max_len : g_input.size;
    for (size_t i = 0; i < n; i++) {
        out[i] = g_input.buf[g_input.tail];
        g_input.tail = (g_input.tail + 1) % INPUT_RING_CAPACITY;
    }
    g_input.size -= n;
    return n;
}

/* ---- TinyEMU console callbacks ---- */

static void bridge_console_write(void *opaque, const uint8_t *buf, int len)
{
    (void)opaque;
    if (len <= 0) {
        return;
    }
    /* Hand the bytes straight to JS — no copy on the C side beyond what
     * EM_ASM already does to expose ptr/len. tinyemu-iframe.html's
     * __yettyTinyemuPostOutput then UTF-8 decodes and postMessages. */
    EM_ASM({
        if (window.__yettyTinyemuPostOutput) {
            window.__yettyTinyemuPostOutput($0, $1);
        }
    }, buf, len);
}

static int bridge_console_read(void *opaque, uint8_t *buf, int len)
{
    (void)opaque;
    if (len <= 0) {
        return 0;
    }
    return (int)input_ring_pop(buf, (size_t)len);
}

/* ---- Block device (simplified — wasm always pre-slurps the image) ---- */

typedef enum {
    BRIDGE_BF_MODE_RO,
    BRIDGE_BF_MODE_RW,
    BRIDGE_BF_MODE_SNAPSHOT,
} BridgeBlockMode;

typedef struct {
    FILE *f;
    int64_t nb_sectors;
    BridgeBlockMode mode;
    uint8_t **sector_table;
    uint8_t *raw_data;
    int64_t raw_size;
} BridgeBlockFile;

static int64_t bridge_bf_get_sector_count(BlockDevice *bs)
{
    BridgeBlockFile *bf = bs->opaque;
    return bf->nb_sectors;
}

static int bridge_bf_read_async(BlockDevice *bs, uint64_t sector_num, uint8_t *buf, int n,
                                BlockDeviceCompletionFunc *cb, void *opaque)
{
    BridgeBlockFile *bf = bs->opaque;
    (void)cb; (void)opaque;
    if (bf->raw_data) {
        if (bf->mode == BRIDGE_BF_MODE_SNAPSHOT) {
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
    /* Fallback: file-backed reads (only if pre-slurp failed). */
    if (bf->mode == BRIDGE_BF_MODE_SNAPSHOT) {
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

static int bridge_bf_write_async(BlockDevice *bs, uint64_t sector_num, const uint8_t *buf, int n,
                                 BlockDeviceCompletionFunc *cb, void *opaque)
{
    BridgeBlockFile *bf = bs->opaque;
    (void)cb; (void)opaque;
    switch (bf->mode) {
    case BRIDGE_BF_MODE_RO:
        return -1;
    case BRIDGE_BF_MODE_RW:
        fseek(bf->f, sector_num * 512, SEEK_SET);
        fwrite(buf, 1, n * 512, bf->f);
        return 0;
    case BRIDGE_BF_MODE_SNAPSHOT:
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

static BlockDevice *bridge_block_device_init(const char *filename, BridgeBlockMode mode)
{
    FILE *f = fopen(filename, mode == BRIDGE_BF_MODE_RW ? "r+b" : "rb");
    if (!f) {
        fprintf(stderr, "tinyemu-bridge: open failed: %s\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    int64_t file_size = ftello(f);

    BlockDevice *bs = mallocz(sizeof(*bs));
    BridgeBlockFile *bf = mallocz(sizeof(*bf));
    bf->mode = mode;
    bf->nb_sectors = file_size / 512;
    bf->f = f;

    if (mode == BRIDGE_BF_MODE_SNAPSHOT) {
        bf->sector_table = mallocz(sizeof(bf->sector_table[0]) * bf->nb_sectors);
    }

    /* Pre-slurp the entire image into a native buffer so per-sector reads
     * are pure memcpy, not MEMFS-hop'd. The whole rootfs lives in this
     * iframe's heap, not yetty's — no duplication. */
    if (file_size > 0) {
        bf->raw_data = malloc((size_t)file_size);
        if (bf->raw_data) {
            fseek(f, 0, SEEK_SET);
            size_t got = fread(bf->raw_data, 1, (size_t)file_size, f);
            if (got != (size_t)file_size) {
                fprintf(stderr, "tinyemu-bridge: short read on %s\n", filename);
                free(bf->raw_data);
                bf->raw_data = NULL;
            } else {
                bf->raw_size = file_size;
            }
        }
    }

    bs->opaque = bf;
    bs->get_sector_count = bridge_bf_get_sector_count;
    bs->read_async = bridge_bf_read_async;
    bs->write_async = bridge_bf_write_async;
    return bs;
}

#ifdef CONFIG_SLIRP
static Slirp *g_slirp = NULL;

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
    (void)pdelay;
    slirp_select_fill(slirp, pfd_max, rfds, wfds, efds);
}

static void slirp_select_poll1(EthernetDevice *net, fd_set *rfds, fd_set *wfds, fd_set *efds,
                               int select_ret)
{
    Slirp *slirp = net->opaque;
    slirp_select_poll(slirp, rfds, wfds, efds, (select_ret <= 0));
}

static EthernetDevice *bridge_slirp_open(const VMEthEntry *e)
{
    struct in_addr net_addr = {.s_addr = htonl(0x0a000200)};
    struct in_addr mask = {.s_addr = htonl(0xffffff00)};
    struct in_addr host = {.s_addr = htonl(0x0a000202)};
    struct in_addr dhcp = {.s_addr = htonl(0x0a00020f)};
    struct in_addr dns = {.s_addr = htonl(0x0a000203)};

    if (g_slirp) {
        return NULL;
    }

    EthernetDevice *net = mallocz(sizeof(*net));
    g_slirp = slirp_init(0, net_addr, mask, host, NULL, "", NULL, dhcp, dns, net);

    if (e) {
        struct in_addr any = {.s_addr = htonl(INADDR_ANY)};
        struct in_addr guest_any = {.s_addr = 0};
        for (int j = 0; j < e->hostfwd_count; j++) {
            const VMEthHostFwd *hf = &e->hostfwd[j];
            slirp_add_hostfwd(g_slirp, hf->is_udp ? 1 : 0, any, hf->host_port,
                              guest_any, hf->guest_port);
        }
    }

    net->mac_addr[0] = 0x02;
    net->mac_addr[1] = 0x00;
    net->mac_addr[2] = 0x00;
    net->mac_addr[3] = 0x00;
    net->mac_addr[4] = 0x00;
    net->mac_addr[5] = 0x01;
    /* Prime slirp's ARP cache with the MAC virtio-net will advertise
     * to the guest via VIRTIO_NET_F_MAC. Without this, the chr-backend
     * SYN injected on session-open would hit if_encap with an empty
     * cache, broadcast an ARP request, and drop the SYN — recovery
     * waits on TCPT_REXMT *and* a guest-side ARP reply, which never
     * comes back reliably because the alpine /init uses static IP and
     * never originates outbound traffic. With the cache primed up
     * front, the very first SYN reaches the guest. */
    slirp_set_client_mac(g_slirp, net->mac_addr);
    net->opaque = g_slirp;
    net->write_packet = slirp_write_packet;
    net->select_fill = slirp_select_fill1;
    net->select_poll = slirp_select_poll1;
    return net;
}
#endif

/* ---- Session table — JS-multiplexed slirp injections ---------------
 *
 * Each yetty terminal opens an independent connection to the in-VM
 * telnetd via tinyemu_session_open(port). We use slirp's chr-backend
 * hook (slirp/chr-backend.{h,c}) to manufacture a synthetic inbound
 * TCP connection — no OS socket involved. Multiple sessions share
 * one slirp instance and one VM telnetd; each gets its own forked
 * shell with its own tty geometry (telnet NAWS handles resize per
 * session).
 *
 * sids are small ints (slot index in g_sessions). They're returned
 * to JS by tinyemu_session_open and used as the lookup key for
 * tinyemu_session_send / tinyemu_session_close. The iframe page
 * keeps a parallel map of {parent's clientSid → wasm sid}. */

#ifdef CONFIG_SLIRP
#define BRIDGE_MAX_SESSIONS 32

struct bridge_session {
    int in_use;
    int wasm_sid;                       /* duplicates the array index. */
    struct slirp_chr_backend backend;
    struct socket *so;                  /* slirp's chr socket — invalid after close */
};

static struct bridge_session g_sessions[BRIDGE_MAX_SESSIONS];

/* Slirp delivers guest output here (via slirp_send → backend->ops->send).
 * Forward to the iframe page, which then postMessages to the parent
 * yetty.wasm where the right telnet-pty's on_data fires.
 *
 * THIS IS THE FIRST POINT THAT FIRES WHEN GUEST TELNETD ANSWERS. If
 * we never see this log, the SYN never got SYN-ACK'd by the guest. */
static void session_backend_send(void *ctx, const void *buf, size_t len)
{
    struct bridge_session *s = (struct bridge_session *)ctx;
    EM_ASM({
        console.log('[bridge] session_backend_send sid=' + $0 +
                    ' len=' + $1 + ' (guest → host)');
    }, s ? s->wasm_sid : -1, (int)len);
    if (!s || !s->in_use || len == 0) {
        return;
    }
    EM_ASM({
        if (window.__yettyTinyemuPostSessionRx) {
            window.__yettyTinyemuPostSessionRx($0, $1, $2);
        } else {
            console.error('[bridge] window.__yettyTinyemuPostSessionRx MISSING');
        }
    }, s->wasm_sid, buf, (int)len);
}

static const struct slirp_chr_backend_ops session_backend_ops = {
    .send = session_backend_send,
};

/* Mint a synthetic inbound connection to (slirp-default-guest-IP,
 * port). Returns the sid (>=0) on success, -1 on failure. */
EMSCRIPTEN_KEEPALIVE
int tinyemu_session_open(int port)
{
    EM_ASM({
        console.log('[bridge] tinyemu_session_open port=' + $0 +
                    ' g_slirp=' + ($1 ? 'OK' : 'NULL') +
                    ' g_running=' + $2);
    }, port, !!g_slirp, g_running);
    if (!g_slirp || !g_running || port <= 0 || port > 65535) {
        return -1;
    }

    int sid = -1;
    for (int i = 0; i < BRIDGE_MAX_SESSIONS; i++) {
        if (!g_sessions[i].in_use) {
            sid = i;
            break;
        }
    }
    if (sid < 0) {
        fprintf(stderr, "tinyemu-bridge: session table full (>%d)\n",
                BRIDGE_MAX_SESSIONS);
        return -1;
    }

    struct bridge_session *s = &g_sessions[sid];
    memset(s, 0, sizeof(*s));
    s->in_use = 1;
    s->wasm_sid = sid;
    s->backend.ops = &session_backend_ops;
    s->backend.ctx = s;

    /* Slirp's default guest gets 10.0.2.15 from the built-in DHCP.
     * We hard-code that here — matches the slirp_init() addresses
     * in this file (net=10.0.2.0/24, vhost=10.0.2.2, dhcp=10.0.2.15).
     * If the cfg ever changes the network, this needs to read the
     * actual guest IP back from slirp. */
    struct in_addr guest_addr;
    guest_addr.s_addr = htonl(0x0a00020fu);  /* 10.0.2.15 */

    s->so = slirp_chr_open(g_slirp, guest_addr, port, &s->backend);
    if (!s->so) {
        fprintf(stderr, "tinyemu-bridge: slirp_chr_open(port=%d) failed\n", port);
        s->in_use = 0;
        return -1;
    }
    fprintf(stderr, "tinyemu-bridge: session %d → 10.0.2.15:%d open\n",
            sid, port);
    return sid;
}

/* Push host bytes (yetty's telnet output) into the synthetic
 * connection's send buffer. Caller responsible for chunking. */
EMSCRIPTEN_KEEPALIVE
void tinyemu_session_send(int sid, const uint8_t *data, int len)
{
    EM_ASM({
        console.log('[bridge] tinyemu_session_send sid=' + $0 + ' len=' + $1);
    }, sid, len);
    if (sid < 0 || sid >= BRIDGE_MAX_SESSIONS || len <= 0 || !data) {
        return;
    }
    struct bridge_session *s = &g_sessions[sid];
    if (!s->in_use || !s->so) {
        EM_ASM({
            console.warn('[bridge] tinyemu_session_send: sid=' + $0 +
                         ' not in_use OR so==NULL — DROPPING');
        }, sid);
        return;
    }
    size_t consumed = slirp_chr_input(s->so, data, (size_t)len);
    EM_ASM({
        console.log('[bridge] slirp_chr_input consumed=' + $0 + '/' + $1);
    }, (int)consumed, len);
}

/* Half-close the connection from the host side. The guest sees a
 * FIN; once it ACKs and closes its end, slirp tears the socket
 * down — at which point we reset the slot. The chr backend's
 * pointer is cleared on the slirp side immediately so any further
 * guest bytes drop on the floor instead of dispatching to a freed
 * session. */
EMSCRIPTEN_KEEPALIVE
void tinyemu_session_close(int sid)
{
    if (sid < 0 || sid >= BRIDGE_MAX_SESSIONS) {
        return;
    }
    struct bridge_session *s = &g_sessions[sid];
    if (!s->in_use) {
        return;
    }
    fprintf(stderr, "tinyemu-bridge: session %d close\n", sid);
    if (s->so) {
        slirp_chr_close(s->so);
        s->so = NULL;
    }
    s->in_use = 0;
}
#endif /* CONFIG_SLIRP */

/* ---- VM main loop tick ---- */

/* Per-call exec budget AND outer time budget. The iframe is hidden
 * (display:none in the parent) so we deliberately AVOID rAF — Chrome
 * and Firefox both throttle or entirely pause requestAnimationFrame for
 * off-screen documents. We use a setTimeout-based main loop, but a
 * single 50k-cycle interp per tick is too few cycles per second to
 * boot a Linux kernel in any reasonable time. Mimic the old in-process
 * pthread loop by burning up to BRIDGE_TICK_BUDGET_MS of wall clock per
 * tick on a tight inner interp loop. ~30M cycles/sec target. */
#define BRIDGE_MAX_EXEC_CYCLES 50000
#define BRIDGE_MAX_SLEEP_MS    2
#define BRIDGE_TICK_BUDGET_MS  8.0

static void bridge_main_loop_tick(void)
{
    if (!g_running || !g_vm) {
        return;
    }

    /* Apply any pending resize once console_dev is available. */
    if (g_pending_cols > 0 && g_pending_rows > 0 && g_vm->console_dev) {
        virtio_console_resize_event(g_vm->console_dev, g_pending_cols, g_pending_rows);
        g_pending_cols = 0;
        g_pending_rows = 0;
    }

    /* Mirror the in-process tinyemu-pty.c::vm_run_once flow. The
     * select() — even with a zero timeout — is load-bearing: it gives
     * the VM's slirp net layer a chance to poll its sockets, AND
     * crucially, virt_machine_get_sleep_duration is what advances
     * tinyemu's virtual clock target so timer interrupts fire. Without
     * this call the kernel hangs in its idle loop forever waiting for
     * a tick that never arrives. */
    fd_set rfds, wfds, efds;
    int fd_max = -1;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    int delay = virt_machine_get_sleep_duration(g_vm, BRIDGE_MAX_SLEEP_MS);
    (void)delay; /* setTimeout drives our outer cadence; we don't sleep here */

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    /* Add console fd for keyboard input (input_ring is preferred but
     * the select machinery still expects a populated set). */
    if (g_vm->console_dev && virtio_console_can_write_data(g_vm->console_dev)) {
        /* No real fd — input arrives via input_ring_push(). Nothing
         * to add here, but the call to virtio_console_can_write_data
         * matters: it lets the device update its writable status. */
    }

#ifdef CONFIG_SLIRP
    if (g_vm->net) {
        g_vm->net->select_fill(g_vm->net, &fd_max, &rfds, &wfds, &efds, &delay);
    }
#endif

    int sret = (fd_max >= 0)
        ? select(fd_max + 1, &rfds, &wfds, &efds, &tv)
        : 0;

#ifdef CONFIG_SLIRP
    if (g_vm->net) {
        g_vm->net->select_poll(g_vm->net, &rfds, &wfds, &efds, sret);
    }
#endif

    /* Feed buffered keyboard input. */
    if (g_vm->console_dev && g_input.size > 0 &&
        virtio_console_can_write_data(g_vm->console_dev)) {
        uint8_t buf[256];
        int max = virtio_console_get_write_len(g_vm->console_dev);
        if (max > (int)sizeof(buf)) {
            max = sizeof(buf);
        }
        size_t got = input_ring_pop(buf, (size_t)max);
        if (got > 0) {
            virtio_console_write_data(g_vm->console_dev, buf, (int)got);
        }
    }

    /* Burn through interp until we've used our wall-clock budget for
     * this tick. Each call is bounded by BRIDGE_MAX_EXEC_CYCLES so we
     * can revisit the input pump and stay responsive to keystrokes. */
    double tick_start = emscripten_get_now();
    do {
        virt_machine_interp(g_vm, BRIDGE_MAX_EXEC_CYCLES);
    } while (emscripten_get_now() - tick_start < BRIDGE_TICK_BUDGET_MS);
}

/* ---- VM initialization ---- */

static int bridge_init_vm(const char *cfg_path)
{
    VirtMachineParams p_s, *p = &p_s;

    virt_machine_set_defaults(p);
    fprintf(stderr, "tinyemu-bridge: loading cfg %s\n", cfg_path);
    virt_machine_load_config_file(p, cfg_path, NULL, NULL);
    fprintf(stderr, "tinyemu-bridge: cfg loaded — drives=%d fs=%d eth=%d\n",
            p->drive_count, p->fs_count, p->eth_count);

    for (int i = 0; i < p->drive_count; i++) {
        char *fname = get_file_path(p->cfg_filename, p->tab_drive[i].filename);
        BlockDevice *drive = bridge_block_device_init(fname, BRIDGE_BF_MODE_SNAPSHOT);
        if (!drive) {
            fprintf(stderr, "tinyemu-bridge: drive%d open failed: %s\n", i, fname);
            free(fname);
            virt_machine_free_config(p);
            return -1;
        }
        free(fname);
        p->tab_drive[i].block_dev = drive;
    }

    for (int i = 0; i < p->fs_count; i++) {
        char *fname = get_file_path(p->cfg_filename, p->tab_fs[i].filename);
        FSDevice *fs = fs_disk_init(fname);
        if (!fs) {
            fprintf(stderr, "tinyemu-bridge: fs%d init failed: %s\n", i, fname);
            free(fname);
            virt_machine_free_config(p);
            return -1;
        }
        free(fname);
        p->tab_fs[i].fs_dev = fs;
    }

#ifdef CONFIG_SLIRP
    for (int i = 0; i < p->eth_count; i++) {
        if (!strcmp(p->tab_eth[i].driver, "user")) {
            p->tab_eth[i].net = bridge_slirp_open(&p->tab_eth[i]);
        }
    }
#endif

    CharacterDevice *console = mallocz(sizeof(*console));
    console->write_data = bridge_console_write;
    console->read_data = bridge_console_read;
    p->console = console;
    p->rtc_real_time = TRUE;

    g_vm = virt_machine_init(p);
    virt_machine_free_config(p);

    if (!g_vm) {
        fprintf(stderr, "tinyemu-bridge: virt_machine_init returned NULL\n");
        return -1;
    }

    if (g_vm->net) {
        g_vm->net->device_set_carrier(g_vm->net, TRUE);
    }
    return 0;
}

/* ---- JS-callable entry points ---- */

EMSCRIPTEN_KEEPALIVE
void tinyemu_bridge_input(const uint8_t *data, int len)
{
    if (len <= 0 || !g_running) {
        return;
    }
    input_ring_push(data, (size_t)len);
}

EMSCRIPTEN_KEEPALIVE
void tinyemu_bridge_resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0) {
        return;
    }
    if (g_vm && g_vm->console_dev) {
        virtio_console_resize_event(g_vm->console_dev, cols, rows);
    } else {
        g_pending_cols = cols;
        g_pending_rows = rows;
    }
}

/* ---- Entry point ---- */

/* Hardcoded cfg path inside the iframe wasm's MEMFS. The iframe's
 * pre-js fetches this and the kernel/opensbi/rootfs from the server,
 * decodes them with _yetty_brotli_decode, and FS.writeFile()s them
 * into /yetty-vm/ before calling tinyemu_bridge_start. */
#define BRIDGE_CFG_PATH "/yetty-vm/yetty-temu-extended.cfg"

/* Called from the iframe's pre-js after every VM asset is in MEMFS.
 * Runs init_vm + installs the rAF main loop. Returns 0 on success.
 *
 * Why not init from main()? When main() runs, the iframe pre-js has
 * NOT yet fetched/decoded the rootfs (those calls live inside its
 * onRuntimeInitialized handler, which fires after main returns). So
 * VM init has to wait for an explicit "assets are ready" signal — JS
 * gives that signal by calling this exported function. */
EMSCRIPTEN_KEEPALIVE
int tinyemu_bridge_start(void)
{
    if (g_running) {
        fprintf(stderr, "tinyemu-bridge: start already called — ignoring\n");
        return 0;
    }

    fprintf(stderr, "tinyemu-bridge: starting VM\n");
    if (bridge_init_vm(BRIDGE_CFG_PATH) < 0) {
        fprintf(stderr, "tinyemu-bridge: VM init failed\n");
        return 1;
    }

    g_running = 1;
    /* fps=100 → emscripten installs a setTimeout-based loop (10 ms
     * period) instead of requestAnimationFrame. Critical for the VM
     * iframe: it's display:none in the parent, and rAF on hidden
     * documents is throttled to ~1 Hz or paused outright by Chrome /
     * Firefox, which kept the kernel from ever finishing boot.
     * setTimeout keeps firing regardless of visibility.
     * sim_loop=0 → return immediately; JS event loop drives ticks. */
    emscripten_set_main_loop(bridge_main_loop_tick, 100, 0);
    fprintf(stderr, "tinyemu-bridge: main loop installed (100 Hz setTimeout)\n");
    return 0;
}

/* main() does nothing meaningful — emscripten requires _main as the
 * default entry point but the actual VM init is deferred. */
int main(void)
{
    fprintf(stderr, "tinyemu-bridge: runtime up, awaiting JS preload\n");
    return 0;
}
