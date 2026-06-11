/*
 * ytop — top-style live monitor: per-core CPU progress bars (header) +
 * a sortable-by-CPU process TABLE (new ygui WIDGET_TABLE).
 *
 * Linux only — reads /proc. Built on top of the ygui engine so input,
 * rendering, and the libuv refresh timer all share one event loop.
 *
 *   Run inside a yetty terminal:
 *       yetty -e ./ytop
 *
 *   Press 'q' to quit.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <uv.h>

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* ------------------------------------------------------------------ */
/* CPU state                                                           */
/* ------------------------------------------------------------------ */

#define MAX_CORES 64
#define MAX_PROCS 2048
#define REFRESH_MS 1000

struct cpu_sample {
    uint64_t user, nice, sys, idle, iowait, irq, softirq, steal;
};

struct cpu_state {
    int n_cores;
    struct cpu_sample prev[MAX_CORES + 1]; /* [0]=aggregate, [1..N]=per-core */
    struct cpu_sample curr[MAX_CORES + 1];
    float pct[MAX_CORES + 1];
};

static int read_proc_stat(struct cpu_state *st)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) {
        return -1;
    }
    char line[512];
    int idx = 0;
    while (idx <= MAX_CORES && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) {
            break;
        }
        if (line[3] != ' ' && (line[3] < '0' || line[3] > '9')) {
            break;
        }
        struct cpu_sample s = {0};
        int n = sscanf(line, "%*s %lu %lu %lu %lu %lu %lu %lu %lu", &s.user, &s.nice, &s.sys,
                       &s.idle, &s.iowait, &s.irq, &s.softirq, &s.steal);
        if (n < 4) {
            break;
        }
        st->curr[idx++] = s;
    }
    fclose(f);
    if (idx == 0) {
        return -1;
    }
    st->n_cores = idx - 1;
    return 0;
}

static float compute_pct(const struct cpu_sample *prev, const struct cpu_sample *curr)
{
    uint64_t prev_idle = prev->idle + prev->iowait;
    uint64_t curr_idle = curr->idle + curr->iowait;
    uint64_t prev_total =
        prev->user + prev->nice + prev->sys + prev_idle + prev->irq + prev->softirq + prev->steal;
    uint64_t curr_total =
        curr->user + curr->nice + curr->sys + curr_idle + curr->irq + curr->softirq + curr->steal;
    if (curr_total <= prev_total) {
        return 0.0f;
    }
    uint64_t total_delta = curr_total - prev_total;
    uint64_t idle_delta = (curr_idle > prev_idle) ? (curr_idle - prev_idle) : 0;
    return 100.0f * (1.0f - (float)idle_delta / (float)total_delta);
}

/* ------------------------------------------------------------------ */
/* Process state                                                       */
/* ------------------------------------------------------------------ */

struct proc_entry {
    pid_t pid;
    char state;
    uint64_t cpu_jiffies;      /* utime + stime — accumulated */
    uint64_t cpu_jiffies_prev; /* previous tick's value, for delta */
    uint64_t rss_kb;
    uid_t uid;
    char user[32];
    char comm[64];
};

static int parse_pid_stat(pid_t pid, struct proc_entry *e)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) {
        return -1;
    }
    buf[n] = '\0';

    /* `comm` is enclosed in parens and may contain spaces / parens. The
     * canonical heuristic is to take everything between the first '(' and
     * the LAST ')'. */
    char *lp = strchr(buf, '(');
    char *rp = strrchr(buf, ')');
    if (!lp || !rp || rp <= lp) {
        return -1;
    }
    e->pid = pid;
    size_t comm_len = (size_t)(rp - lp - 1);
    if (comm_len >= sizeof(e->comm)) {
        comm_len = sizeof(e->comm) - 1;
    }
    memcpy(e->comm, lp + 1, comm_len);
    e->comm[comm_len] = '\0';

    /* After "(comm) " come the rest of the fields, space-separated. The
     * stat(5) field index uses 1-based positions; we map to 0-based here:
     *   [0] state           — char
     *   [11] utime          — clock ticks
     *   [12] stime          — clock ticks
     */
    char *p = rp + 2; /* skip ") " */
    e->state = *p;
    /* Walk to field 11 (utime) — that's 11 spaces past state. */
    for (int i = 0; i < 11; i++) {
        p = strchr(p, ' ');
        if (!p) {
            return -1;
        }
        p++;
    }
    char *endp = NULL;
    unsigned long u = strtoul(p, &endp, 10);
    unsigned long s = endp ? strtoul(endp, &endp, 10) : 0;
    e->cpu_jiffies = (uint64_t)u + (uint64_t)s;
    return 0;
}

