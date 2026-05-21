/*
 * server-dispatch-stub.c — link-time stand-in for server-dispatch.gen.c
 * on platforms where the real dispatcher can't be built.
 *
 * The codegen-emitted dispatcher calls every wgpu* entrypoint declared
 * in our committed webgpu.h. Mobile Dawn prebuilts (Android, iOS, tvOS)
 * ship a slimmed library that doesn't export some of those symbols
 * (`wgpu*SetResourceTable`, `wgpuSharedFenceSetLabel`, …), so linking
 * the full dispatcher there fails. The bridge server only matters on a
 * desktop host that runs remote demos; mobile yetty has no such
 * workflow. Swapping in this stub keeps the OSC handler chain intact —
 * the layer is registered, HELLO/CMD/BYE flow normally — and every CMD
 * just gets UNKNOWN_METHOD back. yterm/CMakeLists.txt picks this file
 * over the .gen.c on the affected targets.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/yrdawn/server.h>
#include <yetty/yrdawn/wire.h>

uint32_t yrdawn_server_dispatch(void *ctx, uint32_t method_id, uint32_t req_id,
                                const void *body, size_t body_len)
{
    (void)ctx; (void)method_id; (void)req_id; (void)body; (void)body_len;
    return YETTY_YRDAWN_REPLY_UNKNOWN_METHOD;
}
