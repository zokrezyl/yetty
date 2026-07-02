#ifndef YTRACE_C_H
#define YTRACE_C_H

/*
 * ytrace-c: C implementation of switchable trace points
 *
 * Each trace point has a static bool that can be toggled at runtime.
 * When disabled, only a single if-check is executed (minimal overhead).
 *
 * Usage:
 *   ytrace("processing item %d", item_id);
 *   ydebug("buffer size: %zu", size);
 *   yinfo("connection established");
 *   ywarn("timeout exceeded: %dms", timeout);
 *   yerror("failed to open file: %s", path);
 *
 * Control:
 *   ytrace_set_all_enabled(true);           // enable all trace points
 *   ytrace_set_level_enabled("trace", false); // disable by level
 *   ytrace_set_file_enabled("foo.c", true);   // enable by file
 *   ytrace_set_floor_level("warn");           // levels >= floor always emit
 *
 * A trace point's on/off state is resolved from, in increasing priority:
 * the "all" master flag, a per-level rule, a per-file rule, a per-function
 * rule, and finally the severity floor. The floor is a hard minimum: any
 * point whose level is at or above it is emitted no matter what the other
 * rules say (pass "off" to disable the floor). This is why errors stay
 * visible without turning tracing on globally. Rules registered via the
 * setters persist and also apply to trace points registered afterwards.
 *
 * Environment (parsed once at init):
 *   YTRACE_DEFAULT_ON=yes                  - enable all trace points
 *   YTRACE_FLOOR=warn                      - severity floor (trace..error|off)
 *   YTRACE_LEVELS=debug=off,info=on        - per-level rules
 *   YTRACE_FILES=container.c=on            - per-file rules
 *   YTRACE_FUNCTIONS=container_render=on   - per-function rules
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/yplatform/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time master switch */
#ifndef YTRACE_C_ENABLED
#define YTRACE_C_ENABLED 1
#endif

/* Per-level compile-time switches (set to 0 to completely remove) */
#ifndef YTRACE_C_ENABLE_TRACE
#define YTRACE_C_ENABLE_TRACE 1
#endif

#ifndef YTRACE_C_ENABLE_DEBUG
#define YTRACE_C_ENABLE_DEBUG 1
#endif

#ifndef YTRACE_C_ENABLE_INFO
#define YTRACE_C_ENABLE_INFO 1
#endif

#ifndef YTRACE_C_ENABLE_WARN
#define YTRACE_C_ENABLE_WARN 1
#endif

#ifndef YTRACE_C_ENABLE_ERROR
#define YTRACE_C_ENABLE_ERROR 1
#endif

#if YTRACE_C_ENABLED

/* Maximum number of trace points (increase if needed) */
#ifndef YTRACE_C_MAX_POINTS
#define YTRACE_C_MAX_POINTS 4096
#endif

/* Trace point information */
typedef struct {
    bool *enabled;
    const char *file;
    int line;
    const char *function;
    const char *level;
    const char *message;
} ytrace_point_t;

/* Initialize the trace system (call once at startup, optional - auto-inits on first use) */
void ytrace_init(void);

/* Shutdown and cleanup */
void ytrace_shutdown(void);

/* Register a trace point - returns initial enabled state */
bool ytrace_register(bool *enabled, const char *file, int line, const char *func, const char *level,
                     const char *message);

/* Output a trace message (called when trace point is enabled) */
// TODO: WE DO NOT WANT TO SEE ANY PLATFORM SPECIFIC CODE! ADD THEN WITH IFDEF!
void ytrace_output(const char *level, const char *file, int line, const char *func, const char *fmt,
                   ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 5, 6)))
#endif
    ;

/* Control functions.
 *
 * The per-level/file/function rules persist: they are remembered and applied
 * to trace points registered after the call, not only to existing ones. */
void ytrace_set_all_enabled(bool enabled);
void ytrace_set_level_enabled(const char *level, bool enabled);
void ytrace_set_file_enabled(const char *file, bool enabled);
void ytrace_set_function_enabled(const char *function, bool enabled);

/* Set the severity floor. Any trace point whose level is at or above `level`
 * is always emitted and cannot be disabled by the other rules. Accepts
 * "trace" | "debug" | "info" | "warn" | "error", or "off" / "none" to remove
 * the floor entirely. The default floor is "warn", so warnings and errors are
 * logged even when tracing is otherwise off. */
void ytrace_set_floor_level(const char *level);

/* Query functions */
size_t ytrace_get_point_count(void);
const ytrace_point_t *ytrace_get_points(void);

/* List all trace points to stderr */
void ytrace_list(void);

/* Maximum number of timer objects (increase if needed) */
#ifndef YTIME_MAX_TIMERS
#define YTIME_MAX_TIMERS 256
#endif

/*
 * Timer object — one per ytime_report() call site.
 *
 * Populated by ytime_timer_observe() on first use (registered = true after
 * that). Subsequent observations update the running stats in place.
 */
struct ytime_timer {
    const char *name;
    const char *file;
    int line;
    const char *function;
    bool registered;
    uint64_t count;
    double sum_ms;
    double last_ms;
    double min_ms;
    double max_ms;
    double avg_ms;
};

/*
 * Record an observation for the given timer. On first call the timer is
 * registered into the global registry so ytime_timer_list() can report it.
 */
void ytime_timer_observe(struct ytime_timer *t, const char *name, const char *file, int line,
                         const char *function, double elapsed_ms);

