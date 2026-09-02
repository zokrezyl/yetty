/*
 * ytop2 — live system monitor on the ygui2 drawable-contract toolkit.
 *
 * The phase-2 proof of the strategy: the whole dashboard renders through
 * DCS drawable envelopes (pure PTY producer — no figures, no RPC, no GPU
 * link). Steady-state frames are INCREMENTAL: value ticks reopen only the
 * widgets whose content changed; a clean tick ships zero bytes.
 *
 * Data: /proc/stat (total + per-core cpu), /proc/meminfo. Quit: q / Ctrl-C.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/api/ygui2/framework.h>
#include <yetty/api/ygui2/widget.h>
#include <yetty/api/ygui2/widgets/label.h>
#include <yetty/api/ygui2/widgets/panel.h>
#include <yetty/api/ygui2/widgets/plot.h>
#include <yetty/api/ygui2/widgets/progress.h>
#include <yetty/api/ygui2/widgets/table.h>
#include <yetty/ygui2/defs.h>

enum {
    YTOP2_CORE_MAX = 8,
    /* Aggregate-cpu history window streamed into the plot: one sample per
     * refresh tick (500ms) — a minute of history. */
    YTOP2_HISTORY_SAMPLES = 120,
};

struct cpu_sample {
    uint64_t busy;
    uint64_t total;
};

struct ytop2_app {
    int running;
    struct yetty_yclass_object *framework;
    struct yetty_yclass_object *title;
    struct yetty_yclass_object *core_bars[YTOP2_CORE_MAX];
    struct yetty_yclass_object *core_labels[YTOP2_CORE_MAX];
    uint32_t core_count;
    struct yetty_yclass_object *memory_bar;
    struct yetty_yclass_object *memory_label;
    struct yetty_yclass_object *info_table;
    struct cpu_sample previous[YTOP2_CORE_MAX + 1];
    /* Streamed aggregate-cpu history plot. */
    struct yetty_yclass_object *cpu_plot;
    /* One history plot PER CORE (grid of two per row). */
    struct yetty_yclass_object *core_plots[YTOP2_CORE_MAX];
};

static int key_callback(uint32_t key, uint32_t mods, void *userdata)
{
    struct ytop2_app *app = userdata;
    (void)mods;
    if (key == 'q' || key == 0x03) {
        app->running = 0;
        return 1;
    }
    return 0;
}

static uint32_t read_cpu_samples(struct cpu_sample *samples, uint32_t sample_max)
{
    FILE *stat_file = fopen("/proc/stat", "r");
    if (!stat_file) {
        return 0;
    }
    char line[512];
    uint32_t count = 0;
    while (count < sample_max && fgets(line, sizeof(line), stat_file)) {
        if (strncmp(line, "cpu", 3) != 0) {
            break;
        }
        uint64_t user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
        const char *cursor = line + 3;
        while (*cursor && *cursor != ' ') {
            cursor++;
        }
        sscanf(cursor, "%llu %llu %llu %llu %llu %llu %llu", (unsigned long long *)&user,
               (unsigned long long *)&nice, (unsigned long long *)&sys, (unsigned long long *)&idle,
               (unsigned long long *)&iowait, (unsigned long long *)&irq,
               (unsigned long long *)&softirq);
        samples[count].busy = user + nice + sys + irq + softirq;
        samples[count].total = samples[count].busy + idle + iowait;
        count++;
    }
    fclose(stat_file);
    return count; /* [0] = aggregate, [1..] = cores */
}

static void read_memory(uint64_t *out_total_kb, uint64_t *out_available_kb)
{
    *out_total_kb = 0;
    *out_available_kb = 0;
    FILE *meminfo_file = fopen("/proc/meminfo", "r");
    if (!meminfo_file) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), meminfo_file)) {
        unsigned long long value = 0;
        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            *out_total_kb = value;
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            *out_available_kb = value;
        }
    }
    fclose(meminfo_file);
}