static void parse_pid_status(pid_t pid, struct proc_entry *e)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            e->rss_kb = strtoul(line + 6, NULL, 10);
        } else if (strncmp(line, "Uid:", 4) == 0) {
            unsigned long uid = strtoul(line + 4, NULL, 10);
            e->uid = (uid_t)uid;
        }
    }
    fclose(f);
    struct passwd *pw = getpwuid(e->uid);
    if (pw && pw->pw_name) {
        strncpy(e->user, pw->pw_name, sizeof(e->user) - 1);
    } else {
        snprintf(e->user, sizeof(e->user), "%d", (int)e->uid);
    }
}

/* Read all PID dirs and populate the list. Returns the number of entries
 * filled in `out`. Carries `cpu_jiffies_prev` from the matching pid in
 * `prev_table` (if any) so we can compute %CPU on the next refresh. */
static int read_processes(struct proc_entry *out, int max, const struct proc_entry *prev_table,
                          int prev_count)
{
    DIR *d = opendir("/proc");
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') {
            continue;
        }
        char *ep = NULL;
        long pid = strtol(de->d_name, &ep, 10);
        if (!ep || *ep != '\0' || pid <= 0) {
            continue;
        }
        struct proc_entry e = {0};
        if (parse_pid_stat((pid_t)pid, &e) < 0) {
            continue;
        }
        parse_pid_status((pid_t)pid, &e);
        /* Carry previous jiffy total for delta. */
        for (int i = 0; i < prev_count; i++) {
            if (prev_table[i].pid == e.pid) {
                e.cpu_jiffies_prev = prev_table[i].cpu_jiffies;
                break;
            }
        }
        out[n++] = e;
    }
    closedir(d);
    return n;
}

static int proc_cmp_cpu_desc(const void *a, const void *b)
{
    const struct proc_entry *pa = a, *pb = b;
    uint64_t da = pa->cpu_jiffies - pa->cpu_jiffies_prev;
    uint64_t db = pb->cpu_jiffies - pb->cpu_jiffies_prev;
    if (db != da) {
        return (db > da) ? 1 : -1;
    }
    /* Fall back to RSS so the busiest process sits on top even when the
     * CPU delta is zero across the board. */
    if (pb->rss_kb != pa->rss_kb) {
        return (pb->rss_kb > pa->rss_kb) ? 1 : -1;
    }
    return (int)pa->pid - (int)pb->pid;
}

/* ------------------------------------------------------------------ */
/* Application state                                                   */
/* ------------------------------------------------------------------ */

#define MAX_TABLE_ROWS 32

struct app_state {
    struct yetty_ygui_framework *engine;
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *header;
    struct yetty_yclass_object *cpu_bars[MAX_CORES + 1];
    struct yetty_yclass_object *cpu_labels[MAX_CORES + 1];
    struct yetty_yclass_object *table;
    int running;

    struct cpu_state cpu_st;
    struct proc_entry procs[MAX_PROCS];
    int n_procs;
    /* Snapshot kept for the next tick's %CPU delta. */
    struct proc_entry prev_procs[MAX_PROCS];
    int n_prev_procs;

    long clock_ticks;
};

/* ------------------------------------------------------------------ */
/* UI building                                                         */
/* ------------------------------------------------------------------ */

