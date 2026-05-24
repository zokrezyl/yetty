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
 * assets_dir is a no-op ("/" placeholder) on Android — assets ship
 * inside the APK and are read via the NDK AAssetManager, not via
 * filesystem paths. Tools that need raw filesystem assets should
 * extract them to data_dir at startup (extract-assets/default.c).
 */

#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/paths.h>

#define ANDROID_APP_ROOT "/data/data/com.yetty.terminal"

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr, "OOM allocating yetty_yplatform_paths");
    }

    strncpy(p->cache_dir_buf, ANDROID_APP_ROOT "/cache", sizeof(p->cache_dir_buf) - 1);
    strncpy(p->data_dir_buf, ANDROID_APP_ROOT "/files/data", sizeof(p->data_dir_buf) - 1);
    strncpy(p->runtime_dir_buf, ANDROID_APP_ROOT "/cache", sizeof(p->runtime_dir_buf) - 1);
    strncpy(p->config_dir_buf, ANDROID_APP_ROOT "/files", sizeof(p->config_dir_buf) - 1);
    strncpy(p->assets_dir_buf, "/", sizeof(p->assets_dir_buf) - 1);

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
