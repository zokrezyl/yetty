// YPaint Flyweight - creates configured flyweight registry for ALL primitives.
//
// Three tiers, see complex-prim-types.h for the type-id ranges:
//   1. SDF default handler                   types [0x00, 0xFF]
//   2. FONT      flyweight handler           type   0x40000001
//   3. TEXT_SPAN flyweight handler           type   0x40000002
//   4. Complex prim handler (factory-based)  types [0x80000000, 0xFFFFFFFF]
//
// All return base ops (size, aabb) for buffer iteration.

#include <yetty/ypaint/flyweight.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ypaint-core/font-prim.h>
#include <yetty/ypaint-core/text-span-prim.h>
#include <yetty/ytrace.h>

struct yetty_ypaint_core_flyweight_registry_ptr_result yetty_ypaint_flyweight_create(void)
{
    struct yetty_ypaint_core_flyweight_registry_ptr_result res =
        yetty_ypaint_core_flyweight_registry_create();
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    struct yetty_ypaint_core_flyweight_registry *reg = res.value;

    // Default handler for SDF primitives (fast path, types 0-255)
    yetty_ypaint_core_flyweight_registry_set_default(reg, yetty_ysdf_handler);

    // Flyweight prims — one handler per type id, registered like SDF/complex
    yetty_ypaint_core_flyweight_registry_add(reg, YETTY_YPAINT_TYPE_FONT, YETTY_YPAINT_TYPE_FONT,
                                        yetty_ypaint_core_font_prim_handler);
    yetty_ypaint_core_flyweight_registry_add(reg, YETTY_YPAINT_TYPE_TEXT_SPAN,
                                        YETTY_YPAINT_TYPE_TEXT_SPAN,
                                        yetty_ypaint_core_text_span_prim_handler);

    // Complex prim handler (types >= 0x80000000)
    yetty_ypaint_core_flyweight_registry_add(reg, YETTY_YPAINT_COMPLEX_TYPE_BASE, 0xFFFFFFFF,
                                        yetty_ypaint_core_complex_prim_handler);

    ydebug("flyweight_create: SDF default + FONT + TEXT_SPAN + complex");
    return res;
}