static float sample_ratio(const struct cpu_sample *previous, const struct cpu_sample *current)
{
    uint64_t busy_delta = current->busy - previous->busy;
    uint64_t total_delta = current->total - previous->total;
    return total_delta ? (float)busy_delta / (float)total_delta : 0.0f;
}

static struct yetty_yclass_object *must(struct yetty_yclass_object_ptr_result result,
                                        const char *what)
{
    if (YETTY_IS_ERR(result)) {
        fprintf(stderr, "ytop2: %s: %s\n", what, result.error.msg);
        exit(1);
    }
    return result.value;
}

static void must_ok(struct yetty_ycore_void_result result, const char *what)
{
    if (YETTY_IS_ERR(result)) {
        fprintf(stderr, "ytop2: %s: %s\n", what, result.error.msg);
        exit(1);
    }
}

static void build_ui(struct ytop2_app *app, uint32_t core_count)
{
    struct yetty_yclass_object *framework = app->framework;
    struct yetty_yclass_object *root = must(
        yetty_ygui2_framework_root_create(framework, yetty_ygui2_panel_class_get().value), "root");
    must_ok(yetty_ygui2_panel_set_bg(root, 0xFF14100Bu), "root bg"); /* BRAND_BG */

    struct yetty_yclass_object *column = must(yetty_ygui2_column_add(root), "column");
    struct yetty_ygui2_layout column_layout = {0};
    must_ok(yetty_ygui2_widget_layout_copy(column, &column_layout), "column layout copy");
    column_layout.grow = 1.0f;
    column_layout.gap = 6.0f;
    column_layout.pad_left = 12.0f;
    column_layout.pad_top = 12.0f;
    column_layout.pad_right = 12.0f;
    must_ok(yetty_ygui2_widget_layout_set(column, &column_layout), "column layout");

    struct yetty_ygui2_layout line_layout = {.basis = 22.0f};
    app->title = must(yetty_ygui2_widget_add(column, yetty_ygui2_label_class_get().value), "title");
    must_ok(yetty_ygui2_widget_layout_set(app->title, &line_layout), "title layout");
    must_ok(yetty_ygui2_label_set_text(app->title, "ytop2 — drawable-contract dashboard"),
            "title text");
    must_ok(yetty_ygui2_label_set_color(app->title, 0xFFA5C574u), "title color");

    app->core_count = core_count < YTOP2_CORE_MAX ? core_count : YTOP2_CORE_MAX;
    struct yetty_ygui2_layout bar_layout = {.basis = 14.0f};
    struct yetty_ygui2_layout bar_label_layout = {.basis = 90.0f};
    for (uint32_t core = 0; core < app->core_count; ++core) {
        struct yetty_yclass_object *row = must(yetty_ygui2_row_add(column), "core row");
        struct yetty_ygui2_layout row_layout = {0};
        must_ok(yetty_ygui2_widget_layout_copy(row, &row_layout), "core row copy");
        row_layout.basis = 16.0f;
        row_layout.gap = 8.0f;
        must_ok(yetty_ygui2_widget_layout_set(row, &row_layout), "core row layout");
        app->core_labels[core] =
            must(yetty_ygui2_widget_add(row, yetty_ygui2_label_class_get().value), "core label");
        must_ok(yetty_ygui2_widget_layout_set(app->core_labels[core], &bar_label_layout),
                "core label layout");
        app->core_bars[core] =
            must(yetty_ygui2_widget_add(row, yetty_ygui2_progress_class_get().value), "core bar");
        struct yetty_ygui2_layout grow_layout = {.grow = 1.0f, .cross_size = 12.0f};
        must_ok(yetty_ygui2_widget_layout_set(app->core_bars[core], &grow_layout),
                "core bar layout");
        (void)bar_layout;
    }

    struct yetty_yclass_object *memory_row = must(yetty_ygui2_row_add(column), "memory row");
    struct yetty_ygui2_layout memory_row_layout = {0};
    must_ok(yetty_ygui2_widget_layout_copy(memory_row, &memory_row_layout), "memory row copy");
    memory_row_layout.basis = 16.0f;
    memory_row_layout.gap = 8.0f;
    must_ok(yetty_ygui2_widget_layout_set(memory_row, &memory_row_layout), "memory row layout");
    app->memory_label = must(
        yetty_ygui2_widget_add(memory_row, yetty_ygui2_label_class_get().value), "memory label");
    must_ok(yetty_ygui2_widget_layout_set(app->memory_label, &bar_label_layout),
            "memory label layout");
    app->memory_bar = must(
        yetty_ygui2_widget_add(memory_row, yetty_ygui2_progress_class_get().value), "memory bar");
    struct yetty_ygui2_layout memory_bar_layout = {.grow = 1.0f, .cross_size = 12.0f};
    must_ok(yetty_ygui2_widget_layout_set(app->memory_bar, &memory_bar_layout),
            "memory bar layout");
    must_ok(yetty_ygui2_progress_set_accent(app->memory_bar, 0xFF79895Au), "memory accent");

    /* Aggregate-cpu history: the creation record ships once; every
     * refresh appends the new percent sample as ONE envelope (sample
     * chunk + ring-head op) — the window itself is never re-sent. */
    app->cpu_plot =
        must(yetty_ygui2_widget_add(column, yetty_ygui2_plot_class_get().value), "cpu plot");
    struct yetty_ygui2_layout plot_layout = {.basis = 150.0f};
    must_ok(yetty_ygui2_widget_layout_set(app->cpu_plot, &plot_layout), "cpu plot layout");
    must_ok(yetty_ygui2_plot_set_title(app->cpu_plot, "cpu total (1 min)"), "cpu plot title");
    must_ok(yetty_ygui2_plot_set_y_range(app->cpu_plot, 0.0f, 100.0f), "cpu plot range");
    must_ok(
        yetty_ygui2_plot_add_stream_buffer(app->cpu_plot, "cpu", YTOP2_HISTORY_SAMPLES, "#6BA892"),
        "cpu plot buffer");

    /* Per-core history plots, two per row — each core streams its own
     * 1-minute window into its own figure. */
    struct yetty_yclass_object *plot_row = NULL;
    for (uint32_t core = 0; core < app->core_count; ++core) {
        if (core % 2u == 0u) {
            plot_row = must(yetty_ygui2_row_add(column), "core plot row");
            struct yetty_ygui2_layout plot_row_layout = {0};
            must_ok(yetty_ygui2_widget_layout_copy(plot_row, &plot_row_layout),
                    "core plot row copy");
            plot_row_layout.basis = 120.0f;
            plot_row_layout.gap = 8.0f;
            must_ok(yetty_ygui2_widget_layout_set(plot_row, &plot_row_layout),
                    "core plot row layout");
        }
        app->core_plots[core] =
            must(yetty_ygui2_widget_add(plot_row, yetty_ygui2_plot_class_get().value), "core plot");
        struct yetty_ygui2_layout core_plot_layout = {.grow = 1.0f};
        must_ok(yetty_ygui2_widget_layout_set(app->core_plots[core], &core_plot_layout),
                "core plot layout");
        char plot_title[32];
        snprintf(plot_title, sizeof(plot_title), "core %u", core);
        must_ok(yetty_ygui2_plot_set_title(app->core_plots[core], plot_title), "core plot title");
        must_ok(yetty_ygui2_plot_set_y_range(app->core_plots[core], 0.0f, 100.0f),
                "core plot range");
        must_ok(yetty_ygui2_plot_add_stream_buffer(app->core_plots[core], "cpu",
                                                   YTOP2_HISTORY_SAMPLES, "#74C5A5"),
                "core plot buffer");
    }

    app->info_table =
        must(yetty_ygui2_widget_add(column, yetty_ygui2_table_class_get().value), "table");
    struct yetty_ygui2_layout table_layout = {.grow = 2.0f};
    must_ok(yetty_ygui2_widget_layout_set(app->info_table, &table_layout), "table layout");
    const char *headers[3] = {"metric", "value", "source"};
    float widths[3] = {160.0f, 0.0f, 160.0f};
    must_ok(yetty_ygui2_table_set_columns(app->info_table, headers, widths, 3u), "columns");
}

