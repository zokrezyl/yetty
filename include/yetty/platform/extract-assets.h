#ifndef YETTY_YPLATFORM_EXTRACT_ASSETS_H
#define YETTY_YPLATFORM_EXTRACT_ASSETS_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig;

/* Extract embedded assets to cache directory */
struct yetty_ycore_void_result yetty_platform_extract_assets(struct yetty_yconfig *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_EXTRACT_ASSETS_H */
