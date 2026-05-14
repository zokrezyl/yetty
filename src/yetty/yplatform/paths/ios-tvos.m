/*
 * iOS / tvOS platform paths — app-container directories.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h.
 *
 * On iOS, ~/Library/Application Support and ~/Library/Caches/<subdir>
 * are not auto-created. Plain mkdir(2) returns EPERM on the standard
 * NSApplicationSupportDirectory path; only NSFileManager is allowed to
 * materialize it. We materialize all 4 dirs at struct-fill time so
 * downstream callers can use plain POSIX I/O.
 *
 * tvOS forbids writing to Library/Application Support entirely (sandbox
 * denies even NSFileManager with "You don't have permission..."). Apps
 * may only write under Library/Caches/ and tmp/ — persistent data is
 * supposed to go to iCloud. yetty doesn't need iCloud, just a place to
 * extract incbin assets, so route both config and data dirs to Caches/
 * on tvOS. iOS has no such restriction; keep Application Support there.
 *
 * assets_dir is the app bundle path — assets ship inside the .app
 * container as read-only resources.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/paths.h>

static void ensure_dir(const char *path)
{
    NSString *p = [NSString stringWithUTF8String:path];
    NSError *err = nil;
    [[NSFileManager defaultManager] createDirectoryAtPath:p
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:&err];
    if (err) {
        fprintf(stderr, "yetty_yplatform_paths: ensure_dir %s failed: %s\n",
                path, [[err localizedDescription] UTF8String]);
    }
}

#if YETTY_TVOS
#define YETTY_PERSIST_SEARCH_PATH NSCachesDirectory
#else
#define YETTY_PERSIST_SEARCH_PATH NSApplicationSupportDirectory
#endif

static void fill_search_path(char *dst, size_t dst_sz,
                             NSSearchPathDirectory dir)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(dir, NSUserDomainMask, YES);
    if (paths.count > 0) {
        snprintf(dst, dst_sz, "%s/yetty", [paths[0] UTF8String]);
        ensure_dir(dst);
    } else {
        snprintf(dst, dst_sz, "/tmp/yetty");
    }
}

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr,
                         "OOM allocating yetty_yplatform_paths");
    }

    fill_search_path(p->cache_dir_buf,  sizeof(p->cache_dir_buf),  NSCachesDirectory);
    fill_search_path(p->config_dir_buf, sizeof(p->config_dir_buf), YETTY_PERSIST_SEARCH_PATH);
    fill_search_path(p->data_dir_buf,   sizeof(p->data_dir_buf),   YETTY_PERSIST_SEARCH_PATH);

    /* runtime is a synonym for cache on iOS/tvOS — no XDG concept. */
    snprintf(p->runtime_dir_buf, sizeof(p->runtime_dir_buf), "%s", p->cache_dir_buf);

    /* assets are inside the app bundle (read-only). */
    NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
    if (bundlePath) {
        snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), "%s", [bundlePath UTF8String]);
    } else {
        snprintf(p->assets_dir_buf, sizeof(p->assets_dir_buf), ".");
    }

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
