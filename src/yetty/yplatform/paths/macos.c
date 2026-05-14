/*
 * macOS platform paths — ~/Library directories.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h. macOS doesn't have XDG, so the
 * resolution rules are Apple's standard locations:
 *   cache_dir  → ~/Library/Caches/yetty
 *   data_dir   → ~/Library/Application Support/yetty
 *   config_dir → ~/Library/Application Support/yetty (same as data)
 *   runtime_dir → /tmp/yetty-<uid>
 *   assets_dir → next to the executable, "../Resources/assets" inside
 *                a .app bundle, else $YETTY_ASSETS_DIR or ./assets
 *
 * Path resolution intentionally mirrors what the previous per-getter
 * impls returned, modulo $YETTY_ASSETS_DIR honour and assets_dir which
 * the prior code didn't fill at all.
 */

#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <yetty/yplatform/paths.h>

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr,
                         "OOM allocating yetty_yplatform_paths");
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(p->cache_dir_buf,  sizeof(p->cache_dir_buf),
                 "%s/Library/Caches/yetty", home);
        snprintf(p->data_dir_buf,   sizeof(p->data_dir_buf),
                 "%s/Library/Application Support/yetty", home);
        snprintf(p->config_dir_buf, sizeof(p->config_dir_buf),
                 "%s/Library/Application Support/yetty", home);
    } else {
        snprintf(p->cache_dir_buf,  sizeof(p->cache_dir_buf),  "/tmp/yetty");
        snprintf(p->data_dir_buf,   sizeof(p->data_dir_buf),   "/tmp/yetty");
        snprintf(p->config_dir_buf, sizeof(p->config_dir_buf), "/tmp/yetty");
    }

    snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf),
             "/tmp/yetty-%d", (int)getuid());

    /* assets_dir = $YETTY_ASSETS_DIR || dirname(_NSGetExecutablePath())/assets */
    const char *env = getenv("YETTY_ASSETS_DIR");
    if (env && *env) {
        snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s", env);
    } else {
        char exe_path[PATH_MAX];
        uint32_t exe_len = (uint32_t)sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &exe_len) == 0) {
            char *slash = strrchr(exe_path, '/');
            if (slash) {
                *slash = '\0';
                snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf),
                         "%s/assets", exe_path);
            } else {
                snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf),
                         "./assets");
            }
        } else {
            snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "./assets");
        }
    }

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
