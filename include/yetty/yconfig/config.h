#ifndef YETTY_YCONFIG_CONFIG_H
#define YETTY_YCONFIG_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/* Platform-specific paths */
struct yetty_yplatform_paths {
    const char *shaders_dir;
    const char *fonts_dir;
    const char *runtime_dir;
    const char *bin_dir;
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
    struct yetty_ycore_void_result (*set_string)(struct yetty_yconfig_config *self, const char *path,
                                                 const char *value);

    /* Get sub-config at path (returns NULL if not found, no ownership transfer) */
    struct yetty_yconfig_config *(*get_node)(const struct yetty_yconfig_config *self, const char *path);

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
#define YETTY_YCONFIG_KEY_DEBUG_DAMAGE_RECTS "debug/damage-rects"
#define YETTY_YCONFIG_KEY_FONT_FAMILY "font/family"
#define YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD "terminal/text-layer/font/render-method"
#define YETTY_YCONFIG_KEY_RPC_HOST "rpc/host"
#define YETTY_YCONFIG_KEY_RPC_PORT "rpc/port"
#define YETTY_YCONFIG_KEY_RPC_SOCKET_PATH "rpc/socket-path"
#define YETTY_YCONFIG_KEY_TEMU "temu"
#define YETTY_YCONFIG_KEY_QEMU "qemu"
#define YETTY_YCONFIG_KEY_SSH "ssh/enabled"

/* Create config */
struct yetty_yconfig_result yetty_yconfig_create(int argc, char *argv[],
                                                 const struct yetty_yplatform_paths *paths);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCONFIG_CONFIG_H */