static void refresh(struct ytop2_app *app)
{
    struct cpu_sample current[YTOP2_CORE_MAX + 1] = {0};
    uint32_t sample_count = read_cpu_samples(current, YTOP2_CORE_MAX + 1);
    char text[128];
    if (sample_count > 0) {
        float aggregate = sample_ratio(&app->previous[0], &current[0]);
        snprintf(text, sizeof(text), "cpu %3.0f%%", (double)(aggregate * 100.0f));
        yetty_ygui2_label_set_text(app->title, text);
        /* APPEND one percent sample — the wire carries ~40 bytes (the
         * sample + a ring-head op); the receiver's shader unwraps the
         * ring, so the history scrolls (steady state re-sends nothing;
         * a structural replacement replays the cached window once). */
        float aggregate_percent = aggregate * 100.0f;
        struct yetty_ycore_void_result stream_res =
            yetty_ygui2_plot_append_samples(app->cpu_plot, &aggregate_percent, 1u);
        if (YETTY_IS_ERR(stream_res)) {
            /* First refresh runs before the first emit (no live figure
             * yet) — quietly skip; every later tick streams. */
            yetty_ycore_error_destroy(stream_res.error);
        }
    }
    for (uint32_t core = 0; core < app->core_count && core + 1 < sample_count; ++core) {
        float ratio = sample_ratio(&app->previous[core + 1], &current[core + 1]);
        snprintf(text, sizeof(text), "core %u %3.0f%%", core, (double)(ratio * 100.0f));
        yetty_ygui2_label_set_text(app->core_labels[core], text);
        yetty_ygui2_progress_set_value(app->core_bars[core], ratio);
        /* Append this core's newest percent sample — O(1) on the wire. */
        if (app->core_plots[core]) {
            float core_percent = ratio * 100.0f;
            struct yetty_ycore_void_result core_stream_res =
                yetty_ygui2_plot_append_samples(app->core_plots[core], &core_percent, 1u);
            if (YETTY_IS_ERR(core_stream_res)) {
                yetty_ycore_error_destroy(core_stream_res.error); /* pre-first-emit */
            }
        }
    }
    memcpy(app->previous, current, sizeof(app->previous));

    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    read_memory(&total_kb, &available_kb);
    if (total_kb > 0) {
        float used_ratio = (float)(total_kb - available_kb) / (float)total_kb;
        snprintf(text, sizeof(text), "mem %3.0f%%", (double)(used_ratio * 100.0f));
        yetty_ygui2_label_set_text(app->memory_label, text);
        yetty_ygui2_progress_set_value(app->memory_bar, used_ratio);

        yetty_ygui2_table_clear_rows(app->info_table);
        char value_text[64];
        snprintf(value_text, sizeof(value_text), "%llu MiB",
                 (unsigned long long)(total_kb / 1024u));
        const char *total_row[3] = {"mem total", value_text, "/proc/meminfo"};
        yetty_ygui2_table_add_row(app->info_table, total_row, 3u);
        char available_text[64];
        snprintf(available_text, sizeof(available_text), "%llu MiB",
                 (unsigned long long)(available_kb / 1024u));
        const char *available_row[3] = {"mem available", available_text, "/proc/meminfo"};
        yetty_ygui2_table_add_row(app->info_table, available_row, 3u);
        char cores_text[32];
        snprintf(cores_text, sizeof(cores_text), "%u", app->core_count);
        const char *cores_row[3] = {"cores shown", cores_text, "/proc/stat"};
        yetty_ygui2_table_add_row(app->info_table, cores_row, 3u);
    }
}