/* Query functions for timer objects */
size_t ytime_timer_get_count(void);
const struct ytime_timer *const *ytime_timer_get_all(void);

/* List all registered timers to stderr */
void ytime_timer_list(void);

/* Reset stats on all registered timers (keeps registration). */
void ytime_timer_reset_all(void);

/*
 * Trace macros
 *
 * Each macro creates a function-local static bool that:
 * 1. Is initialized by calling ytrace_register() on first execution
 * 2. Can be toggled later via the control functions
 * 3. Guards the actual trace output with a simple if-check
 */

#if YTRACE_C_ENABLE_TRACE
#define ytrace(fmt, ...)                                                                           \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "trace", fmt);    \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output("trace", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);              \
        }                                                                                          \
    } while (0)
#else
#define ytrace(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_DEBUG
#define ydebug(fmt, ...)                                                                           \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "debug", fmt);    \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output("debug", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);              \
        }                                                                                          \
    } while (0)
#else
#define ydebug(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_INFO
#define yinfo(fmt, ...)                                                                            \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "info", fmt);     \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output("info", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);               \
        }                                                                                          \
    } while (0)
#else
#define yinfo(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_WARN
#define ywarn(fmt, ...)                                                                            \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "warn", fmt);     \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output("warn", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);               \
        }                                                                                          \
    } while (0)
#else
#define ywarn(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_ERROR
#define yerror(fmt, ...)                                                                           \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "error", fmt);    \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output("error", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);              \
        }                                                                                          \
    } while (0)
#else
#define yerror(fmt, ...) ((void)0)
#endif

/*
 * Simple timer macros
 *
 * Usage:
 *   ytime_start(frame_render);
 *   ... work ...
 *   ytime_report(frame_render);
 *
 * ytime_start(name) declares a local double `ytime_<name>` holding the start
 * time. ytime_report(name) computes the elapsed time, feeds it into a static
 * `struct ytime_timer` registered once per call site (like ytrace points),
 * and emits via ydebug:
 *
 *     frame_render: 1.234 ms  (avg 1.111 ms, min 0.900, max 3.200, n=42)
 *
 * Because it uses ydebug, it obeys the standard ytrace enable/disable
 * controls. Start and report must live in the same scope.
 */
#define ytime_start(name) double ytime_##name = yetty_yplatform_ytime_monotonic_sec()

#define ytime_report(name)                                                                         \
    do {                                                                                           \
        static struct ytime_timer _ytime_timer_##name;                                             \
        double _ytime_elapsed_ms_ =                                                                \
            (yetty_yplatform_ytime_monotonic_sec() - ytime_##name) * 1000.0;                       \
        ytime_timer_observe(&_ytime_timer_##name, #name, __FILE__, __LINE__, __func__,             \
                            _ytime_elapsed_ms_);                                                   \
        yinfo(#name ": %.3f ms  (avg %.3f ms, min %.3f, max %.3f, n=%llu)", _ytime_elapsed_ms_,    \
              _ytime_timer_##name.avg_ms, _ytime_timer_##name.min_ms, _ytime_timer_##name.max_ms,  \
              (unsigned long long)_ytime_timer_##name.count);                                      \
    } while (0)

/* Generic log macro with explicit level */
#define ylog(lvl, fmt, ...)                                                                        \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ =                                                                     \
                ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, lvl, fmt);        \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output(lvl, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);                  \
        }                                                                                          \
    } while (0)

#else /* !YTRACE_C_ENABLED */

/* All macros become no-ops when ytrace-c is disabled */
#define ytrace(fmt, ...) ((void)0)
#define ydebug(fmt, ...) ((void)0)
#define yinfo(fmt, ...) ((void)0)
#define ywarn(fmt, ...) ((void)0)
#define yerror(fmt, ...) ((void)0)
#define ylog(lvl, fmt, ...) ((void)0)
#define ytime_start(name) ((void)0)
#define ytime_report(name) ((void)0)

static inline void ytrace_init(void)
{
}
static inline void ytrace_shutdown(void)
{
}
static inline void ytrace_set_all_enabled(bool enabled)
{
    (void)enabled;
}
static inline void ytrace_set_level_enabled(const char *level, bool enabled)
{
    (void)level;
    (void)enabled;
}
static inline void ytrace_set_file_enabled(const char *file, bool enabled)
{
    (void)file;
    (void)enabled;
}
static inline void ytrace_set_function_enabled(const char *function, bool enabled)
{
    (void)function;
    (void)enabled;
}
static inline void ytrace_set_floor_level(const char *level)
{
    (void)level;
}
static inline size_t ytrace_get_point_count(void)
{
    return 0;
}
static inline const ytrace_point_t *ytrace_get_points(void)
{
    return NULL;
}
static inline void ytrace_list(void)
{
}
struct ytime_timer;
static inline void ytime_timer_observe(struct ytime_timer *t, const char *name, const char *file,
                                       int line, const char *function, double elapsed_ms)
{
    (void)t;
    (void)name;
    (void)file;
    (void)line;
    (void)function;
    (void)elapsed_ms;
}
static inline size_t ytime_timer_get_count(void)
{
    return 0;
}
static inline const struct ytime_timer *const *ytime_timer_get_all(void)
{
    return NULL;
}
static inline void ytime_timer_list(void)
{
}
static inline void ytime_timer_reset_all(void)
{
}

#endif /* YTRACE_C_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* YTRACE_C_H */
