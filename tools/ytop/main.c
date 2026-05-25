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

#include <yetty/ygui-old/ygui.h>

#include <dirent.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <uv.h>

/* ------------------------------------------------------------------ */
/* CPU state                                                           */
/* ------------------------------------------------------------------ */

#define MAX_CORES   64
#define MAX_PROCS   2048
#define REFRESH_MS  1000

struct cpu_sample {
    uint64_t user, nice, sys, idle, iowait, irq, softirq, steal;
};

struct cpu_state {
    int n_cores;
    struct cpu_sample prev[MAX_CORES + 1];   /* [0]=aggregate, [1..N]=per-core */
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
        int n = sscanf(line, "%*s %lu %lu %lu %lu %lu %lu %lu %lu",
                       &s.user, &s.nice, &s.sys, &s.idle, &s.iowait, &s.irq,
                       &s.softirq, &s.steal);
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
    uint64_t prev_total = prev->user + prev->nice + prev->sys + prev_idle
                          + prev->irq + prev->softirq + prev->steal;
    uint64_t curr_total = curr->user + curr->nice + curr->sys + curr_idle
                          + curr->irq + curr->softirq + curr->steal;
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
    char  state;
    uint64_t cpu_jiffies;       /* utime + stime — accumulated */
    uint64_t cpu_jiffies_prev;  /* previous tick's value, for delta */
    uint64_t rss_kb;
    uid_t uid;
    char  user[32];
    char  comm[64];
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
    char *p = rp + 2;   /* skip ") " */
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
static int read_processes(struct proc_entry *out, int max,
                          const struct proc_entry *prev_table, int prev_count)
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

#define MAX_TABLE_ROWS  32

struct app_state {
    struct yetty_ygui_old_engine *engine;
    struct yetty_ygui_old_widget *header;
    struct yetty_ygui_old_widget *cpu_bars[MAX_CORES + 1];
    struct yetty_ygui_old_widget *cpu_labels[MAX_CORES + 1];
    struct yetty_ygui_old_widget *table;

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

#define HEADER_H        28.0f
#define ROW_H           22.0f
#define BAR_W           140.0f
#define LABEL_W         110.0f
#define MARGIN          12.0f
#define TABLE_TOP_PAD   8.0f

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static uint32_t core_accent(int core)
{
    static const uint8_t pal[8][3] = {
        {255, 110, 102}, {110, 220, 130}, {102, 170, 255}, {255, 222, 100},
        {200, 130, 255}, {110, 230, 220}, {255, 165,  90}, {245, 130, 200},
    };
    const uint8_t *c = pal[core % 8];
    return rgba(c[0], c[1], c[2], 255);
}

static void build_ui(struct app_state *s, int n_cores)
{
    /* Header label across the top. */
    s->header = yetty_ygui_old_engine_label(s->engine, "ytop_title", MARGIN, MARGIN,
                                         "ytop — q to quit");

    /* Per-core rows: "cpuN" label + progress bar + "%" label, wrapped two
     * cores per row to fit horizontally on typical card widths. */
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

        char id_lbl[32], id_bar[32], id_val[32], lbl_text[16];
        snprintf(id_lbl, sizeof(id_lbl), "cpu_lbl_%d", i);
        snprintf(id_bar, sizeof(id_bar), "cpu_bar_%d", i);
        snprintf(id_val, sizeof(id_val), "cpu_val_%d", i);
        snprintf(lbl_text, sizeof(lbl_text), "cpu%d", i - 1);

        yetty_ygui_old_engine_label(s->engine, id_lbl, x, y + 4.0f, lbl_text);
        s->cpu_bars[i] = yetty_ygui_old_engine_progress(s->engine, id_bar,
                                                     x + 50.0f, y, BAR_W, ROW_H, 0.0f);
        if (s->cpu_bars[i]) {
            yetty_ygui_old_widget_set_accent_color(s->cpu_bars[i], core_accent(i - 1));
        }
        s->cpu_labels[i] = yetty_ygui_old_engine_label(s->engine, id_val,
                                                    x + 50.0f + BAR_W + 8.0f, y + 4.0f,
                                                    "  0%");
    }

