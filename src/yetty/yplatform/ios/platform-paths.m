/* iOS platform paths - app container directories */

#import <Foundation/Foundation.h>
#include <stdio.h>

static char cache_dir_buf[512];
static char config_dir_buf[512];
static char bundle_dir_buf[512];
static char data_dir_buf[512];

// TODO: unify the platform-paths.c for all platform into one
// provide the primitives like get env as platform function

/* On iOS, ~/Library/Application Support and ~/Library/Caches/<custom-subdir>
 * are not auto-created. Plain mkdir(2) returns EPERM on the standard
 * NSApplicationSupportDirectory path; only NSFileManager is allowed to
 * materialize it. Call this on every path getter so callers can use plain
 * POSIX I/O once they hold the path. */
static void ensure_dir(const char *path) {
  NSString *p = [NSString stringWithUTF8String:path];
  NSError *err = nil;
  BOOL ok = [[NSFileManager defaultManager] createDirectoryAtPath:p
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:&err];
  fprintf(stderr, "ensure_dir: '%s' -> ok=%d err=%s\n", path, ok,
          err ? [[err localizedDescription] UTF8String] : "(none)");
}

const char *yetty_yplatform_get_bundle_dir(void) {
  NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
  if (bundlePath) {
    snprintf(bundle_dir_buf, sizeof(bundle_dir_buf), "%s",
             [bundlePath UTF8String]);
    return bundle_dir_buf;
  }
  return ".";
}

const char *yetty_yplatform_get_cache_dir(void) {
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                                       NSUserDomainMask, YES);
  if (paths.count > 0) {
    snprintf(cache_dir_buf, sizeof(cache_dir_buf), "%s/yetty",
             [paths[0] UTF8String]);
    ensure_dir(cache_dir_buf);
    return cache_dir_buf;
  }
  return "/tmp/yetty";
}

const char *yetty_yplatform_get_runtime_dir(void) {
  return yetty_yplatform_get_cache_dir();
}

/* tvOS forbids writing to Library/Application Support entirely (sandbox
 * denies even NSFileManager with "You don't have permission..."). Apps
 * may only write under Library/Caches/ and tmp/ — persistent data is
 * supposed to go to iCloud. We don't need iCloud, just a place to extract
 * incbin assets, so route both config and data dirs to Caches/ on tvOS.
 * iOS has no such restriction; keep Application Support there.
 *
 * Note: Caches/ is the right choice on tvOS in any case — apps can be
 * evicted at any time, so Caches/ is the only writable persistent-ish
 * storage. iOS Caches/ is also fair game (system may purge under pressure
 * but that's on extraction-needed flag handling). */
#if YETTY_TVOS
#define YETTY_PERSIST_SEARCH_PATH NSCachesDirectory
#else
#define YETTY_PERSIST_SEARCH_PATH NSApplicationSupportDirectory
#endif

const char *yetty_yplatform_get_config_dir(void) {
  NSArray *paths = NSSearchPathForDirectoriesInDomains(
      YETTY_PERSIST_SEARCH_PATH, NSUserDomainMask, YES);
  if (paths.count > 0) {
    snprintf(config_dir_buf, sizeof(config_dir_buf), "%s/yetty",
             [paths[0] UTF8String]);
    ensure_dir(config_dir_buf);
    return config_dir_buf;
  }
  return "/tmp/yetty";
}

const char *yetty_yplatform_get_assets_dir(void) {
  /* iOS assets are in the app bundle */
  return yetty_yplatform_get_bundle_dir();
}

/* Persistent app-private data lives under Application Support — that's
 * where extract-assets.c writes the extracted incbin payload (yemu/, etc.)
 * and where shared/tinyemu-pty.c expects to find them. */
const char *yetty_yplatform_get_data_dir(void) {
  NSArray *paths = NSSearchPathForDirectoriesInDomains(
      YETTY_PERSIST_SEARCH_PATH, NSUserDomainMask, YES);
  if (paths.count > 0) {
    snprintf(data_dir_buf, sizeof(data_dir_buf), "%s/yetty",
             [paths[0] UTF8String]);
    ensure_dir(data_dir_buf);
    return data_dir_buf;
  }
  return "/tmp/yetty";
}