#define HEADER_H 28.0f
#define ROW_H 22.0f
#define BAR_W 140.0f
#define LABEL_W 110.0f
#define MARGIN 12.0f
#define TABLE_TOP_PAD 8.0f

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static uint32_t core_accent(int core)
{
    static const uint8_t pal[8][3] = {
        {255, 110, 102}, {110, 220, 130}, {102, 170, 255}, {255, 222, 100},
        {200, 130, 255}, {110, 230, 220}, {255, 165, 90},  {245, 130, 200},
    };
    const uint8_t *c = pal[core % 8];
    return rgba(c[0], c[1], c[2], 255);
}

/* Add `cls` under `parent`, position + size it absolutely, return it
 * (or NULL). Absolute placement mirrors ytop's hand-laid-out UI. */
static struct yetty_yclass_object *ytop_place(struct app_state *s,
                                            struct yetty_yclass_ptr_result cls_r, float x, float y,
                                            float w, float h)
{
    if (YETTY_IS_ERR(cls_r)) {
        yetty_ycore_error_destroy(cls_r.error);
        return NULL;
    }
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(cls_r.value, s->root);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(r.value, x, y));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(r.value, w, h));
    return r.value;
}

static void build_ui(struct app_state *s, int n_cores)
{
    /* Header label across the top. */
    s->header = ytop_place(s, yetty_ygui_label_class_get(), MARGIN, MARGIN, 400.0f, 20.0f);
    if (s->header) {
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(s->header, "ytop — q to quit"));
    }

    /* Per-core rows: "cpuN" label + progress bar + "%" label, wrapped
     * four cores per row to fit horizontally. */
    int cores_per_row = 4;
    if (n_cores < cores_per_row) {
        cores_per_row = n_cores > 0 ? n_cores : 1;
    }
    float core_block_w = LABEL_W + 8.0f + BAR_W + 8.0f + 60.0f;
    for (int i = 1; i <= n_cores; i++) {
        int idx = i - 1;
        int row = idx / cores_per_row;
        int col = idx % cores_per_row;
        float x = MARGIN + (float)col * core_block_w;
        float y = MARGIN + HEADER_H + (float)row * (ROW_H + 4.0f);

        char lbl_text[16];
        snprintf(lbl_text, sizeof(lbl_text), "cpu%d", i - 1);

        struct yetty_yclass_object *core_lbl =
            ytop_place(s, yetty_ygui_label_class_get(), x, y + 4.0f, 48.0f, 18.0f);
        if (core_lbl) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(core_lbl, lbl_text));
        }
        s->cpu_bars[i] = ytop_place(s, yetty_ygui_progress_class_get(), x + 50.0f, y, BAR_W, ROW_H);
        if (s->cpu_bars[i]) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_progress_set_accent(s->cpu_bars[i], core_accent(i - 1)));
            yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(s->cpu_bars[i], 0.0f));
        }
        s->cpu_labels[i] = ytop_place(s, yetty_ygui_label_class_get(), x + 50.0f + BAR_W + 8.0f,
                                      y + 4.0f, 60.0f, 18.0f);
        if (s->cpu_labels[i]) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(s->cpu_labels[i], "  0%"));
        }
    }

    /* Table below the per-core block. */
    int n_core_rows = (n_cores + cores_per_row - 1) / cores_per_row;
    float table_y = MARGIN + HEADER_H + (float)n_core_rows * (ROW_H + 4.0f) + TABLE_TOP_PAD;
    float table_w = (float)cores_per_row * core_block_w;
    if (table_w < 720.0f) {
        table_w = 720.0f;
    }
    float table_h = ROW_H + (MAX_TABLE_ROWS + 1) * ROW_H;

    s->table = ytop_place(s, yetty_ygui_table_class_get(), MARGIN, table_y, table_w, table_h);
    if (s->table) {
        static const char *const names[] = {"PID", "USER", "STATE", "%CPU", "RSS", "COMMAND"};
        yetty_ycore_error_destroy_safe(
            yetty_ygui_table_set_columns(s->table, (int)(sizeof(names) / sizeof(names[0])), names));
    }
}

/* ------------------------------------------------------------------ */
/* Refresh tick                                                        */
/* ------------------------------------------------------------------ */

