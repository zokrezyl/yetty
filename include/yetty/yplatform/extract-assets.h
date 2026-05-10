#ifndef YETTY_YPLATFORM_EXTRACT_ASSETS_H
#define YETTY_YPLATFORM_EXTRACT_ASSETS_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Extract embedded assets to data/config directories. Resolves the target
 * dirs via the platform path getters (yetty_yplatform_get_data_dir() etc.),
 * so it must run AFTER those are valid but BEFORE the config is loaded —
 * on first launch the bundled config.yaml is what the loader reads. */
struct yetty_ycore_void_result yetty_platform_extract_assets(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_EXTRACT_ASSETS_H */
