#ifndef YETTY_YCONFIG_CONFIG_H
#define YETTY_YCONFIG_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/* yconfig-resolved resource paths. Distinct from the canonical
 * XDG-style locations in <yetty/yplatform/paths.h> (cache/data/config/...);
 * this struct carries the *config-applied* shader/font/bin overrides
 * that a yetty exec may receive via yconfig_create_with_paths. */
struct yetty_yconfig_paths {
    const char *shaders_dir;
    const char *fonts_dir;
    const char *runtime_dir;
    const char *bin_dir;
    const char *config_dir;
    const char *cache_dir;
    const char *data_dir;
};

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yconfig, struct yetty_yconfig_config *);

/* Resolved shell argv for execvp.
 *
 * Caller-owned buffer; filled by ops->get_shell_argv. argv[] points into buf.
 * argv is NULL-terminated and ready to pass to execvp. */
#define YETTY_YCONFIG_SHELL_ARGV_MAX 64
#define YETTY_YCONFIG_SHELL_BUF_SIZE 2048
struct yetty_yconfig_shell_argv {
    char *argv[YETTY_YCONFIG_SHELL_ARGV_MAX];
    char buf[YETTY_YCONFIG_SHELL_BUF_SIZE];
    int argc;
};

/* Config ops */
struct yetty_yconfig_config_ops {
    void (*destroy)(struct yetty_yconfig_config *self);

    /* Get string value by slash path */
    const char *(*get_string)(const struct yetty_yconfig_config *self, const char *path,
                              const char *default_value);
    /* Get int value by slash path */
    int (*get_int)(const struct yetty_yconfig_config *self, const char *path, int default_value);
    /* Get bool value by slash path */
    int (*get_bool)(const struct yetty_yconfig_config *self, const char *path, int default_value);

    /* Check if key exists */
    int (*has)(const struct yetty_yconfig_config *self, const char *path);

    /* Set string value */
    struct yetty_ycore_void_result (*set_string)(struct yetty_yconfig_config *self,
                                                 const char *path, const char *value);

    /* Get sub-config at path (returns NULL if not found, no ownership transfer) */
    struct yetty_yconfig_config *(*get_node)(const struct yetty_yconfig_config *self,
                                             const char *path);

    /* Sequence accessors. The YAML loader stores sequence items as
     * scalar children with numeric keys ("0", "1", …). get_array_count
     * returns the number of items at `path` (or 0 if `path` is missing /
     * not a sequence). get_array_item returns the scalar text of item
     * `index`, or `default_value` if absent. */
    int (*get_array_count)(const struct yetty_yconfig_config *self, const char *path);
    const char *(*get_array_item)(const struct yetty_yconfig_config *self, const char *path,
                                  int index, const char *default_value);

    /* Generic child iteration for mapping-style nodes. get_child_count
     * returns the number of direct children at `path` (0 for leaves /
     * missing paths). get_child_key returns the key string of the i-th
     * child, or NULL if `index` is out of range. Pass NULL or "" for
     * `path` to address the root. The returned key pointer is owned by
     * the config; do not free, do not assume it survives a config
     * mutation. */
    int (*get_child_count)(const struct yetty_yconfig_config *self, const char *path);
    const char *(*get_child_key)(const struct yetty_yconfig_config *self, const char *path,
                                 int index);

    /* Resolve shell argv for execvp.
     *
     * If shell/command is set (via -e), tokenize it into argv (no shell
     * wrapper). Otherwise argv is { resolved_shell, NULL } where the shell
     * is $SHELL > shell/default > /bin/bash. */
    struct yetty_ycore_void_result (*get_shell_argv)(const struct yetty_yconfig_config *self,
                                                     struct yetty_yconfig_shell_argv *out);

    /* Legacy accessors */
    int (*use_damage_tracking)(const struct yetty_yconfig_config *self);
    int (*show_fps)(const struct yetty_yconfig_config *self);
    int (*debug_damage_rects)(const struct yetty_yconfig_config *self);
    uint32_t (*scrollback_lines)(const struct yetty_yconfig_config *self);
    const char *(*font_family)(const struct yetty_yconfig_config *self);
};

/* Config base */
struct yetty_yconfig_config {
    const struct yetty_yconfig_config_ops *ops;
};

/* Common config keys */
#define YETTY_YCONFIG_KEY_PLUGINS_PATH "plugins/path"
#define YETTY_YCONFIG_KEY_RENDERING_DAMAGE_TRACKING "rendering/damage-tracking"
#define YETTY_YCONFIG_KEY_RENDERING_SHOW_FPS "rendering/show-fps"
#define YETTY_YCONFIG_KEY_SCROLLBACK_LINES "scrollback/lines"
/* Hot window of the tiered scroll buffer: the newest N lines keep their
 * anchored figures fully materialized (decoded pixels, GPU binders, video
 * decoders). Lines aging past it keep only their creating wire envelopes;
 * the runtime is rebuilt on demand when scrolled back into view. */
