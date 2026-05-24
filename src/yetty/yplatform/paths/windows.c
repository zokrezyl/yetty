/*
 * Windows platform paths — AppData directories.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h. Windows directory conventions:
 *   cache_dir   → %LOCALAPPDATA%\yetty\cache
 *   data_dir    → %LOCALAPPDATA%\yetty\data
 *   config_dir  → %APPDATA%\yetty             (roaming)
 *   runtime_dir → %TEMP%\yetty
 *   assets_dir  → directory holding yetty.exe + "\\assets",
 *                  $YETTY_ASSETS_DIR override
 *
 * All hardcoded fallbacks land in C:\temp\yetty when env-vars are
 * absent — same as the previous per-getter behaviour.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <yetty/yplatform/paths.h>

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr, "OOM allocating yetty_yplatform_paths");
    }

    const char *localAppData = getenv("LOCALAPPDATA");
    if (localAppData && *localAppData) {
        snprintf(p->cache_dir_buf, sizeof(p->cache_dir_buf), "%s\\yetty\\cache", localAppData);
        snprintf(p->data_dir_buf, sizeof(p->data_dir_buf), "%s\\yetty\\data", localAppData);
    } else {
        snprintf(p->cache_dir_buf, sizeof(p->cache_dir_buf), "C:\\temp\\yetty");
        snprintf(p->data_dir_buf, sizeof(p->data_dir_buf), "C:\\temp\\yetty");
    }

    const char *temp = getenv("TEMP");
    if (temp && *temp) {
        snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "%s\\yetty", temp);
    } else {
        snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "C:\\temp\\yetty");
    }

    const char *appData = getenv("APPDATA");
    if (appData && *appData) {
        snprintf(p->config_dir_buf, sizeof(p->config_dir_buf), "%s\\yetty", appData);
    } else {
        snprintf(p->config_dir_buf, sizeof(p->config_dir_buf), "C:\\temp\\yetty");
    }

    /* assets_dir = $YETTY_ASSETS_DIR || dirname(GetModuleFileName())\assets */
    const char *env = getenv("YETTY_ASSETS_DIR");
    if (env && *env) {
        snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s", env);
    } else {
        char exe_path[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        if (n > 0 && n < sizeof(exe_path)) {
            char *slash = strrchr(exe_path, '\\');
            if (slash) {
                *slash = '\0';
                snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s\\assets", exe_path);
            } else {
                snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), ".\\assets");
            }
        } else {
            snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), ".\\assets");
        }
    }

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
