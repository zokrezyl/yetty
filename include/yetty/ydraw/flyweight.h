// YPaint Flyweight - creates configured flyweight registry with all handlers
#pragma once

#include <yetty/ydraw-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create flyweight registry with all handlers registered (SDF, yplot, etc.)
struct yetty_ydraw_core_flyweight_registry_ptr_result yetty_ydraw_flyweight_create(void);

#ifdef __cplusplus
}
#endif
