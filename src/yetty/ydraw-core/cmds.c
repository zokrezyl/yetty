/* cmds.c — control-cmd producer + drawable-list entry handler.
 *
 * The wire layout matches FONT/TEXT_SPAN: 8-byte FAM header (u32 type +
 * u32 payload_size) followed by `payload_size` bytes. Cmds with no
 * payload (CMD_ZERO) are an 8-byte record. */

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ycore/types.h>

/* size op — read payload_size from prim[1], add the 8-byte header. */
static struct yetty_ycore_size_result cmd_drawable_size(const uint32_t *prim)
{
    if (!prim) {
        return YETTY_ERR(yetty_ycore_size, "prim is NULL");
    }
    uint32_t payload = prim[1];
    return YETTY_OK(yetty_ycore_size, (size_t)(8u + payload));
}

/* aabb op — cmds don't render so they have an empty bounding box. The
 * canvas's grid placement skips zero-area prims. */
static struct rectangle_result cmd_drawable_aabb(const uint32_t *prim)
{
    (void)prim;
    struct yetty_ycore_rectangle r = {0};
    return YETTY_OK(rectangle, r);
}

static const struct yetty_ydraw_drawable_list_entry_ops cmd_base_ops = {
    .size = cmd_drawable_size,
    .aabb = cmd_drawable_aabb,
};

struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ydraw_cmd_handler(uint32_t drawable_type)
{
    if (drawable_type <= YETTY_YDRAW_CMD_END) {
        return YETTY_OK(yetty_ydraw_drawable_list_entry_ops_ptr, &cmd_base_ops);
    }
    return YETTY_ERR(yetty_ydraw_drawable_list_entry_ops_ptr, "not a cmd type");
}

struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_zero(struct yetty_ydraw_drawable_list *buf)
{
    /* FAM header only: type=ZERO, payload_size=0. */
    uint32_t header[2] = {YETTY_YDRAW_CMD_ZERO, 0u};
    struct yetty_ydraw_id_result r = yetty_ydraw_drawable_list_add_prim(buf, header, sizeof(header));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "add_cmd_zero: add_prim failed");
    return YETTY_OK_VOID();
}
