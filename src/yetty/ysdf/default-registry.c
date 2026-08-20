/* default-registry.c — configured drawable-list registry for ALL primitives.
 *
 * Type-id space (see ydraw-list/cmds.h for the canonical layout):
 *   1. CMD tier handler                        types [0x00000000, 0x0000FFFF]
 *   2. SDF default handler                     types [0x10000000, 0x1FFFFFFF]
 *   3. FONT drawable-list entry handler        type   0x40000001
 *   4. TEXT_DRAWABLE_LIST entry handler        type   0x40000002
 *   5. Complex prim handler (factory-based)    types [0x80000000, 0xFFFFFFFF]
 *
 * All return base ops (size, aabb) for buffer iteration.
 */

#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ysdf/default-registry.h>
#include <yetty/ysdf/handler.h>

struct yetty_ydraw_drawable_list_registry_ptr_result
yetty_ydraw_drawable_list_registry_create_default(void)
{
    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res =
        yetty_ydraw_drawable_list_registry_create();
    if (YETTY_IS_ERR(registry_res)) {
        return registry_res;
    }

    struct yetty_ydraw_drawable_list_registry *registry = registry_res.value;

    /* Default handler for SDF primitives (tier [0x10000000, 0x1FFFFFFF]). */
    yetty_ydraw_drawable_list_registry_set_default(registry, yetty_ysdf_handler);

    /* Cmd tier — control commands at the bottom of the id space. */
    struct yetty_ycore_void_result cmd_res = yetty_ydraw_drawable_list_registry_add(
        registry, YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END, yetty_ydraw_cmd_handler);
    if (YETTY_IS_ERR(cmd_res)) {
        yetty_ydraw_drawable_list_registry_destroy(registry);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "drawable_list_registry_create_default: register CMD handler", cmd_res);
    }

    /* Drawable-list entry prims — one handler per type id. */
    struct yetty_ycore_void_result font_res = yetty_ydraw_drawable_list_registry_add(
        registry, YETTY_YDRAW_RESOURCE_FONT, YETTY_YDRAW_RESOURCE_FONT,
        yetty_ydraw_font_resource_handler);
    if (YETTY_IS_ERR(font_res)) {
        yetty_ydraw_drawable_list_registry_destroy(registry);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "drawable_list_registry_create_default: register FONT handler", font_res);
    }
    struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_registry_add(
        registry, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST,
        yetty_ydraw_text_drawable_list_handler);
    if (YETTY_IS_ERR(text_res)) {
        yetty_ydraw_drawable_list_registry_destroy(registry);
        return YETTY_ERR(
            yetty_ydraw_drawable_list_registry_ptr,
            "drawable_list_registry_create_default: register TEXT_DRAWABLE_LIST handler", text_res);
    }

    /* Complex prim handler (types >= 0x80000000). */
    struct yetty_ycore_void_result complex_res =
        yetty_ydraw_drawable_list_registry_add(registry, YETTY_YDRAW_COMPLEX_TYPE_BASE,
                                               0xFFFFFFFF, yetty_ydraw_complex_record_handler);
    if (YETTY_IS_ERR(complex_res)) {
        yetty_ydraw_drawable_list_registry_destroy(registry);
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr,
                         "drawable_list_registry_create_default: register complex handler",
                         complex_res);
    }

    return registry_res;
}