static void format_rss(uint64_t kb, char *out, size_t out_len)
{
    if (kb >= 1024UL * 1024) {
        snprintf(out, out_len, "%.1f GB", (double)kb / (1024.0 * 1024.0));
    } else if (kb >= 1024) {
        snprintf(out, out_len, "%.1f MB", (double)kb / 1024.0);
    } else {
        snprintf(out, out_len, "%lu KB", (unsigned long)kb);
    }
}

static void refresh(struct app_state *s)
{
    /* CPU. */
    if (read_proc_stat(&s->cpu_st) == 0) {
        for (int i = 0; i <= s->cpu_st.n_cores; i++) {
            float p = compute_pct(&s->cpu_st.prev[i], &s->cpu_st.curr[i]);
            s->cpu_st.pct[i] = p;
            if (i >= 1 && i <= MAX_CORES) {
                if (s->cpu_bars[i]) {
                    yetty_ycore_error_destroy_safe(
                        yetty_ygui_progress_set_value(s->cpu_bars[i], p / 100.0f));
                }
                if (s->cpu_labels[i]) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%5.1f%%", p);
                    yetty_ycore_error_destroy_safe(
                        yetty_ygui_label_set_text(s->cpu_labels[i], buf));
                }
            }
        }
        memcpy(s->cpu_st.prev, s->cpu_st.curr, sizeof(s->cpu_st.curr));
    }

    /* Processes. */
    s->n_procs = read_processes(s->procs, MAX_PROCS, s->prev_procs, s->n_prev_procs);
    /* Compute %CPU using delta jiffies / clock_ticks_per_sec / refresh_seconds. */
    float interval_s = (float)REFRESH_MS / 1000.0f;
    float ticks_per_s = (float)s->clock_ticks;
    qsort(s->procs, (size_t)s->n_procs, sizeof(s->procs[0]), proc_cmp_cpu_desc);

    yetty_ycore_error_destroy_safe(yetty_ygui_table_clear_rows(s->table));
    int rows_to_show = s->n_procs < MAX_TABLE_ROWS ? s->n_procs : MAX_TABLE_ROWS;
    for (int i = 0; i < rows_to_show; i++) {
        const struct proc_entry *e = &s->procs[i];
        char pid_s[16], state_s[4], cpu_s[16], rss_s[24];
        snprintf(pid_s, sizeof(pid_s), "%d", (int)e->pid);
        state_s[0] = e->state;
        state_s[1] = '\0';
        uint64_t delta = e->cpu_jiffies - e->cpu_jiffies_prev;
        float pct = (ticks_per_s > 0.0f && interval_s > 0.0f)
                        ? 100.0f * (float)delta / (ticks_per_s * interval_s)
                        : 0.0f;
        snprintf(cpu_s, sizeof(cpu_s), "%5.1f", pct);
        format_rss(e->rss_kb, rss_s, sizeof(rss_s));
        const char *cells[] = {
            pid_s, e->user[0] ? e->user : "?", state_s, cpu_s, rss_s, e->comm,
        };
        yetty_ycore_error_destroy_safe(yetty_ygui_table_add_row(s->table, cells, 6));
    }

    /* Snapshot for the next delta. */
    memcpy(s->prev_procs, s->procs, sizeof(struct proc_entry) * (size_t)s->n_procs);
    s->n_prev_procs = s->n_procs;

    /* No engine_mark_dirty — that forces a full redraw. The setters
     * above already dirty the widgets they touch, which is what
     * incremental mode needs. */
}

/* ------------------------------------------------------------------ */
/* Glue                                                                */
/* ------------------------------------------------------------------ */

static void on_refresh_timer(uv_timer_t *t)
{
    struct app_state *s = (struct app_state *)t->data;
    refresh(s);
}

/* ------------------------------------------------------------------ */
/* Client-mode harness — ytop runs inside a hosting yetty (`yetty -e     */
/* ./ytop`): ygui ships its OSC envelopes over stdout, real keystrokes   */
/* arrive on stdin, SIGWINCH carries the pixel size. Mirrors ygreeter's  */
/* client mode + a 1 s refresh timer.                                    */
/* ------------------------------------------------------------------ */

