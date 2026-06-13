/*
 * Android platform paths — app-internal storage.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h. Android isolates apps under
 * /data/data/<package>/, with subdirs for cache (eligible for
 * eviction by the OS) and files (persistent). yetty doesn't have
 * a real "config" location distinct from data; we point at the
 * top of files/.
 *
 * The package isn't hardcoded: each flavor APK (com.yetty.terminal,
 * com.yetty.greeter, ...) runs in its own sandbox, so the root is derived
 * at runtime from the process/package name. A hardcoded package would make
 * one app write into another app's sandbox (permission denied).
 *
 * assets_dir is a no-op ("/" placeholder) on Android — assets ship
 * inside the APK and are read via the NDK AAssetManager, not via
 * filesystem paths. Tools that need raw filesystem assets should
 * extract them to data_dir at startup (extract-assets/default.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/paths.h>

/* Used only if the process/package name can't be read. */
#define ANDROID_FALLBACK_PACKAGE "com.yetty.terminal"

/* Resolve the app sandbox root (/data/data/<package>) from the running
 * process. On Android the main process name equals the package name and is
 * the first NUL-delimited token of /proc/self/cmdline. A private process
 * appends a ":suffix" that we strip to reach the package sandbox. */
static void android_app_root(char *out, size_t out_size)
{
    char name[128] = {0};
    FILE *cmdline = fopen("/proc/self/cmdline", "r");
    if (cmdline) {
        size_t read_len = fread(name, 1, sizeof(name) - 1, cmdline);
        name[read_len] = '\0';
        fclose(cmdline);
    }
    char *suffix = strchr(name, ':');
    if (suffix) {
        *suffix = '\0';
    }
    const char *package = name[0] ? name : ANDROID_FALLBACK_PACKAGE;
    snprintf(out, out_size, "/data/data/%s", package);
}

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr, "OOM allocating yetty_yplatform_paths");
    }

    char root[160];
    android_app_root(root, sizeof(root));

    snprintf(p->cache_dir_buf, sizeof(p->cache_dir_buf), "%s/cache", root);
    snprintf(p->data_dir_buf, sizeof(p->data_dir_buf), "%s/files/data", root);
    snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "%s/cache", root);
    snprintf(p->config_dir_buf, sizeof(p->config_dir_buf), "%s/files", root);
    strncpy(p->assets_dir_buf, "/", sizeof(p->assets_dir_buf) - 1);

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
