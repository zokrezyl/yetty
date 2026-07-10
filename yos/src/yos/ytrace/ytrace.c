/*
 * ytrace-c: C implementation of switchable trace points
 */

#include <yos/ytrace/ytrace.h>

#if YTRACE_C_ENABLED

#include <yos/yplatform/thread.h>
#include <yos/yplatform/term.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>      /* pthread_self() — for the trace-file tid suffix */
#include <stdint.h>       /* uintptr_t */

/* Global state */
static ytrace_point_t g_points[YTRACE_C_MAX_POINTS];
static size_t g_point_count = 0;
static struct yos_yplatform_ymutex *g_mutex = NULL;
static bool g_initialized = false;
static bool g_default_enabled = false;

/* Timer registry */
static struct ytime_timer *g_timers[YTIME_MAX_TIMERS];
static size_t g_timer_count = 0;

static void ensure_mutex(void)
{
    if (!g_mutex) {
        g_mutex = yos_yplatform_ymutex_create();
    }
}

#define YTRACE_LOCK()                                                                              \
    do {                                                                                           \
        ensure_mutex();                                                                            \
        yos_yplatform_ymutex_lock(g_mutex);                                                      \
    } while (0)
#define YTRACE_UNLOCK() yos_yplatform_ymutex_unlock(g_mutex)

/* ANSI color codes */
#define ANSI_RESET "\033[0m"
#define ANSI_GRAY "\033[90m"
#define ANSI_CYAN "\033[36m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED "\033[31m"
#define ANSI_BOLD "\033[1m"

/* Check if output is a TTY for color support */
static bool g_use_colors = false;

static void check_color_support(void)
{
    const char *no_color = getenv("NO_COLOR");

    if (no_color != NULL) {
        g_use_colors = false;
        return;
    }

    g_use_colors = yos_yplatform_stderr_supports_color();
}

/* Optional per-thread trace file. When YTRACE_FILE_PREFIX is set,
 * each thread opens a separate "<prefix>-<comm>-<tid>" file on
 * first ytrace_output call and writes there INSTEAD of stderr.
 * One file per host thread = one file per yos guest process (every
 * forked guest gets its own fork_thread_func host pthread). Lets
 * us follow one process at a time without 4 procs' traces
 * interleaving line-by-line on stderr.
 *
 * yos_ytrace_set_comm(name) labels the current thread so the file
 * name is human-readable ("trace-nvim-12345" instead of just
 * "trace-12345"). Called from main.c / impl/proc.c on each exec. */
static __thread FILE *_yt_file = NULL;
static __thread int   _yt_file_inited = 0;
static __thread char  _yt_comm[32] = "yos";

static FILE *yt_thread_file(void)
{
    if (_yt_file_inited) return _yt_file;  /* may be NULL = use stderr */
    _yt_file_inited = 1;
    const char *prefix = getenv("YTRACE_FILE_PREFIX");
    if (!prefix || !*prefix) return NULL;
    char path[512];
    long tid = (long)(uintptr_t)pthread_self();
    snprintf(path, sizeof path, "%s-%s-%ld", prefix, _yt_comm, tid);
    _yt_file = fopen(path, "w");
    if (_yt_file) setvbuf(_yt_file, NULL, _IOLBF, 0);
    return _yt_file;
}

void yos_ytrace_set_comm(const char *comm)
{
    if (!comm) return;
    strncpy(_yt_comm, comm, sizeof _yt_comm - 1);
    _yt_comm[sizeof _yt_comm - 1] = '\0';
    /* If a file is already open under the old comm, close it so the
     * next ytrace_output reopens with the new name. */
    if (_yt_file) {
        fclose(_yt_file);
        _yt_file = NULL;
    }
    _yt_file_inited = 0;
}

static const char *level_color(const char *level)
{
    if (!g_use_colors) {
        return "";
    }

    if (strcmp(level, "trace") == 0) {
        return ANSI_GRAY;
    }
    if (strcmp(level, "debug") == 0) {
        return ANSI_CYAN;
    }
    if (strcmp(level, "info") == 0) {
        return ANSI_GREEN;
    }
    if (strcmp(level, "warn") == 0) {
        return ANSI_YELLOW;
    }
    if (strcmp(level, "error") == 0) {
        return ANSI_RED ANSI_BOLD;
    }
    return "";
}

static const char *color_reset(void)
{
    return g_use_colors ? ANSI_RESET : "";
}

