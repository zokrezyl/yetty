/* Linux platform paths - XDG directories */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#include <yetty/yplatform/paths.h>

static char cache_dir_buf[512];
static char data_dir_buf[512];
static char runtime_dir_buf[512];
static char config_dir_buf[512];
static char assets_dir_buf[PATH_MAX];

// TODO: unify the platform-paths.c for all platform into one
const char *yetty_yplatform_get_cache_dir(void)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg) {
        snprintf(cache_dir_buf, sizeof(cache_dir_buf), "%s/yetty", xdg);
        return cache_dir_buf;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(cache_dir_buf, sizeof(cache_dir_buf), "%s/.cache/yetty", home);
        return cache_dir_buf;
    }

    return "/tmp/yetty";
}

const char *yetty_yplatform_get_data_dir(void)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        snprintf(data_dir_buf, sizeof(data_dir_buf), "%s/yetty", xdg);
        return data_dir_buf;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(data_dir_buf, sizeof(data_dir_buf), "%s/.local/share/yetty", home);
        return data_dir_buf;
    }

    return "/tmp/yetty";
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        snprintf(runtime_dir_buf, sizeof(runtime_dir_buf), "%s/yetty", xdg);
        return runtime_dir_buf;
    }

    snprintf(runtime_dir_buf, sizeof(runtime_dir_buf), "/tmp/yetty-%d", getuid());
    return runtime_dir_buf;
}

const char *yetty_yplatform_get_config_dir(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(config_dir_buf, sizeof(config_dir_buf), "%s/yetty", xdg);
        return config_dir_buf;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(config_dir_buf, sizeof(config_dir_buf), "%s/.config/yetty", home);
        return config_dir_buf;
    }

    return "/tmp/yetty";
}

const char *yetty_yplatform_get_assets_dir(void)
{
    /* First check YETTY_ASSETS_DIR environment variable */
    const char *env = getenv("YETTY_ASSETS_DIR");
    if (env) {
        snprintf(assets_dir_buf, sizeof(assets_dir_buf), "%s", env);
        return assets_dir_buf;
    }

    /* Get directory containing the executable */
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(assets_dir_buf, sizeof(assets_dir_buf), "%s/assets", dir);
        return assets_dir_buf;
    }

    /* Fallback to current directory */
    return "./assets";
}

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths()
{

    struct yetty_yplatform_paths_ptr_result res;
    return res;
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{

    struct yetty_ycore_void_result res;
    return res;
}
