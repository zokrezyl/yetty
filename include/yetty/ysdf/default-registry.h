/* default-registry.h — fully wired ydraw drawable-list registry.
 *
 * Builds a ydraw-list registry with every wire tier registered: control
 * cmds, the ysdf SDF default handler, the FONT / TEXT_DRAWABLE_LIST
 * entry handlers, and the complex record handler. Declared here (not
 * in ydraw-list) because ydraw-list must stay free of the ysdf handler
 * dependency; ysdf is the lowest module that sees both.
 */
#pragma once

#include <yetty/ydraw-list/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list_registry_ptr_result
yetty_ydraw_drawable_list_registry_create_default(void);

#ifdef __cplusplus
}
#endif
