/*
 * ytrace-c: C implementation of switchable trace points
 */

#include <yetty/ytrace/ytrace.h>

#if YTRACE_C_ENABLED

#include <yetty/yplatform/thread.h>
#include <yetty/yplatform/term.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Number of named levels, ordered by increasing severity (see level_severity). */
#define YTRACE_LEVEL_COUNT 5

/* Floor value meaning "no floor" — no level is force-enabled by the floor. */
#define YTRACE_FLOOR_OFF (YTRACE_LEVEL_COUNT + 1)

/* Default severity floor: warnings and errors are emitted even when tracing is
 * otherwise off, so failures are never silently swallowed. */
#define YTRACE_FLOOR_DEFAULT 3 /* "warn" */

/* Capacity of the per-file and per-function rule tables. */
#define YTRACE_C_MAX_NAME_RULES 128

/* A persistent per-file or per-function rule. The name is copied so it does not
 * depend on the caller keeping the string alive. */
struct ytrace_name_rule {
    char name[128];
    bool enabled;
    bool used;
};

/* Global state */
static ytrace_point_t g_points[YTRACE_C_MAX_POINTS];
static size_t g_point_count = 0;
static struct yetty_yplatform_ymutex *g_mutex = NULL;
static bool g_initialized = false;

/* Resolution state — see resolve_point_enabled(). */
static bool g_all_enabled = false;           /* master: emit every point */
static int g_floor_severity = YTRACE_FLOOR_DEFAULT;
static bool g_level_rule_used[YTRACE_LEVEL_COUNT];
static bool g_level_rule_enabled[YTRACE_LEVEL_COUNT];
static struct ytrace_name_rule g_file_rules[YTRACE_C_MAX_NAME_RULES];
static struct ytrace_name_rule g_func_rules[YTRACE_C_MAX_NAME_RULES];

/* Timer registry */
static struct ytime_timer *g_timers[YTIME_MAX_TIMERS];
static size_t g_timer_count = 0;

static void ensure_mutex(void)
{
    if (!g_mutex) {
        g_mutex = yetty_yplatform_ymutex_create();
    }
}

#define YTRACE_LOCK()                                                                              \
    do {                                                                                           \
        ensure_mutex();                                                                            \
        yetty_yplatform_ymutex_lock(g_mutex);                                                      \
    } while (0)
#define YTRACE_UNLOCK() yetty_yplatform_ymutex_unlock(g_mutex)

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

    g_use_colors = yetty_yplatform_stderr_supports_color();
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

/* Map a level name to its severity index (0 = trace .. 4 = error). Unknown
 * names are treated as "info" so an ad-hoc ylog() level still resolves. */
static int level_severity(const char *level)
{
    if (strcmp(level, "trace") == 0) {
        return 0;
    }
    if (strcmp(level, "debug") == 0) {
        return 1;
    }
    if (strcmp(level, "info") == 0) {
        return 2;
    }
    if (strcmp(level, "warn") == 0) {
        return 3;
    }
    if (strcmp(level, "error") == 0) {
        return 4;
    }
    return 2;
}

/* Parse a boolean word. Accepts 1/on/true/yes and 0/off/false/no; anything
 * else yields `fallback`. */
static bool parse_bool_word(const char *word, bool fallback)
{
    if (!word || !word[0]) {
        return fallback;
    }
    if (strcmp(word, "1") == 0 || strcmp(word, "on") == 0 || strcmp(word, "true") == 0 ||
        strcmp(word, "yes") == 0) {
        return true;
    }
    if (strcmp(word, "0") == 0 || strcmp(word, "off") == 0 || strcmp(word, "false") == 0 ||
        strcmp(word, "no") == 0) {
        return false;
    }
    return fallback;
}

/* Parse a floor level name into a severity, or YTRACE_FLOOR_OFF for
 * off/none/never. An empty/unknown value falls back to the default floor. */
static int parse_floor_severity(const char *level)
{
    if (!level || !level[0]) {
        return YTRACE_FLOOR_DEFAULT;
    }
    if (strcmp(level, "off") == 0 || strcmp(level, "none") == 0 || strcmp(level, "never") == 0) {
        return YTRACE_FLOOR_OFF;
    }
    return level_severity(level);
}

/* Return the basename portion of a path (after the last '/' or '\\'). */
static const char *path_basename(const char *file)
{
    const char *slash = strrchr(file, '/');
    if (!slash) {
        slash = strrchr(file, '\\');
    }
    return slash ? slash + 1 : file;
}

/* Store a per-level rule (caller holds the lock). */
static void set_level_rule_locked(const char *level, bool enabled)
{
    int idx = level_severity(level);
    if (idx < 0 || idx >= YTRACE_LEVEL_COUNT) {
        return;
    }
    g_level_rule_used[idx] = true;
    g_level_rule_enabled[idx] = enabled;
}

/* Store a per-file or per-function rule in `table` (caller holds the lock).
 * Updates an existing entry with the same name, else inserts a new one; a full
 * table drops the rule silently. */
static void set_name_rule_locked(struct ytrace_name_rule *table, const char *name, bool enabled)
{
    if (!name || !name[0]) {
        return;
    }
    for (size_t i = 0; i < YTRACE_C_MAX_NAME_RULES; i++) {
        if (table[i].used && strcmp(table[i].name, name) == 0) {
            table[i].enabled = enabled;
            return;
        }
    }
    for (size_t i = 0; i < YTRACE_C_MAX_NAME_RULES; i++) {
        if (!table[i].used) {
            snprintf(table[i].name, sizeof(table[i].name), "%s", name);
            table[i].enabled = enabled;
            table[i].used = true;
            return;
        }
    }
}