struct ytop_stdout_pty {
    struct yetty_platform_pty base;
    uv_pipe_t pipe;
};

struct ytop_write {
    uv_write_t req;
    char *data;
};

static void ytop_on_write_done(uv_write_t *req, int status)
{
    struct ytop_write *w = (struct ytop_write *)req;
    (void)status;
    free(w->data);
    free(w);
}

static struct yetty_ycore_size_result ytop_pty_write(struct yetty_platform_pty *self,
                                                     const char *data, size_t len)
{
    struct ytop_stdout_pty *p = (struct ytop_stdout_pty *)self;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct ytop_write *w = calloc(1, sizeof(*w));
    if (!w) {
        return YETTY_ERR(yetty_ycore_size, "ytop_pty_write: calloc");
    }
    w->data = malloc(len);
    if (!w->data) {
        free(w);
        return YETTY_ERR(yetty_ycore_size, "ytop_pty_write: malloc");
    }
    memcpy(w->data, data, len);
    uv_buf_t buf = uv_buf_init(w->data, (unsigned)len);
    if (uv_write(&w->req, (uv_stream_t *)&p->pipe, &buf, 1, ytop_on_write_done) != 0) {
        free(w->data);
        free(w);
        return YETTY_ERR(yetty_ycore_size, "ytop_pty_write: uv_write");
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result ytop_pty_read(struct yetty_platform_pty *s, char *b, size_t n)
{
    (void)s;
    (void)b;
    (void)n;
    return YETTY_OK(yetty_ycore_size, 0);
}
static struct yetty_ycore_void_result ytop_pty_noop(struct yetty_platform_pty *s)
{
    (void)s;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result ytop_pty_resize(struct yetty_platform_pty *s, uint32_t c,
                                                      uint32_t r, uint32_t pw, uint32_t ph)
{
    (void)s;
    (void)c;
    (void)r;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}
static struct yetty_platform_pty_pipe_source *ytop_pty_pipe_source(struct yetty_platform_pty *s)
{
    (void)s;
    return NULL;
}
static const struct yetty_platform_pty_ops *ytop_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = ytop_pty_noop,
        .read = ytop_pty_read,
        .write = ytop_pty_write,
        .resize = ytop_pty_resize,
        .stop = ytop_pty_noop,
        .pipe_source = ytop_pty_pipe_source,
    };
    return &ops;
}

struct ytop_client {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t refresh_timer;
    struct ytop_stdout_pty out;
    struct app_state *s;
};

static int on_key(struct yetty_ygui_framework *engine, uint32_t key, int mods, void *user)
{
    (void)engine;
    (void)mods;
    struct app_state *s = user;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        s->running = 0;
        return 1;
    }
    return 0;
}

static void ytop_stdin_cb(uv_poll_t *h, int status, int events)
{
    struct ytop_client *c = h->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[1024];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        yetty_ycore_error_destroy_safe(
            yetty_ygui_framework_feed_input(c->s->engine, buf, (size_t)n));
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        c->s->running = 0;
    }
}

static void ytop_pickup_winsz(struct ytop_client *c)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        yetty_ycore_error_destroy_safe(yetty_ygui_framework_set_viewport(
            c->s->engine, (float)ws.ws_xpixel, (float)ws.ws_ypixel));
    }
}

static void ytop_sigwinch_cb(uv_signal_t *h, int signum)
{
    (void)signum;
    ytop_pickup_winsz((struct ytop_client *)h->data);
}

static void ytop_prep_cb(uv_prepare_t *h)
{
    struct ytop_client *c = h->data;
    if (yetty_ygui_framework_is_dirty(c->s->engine)) {
        yetty_ycore_error_destroy_safe(yetty_ygui_framework_emit(c->s->engine));
    }
    if (!c->s->running) {
        uv_stop(h->loop);
    }
}

