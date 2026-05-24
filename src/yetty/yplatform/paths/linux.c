/*
 * Linux platform paths — XDG directories.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h. Resolution order per field is
 * XDG_*_HOME env var → $HOME/.{cache,config,...}/yetty → /tmp/yetty
 * fallback (the last branch covers minimal-env service contexts where
 * neither XDG nor HOME is set).
 *
 * assets_dir is the directory holding the running executable, plus
 * "/assets". $YETTY_ASSETS_DIR overrides for dev-tree builds where
 * the exec lives in build-*-ytrace-release/ but the assets stay in
 * the source tree.
 */

#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/yplatform/paths.h>

static void fill_xdg(char *dst, size_t dst_sz, const char *xdg_var, const char *home_fallback)
{
    const char *xdg = getenv(xdg_var);
    if (xdg && *xdg) {
        snprintf(dst, dst_sz, "%s/yetty", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(dst, dst_sz, "%s/%s/yetty", home, home_fallback);
        return;
    }
    snprintf(dst, dst_sz, "/tmp/yetty");
}

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr, "OOM allocating yetty_yplatform_paths");
    }

    fill_xdg(p->cache_dir_buf, sizeof(p->cache_dir_buf), "XDG_CACHE_HOME", ".cache");
    fill_xdg(p->data_dir_buf, sizeof(p->data_dir_buf), "XDG_DATA_HOME", ".local/share");
    fill_xdg(p->config_dir_buf, sizeof(p->config_dir_buf), "XDG_CONFIG_HOME", ".config");

    /* runtime_dir has a uid-suffixed fallback (per XDG spec). */
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && *xdg_runtime) {
        snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "%s/yetty", xdg_runtime);
    } else {
        snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "/tmp/yetty-%d", (int)getuid());
    }

    /* assets_dir = $YETTY_ASSETS_DIR || dirname(/proc/self/exe)/assets ||
     * ./assets. */
    const char *env = getenv("YETTY_ASSETS_DIR");
    if (env && *env) {
        snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s", env);
    } else {
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            char *dir = dirname(exe_path);
            snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s/assets", dir);
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
