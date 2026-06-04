// YDraw Drawable-list entry - creates configured drawable-list registry with all handlers
#pragma once

#include <yetty/ydraw-core/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create drawable-list registry with all handlers registered (SDF, yplot, etc.)
struct yetty_ydraw_drawable_list_registry_ptr_result yetty_ydraw_drawable_list_registry_create_default(void);

#ifdef __cplusplus
}
#endif
