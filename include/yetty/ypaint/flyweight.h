// YPaint Flyweight - creates configured flyweight registry with all handlers
#pragma once

#include <yetty/ypaint-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create flyweight registry with all handlers registered (SDF, yplot, etc.)
struct yetty_ypaint_flyweight_registry_ptr_result yetty_ypaint_flyweight_create(void);

#ifdef __cplusplus
}
#endif