static void apply_level_rule_locked(const char *name, bool enabled)
{
    set_level_rule_locked(name, enabled);
}
static void apply_file_rule_locked(const char *name, bool enabled)
{
    set_name_rule_locked(g_file_rules, name, enabled);
}
static void apply_func_rule_locked(const char *name, bool enabled)
{
    set_name_rule_locked(g_func_rules, name, enabled);
}

/* Parse a "name=value,name2,..." spec and apply each pair via `apply`. A bare
 * "name" (no '=') means enabled. Works on a bounded copy so it can split in
 * place without touching the source string. Caller holds the lock. */
static void parse_pairs_locked(const char *spec, void (*apply)(const char *, bool))
{
    if (!spec || !spec[0]) {
        return;
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", spec);
    size_t start = 0;
    for (size_t i = 0;; i++) {
        char ch = buf[i];
        if (ch == ',' || ch == '\0') {
            buf[i] = '\0';
            char *token = &buf[start];
            if (token[0]) {
                char *equals = strchr(token, '=');
                bool enabled = true;
                if (equals) {
                    *equals = '\0';
                    enabled = parse_bool_word(equals + 1, true);
                }
                if (token[0]) {
                    apply(token, enabled);
                }
            }
            if (ch == '\0') {
                break;
            }
            start = i + 1;
        }
    }
}

/* Resolve a trace point's enabled state from the current rules, in increasing
 * priority: master flag, per-level, per-file, per-function, then the severity
 * floor (a hard minimum that can only force a point on). */
static bool resolve_point_enabled(const char *level, const char *file, const char *func)
{
    int sev = level_severity(level);
    bool enabled = g_all_enabled;

    if (sev >= 0 && sev < YTRACE_LEVEL_COUNT && g_level_rule_used[sev]) {
        enabled = g_level_rule_enabled[sev];
    }

    const char *base = path_basename(file);
    for (size_t i = 0; i < YTRACE_C_MAX_NAME_RULES; i++) {
        if (g_file_rules[i].used &&
            (strcmp(g_file_rules[i].name, file) == 0 || strcmp(g_file_rules[i].name, base) == 0)) {
            enabled = g_file_rules[i].enabled;
            break;
        }
    }

    for (size_t i = 0; i < YTRACE_C_MAX_NAME_RULES; i++) {
        if (g_func_rules[i].used && strcmp(g_func_rules[i].name, func) == 0) {
            enabled = g_func_rules[i].enabled;
            break;
        }
    }

    if (sev >= g_floor_severity) {
        enabled = true;
    }
    return enabled;
}

/* Recompute every registered trace point's enabled state (caller holds the
 * lock). Called after any rule change so already-registered points pick it up. */
static void reapply_all_locked(void)
{
    for (size_t i = 0; i < g_point_count; i++) {
        ytrace_point_t *point = &g_points[i];
        *point->enabled = resolve_point_enabled(point->level, point->file, point->function);
    }
}

void ytrace_init(void)
{
    YTRACE_LOCK();

    if (g_initialized) {
        YTRACE_UNLOCK();
        return;
    }

    /* Master enable — all points on (YTRACE_DEFAULT_ON, kept for compatibility). */
    const char *default_on = getenv("YTRACE_DEFAULT_ON");
    if (default_on != NULL) {
        g_all_enabled = parse_bool_word(default_on, g_all_enabled);
    }

    /* Severity floor — defaults to "warn" so warnings and errors are always
     * emitted even with tracing otherwise off. YTRACE_FLOOR overrides it. */
    g_floor_severity = YTRACE_FLOOR_DEFAULT;
    const char *floor = getenv("YTRACE_FLOOR");
    if (floor != NULL) {
        g_floor_severity = parse_floor_severity(floor);
    }

    /* Per-level / per-file / per-function rules from the environment. */
    parse_pairs_locked(getenv("YTRACE_LEVELS"), apply_level_rule_locked);
    parse_pairs_locked(getenv("YTRACE_FILES"), apply_file_rule_locked);
    parse_pairs_locked(getenv("YTRACE_FUNCTIONS"), apply_func_rule_locked);

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

    /* Set initial state from the current rules. */
    *enabled = resolve_point_enabled(level, file, func);

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
    yetty_yplatform_format_timestamp(time_buf, sizeof(time_buf));

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
    fprintf(stderr, "[%s] %s[%-5s]%s %s:%d (%s): %s\n", time_buf, level_color(level), level,
            color_reset(), bname, line, func, msg_buf);
}

void ytrace_set_all_enabled(bool enabled)
{
    YTRACE_LOCK();
    g_all_enabled = enabled;
    reapply_all_locked();
    YTRACE_UNLOCK();
}

void ytrace_set_level_enabled(const char *level, bool enabled)
{
    YTRACE_LOCK();
    set_level_rule_locked(level, enabled);
    reapply_all_locked();
    YTRACE_UNLOCK();
}

void ytrace_set_file_enabled(const char *file, bool enabled)
{
    YTRACE_LOCK();
    set_name_rule_locked(g_file_rules, file, enabled);
    reapply_all_locked();
    YTRACE_UNLOCK();
}

void ytrace_set_function_enabled(const char *function, bool enabled)
{
    YTRACE_LOCK();
    set_name_rule_locked(g_func_rules, function, enabled);
    reapply_all_locked();
    YTRACE_UNLOCK();
}

void ytrace_set_floor_level(const char *level)
{
    YTRACE_LOCK();
    g_floor_severity = parse_floor_severity(level);
    reapply_all_locked();
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