#define YETTY_YCONFIG_KEY_SCROLLBACK_HOT_LINES "scrollback/hot-lines"
/* Warm-tier byte budget: lz4-compressed scrollback segments held in RAM
 * before spilling to the session-scoped cold file. 0 = built-in default. */
#define YETTY_YCONFIG_KEY_SCROLLBACK_WARM_BYTES "scrollback/warm-bytes"
/* Cold-tier cap: maximum bytes of the session-scoped spill file. 0 =
 * unlimited (scrollback depth is bounded only by disk). */
#define YETTY_YCONFIG_KEY_SCROLLBACK_FILE_MAX_BYTES "scrollback/file-max-bytes"
#define YETTY_YCONFIG_KEY_DEBUG_DAMAGE_RECTS "debug/damage-rects"
#define YETTY_YCONFIG_KEY_FONT_FAMILY "font/family"
#define YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD "terminal/text-layer/font/render-method"
#define YETTY_YCONFIG_KEY_RPC_HOST "rpc/host"
#define YETTY_YCONFIG_KEY_RPC_PORT "rpc/port"
#define YETTY_YCONFIG_KEY_RPC_SOCKET_PATH "rpc/socket-path"
/* ymux: run as the headless, GPU-less terminal server. Value = session name
 * ("" / "default" when the flag is given bare). Presence of this key is what
 * flips the platform bootstrap into the windowless server path. */
#define YETTY_YCONFIG_KEY_YMUX_SERVER "ymux/server"
/* ymux: TCP control port the headless server binds (msgpack-RPC). */
#define YETTY_YCONFIG_KEY_YMUX_PORT "ymux/port"
/* ymux: address the headless server binds. Default 127.0.0.1 (local only); set
 * to 0.0.0.0 to accept remote clients. */
#define YETTY_YCONFIG_KEY_YMUX_BIND "ymux/bind"
/* ymux: attach this (windowed) yetty to a running server as a client. Value =
 * "HOST:PORT" (bare "" / missing PORT → 127.0.0.1:9998). The terminal's PTY is
 * bridged to the server pane instead of forking a local shell. */
#define YETTY_YCONFIG_KEY_YMUX_ATTACH "ymux/attach"
/* ymux: tmux-style auto mode — connect to the local server or spawn a detached
 * one, then attach. Set by --ymux. Value "true" when enabled. */
#define YETTY_YCONFIG_KEY_YMUX_AUTO "ymux/auto"
#define YETTY_YCONFIG_KEY_TEMU "temu"
#define YETTY_YCONFIG_KEY_QEMU "qemu"
#define YETTY_YCONFIG_KEY_SSH "ssh/enabled"
#define YETTY_YCONFIG_KEY_TELNET "telnet/enabled"
#define YETTY_YCONFIG_KEY_TELNET_HOST "telnet/host"
#define YETTY_YCONFIG_KEY_TELNET_PORT "telnet/port"
#define YETTY_YCONFIG_KEY_WEBSOCKET "websocket/enabled"
#define YETTY_YCONFIG_KEY_WEBSOCKET_URL "websocket/url"
#define YETTY_YCONFIG_KEY_NET_RELAY "net/relay"

/* WebGPU device limits requested at device creation (ywebgpu/limits.c).
 * Every requested value is clamped to the adapter-supported maximum at
 * request time, so oversized values degrade gracefully instead of
 * rejecting the device request. Values must be positive integers; a
 * missing (or non-positive) key falls back to the built-in default.
 * The two `-mb` keys are byte sizes expressed in MiB. */
#define YETTY_YCONFIG_KEY_GPU_MAX_TEXTURE_DIMENSION_2D "gpu/max-texture-dimension-2d"
#define YETTY_YCONFIG_KEY_GPU_MAX_STORAGE_BINDING_SIZE_MB "gpu/max-storage-binding-size-mb"
#define YETTY_YCONFIG_KEY_GPU_MAX_BUFFER_SIZE_MB "gpu/max-buffer-size-mb"
#define YETTY_YCONFIG_KEY_GPU_MAX_STORAGE_BUFFERS_PER_SHADER_STAGE                                 \
    "gpu/max-storage-buffers-per-shader-stage"

/* Logging / tracing (ytrace) control. `log/floor` is the severity floor
 * (trace|debug|info|warn|error|off); `log/all` is the master enable. The
 * `log/levels`, `log/files`, `log/functions` subtrees hold per-level /
 * per-file / per-function on|off rules keyed by name. */
#define YETTY_YCONFIG_KEY_LOG_FLOOR "log/floor"
#define YETTY_YCONFIG_KEY_LOG_ALL "log/all"
#define YETTY_YCONFIG_KEY_LOG_LEVELS "log/levels"
#define YETTY_YCONFIG_KEY_LOG_FILES "log/files"
#define YETTY_YCONFIG_KEY_LOG_FUNCTIONS "log/functions"

/* Create config. Resolves the platform directory layout itself via the
 * yplatform paths abstraction (yetty_yplatform_paths_create) — callers no
 * longer pass directories in. */
struct yetty_yconfig_result yetty_yconfig_create(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCONFIG_CONFIG_H */
