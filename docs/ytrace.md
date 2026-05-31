# Tracing: ytrace

Switchable trace points with minimal overhead when disabled.

## Levels

Five levels, from most verbose to most critical:

| Macro | Level | Use |
|-------|-------|-----|
| `ytrace(fmt, ...)` | trace | Hot-path instrumentation, per-frame data |
| `ydebug(fmt, ...)` | debug | Development diagnostics |
| `yinfo(fmt, ...)`  | info  | Significant state changes |
| `ywarn(fmt, ...)`   | warn  | Recoverable problems |
| `yerror(fmt, ...)`  | error | Failures |

## How It Works

Each macro expands to a function-local static bool guarding the output call:

```c
ydebug("buffer size: %zu", size);

/* Expands to: */
do {
    static bool _ytrace_enabled_ = false;
    static bool _ytrace_registered_ = false;
    if (!_ytrace_registered_) {
        _ytrace_enabled_ = ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "debug", ...);
        _ytrace_registered_ = true;
    }
    if (_ytrace_enabled_) {
        ytrace_output("debug", __FILE__, __LINE__, __func__, "buffer size: %zu", size);
    }
} while (0)
```

When disabled: one `if` check per call site. No string formatting, no IO.

## Runtime Control

```c
ytrace_set_all_enabled(true);              /* enable everything */
ytrace_set_level_enabled("trace", false);  /* disable trace level */
ytrace_set_file_enabled("foo.c", true);    /* enable all points in foo.c */
ytrace_set_function_enabled("render", true); /* enable by function name */
```

Environment variable `YTRACE_DEFAULT_ON=yes` enables all trace points at startup.

## Compile-Time Switches

Set to 0 to completely remove a level — macros become `((void)0)`:

```c
#define YTRACE_C_ENABLE_TRACE 0  /* remove ytrace() entirely */
#define YTRACE_C_ENABLE_DEBUG 0  /* remove ydebug() entirely */
```

Build targets control this: `ytrace` builds have all levels enabled, `yinfo` builds remove trace and debug.

## Build Targets

- `build-desktop-ytrace-release` — all levels enabled, release optimization
- `build-desktop-yinfo-release` — info/warn/error only, for deployment

## Header

`include/yetty/ytrace/ytrace.h`
