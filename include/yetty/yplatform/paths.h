#ifndef YETTY_YPLATFORM_PATHS_H
#define YETTY_YPLATFORM_PATHS_H

#include <limits.h>
#include <yetty/ycore/result.h>

/* MSVC's <limits.h> doesn't define PATH_MAX. Windows has MAX_PATH (260)
 * in <windows.h> and the older _MAX_PATH alias in <stdlib.h>, but
 * neither is a drop-in for POSIX PATH_MAX. Modern Windows 10+ with the
 * long-paths registry flag allows paths well beyond 260. Use the
 * Linux value here so the struct layout stays uniform across platforms;
 * it's a process-lifetime singleton, so a few extra KB is irrelevant. */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolved process-wide directory layout for a yetty install. Each
 * field is a NUL-terminated absolute path string ready to be used by
 * snprintf / fopen / etc. — never a symlink to be followed, never
 * with a trailing slash. Backed by per-platform impls under
 * src/yetty/yplatform/paths/<platform>.{c,m}.
 *
 * Field semantics:
 *   cache_dir   — XDG_CACHE_HOME-equivalent: writeable, evictable
 *                  scratch (font atlases, render caches, brotli'd
 *                  intermediates). Linux: $XDG_CACHE_HOME/yetty.
 *   data_dir    — XDG_DATA_HOME-equivalent: writeable, persistent
 *                  (extracted yemu rootfs, downloaded assets). Linux:
 *                  $XDG_DATA_HOME/yetty.
 *   runtime_dir — XDG_RUNTIME_DIR-equivalent: short-lived per-process
 *                  state (sockets, lock files). Linux: $XDG_RUNTIME_DIR/yetty,
 *                  falls back to /tmp/yetty-<uid>.
 *   config_dir  — XDG_CONFIG_HOME-equivalent: user-editable config files
 *                  (qemu/qemu.cfg, temu/root-riscv64.cfg). Linux:
 *                  $XDG_CONFIG_HOME/yetty.
 *   assets_dir  — read-only bundled assets (shaders, fonts) shipped
 *                  alongside the executable. Resolved from the exec
 *                  path; honoured by $YETTY_ASSETS_DIR override.
 *
 * PATH_MAX is platform-specific (4096 on Linux, 1024 on macOS, 260 on
 * Windows MAX_PATH). 5×PATH_MAX ≈ 20 KiB on Linux — chunky for a heap
 * alloc, fine for a process-lifetime singleton (see below).
 */
struct yetty_yplatform_paths {
    char cache_dir_buf[PATH_MAX];
    char data_dir_buf[PATH_MAX];
    char runtime_dir_buf[PATH_MAX];
    char config_dir_buf[PATH_MAX];
    char assets_dir_buf[PATH_MAX];
};

YETTY_YRESULT_DECLARE(yetty_yplatform_paths_ptr, struct yetty_yplatform_paths *);

/*
 * Allocate and populate a fresh paths struct for this process.
 * Caller owns the returned pointer and must release it with
 * yetty_yplatform_paths_destroy. Most call sites should prefer the
 * convenience getters below (process-static singleton, no ownership).
 */
struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void);

/*
 * Free a struct returned by yetty_yplatform_paths_get_platform_paths.
 * Tolerates NULL. Returns void result for API uniformity with the
 * rest of the ycore-result family — never actually fails.
 */
struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths);

/*
 * Convenience getters. Each returns a pointer into a process-wide
 * singleton; the singleton is lazily created on first call and lives
 * for the lifetime of the process — never destroyed, never reallocated.
 * Safe to call from any thread (read-only after first init); first
 * caller wins on the rare init race. Returns "" on internal failure
 * (OOM during the very first init) rather than NULL so callers can
 * pass the result straight into snprintf("%s/...").
 *
 * New code that needs *all* paths or wants explicit ownership should
 * call yetty_yplatform_paths_get_platform_paths directly.
 */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);
const char *yetty_yplatform_get_config_dir(void);
const char *yetty_yplatform_get_assets_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PATHS_H */
