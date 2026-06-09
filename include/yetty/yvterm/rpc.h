/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YVTERM_RPC_H
#define YETTY_YCLASSGEN_YVTERM_RPC_H

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_create(struct yetty_yclass_ctx *ctx);

/* Installs this module's yclass-RPC server-side discovery hooks
 * (accessor lookup feeding yetty_yclass_by_name; skel lookup
 * feeding RPC dispatch when the module exposes wire methods).
 * Call once when the yclass RPC / remote-object server is brought
 * up; idempotent, so repeated calls are no-ops. Replaces the
 * former load-time __attribute__((constructor)) installer. */
struct yetty_ycore_void_result yetty_yvterm_register(void);

#endif
