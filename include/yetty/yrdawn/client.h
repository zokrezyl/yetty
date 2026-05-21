/*
 * yrdawn/client.h — pure-C side of the WebGPU-over-OSC bridge.
 *
 * Links into the remote wasm process. Owns a yface for outbound OSC
 * envelopes (CMD/BULK/HELLO/BYE) and another for inbound (REPLY/EVENT/
 * HELLO_ACK/BULK/ERROR). Handles are allocated client-side so every
 * wgpu* call can return immediately; the server creates the matching
 * WGPU* object lazily when it processes the corresponding CMD.
 *
 * No file-scope state — every wasm process holds at most one client,
 * created at startup and threaded through the codegen-emitted webgpu.h
 * shim. The shim caches the active client per process by whatever
 * mechanism the runtime provides (emscripten module state, thread-
 * local, etc.); this header doesn't pick one.
 */
#ifndef YETTY_YRDAWN_CLIENT_H
#define YETTY_YRDAWN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrdawn_client;

YETTY_YRESULT_DECLARE(yetty_yrdawn_client_ptr, struct yetty_yrdawn_client *);

/* Fires when a REPLY arrives matching a CMD that was sent _async. The
 * body is the method-specific reply payload (codegen-aware decode);
 * status is enum yetty_yrdawn_reply_status. The slot is freed before
 * the callback runs. */
typedef void (*yetty_yrdawn_reply_cb)(void *user, uint32_t status, uint32_t method_id,
                                     const uint8_t *body, size_t body_len);

/* Fires when an EVENT arrives (device-lost, uncaptured-error, logging).
 * kind is enum yetty_yrdawn_event_kind; body is the kind-specific tail. */
typedef void (*yetty_yrdawn_event_cb)(void *user, uint32_t kind, uint64_t device_handle,
                                     const uint8_t *body, size_t body_len);

/* Fires when a SC_KEY input frame arrives. kind is enum
 * yetty_yrdawn_input_key_kind; key/mods/codepoint match the wire struct. */
typedef void (*yetty_yrdawn_input_key_cb)(void *user, uint32_t kind, int32_t key,
                                         int32_t mods, uint32_t codepoint);

/* Fires when the layer's pane pixel size changes. */
typedef void (*yetty_yrdawn_input_resize_cb)(void *user, float width, float height);

/* Construct a client bound to (in_fd, out_fd). For the typical wasm
 * deployment, in_fd = STDIN_FILENO and out_fd = STDOUT_FILENO. Sets
 * in_fd non-blocking. Sends no HELLO — call _send_hello to bootstrap. */
struct yetty_yrdawn_client_ptr_result yetty_yrdawn_client_create(int in_fd, int out_fd);

struct yetty_ycore_void_result yetty_yrdawn_client_destroy(struct yetty_yrdawn_client *c);

void yetty_yrdawn_client_set_event_cb(struct yetty_yrdawn_client *c,
                                     yetty_yrdawn_event_cb cb, void *user);

void yetty_yrdawn_client_set_input_key_cb(struct yetty_yrdawn_client *c,
                                         yetty_yrdawn_input_key_cb cb, void *user);

void yetty_yrdawn_client_set_input_resize_cb(struct yetty_yrdawn_client *c,
                                            yetty_yrdawn_input_resize_cb cb, void *user);

/* Emit a HELLO. Connected flag flips on the next _pump() call that
 * sees a HELLO_ACK with status OK. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_hello(struct yetty_yrdawn_client *c);

int yetty_yrdawn_client_connected(const struct yetty_yrdawn_client *c);

/* Monotonic u64 handle allocator. Never returns YETTY_YRDAWN_HANDLE_NULL.
 * Wraparound is not anticipated within a session — a 64-bit counter at
 * 10M handles/sec lasts ~58000 years. */
uint64_t yetty_yrdawn_client_alloc_handle(struct yetty_yrdawn_client *c);

/* Send a CMD whose entrypoint has no callback. body is the codegen-
 * emitted method-specific args. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_sync(
    struct yetty_yrdawn_client *c, uint32_t method_id,
    const void *body, size_t body_len);

/* Send a CMD whose entrypoint has a callback. Allocates a non-zero
 * req_id, registers (cb, user), writes the frame. cb fires from a
 * later _pump() call when the matching REPLY arrives. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_async(
    struct yetty_yrdawn_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    yetty_yrdawn_reply_cb cb, void *user);

/* Send a CMD with a fixed-size return value, wait for the matching
 * REPLY, copy its payload into *out (caller-supplied buffer, must hold
 * at least `out_size` bytes). Used by codegen wrappers for methods
 * like wgpuBufferGetSize that need a value back from Dawn.
 *
 * Blocks via the existing pump loop — call only on the thread that
 * owns the client. `out_status` receives the REPLY status (NULL OK).
 * Returns failure on timeout or wire error. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking(
    struct yetty_yrdawn_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    void *out, size_t out_size, uint32_t *out_status);

/* Variant that allocates the reply payload buffer (variable-length).
 * On success *out_buf points to a malloc'd buffer of *out_len bytes —
 * caller frees with free(). For methods that fill an output struct
 * which itself contains variable-length inner data. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking_dyn(
    struct yetty_yrdawn_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    uint8_t **out_buf, size_t *out_len, uint32_t *out_status);

struct yetty_ycore_void_result yetty_yrdawn_client_send_bye(struct yetty_yrdawn_client *c);

/* Emit a complete BULK payload as one or more chunk frames under a
 * caller-supplied non-zero ref. The receiver reassembles by seq. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_bulk(
    struct yetty_yrdawn_client *c, uint32_t ref,
    const void *bytes, size_t len);

/* Ship a single RGBA8 frame to the local yetty's yrdawn-layer. The
 * pixels travel over the BULK channel; a follow-up CMD references
 * them. Caller-owned bytes; safe to free after return. */
struct yetty_ycore_void_result yetty_yrdawn_client_present_frame(
    struct yetty_yrdawn_client *c, uint32_t width, uint32_t height,
    const void *pixels, size_t bytes);

/* Read available bytes from in_fd, parse OSC envelopes, dispatch by
 * code (HELLO_ACK / REPLY / EVENT / BULK / ERROR), fire any matching
 * reply / event callbacks. Returns immediately when in_fd has no
 * pending bytes. */
struct yetty_ycore_void_result yetty_yrdawn_client_pump(struct yetty_yrdawn_client *c);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRDAWN_CLIENT_H */
