#ifndef YETTY_YPLATFORM_PATHS_H
#define YETTY_YPLATFORM_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <yetty/ycore/result.h>

struct yetty_yplatform_paths {
    char cache_dir_buf[PATH_MAX];
    char data_dir_buf[PATH_MAX];
    char runtime_dir_buf[PATH_MAX];
    char config_dir_buf[PATH_MAX];
    char assets_dir_buf[PATH_MAX];
};

YETTY_YRESULT_DECLARE(yetty_yplatform_paths_ptr, struct yetty_platform_paths *);

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths();
struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths);

#ifdef __cplusplus
}
#endif

#endif // YETTY_YPLATFORM_PATHS_H