void ytrace_init(void)
{
    YTRACE_LOCK();

    if (g_initialized) {
        YTRACE_UNLOCK();
        return;
    }

    /* Check YTRACE_DEFAULT_ON environment variable */
    const char *default_on = getenv("YTRACE_DEFAULT_ON");
    if (default_on != NULL) {
        g_default_enabled = (strcmp(default_on, "yes") == 0 || strcmp(default_on, "1") == 0 ||
                             strcmp(default_on, "true") == 0);
    }

    check_color_support();
    g_initialized = true;

    YTRACE_UNLOCK();
}

void ytrace_shutdown(void)
{
    YTRACE_LOCK();
    g_point_count = 0;
    g_initialized = false;
    YTRACE_UNLOCK();
}

bool ytrace_default_enabled(void)
{
    if (!g_initialized) ytrace_init();
    return g_default_enabled;
}

bool ytrace_register(bool *enabled, const char *file, int line, const char *func, const char *level,
                     const char *message)
{
    YTRACE_LOCK();

    /* Auto-initialize on first registration */
    if (!g_initialized) {
        YTRACE_UNLOCK();
        ytrace_init();
        YTRACE_LOCK();
    }

    /* Set initial state */
    *enabled = g_default_enabled;

    /* Register if space available */
    if (g_point_count < YTRACE_C_MAX_POINTS) {
        g_points[g_point_count] = (ytrace_point_t){.enabled = enabled,
                                                   .file = file,
                                                   .line = line,
                                                   .function = func,
                                                   .level = level,
                                                   .message = message};
        g_point_count++;
    } else {
        /* Overflow - print warning once */
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[ytrace-c] WARNING: max trace points (%d) exceeded\n",
                    YTRACE_C_MAX_POINTS);
            warned = true;
        }
    }

    YTRACE_UNLOCK();
    return *enabled;
}

void ytrace_output(const char *level, const char *file, int line, const char *func, const char *fmt,
                   ...)
{
    char msg_buf[1024];
    char time_buf[32];

    /* Format the user message */
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Get timestamp with milliseconds */
    yos_yplatform_format_timestamp(time_buf, sizeof(time_buf));

    /* Extract basename from file path */
    const char *bname = strrchr(file, '/');
    if (!bname) {
        bname = strrchr(file, '\\');
    }
    if (bname) {
        bname++;
    } else {
        bname = file;
    }

    /* Output format: [HH:MM:SS.mmm] [level] file:line (func): message */
    FILE *sink = yt_thread_file();
    if (sink) {
        /* File output: never colorised; lines are easier to grep. */
        fprintf(sink, "[%s] [%-5s] %s:%d (%s): %s\n",
                time_buf, level, bname, line, func, msg_buf);
    } else {
        fprintf(stderr, "[%s] %s[%-5s]%s %s:%d (%s): %s\n",
                time_buf, level_color(level), level, color_reset(),
                bname, line, func, msg_buf);
    }
}

void ytrace_set_all_enabled(bool enabled)
{
    YTRACE_LOCK();
    /* Update the default too so trace points that REGISTER later
     * (lazy first-touch from new code paths, or new wasm modules
     * loaded after exec) start out enabled rather than picking up
     * the stale startup-time default. Without this, ytrace_set_all_enabled
     * only affects already-registered points and any subsequent
     * first-hit registration silently re-disables itself. */
    g_default_enabled = enabled;
    for (size_t i = 0; i < g_point_count; i++) {
        *g_points[i].enabled = enabled;
    }
    YTRACE_UNLOCK();
}

void ytrace_set_level_enabled(const char *level, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        if (strcmp(g_points[i].level, level) == 0) {
            *g_points[i].enabled = enabled;
        }
    }
    YTRACE_UNLOCK();
}

void ytrace_set_file_enabled(const char *file, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        /* Match full path or basename */
        const char *point_basename = strrchr(g_points[i].file, '/');
        if (!point_basename) {
            point_basename = strrchr(g_points[i].file, '\\');
        }
        point_basename = point_basename ? point_basename + 1 : g_points[i].file;

        if (strcmp(g_points[i].file, file) == 0 || strcmp(point_basename, file) == 0) {
            *g_points[i].enabled = enabled;
        }
    }
    YTRACE_UNLOCK();
}

void ytrace_set_function_enabled(const char *function, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        if (strcmp(g_points[i].function, function) == 0) {
            *g_points[i].enabled = enabled;
        }
    }
    YTRACE_UNLOCK();
}

size_t ytrace_get_point_count(void)
{
    YTRACE_LOCK();
    size_t count = g_point_count;
    YTRACE_UNLOCK();
    return count;
}

const ytrace_point_t *ytrace_get_points(void)
{
    return g_points;
}