int main(void)
{
    struct ytop2_app app = {0};
    app.running = 1;

    /* Viewport: cell estimate from the tty size (a later phase reads the
     * geometry envelope; the estimate only affects layout density). */
    struct winsize window_size = {0};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);
    float viewport_w = (float)(window_size.ws_col ? window_size.ws_col : 80) * 8.0f;
    float viewport_h = (float)(window_size.ws_row ? window_size.ws_row : 40) * 16.0f;

    struct termios saved_termios;
    tcgetattr(STDIN_FILENO, &saved_termios);
    struct termios raw_termios = saved_termios;
    /* ISIG off: Ctrl-C arrives as byte 0x03 and takes the clean quit path
     * (unsubscribe + restore) instead of killing the process with the
     * pane's input subscription still armed. */
    raw_termios.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
    /* Alternate screen + hidden cursor: the fullscreen mode of the
     * strategy — no terminal scroll, the insertion lives for the run. */
    fputs("\x1b[?1049h\x1b[?25l\x1b[H", stdout);
    fflush(stdout);

    app.framework = must(yetty_ygui2_framework_make(), "framework");
    must_ok(yetty_ygui2_framework_attach(app.framework, STDIN_FILENO, STDOUT_FILENO), "attach");
    must_ok(yetty_ygui2_framework_set_viewport(app.framework, viewport_w, viewport_h), "viewport");
    must_ok(yetty_ygui2_framework_set_key_cb(app.framework, key_callback, &app), "key cb");

    struct cpu_sample bootstrap[YTOP2_CORE_MAX + 1] = {0};
    uint32_t sample_count = read_cpu_samples(bootstrap, YTOP2_CORE_MAX + 1);
    memcpy(app.previous, bootstrap, sizeof(app.previous));
    build_ui(&app, sample_count > 1 ? sample_count - 1 : 1);
    refresh(&app);
    must_ok(yetty_ygui2_framework_emit(app.framework), "first emit");

    /* MONOTONIC refresh schedule: the select wakes on every input
     * envelope (pane mouse moves arrive at pointer frequency) and the
     * data refresh — which STREAMS a plot window — must fire on its own
     * 500ms deadline only, never per wakeup. */
    struct timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC, &now_spec);
    double now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
    double next_refresh_seconds = now_seconds + 0.5;
    while (app.running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        clock_gettime(CLOCK_MONOTONIC, &now_spec);
        now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
        double wait_seconds = next_refresh_seconds - now_seconds;
        if (wait_seconds < 0.0) {
            wait_seconds = 0.0;
        }
        struct timeval tick = {
            .tv_sec = (time_t)wait_seconds,
            .tv_usec = (suseconds_t)((wait_seconds - (double)(time_t)wait_seconds) * 1e6)};
        int ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tick);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            uint8_t bytes[64];
            ssize_t byte_count = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (byte_count > 0) {
                yetty_ygui2_framework_feed_input(app.framework, bytes, (size_t)byte_count);
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &now_spec);
        now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
        if (now_seconds >= next_refresh_seconds) {
            refresh(&app);
            next_refresh_seconds = now_seconds + 0.5;
        }
        struct yetty_ycore_int_result dirty_res = yetty_ygui2_framework_is_dirty(app.framework);
        if (YETTY_IS_OK(dirty_res) && dirty_res.value) {
            must_ok(yetty_ygui2_framework_emit(app.framework), "emit");
        }
        fflush(stdout);
    }

    yetty_ygui2_framework_clear(app.framework);
    /* Unsubscribe pane input before restoring the terminal — a leaked
     * subscription sprays mouse envelopes at the shell forever. */
    yetty_ygui2_framework_detach(app.framework);
    yetty_ygui2_framework_dispose(app.framework);
    fputs("\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    return 0;
}
