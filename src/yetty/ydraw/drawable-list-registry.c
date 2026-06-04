// YDraw Flyweight - creates configured flyweight registry for ALL primitives.
//
// Type-id space (see ydraw-core/cmds.h for the canonical layout):
//   1. CMD tier handler                      types [0x00000000, 0x0000FFFF]
//   2. SDF default handler                   types [0x10000000, 0x1FFFFFFF]
//   3. FONT      flyweight handler           type   0x40000001
//   4. TEXT_SPAN flyweight handler           type   0x40000002
//   5. Complex prim handler (factory-based)  types [0x80000000, 0xFFFFFFFF]
//
// All return base ops (size, aabb) for buffer iteration.

#include <yetty/ydraw/drawable-list-registry.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-core/font-resource.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ydraw_drawable_list_registry_ptr_result yetty_ydraw_drawable_list_registry_create_default(void)
{
    struct yetty_ydraw_drawable_list_registry_ptr_result res = yetty_ydraw_drawable_list_registry_create();
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    struct yetty_ydraw_drawable_list_registry *reg = res.value;

    // Default handler for SDF primitives (tier [0x10000000, 0x1FFFFFFF])
    yetty_ydraw_drawable_list_registry_set_default(reg, yetty_ysdf_handler);

    // Cmd tier — control commands at the bottom of the id space.
    struct yetty_ycore_void_result r_cmd = yetty_ydraw_drawable_list_registry_add(
        reg, YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END, yetty_ydraw_cmd_handler);
    if (YETTY_IS_ERR(r_cmd)) {
        yetty_ydraw_drawable_list_registry_destroy(reg);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "flyweight_create: register CMD handler", r_cmd);
    }

    // Flyweight prims — one handler per type id, registered like SDF/complex
    struct yetty_ycore_void_result r_font = yetty_ydraw_drawable_list_registry_add(
        reg, YETTY_YDRAW_RESOURCE_FONT, YETTY_YDRAW_RESOURCE_FONT, yetty_ydraw_font_resource_handler);
    if (YETTY_IS_ERR(r_font)) {
        yetty_ydraw_drawable_list_registry_destroy(reg);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "flyweight_create: register FONT handler", r_font);
    }
    struct yetty_ycore_void_result r_ts = yetty_ydraw_drawable_list_registry_add(
        reg, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST,
        yetty_ydraw_text_drawable_list_handler);
    if (YETTY_IS_ERR(r_ts)) {
        yetty_ydraw_drawable_list_registry_destroy(reg);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "flyweight_create: register TEXT_SPAN handler", r_ts);
    }

    // Complex prim handler (types >= 0x80000000)
    struct yetty_ycore_void_result r_complex = yetty_ydraw_drawable_list_registry_add(
        reg, YETTY_YDRAW_COMPOSITE_TYPE_BASE, 0xFFFFFFFF, yetty_ydraw_composite_handler);
    if (YETTY_IS_ERR(r_complex)) {
        yetty_ydraw_drawable_list_registry_destroy(reg);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "flyweight_create: register complex handler", r_complex);
    }

    ydebug("flyweight_create: cmd + SDF default + FONT + TEXT_SPAN + complex");
    return res;
}