void ytrace_list(void)
{
    YTRACE_LOCK();

    fprintf(stderr, "\n[ytrace-c] Registered trace points: %zu\n", g_point_count);
    fprintf(stderr, "%-4s %-7s %-6s %-30s %-20s %s\n", "IDX", "ENABLED", "LEVEL", "FILE:LINE",
            "FUNCTION", "MESSAGE");
    fprintf(stderr, "%-4s %-7s %-6s %-30s %-20s %s\n", "---", "-------", "-----",
            "-----------------------------", "-------------------", "-------");

    for (size_t i = 0; i < g_point_count; i++) {
        const ytrace_point_t *p = &g_points[i];

        /* Extract basename */
        const char *bname = strrchr(p->file, '/');
        if (!bname) {
            bname = strrchr(p->file, '\\');
        }
        bname = bname ? bname + 1 : p->file;

        char loc_buf[32];
        snprintf(loc_buf, sizeof(loc_buf), "%s:%d", bname, p->line);

        /* Truncate message for display */
        char msg_buf[32];
        if (p->message && strlen(p->message) > 0) {
            snprintf(msg_buf, sizeof(msg_buf), "%.28s%s", p->message,
                     strlen(p->message) > 28 ? ".." : "");
        } else {
            msg_buf[0] = '\0';
        }

        fprintf(stderr, "%-4zu %-7s %-6s %-30s %-20s %s\n", i, *p->enabled ? "ON" : "off", p->level,
                loc_buf, p->function, msg_buf);
    }

    fprintf(stderr, "\n");
    YTRACE_UNLOCK();
}

void ytime_timer_observe(struct ytime_timer *t, const char *name, const char *file, int line,
                         const char *function, double elapsed_ms)
{
    YTRACE_LOCK();

    if (!t->registered) {
        t->name = name;
        t->file = file;
        t->line = line;
        t->function = function;
        t->count = 0;
        t->sum_ms = 0.0;
        t->last_ms = 0.0;
        t->min_ms = elapsed_ms;
        t->max_ms = elapsed_ms;
        t->avg_ms = 0.0;

        if (g_timer_count < YTIME_MAX_TIMERS) {
            g_timers[g_timer_count++] = t;
        } else {
            static bool warned = false;
            if (!warned) {
                fprintf(stderr, "[ytrace-c] WARNING: max timers (%d) exceeded\n", YTIME_MAX_TIMERS);
                warned = true;
            }
        }

        t->registered = true;
    }

    t->count++;
    t->sum_ms += elapsed_ms;
    t->last_ms = elapsed_ms;
    t->avg_ms = t->sum_ms / (double)t->count;
    if (elapsed_ms < t->min_ms) {
        t->min_ms = elapsed_ms;
    }
    if (elapsed_ms > t->max_ms) {
        t->max_ms = elapsed_ms;
    }

    YTRACE_UNLOCK();
}

size_t ytime_timer_get_count(void)
{
    YTRACE_LOCK();
    size_t count = g_timer_count;
    YTRACE_UNLOCK();
    return count;
}

const struct ytime_timer *const *ytime_timer_get_all(void)
{
    return (const struct ytime_timer *const *)g_timers;
}

void ytime_timer_list(void)
{
    YTRACE_LOCK();

    fprintf(stderr, "\n[ytrace-c] Registered timers: %zu\n", g_timer_count);
    fprintf(stderr, "%-4s %-20s %-30s %10s %10s %10s %10s %10s\n", "IDX", "NAME", "FILE:LINE", "N",
            "LAST(ms)", "AVG(ms)", "MIN(ms)", "MAX(ms)");

    for (size_t i = 0; i < g_timer_count; i++) {
        const struct ytime_timer *t = g_timers[i];

        const char *bname = strrchr(t->file, '/');
        if (!bname) {
            bname = strrchr(t->file, '\\');
        }
        bname = bname ? bname + 1 : t->file;

        char loc_buf[32];
        snprintf(loc_buf, sizeof(loc_buf), "%s:%d", bname, t->line);

        fprintf(stderr, "%-4zu %-20s %-30s %10llu %10.3f %10.3f %10.3f %10.3f\n", i,
                t->name ? t->name : "", loc_buf, (unsigned long long)t->count, t->last_ms,
                t->avg_ms, t->min_ms, t->max_ms);
    }

    fprintf(stderr, "\n");
    YTRACE_UNLOCK();
}

void ytime_timer_reset_all(void)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_timer_count; i++) {
        struct ytime_timer *t = g_timers[i];
        t->count = 0;
        t->sum_ms = 0.0;
        t->last_ms = 0.0;
        t->min_ms = 0.0;
        t->max_ms = 0.0;
        t->avg_ms = 0.0;
    }
    YTRACE_UNLOCK();
}

#endif /* YTRACE_C_ENABLED */
