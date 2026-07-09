#ifndef YETTY_YMUX_SERVER_H
#define YETTY_YMUX_SERVER_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/*
 * Run the headless, GPU-less ymux terminal server.
 *
 * This is the second process in the tmux-style split: it owns the event
 * loop, a PTY factory, one or more ymux panes (the GPU-free terminal
 * models) and a msgpack-RPC control channel. No window, no WebGPU
 * instance, no renderer — the bootstrap branches here BEFORE any display
 * server is touched.
 *
 * Blocks pumping the loop until an explicit shutdown verb (or a loop
 * error). `config` is borrowed: the caller (the platform bootstrap)
 * owns it and destroys it after this returns.
 */
struct yetty_ycore_void_result yetty_ymux_server_run(struct yetty_yconfig_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_SERVER_H */