    /* Table below the per-core block. */
    int n_core_rows = (n_cores + cores_per_row - 1) / cores_per_row;
    float table_y = MARGIN + HEADER_H + (float)n_core_rows * (ROW_H + 4.0f) + TABLE_TOP_PAD;
    float table_w = (float)cores_per_row * core_block_w;
    if (table_w < 720.0f) {
        table_w = 720.0f;
    }
    float table_h = ROW_H + (MAX_TABLE_ROWS + 1) * ROW_H;

    s->table = yetty_ygui_old_engine_table(s->engine, "procs", MARGIN, table_y,
                                        table_w, table_h);
    const char *names[]  = { "PID", "USER",   "STATE", "%CPU",  "RSS",   "COMMAND" };
    const float widths[] = { 70.0f, 90.0f,    60.0f,    70.0f,   100.0f,  0.0f     };
    yetty_ygui_old_widget_table_set_columns(s->table, names, widths,
                                         (int)(sizeof(names) / sizeof(names[0])));
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
                    yetty_ygui_old_widget_progress_set_value(s->cpu_bars[i], p / 100.0f);
                }
                if (s->cpu_labels[i]) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%5.1f%%", p);
                    yetty_ygui_old_widget_label_set_text(s->cpu_labels[i], buf);
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

    yetty_ygui_old_widget_table_clear_rows(s->table);
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
            pid_s,
            e->user[0] ? e->user : "?",
            state_s,
            cpu_s,
            rss_s,
            e->comm,
        };
        yetty_ygui_old_widget_table_add_row(s->table, cells, 6);
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

static void on_key(struct yetty_ygui_old_engine *engine, uint32_t key, int mods, void *user)
{
    (void)mods;
    (void)user;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_old_engine_stop(engine);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (yetty_ygui_old_init() != 0) {
        fprintf(stderr, "ytop: ygui_init failed (run inside a real terminal)\n");
        return 1;
    }

    struct app_state *s = calloc(1, sizeof(*s));
    if (!s) {
        yetty_ygui_old_shutdown();
        return 1;
    }
    s->clock_ticks = sysconf(_SC_CLK_TCK);
    if (s->clock_ticks <= 0) {
        s->clock_ticks = 100;
    }

    /* Seed the CPU sampler so the first delta represents real activity
     * rather than seconds-since-boot. */
    if (read_proc_stat(&s->cpu_st) < 0) {
        fprintf(stderr, "ytop: cannot read /proc/stat (Linux only)\n");
        free(s);
        yetty_ygui_old_shutdown();
        return 1;
    }
    memcpy(s->cpu_st.prev, s->cpu_st.curr, sizeof(s->cpu_st.curr));

    struct yetty_ygui_old_engine_args args = { .name = "ytop" };
    struct ygui_engine_ptr_result eng_r = yetty_ygui_old_engine_create(args);
    if (!eng_r.ok) {
        yetty_ycore_error_destroy(eng_r.error);
        free(s);
        yetty_ygui_old_shutdown();
        return 1;
    }
    s->engine = eng_r.value;
    build_ui(s, s->cpu_st.n_cores);
    yetty_ygui_old_engine_on_key(s->engine, on_key, NULL);

    /* Per-tick CPU/memory sampler timer attached to the engine's loop
     * (the engine owns the loop now). */
    uv_loop_t *loop = yetty_ygui_old_engine_get_loop(s->engine);
    uv_timer_t refresh_timer;
    uv_timer_init(loop, &refresh_timer);
    refresh_timer.data = s;
    uv_timer_start(&refresh_timer, on_refresh_timer, /*timeout_ms=*/0,
                   /*repeat_ms=*/REFRESH_MS);

    yetty_ygui_old_engine_run(s->engine);

    uv_timer_stop(&refresh_timer);
    uv_close((uv_handle_t *)&refresh_timer, NULL);

    yetty_ygui_old_engine_destroy(s->engine);
    free(s);
    yetty_ygui_old_shutdown();
    return 0;
}