static void ytop_close_cb(uv_handle_t *h)
{
    (void)h;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ytrace_init();

    struct app_state *s = calloc(1, sizeof(*s));
    if (!s) {
        return 1;
    }
    s->clock_ticks = sysconf(_SC_CLK_TCK);
    if (s->clock_ticks <= 0) {
        s->clock_ticks = 100;
    }
    s->running = 1;

    /* Seed the CPU sampler so the first delta is real activity. */
    if (read_proc_stat(&s->cpu_st) < 0) {
        fprintf(stderr, "ytop: cannot read /proc/stat (Linux only)\n");
        free(s);
        return 1;
    }
    memcpy(s->cpu_st.prev, s->cpu_st.curr, sizeof(s->cpu_st.curr));

    /* Raw tty BEFORE any OSC write — otherwise the slave tty echoes the
     * ESC bytes back and the host yetty renders "^[" garbage. */
    struct termios saved;
    int tty_raw_ok = 0;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw = saved;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            tty_raw_ok = 1;
        }
    }

    struct ytop_client c = {0};
    c.s = s;
    if (uv_loop_init(&c.loop) != 0) {
        fprintf(stderr, "ytop: uv_loop_init failed\n");
        goto cleanup;
    }
    c.out.base.ops = ytop_pty_ops();
    if (uv_pipe_init(&c.loop, &c.out.pipe, 0) != 0 ||
        uv_pipe_open(&c.out.pipe, STDOUT_FILENO) != 0) {
        fprintf(stderr, "ytop: stdout pipe init failed\n");
        goto cleanup_loop;
    }

    struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(&c.out.base);
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "ytop: framework_create failed: %s\n", fr.error.msg);
        yetty_ycore_error_destroy(fr.error);
        goto cleanup_loop;
    }
    s->engine = fr.value;
    yetty_ygui_framework_set_key_cb(s->engine, on_key, s);

    /* Root + widget tree (absolutely positioned, mirroring ytop's
     * hand-laid-out chrome). */
    struct yetty_ygui_object_ptr_result rr =
        yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
    if (YETTY_IS_OK(rr)) {
        s->root = rr.value;
        yetty_ycore_error_destroy_safe(yetty_ygui_framework_set_root(s->engine, s->root));
        build_ui(s, s->cpu_st.n_cores);
    } else {
        yetty_ycore_error_destroy(rr.error);
    }

    if (uv_poll_init(&c.loop, &c.stdin_poll, STDIN_FILENO) == 0) {
        c.stdin_poll.data = &c;
        uv_poll_start(&c.stdin_poll, UV_READABLE, ytop_stdin_cb);
    }
    if (uv_signal_init(&c.loop, &c.sigwinch) == 0) {
        c.sigwinch.data = &c;
        uv_signal_start(&c.sigwinch, ytop_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&c.loop, &c.prep) == 0) {
        c.prep.data = &c;
        uv_prepare_start(&c.prep, ytop_prep_cb);
    }
    uv_timer_init(&c.loop, &c.refresh_timer);
    c.refresh_timer.data = s;
    uv_timer_start(&c.refresh_timer, on_refresh_timer, /*timeout_ms=*/0, /*repeat_ms=*/REFRESH_MS);

    ytop_pickup_winsz(&c);

    uv_run(&c.loop, UV_RUN_DEFAULT);

    uv_timer_stop(&c.refresh_timer);
    uv_poll_stop(&c.stdin_poll);
    uv_signal_stop(&c.sigwinch);
    uv_prepare_stop(&c.prep);
    uv_close((uv_handle_t *)&c.refresh_timer, ytop_close_cb);
    uv_close((uv_handle_t *)&c.stdin_poll, ytop_close_cb);
    uv_close((uv_handle_t *)&c.sigwinch, ytop_close_cb);
    uv_close((uv_handle_t *)&c.prep, ytop_close_cb);
    uv_close((uv_handle_t *)&c.out.pipe, ytop_close_cb);
    uv_run(&c.loop, UV_RUN_NOWAIT);

    yetty_ycore_error_destroy_safe(yetty_ygui_framework_destroy(s->engine));

cleanup_loop:
    uv_loop_close(&c.loop);
cleanup:
    if (tty_raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    free(s);
    return 0;
}
