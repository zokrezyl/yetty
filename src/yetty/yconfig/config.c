/* config.c - Configuration management using libyaml */

#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/yplatform/getopt.h>
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <yetty/yplatform/compat.h>

#define MAX_KEY_LEN 64
#define MAX_VALUE_LEN 512
#define MAX_CHILDREN 64

/* Config node - tree structure */
struct config_node {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
    int is_int;
    int int_value;
    int is_bool;
    int bool_value;
    struct config_node *children[MAX_CHILDREN];
    int child_count;
};

/* Config implementation */
struct config_impl {
    struct yetty_yconfig_config base;
    struct config_node *root;
    struct yetty_yconfig_paths paths;
    char shaders_dir[512];
    char fonts_dir[512];
    char runtime_dir[512];
    char bin_dir[512];
    char config_dir[512];
};

/* Sub-config view - lightweight wrapper pointing to a sub-node */
#define MAX_SUBCONFIGS 64
struct config_subnode {
    struct yetty_yconfig_config base;
    struct config_node *node;
};

static struct config_subnode g_subconfigs[MAX_SUBCONFIGS];
static int g_subconfig_count = 0;

/* Forward declarations */
static int load_yaml_file_with_dir(struct config_node *root, const char *path, const char *dir);
static void config_destroy(struct yetty_yconfig_config *self);
static const char *config_get_string(const struct yetty_yconfig_config *self, const char *path,
                                     const char *default_value);
static int config_get_int(const struct yetty_yconfig_config *self, const char *path,
                          int default_value);
static int config_get_bool(const struct yetty_yconfig_config *self, const char *path,
                           int default_value);
static int config_has(const struct yetty_yconfig_config *self, const char *path);
static struct yetty_ycore_void_result config_set_string(struct yetty_yconfig_config *self,
                                                        const char *path, const char *value);
static int config_use_damage_tracking(const struct yetty_yconfig_config *self);
static int config_show_fps(const struct yetty_yconfig_config *self);
static int config_debug_damage_rects(const struct yetty_yconfig_config *self);
static uint32_t config_scrollback_lines(const struct yetty_yconfig_config *self);
static const char *config_font_family(const struct yetty_yconfig_config *self);

static struct yetty_yconfig_config *config_get_node(const struct yetty_yconfig_config *self,
                                                    const char *path);
static int config_get_array_count(const struct yetty_yconfig_config *self, const char *path);
static const char *config_get_array_item(const struct yetty_yconfig_config *self, const char *path,
                                         int index, const char *default_value);
static int config_get_child_count(const struct yetty_yconfig_config *self, const char *path);
static const char *config_get_child_key(const struct yetty_yconfig_config *self, const char *path,
                                        int index);
static struct yetty_ycore_void_result config_get_shell_argv(const struct yetty_yconfig_config *self,
                                                            struct yetty_yconfig_shell_argv *out);

static const struct yetty_yconfig_config_ops config_ops = {
    .destroy = config_destroy,
    .get_string = config_get_string,
    .get_int = config_get_int,
    .get_bool = config_get_bool,
    .has = config_has,
    .set_string = config_set_string,
    .get_node = config_get_node,
    .get_array_count = config_get_array_count,
    .get_array_item = config_get_array_item,
    .get_child_count = config_get_child_count,
    .get_child_key = config_get_child_key,
    .get_shell_argv = config_get_shell_argv,
    .use_damage_tracking = config_use_damage_tracking,
    .show_fps = config_show_fps,
    .debug_damage_rects = config_debug_damage_rects,
    .scrollback_lines = config_scrollback_lines,
    .font_family = config_font_family,
};

/* Node management */

static struct config_node *node_create(const char *key)
{
    struct config_node *node = calloc(1, sizeof(struct config_node));
    if (!node) {
        return NULL;
    }

    if (key) {
        strncpy(node->key, key, MAX_KEY_LEN - 1);
    }

    return node;
}

static void node_destroy(struct config_node *node)
{
    if (!node) {
        return;
    }

    for (int i = 0; i < node->child_count; i++) {
        node_destroy(node->children[i]);
    }

    free(node);
}

static struct config_node *node_find_child(struct config_node *node, const char *key)
{
    for (int i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i]->key, key) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}

static struct config_node *node_get_or_create_child(struct config_node *node, const char *key)
{
    struct config_node *child = node_find_child(node, key);
    if (child) {
        return child;
    }

    if (node->child_count >= MAX_CHILDREN) {
        return NULL;
    }

    child = node_create(key);
    if (!child) {
        return NULL;
    }

    node->children[node->child_count++] = child;
    return child;
}

/* Navigate to node by slash-path, returns leaf key in out_key */
static struct config_node *navigate_path(struct config_node *root, const char *path, char *out_key)
{
    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    struct config_node *current = root;
    char *token = strtok(buf, "/");

    while (token) {
        char *next = strtok(NULL, "/");
        if (!next) {
            /* This is the leaf key */
            if (out_key) {
                strncpy(out_key, token, MAX_KEY_LEN - 1);
            }
            return current;
        }

        current = node_find_child(current, token);
        if (!current) {
            return NULL;
        }

        token = next;
    }

    return current;
}

/* Navigate and create intermediate nodes */
static struct config_node *navigate_or_create(struct config_node *root, const char *path,
                                              char *out_key)
{
    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Split path and find all tokens first */
    char *tokens[32];
    int token_count = 0;

    char *p = buf;
    while (*p && token_count < 32) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }

        tokens[token_count++] = p;

        while (*p && *p != '/') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }

    if (token_count == 0) {
        return NULL;
    }

    /* Navigate to parent, creating as needed */
    struct config_node *current = root;
    for (int i = 0; i < token_count - 1; i++) {
        current = node_get_or_create_child(current, tokens[i]);
        if (!current) {
            return NULL;
        }
    }

    /* Return leaf key */
    if (out_key) {
        strncpy(out_key, tokens[token_count - 1], MAX_KEY_LEN - 1);
    }

    return current;
}

/* Set value on a node */
static void node_set_value(struct config_node *parent, const char *key, const char *value)
{
    struct config_node *node = node_get_or_create_child(parent, key);
    if (!node) {
        return;
    }

    strncpy(node->value, value, MAX_VALUE_LEN - 1);

    /* Try to parse as bool */
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcmp(value, "1") == 0) {
        node->is_bool = 1;
        node->bool_value = 1;
    } else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
               strcmp(value, "0") == 0) {
        node->is_bool = 1;
        node->bool_value = 0;
    }

    /* Try to parse as int */
    char *endptr;
    long val = strtol(value, &endptr, 10);
    if (*endptr == '\0' && endptr != value) {
        node->is_int = 1;
        node->int_value = (int)val;
    }
}

/* YAML loading */

static void load_yaml_mapping(struct yaml_parser_s *parser, struct config_node *node,
                              const char *dir);

static void import_config_file(struct config_node *node, const char *filename, const char *dir)
{
    char path[1024];
    if (!filename || !filename[0]) {
        return;
    }
    if (filename[0] == '/') {
        snprintf(path, sizeof(path), "%s", filename);
    } else {
        snprintf(path, sizeof(path), "%s/%s", dir && dir[0] ? dir : ".", filename);
    }
    char import_dir[1024];
    strncpy(import_dir, path, sizeof(import_dir) - 1);
    import_dir[sizeof(import_dir) - 1] = '\0';
    char *slash = strrchr(import_dir, '/');
    if (slash) {
        *slash = '\0';
    } else {
        snprintf(import_dir, sizeof(import_dir), ".");
    }
    load_yaml_file_with_dir(node, path, import_dir);
}

static void load_yaml_value(struct yaml_parser_s *parser, struct config_node *parent,
                            const char *key, const char *dir)
{
    yaml_event_t event;

    if (!yaml_parser_parse(parser, &event)) {
        return;
    }

    if (strcmp(key, "import") == 0) {
        if (event.type == YAML_SCALAR_EVENT) {
            import_config_file(parent, (const char *)event.data.scalar.value, dir);
            yaml_event_delete(&event);
        } else if (event.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&event);
            while (yaml_parser_parse(parser, &event)) {
                if (event.type == YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&event);
                    break;
                }
                if (event.type == YAML_SCALAR_EVENT) {
                    import_config_file(parent, (const char *)event.data.scalar.value, dir);
                }
                yaml_event_delete(&event);
            }
        } else {
            yaml_event_delete(&event);
        }
        return;
    }

    if (event.type == YAML_SCALAR_EVENT) {
        node_set_value(parent, key, (const char *)event.data.scalar.value);
        yaml_event_delete(&event);
    } else if (event.type == YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&event);
        struct config_node *child = node_get_or_create_child(parent, key);
        if (child) {
            load_yaml_mapping(parser, child, dir);
        }
    } else if (event.type == YAML_SEQUENCE_START_EVENT) {
        /* Store the sequence as a child node with each item as a child
         * keyed by index "0", "1", … . Nested sequences/mappings inside
         * the sequence are still skipped — yetty config consumers only
         * need flat string lists today (e.g. shaders/preload/glyphs). */
        yaml_event_delete(&event);
        struct config_node *seq = node_get_or_create_child(parent, key);
        int index = 0;
        for (;;) {
            if (!yaml_parser_parse(parser, &event)) {
                break;
            }
            if (event.type == YAML_SEQUENCE_END_EVENT) {
                yaml_event_delete(&event);
                break;
            }
            if (event.type == YAML_SCALAR_EVENT) {
                if (seq) {
                    char idx_key[16];
                    snprintf(idx_key, sizeof(idx_key), "%d", index);
                    node_set_value(seq, idx_key, (const char *)event.data.scalar.value);
                }
                index++;
                yaml_event_delete(&event);
                continue;
            }
            /* Nested seq/map: skip the whole sub-tree. */
            int depth = 1;
            if (event.type == YAML_SEQUENCE_START_EVENT || event.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&event);
                while (depth > 0 && yaml_parser_parse(parser, &event)) {
                    if (event.type == YAML_SEQUENCE_START_EVENT ||
                        event.type == YAML_MAPPING_START_EVENT) {
                        depth++;
                    } else if (event.type == YAML_SEQUENCE_END_EVENT ||
                               event.type == YAML_MAPPING_END_EVENT) {
                        depth--;
                    }
                    yaml_event_delete(&event);
                }
            } else {
                yaml_event_delete(&event);
            }
        }
    } else {
        yaml_event_delete(&event);
    }
}

static void load_yaml_mapping(struct yaml_parser_s *parser, struct config_node *node,
                              const char *dir)
{
    yaml_event_t event;
    char key[MAX_KEY_LEN] = {0};

    while (yaml_parser_parse(parser, &event)) {
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }

        if (event.type == YAML_SCALAR_EVENT) {
            strncpy(key, (const char *)event.data.scalar.value, MAX_KEY_LEN - 1);
            yaml_event_delete(&event);
            load_yaml_value(parser, node, key, dir);
        } else {
            yaml_event_delete(&event);
        }
    }
}

static int load_yaml_file_with_dir(struct config_node *root, const char *path, const char *dir)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    struct yaml_parser_s parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        return 0;
    }

    yaml_parser_set_input_file(&parser, file);

    yaml_event_t event;
    while (yaml_parser_parse(&parser, &event)) {
        if (event.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&event);
            load_yaml_mapping(&parser, root, dir);
            break;
        }
        yaml_event_delete(&event);
        if (event.type == YAML_STREAM_END_EVENT) {
            break;
        }
    }

    yaml_parser_delete(&parser);
    fclose(file);
    return 1;
}

static int load_yaml_file(struct config_node *root, const char *path)
{
    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof(dir), ".");
    }
    return load_yaml_file_with_dir(root, path, dir);
}

/* Config ops implementation */

static void config_destroy(struct yetty_yconfig_config *self)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    node_destroy(impl->root);
    free(impl);
}

static const char *config_get_string(const struct yetty_yconfig_config *self, const char *path,
                                     const char *default_value)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->value[0]) {
        return default_value;
    }

    return node->value;
}

static int config_get_int(const struct yetty_yconfig_config *self, const char *path,
                          int default_value)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->is_int) {
        return default_value;
    }

    return node->int_value;
}

static int config_get_bool(const struct yetty_yconfig_config *self, const char *path,
                           int default_value)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->is_bool) {
        return default_value;
    }

    return node->bool_value;
}

static int config_has(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return 0;
    }

    return node_find_child(parent, key) != NULL;
}

static struct yetty_ycore_void_result config_set_string(struct yetty_yconfig_config *self,
                                                        const char *path, const char *value)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_or_create(impl->root, path, key);
    if (!parent) {
        return YETTY_ERR(yetty_ycore_void, "failed to create config path");
    }

    node_set_value(parent, key, value);
    return YETTY_OK_VOID();
}

static int config_use_damage_tracking(const struct yetty_yconfig_config *self)
{
    return config_get_bool(self, YETTY_YCONFIG_KEY_RENDERING_DAMAGE_TRACKING, 1);
}

static int config_show_fps(const struct yetty_yconfig_config *self)
{
    return config_get_bool(self, YETTY_YCONFIG_KEY_RENDERING_SHOW_FPS, 1);
}

static int config_debug_damage_rects(const struct yetty_yconfig_config *self)
{
    return config_get_bool(self, YETTY_YCONFIG_KEY_DEBUG_DAMAGE_RECTS, 0);
}

static uint32_t config_scrollback_lines(const struct yetty_yconfig_config *self)
{
    return (uint32_t)config_get_int(self, YETTY_YCONFIG_KEY_SCROLLBACK_LINES, 10000);
}

static const char *config_font_family(const struct yetty_yconfig_config *self)
{
    return config_get_string(self, YETTY_YCONFIG_KEY_FONT_FAMILY, "default");
}

/* Forward declaration */
static const struct yetty_yconfig_config_ops subnode_ops;

/* Subnode ops - same as config ops but uses subnode's node as root */
static void subnode_destroy(struct yetty_yconfig_config *self)
{
    /* No-op: subnodes don't own their data */
    (void)self;
}

static const char *subnode_get_string(const struct yetty_yconfig_config *self, const char *path,
                                      const char *default_value)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->value[0]) {
        return default_value;
    }

    return node->value;
}

static int subnode_get_int(const struct yetty_yconfig_config *self, const char *path,
                           int default_value)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->is_int) {
        return default_value;
    }

    return node->int_value;
}

static int subnode_get_bool(const struct yetty_yconfig_config *self, const char *path,
                            int default_value)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return default_value;
    }

    struct config_node *node = node_find_child(parent, key);
    if (!node || !node->is_bool) {
        return default_value;
    }

    return node->bool_value;
}

static int subnode_has(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return 0;
    }

    return node_find_child(parent, key) != NULL;
}

static struct yetty_ycore_void_result subnode_set_string(struct yetty_yconfig_config *self,
                                                         const char *path, const char *value)
{
    (void)self;
    (void)path;
    (void)value;
    return YETTY_ERR(yetty_ycore_void, "cannot set on subnode");
}

static struct yetty_yconfig_config *subnode_get_node(const struct yetty_yconfig_config *self,
                                                     const char *path);
static int subnode_get_array_count(const struct yetty_yconfig_config *self, const char *path);
static const char *subnode_get_array_item(const struct yetty_yconfig_config *self, const char *path,
                                          int index, const char *default_value);
static int subnode_get_child_count(const struct yetty_yconfig_config *self, const char *path);
static const char *subnode_get_child_key(const struct yetty_yconfig_config *self, const char *path,
                                         int index);

static const struct yetty_yconfig_config_ops subnode_ops = {
    .destroy = subnode_destroy,
    .get_string = subnode_get_string,
    .get_int = subnode_get_int,
    .get_bool = subnode_get_bool,
    .has = subnode_has,
    .set_string = subnode_set_string,
    .get_node = subnode_get_node,
    .get_array_count = subnode_get_array_count,
    .get_array_item = subnode_get_array_item,
    .get_child_count = subnode_get_child_count,
    .get_child_key = subnode_get_child_key,
    .get_shell_argv = NULL,
    .use_damage_tracking = NULL,
    .show_fps = NULL,
    .debug_damage_rects = NULL,
    .scrollback_lines = NULL,
    .font_family = NULL,
};

static struct yetty_yconfig_config *create_subconfig(struct config_node *node)
{
    if (!node || g_subconfig_count >= MAX_SUBCONFIGS) {
        return NULL;
    }

    struct config_subnode *sub = &g_subconfigs[g_subconfig_count++];
    sub->base.ops = &subnode_ops;
    sub->node = node;
    return &sub->base;
}

static struct yetty_yconfig_config *config_get_node(const struct yetty_yconfig_config *self,
                                                    const char *path)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return NULL;
    }

    struct config_node *node = node_find_child(parent, key);
    return create_subconfig(node);
}

static struct yetty_yconfig_config *subnode_get_node(const struct yetty_yconfig_config *self,
                                                     const char *path)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};

    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return NULL;
    }

    struct config_node *node = node_find_child(parent, key);
    return create_subconfig(node);
}

/* Resolve a slash-path to a config_node. NULL, empty, or "/" returns
 * `root` itself (the implicit top-of-tree). For any other path we
 * reuse navigate_path's parent+leaf-key split and look up the leaf
 * child by name. Returns NULL if any path segment is missing. */
static struct config_node *resolve_node(struct config_node *root, const char *path)
{
    if (!path || !path[0] || (path[0] == '/' && !path[1])) {
        return root;
    }
    char key[MAX_KEY_LEN] = {0};
    struct config_node *parent = navigate_path(root, path, key);
    if (!parent) {
        return NULL;
    }
    return node_find_child(parent, key);
}

/* Array accessors (root and subnode flavours).
 *
 * A YAML sequence is stored as a child node whose own children carry the
 * items, keyed by stringified index ("0", "1", …). get_array_count
 * counts those children; get_array_item walks them and returns the
 * value of the one whose key parses to `index`. */
static int node_array_count(struct config_node *node)
{
    if (!node) {
        return 0;
    }
    return node->child_count;
}

static const char *node_array_item(struct config_node *node, int index, const char *default_value)
{
    if (!node) {
        return default_value;
    }
    char idx_key[16];
    snprintf(idx_key, sizeof(idx_key), "%d", index);
    struct config_node *item = node_find_child(node, idx_key);
    if (!item || item->value[0] == '\0') {
        return default_value;
    }
    return item->value;
}

static int config_get_array_count(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};
    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return 0;
    }
    return node_array_count(node_find_child(parent, key));
}

static const char *config_get_array_item(const struct yetty_yconfig_config *self, const char *path,
                                         int index, const char *default_value)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    char key[MAX_KEY_LEN] = {0};
    struct config_node *parent = navigate_path(impl->root, path, key);
    if (!parent) {
        return default_value;
    }
    return node_array_item(node_find_child(parent, key), index, default_value);
}

static int config_get_child_count(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    struct config_node *node = resolve_node(impl->root, path);
    return node ? node->child_count : 0;
}

static const char *config_get_child_key(const struct yetty_yconfig_config *self, const char *path,
                                        int index)
{
    struct config_impl *impl = container_of(self, struct config_impl, base);
    struct config_node *node = resolve_node(impl->root, path);
    if (!node || index < 0 || index >= node->child_count) {
        return NULL;
    }
    return node->children[index]->key;
}

static int subnode_get_array_count(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};
    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return 0;
    }
    return node_array_count(node_find_child(parent, key));
}

static const char *subnode_get_array_item(const struct yetty_yconfig_config *self, const char *path,
                                          int index, const char *default_value)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    char key[MAX_KEY_LEN] = {0};
    struct config_node *parent = navigate_path(sub->node, path, key);
    if (!parent) {
        return default_value;
    }
    return node_array_item(node_find_child(parent, key), index, default_value);
}

static int subnode_get_child_count(const struct yetty_yconfig_config *self, const char *path)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    struct config_node *node = resolve_node(sub->node, path);
    return node ? node->child_count : 0;
}

static const char *subnode_get_child_key(const struct yetty_yconfig_config *self, const char *path,
                                         int index)
{
    struct config_subnode *sub = (struct config_subnode *)self;
    struct config_node *node = resolve_node(sub->node, path);
    if (!node || index < 0 || index >= node->child_count) {
        return NULL;
    }
    return node->children[index]->key;
}

/* Store platform paths */

static void store_platform_paths(struct config_impl *impl, const struct yetty_yconfig_paths *paths)
{
    if (!paths) {
        return;
    }

    if (paths->shaders_dir) {
        strncpy(impl->shaders_dir, paths->shaders_dir, sizeof(impl->shaders_dir) - 1);
        char key[MAX_KEY_LEN];
        struct config_node *parent = navigate_or_create(impl->root, "paths/shaders", key);
        if (parent) {
            node_set_value(parent, key, paths->shaders_dir);
        }
    }

    if (paths->fonts_dir) {
        strncpy(impl->fonts_dir, paths->fonts_dir, sizeof(impl->fonts_dir) - 1);
        char key[MAX_KEY_LEN];
        struct config_node *parent = navigate_or_create(impl->root, "paths/fonts", key);
        if (parent) {
            node_set_value(parent, key, paths->fonts_dir);
        }
    }

    if (paths->runtime_dir) {
        strncpy(impl->runtime_dir, paths->runtime_dir, sizeof(impl->runtime_dir) - 1);
        char key[MAX_KEY_LEN];
        struct config_node *parent = navigate_or_create(impl->root, "paths/runtime", key);
        if (parent) {
            node_set_value(parent, key, paths->runtime_dir);
        }
    }

    if (paths->bin_dir) {
        strncpy(impl->bin_dir, paths->bin_dir, sizeof(impl->bin_dir) - 1);
        char key[MAX_KEY_LEN];
        struct config_node *parent = navigate_or_create(impl->root, "paths/bin", key);
        if (parent) {
            node_set_value(parent, key, paths->bin_dir);
        }
    }

    if (paths->config_dir) {
        strncpy(impl->config_dir, paths->config_dir, sizeof(impl->config_dir) - 1);
        char key[MAX_KEY_LEN];
        struct config_node *parent = navigate_or_create(impl->root, "paths/config", key);
        if (parent) {
            node_set_value(parent, key, paths->config_dir);
        }
    }
}

/* Try to find and load config file */

static void try_load_config_file(struct config_impl *impl, int argc, char *argv[],
                                 const struct yetty_yconfig_paths *paths)
{
    /* Check for -c or --config argument */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            load_yaml_file(impl->root, argv[i + 1]);
            return;
        }
        if (strncmp(argv[i], "-c=", 3) == 0) {
            load_yaml_file(impl->root, argv[i] + 3);
            return;
        }
        if (strncmp(argv[i], "--config=", 9) == 0) {
            load_yaml_file(impl->root, argv[i] + 9);
            return;
        }
    }

    /* Try XDG config path */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char path[512];

    if (xdg) {
        snprintf(path, sizeof(path), "%s/yetty/config.yaml", xdg);
        if (load_yaml_file(impl->root, path)) {
            return;
        }
    }

    if (home) {
        snprintf(path, sizeof(path), "%s/.config/yetty/config.yaml", home);
        if (load_yaml_file(impl->root, path)) {
            return;
        }
    }

    /* Platform-supplied config dir (webasm: /config, android: app files dir,
     * etc.). Last resort so $XDG_CONFIG_HOME / $HOME can still override on
     * desktop. */
    if (paths && paths->config_dir && paths->config_dir[0]) {
        snprintf(path, sizeof(path), "%s/config.yaml", paths->config_dir);
        load_yaml_file(impl->root, path);
    }
}

/* Tokenize a command string into argv, respecting single and double quotes.
 *
 * Writes pointers into out->argv (NULL-terminated) that reference into
 * out->buf. Sets out->argc. Returns 0 on success, -1 on overflow. */
static int tokenize_command(const char *cmd, struct yetty_yconfig_shell_argv *out)
{
    const char *p = cmd;
    size_t bp = 0;
    int ac = 0;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        if (ac >= YETTY_YCONFIG_SHELL_ARGV_MAX - 1) {
            return -1;
        }
        out->argv[ac++] = &out->buf[bp];

        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '\'' || *p == '"') {
                char q = *p++;
                while (*p && *p != q) {
                    if (bp >= YETTY_YCONFIG_SHELL_BUF_SIZE - 1) {
                        return -1;
                    }
                    out->buf[bp++] = *p++;
                }
                if (*p == q) {
                    p++;
                }
            } else {
                if (bp >= YETTY_YCONFIG_SHELL_BUF_SIZE - 1) {
                    return -1;
                }
                out->buf[bp++] = *p++;
            }
        }
        if (bp >= YETTY_YCONFIG_SHELL_BUF_SIZE) {
            return -1;
        }
        out->buf[bp++] = '\0';
    }
    out->argv[ac] = NULL;
    out->argc = ac;
    return 0;
}

static struct yetty_ycore_void_result config_get_shell_argv(const struct yetty_yconfig_config *self,
                                                            struct yetty_yconfig_shell_argv *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "out is NULL");
    }

    memset(out, 0, sizeof(*out));

    /* -e bypasses the shell entirely: tokenize and exec the command directly */
    const char *command = self->ops->get_string(self, "shell/command", NULL);
    if (command && command[0]) {
        if (tokenize_command(command, out) < 0 || out->argc == 0) {
            return YETTY_ERR(yetty_ycore_void, "failed to tokenize shell/command");
        }
        return YETTY_OK_VOID();
    }

    /* No -e: run the resolved shell. Resolution order: $SHELL > shell/default
     * > /bin/bash. $SHELL is folded into shell/default at config-create time. */
    const char *shell = self->ops->get_string(self, "shell/default", "/bin/bash");
    size_t len = strlen(shell);
    if (len + 1 > YETTY_YCONFIG_SHELL_BUF_SIZE) {
        return YETTY_ERR(yetty_ycore_void, "shell path too long");
    }
    memcpy(out->buf, shell, len + 1);
    out->argv[0] = out->buf;
    out->argv[1] = NULL;
    out->argc = 1;
    return YETTY_OK_VOID();
}

/* Parse "user@host[:port]" SSH target into ssh/{username,host,port} */
static int parse_ssh_target(struct config_impl *impl, const char *target)
{
    const char *at, *colon;
    char key[MAX_KEY_LEN];
    char user[MAX_VALUE_LEN];
    char host[MAX_VALUE_LEN];
    char port[32];
    size_t user_len, host_len;
    struct config_node *parent;

    at = strchr(target, '@');
    if (!at || at == target) {
        return 0;
    }

    user_len = (size_t)(at - target);
    if (user_len >= sizeof(user)) {
        return 0;
    }
    memcpy(user, target, user_len);
    user[user_len] = '\0';

    colon = strchr(at + 1, ':');
    if (colon) {
        host_len = (size_t)(colon - (at + 1));
        snprintf(port, sizeof(port), "%s", colon + 1);
    } else {
        host_len = strlen(at + 1);
        port[0] = '\0';
    }
    if (host_len == 0 || host_len >= sizeof(host)) {
        return 0;
    }
    memcpy(host, at + 1, host_len);
    host[host_len] = '\0';

    parent = navigate_or_create(impl->root, "ssh/username", key);
    if (parent) {
        node_set_value(parent, key, user);
    }

    parent = navigate_or_create(impl->root, "ssh/host", key);
    if (parent) {
        node_set_value(parent, key, host);
    }

    if (port[0]) {
        parent = navigate_or_create(impl->root, "ssh/port", key);
        if (parent) {
            node_set_value(parent, key, port);
        }
    }
    return 1;
}

/* Parse "[HOST]:PORT" or bare numeric PORT into telnet/{host,port}.
 * Empty host => 127.0.0.1. Returns 1 on success, 0 on malformed input. */
static int parse_telnet_target(struct config_impl *impl, const char *target)
{
    char host[MAX_VALUE_LEN];
    char port[32];
    const char *colon = strchr(target, ':');

    if (colon) {
        size_t host_len = (size_t)(colon - target);
        if (host_len >= sizeof(host)) {
            return 0;
        }
        if (host_len == 0) {
            snprintf(host, sizeof(host), "%s", "127.0.0.1");
        } else {
            memcpy(host, target, host_len);
            host[host_len] = '\0';
        }
        snprintf(port, sizeof(port), "%s", colon + 1);
    } else {
        /* Bare numeric port — convenience form. */
        for (const char *p = target; *p; p++) {
            if (*p < '0' || *p > '9') {
                return 0;
            }
        }
        snprintf(host, sizeof(host), "%s", "127.0.0.1");
        snprintf(port, sizeof(port), "%s", target);
    }

    if (port[0] == '\0') {
        return 0;
    }

    char key[MAX_KEY_LEN];
    struct config_node *parent;
    parent = navigate_or_create(impl->root, YETTY_YCONFIG_KEY_TELNET_HOST, key);
    if (parent) {
        node_set_value(parent, key, host);
    }
    parent = navigate_or_create(impl->root, YETTY_YCONFIG_KEY_TELNET_PORT, key);
    if (parent) {
        node_set_value(parent, key, port);
    }
    return 1;
}

/* Long-only option ids (above 255 to avoid colliding with short-form ASCII).
 * yvnc/ydvnc are deliberately distinct prefixes from "vnc" so the flags can't
 * be mistaken for a generic VNC client/server. */
enum {
    OPT_YVNC_SERVER = 1000,
    OPT_YVNC_HEADLESS,
    OPT_YVNC_PORT,
    OPT_YVNC_CLIENT,
    OPT_YVNC_RAW,
    OPT_YVNC_COMPRESSION_QUALITY,
    OPT_YVNC_ALWAYS_FULL,
    OPT_YVNC_USE_H264,
    OPT_YVNC_MERGE_RECTS,
    OPT_YVNC_H264_BITRATE,
    OPT_YVNC_H264_FRAMERATE,
    OPT_YVNC_H264_IDR_INTERVAL,
    OPT_YVNC_H264_SCREEN_CONTENT,
    OPT_RECORD,
    OPT_RPC_HOST,
    OPT_TEMU,
    OPT_QEMU,
    OPT_SSH,
    OPT_SSH_PASSWORD,
    OPT_TELNET,
    OPT_WEBSOCKET,
    OPT_WEBSOCKET_URL,
    OPT_NET_RELAY,
    OPT_YDVNC_CLIENT,
    OPT_YDVNC_PASSWORD,
    OPT_CMD,
    OPT_POWERSHELL,
    OPT_PWSH,
    OPT_WSL,
};

static struct yetty_yplatform_option long_options[] = {
    {"config", required_argument, 0, 'c'},
    {"execute", required_argument, 0, 'e'},
    {"yvnc-server", no_argument, 0, OPT_YVNC_SERVER},
    {"yvnc-headless", no_argument, 0, OPT_YVNC_HEADLESS},
    {"yvnc-port", required_argument, 0, OPT_YVNC_PORT},
    {"yvnc-client", required_argument, 0, OPT_YVNC_CLIENT},
    {"ydvnc-client", required_argument, 0, OPT_YDVNC_CLIENT},
    {"ydvnc-password", required_argument, 0, OPT_YDVNC_PASSWORD},
    {"yvnc-raw", no_argument, 0, OPT_YVNC_RAW},
    {"yvnc-compression-quality", required_argument, 0, OPT_YVNC_COMPRESSION_QUALITY},
    {"yvnc-always-full", no_argument, 0, OPT_YVNC_ALWAYS_FULL},
    {"yvnc-use-h264", no_argument, 0, OPT_YVNC_USE_H264},
    {"yvnc-merge-rects", no_argument, 0, OPT_YVNC_MERGE_RECTS},
    {"yvnc-h264-bitrate", required_argument, 0, OPT_YVNC_H264_BITRATE},
    {"yvnc-h264-framerate", required_argument, 0, OPT_YVNC_H264_FRAMERATE},
    {"yvnc-h264-idr-interval", required_argument, 0, OPT_YVNC_H264_IDR_INTERVAL},
    {"yvnc-h264-screen-content", required_argument, 0, OPT_YVNC_H264_SCREEN_CONTENT},
    {"record", required_argument, 0, OPT_RECORD},
    {"rpc-host", required_argument, 0, OPT_RPC_HOST},
    {"rpc-port", required_argument, 0, 'r'},
    {"temu", no_argument, 0, OPT_TEMU},
    {"qemu", no_argument, 0, OPT_QEMU},
    {"ssh", optional_argument, 0, OPT_SSH},
    {"ssh-password", required_argument, 0, OPT_SSH_PASSWORD},
    {"telnet", optional_argument, 0, OPT_TELNET},
    {"websocket", optional_argument, 0, OPT_WEBSOCKET},
    {"websocket-url", required_argument, 0, OPT_WEBSOCKET_URL},
    {"net-relay", required_argument, 0, OPT_NET_RELAY},
    {"cmd", no_argument, 0, OPT_CMD},
    {"powershell", no_argument, 0, OPT_POWERSHELL},
    {"pwsh", no_argument, 0, OPT_PWSH},
    {"wsl", optional_argument, 0, OPT_WSL},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}};

/* Usage destination follows GNU conventions:
 *  - explicit -h/--help → stdout (so `yetty --help | less` works)
 *  - usage shown on flag error → stderr (it's a diagnostic) */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out, "Usage: %s [options]\n", prog);
    fprintf(out, "Options:\n");
    fprintf(out, "  -c, --config=FILE                  Load config from FILE\n");
    fprintf(out, "  -e, --execute=CMD                  Execute CMD in terminal\n");
    fprintf(out,
            "      --yvnc-server                  Run yvnc server (mirror mode - window + yvnc)\n");
    fprintf(out, "      --yvnc-headless                Run yvnc server (headless - no window)\n");
    fprintf(out, "      --yvnc-port=PORT               yvnc server port (default: 5900)\n");
    fprintf(out, "      --yvnc-client=HOST[:PORT]      Connect as yvnc client to HOST\n");
    fprintf(out, "      --yvnc-raw                     Disable JPEG, send raw BGRA tiles\n");
    fprintf(out, "      --yvnc-compression-quality Q   JPEG quality 1-100 (default: 80)\n");
    fprintf(out,
            "      --yvnc-always-full             Disable delta, send full frame every time\n");
    fprintf(out, "      --yvnc-use-h264                Use H.264 encoder instead of JPEG "
                 "(requires openh264)\n");
    fprintf(
        out,
        "      --yvnc-merge-rects             Merge adjacent dirty tiles into bigger rectangles\n");
    fprintf(out, "      --yvnc-h264-bitrate BPS        H.264 target bitrate in bps (default: "
                 "auto-scales with resolution)\n");
    fprintf(out, "      --yvnc-h264-framerate FPS      H.264 encode framerate (default: 30)\n");
    fprintf(out, "      --yvnc-h264-idr-interval N     Frames between H.264 keyframes (default: "
                 "60 — 2s at 30 fps)\n");
    fprintf(
        out,
        "      --yvnc-h264-screen-content 0|1 H.264 screen-content optimisation (default: 1)\n");
    fprintf(out, "      --ydvnc-client=HOST[:PORT]     Connect as RFB (RFC 6143) client\n");
    fprintf(out, "      --ydvnc-password=PASSWORD      VNC password for --ydvnc-client (or env "
                 "YDVNC_PASSWORD)\n");
    fprintf(out,
            "      --record=FILE                  Record session to MP4 (forces H.264 encoding)\n");
    fprintf(out, "      --rpc-host=HOST                RPC server host\n");
    fprintf(out, "  -r, --rpc-port=PORT                RPC server port\n");
    fprintf(out, "      --temu                         Run in-process TinyEMU RISC-V VM\n");
    fprintf(out, "      --qemu                         Run external QEMU RISC-V VM (via telnet)\n");
    fprintf(out, "      --ssh [USER@HOST[:PORT]]       Connect to SSH remote shell\n");
    fprintf(out, "      --ssh-password=PASSWORD        SSH password (webasm; desktop can also "
                 "use keys)\n");
    fprintf(out, "      --telnet [[HOST]:PORT]         Connect to a telnet server (default host "
                 "127.0.0.1)\n");
    fprintf(out, "      --websocket [URL]              Connect to a websocket PTY server "
                 "(webasm; ws://HOST:PORT)\n");
    fprintf(out, "      --websocket-url=URL            Websocket endpoint for --telnet on webasm "
                 "(no mode change)\n");
    fprintf(out, "      --cmd                          Launch cmd.exe as the shell (Windows)\n");
    fprintf(out,
            "      --powershell                   Launch Windows PowerShell 5.x as the shell\n");
    fprintf(out,
            "      --pwsh                         Launch PowerShell 7+ (pwsh.exe) as the shell\n");
    fprintf(
        out,
        "      --wsl [DISTRO]                 Launch a WSL session (optionally a named distro)\n");
    fprintf(out, "  -h, --help                         Show this help\n");
    fprintf(out, "\n");
    fprintf(out, "Session-mode flags are mutually exclusive:\n");
    fprintf(out,
            "  --cmd, --powershell, --pwsh, --wsl, --ssh, --telnet, --websocket, --temu, --qemu\n");
}

static void set_config(struct config_impl *impl, const char *path, const char *value)
{
    char key[MAX_KEY_LEN];
    struct config_node *parent = navigate_or_create(impl->root, path, key);
    if (parent) {
        node_set_value(parent, key, value);
    }
}

/* Reject more than one session-mode flag in a single invocation.
 * The mutually-exclusive set is: --cmd, --powershell, --pwsh, --wsl, --ssh,
 * --telnet, --websocket, --temu, --qemu. The first one parsed wins; any
 * second one prints a clear diagnostic naming both flags and exits non-zero. */
static void claim_session_mode(const char **slot, const char *flag)
{
    if (*slot) {
        fprintf(stderr, "yetty: --%s and --%s are mutually exclusive\n", *slot, flag);
        exit(1);
    }
    *slot = flag;
}

/* Parse the command line into config. Recognises -c/--config (already loaded
 * by try_load_config_file before we get here), -e command, VNC flags, RPC
 * host/port, --temu/--qemu, --ssh [user@host[:port]], --telnet,
 * --websocket [url] / --websocket-url, and the Windows session-mode flags
 * --cmd / --powershell / --pwsh / --wsl.
 *
 * On -h/--help or unknown args, prints usage and exits. */
static void parse_cmdline(struct config_impl *impl, int argc, char *argv[])
{
    yetty_yplatform_optreset = 1;
    yetty_yplatform_optind = 1;
    const char *session_mode = NULL;
    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "c:e:r:h", long_options, NULL)) != -1) {
        switch (c) {
        case 'c':
            /* config file already loaded by try_load_config_file */
            break;
        case 'e':
            set_config(impl, "shell/command", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_SERVER:
            set_config(impl, "vnc/server", "true");
            break;
        case OPT_YVNC_HEADLESS:
            set_config(impl, "vnc/headless", "true");
            break;
        case OPT_YVNC_PORT:
            set_config(impl, "vnc/port", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_CLIENT:
            set_config(impl, "vnc/client", yetty_yplatform_optarg);
            break;
        case OPT_YDVNC_CLIENT:
            set_config(impl, "vnc/desktop-client", yetty_yplatform_optarg);
            break;
        case OPT_YDVNC_PASSWORD:
            set_config(impl, "vnc/ydvnc-password", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_RAW:
            set_config(impl, "vnc/raw", "true");
            break;
        case OPT_YVNC_COMPRESSION_QUALITY:
            set_config(impl, "vnc/compression-quality", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_ALWAYS_FULL:
            set_config(impl, "vnc/always-full", "true");
            break;
        case OPT_YVNC_USE_H264:
            set_config(impl, "vnc/use-h264", "true");
            break;
        case OPT_YVNC_MERGE_RECTS:
            set_config(impl, "vnc/merge-rects", "true");
            break;
        case OPT_YVNC_H264_BITRATE:
            set_config(impl, "vnc/h264/bitrate", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_H264_FRAMERATE:
            set_config(impl, "vnc/h264/framerate", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_H264_IDR_INTERVAL:
            set_config(impl, "vnc/h264/idr-interval", yetty_yplatform_optarg);
            break;
        case OPT_YVNC_H264_SCREEN_CONTENT:
            set_config(impl, "vnc/h264/screen-content", yetty_yplatform_optarg);
            break;
        case OPT_RECORD:
            set_config(impl, "vnc/record-file", yetty_yplatform_optarg);
            set_config(impl, "vnc/use-h264", "true");
            break;
        case OPT_RPC_HOST:
            set_config(impl, YETTY_YCONFIG_KEY_RPC_HOST, yetty_yplatform_optarg);
            break;
        case 'r':
            set_config(impl, YETTY_YCONFIG_KEY_RPC_PORT, yetty_yplatform_optarg);
            break;
        case OPT_TEMU:
            claim_session_mode(&session_mode, "temu");
            set_config(impl, YETTY_YCONFIG_KEY_TEMU, "true");
            break;
        case OPT_QEMU:
            claim_session_mode(&session_mode, "qemu");
            set_config(impl, YETTY_YCONFIG_KEY_QEMU, "true");
            break;
        case OPT_SSH: {
            claim_session_mode(&session_mode, "ssh");
            /* getopt's optional_argument only binds `--ssh=USER@HOST`. Also
             * accept the natural `--ssh USER@HOST` by peeking the next argv
             * (same pattern as --telnet / --websocket). Bare --ssh keeps the
             * desktop behaviour of reading host/user from ssh config. */
            const char *target = yetty_yplatform_optarg;
            if (!target && yetty_yplatform_optind < argc) {
                const char *next = argv[yetty_yplatform_optind];
                if (next && next[0] != '-') {
                    target = next;
                    yetty_yplatform_optind++;
                }
            }
            if (target) {
                parse_ssh_target(impl, target);
            }
            set_config(impl, YETTY_YCONFIG_KEY_SSH, "true");
            break;
        }
        case OPT_SSH_PASSWORD:
            /* Endpoint credential only — no session-mode claim, rides
             * along with --ssh. */
            set_config(impl, "ssh/password", yetty_yplatform_optarg);
            break;
        case OPT_TELNET: {
            claim_session_mode(&session_mode, "telnet");
            /* getopt's optional_argument only picks up `--telnet=VALUE`. To
             * also accept the natural `--telnet VALUE` form (matches the
             * `telnet HOST PORT`-style ergonomics users expect), peek at
             * the next argv when no inline arg was given. */
            const char *arg = yetty_yplatform_optarg;
            if (!arg && yetty_yplatform_optind < argc) {
                const char *next = argv[yetty_yplatform_optind];
                if (next && next[0] != '-') {
                    arg = next;
                    yetty_yplatform_optind++;
                }
            }
            if (arg && !parse_telnet_target(impl, arg)) {
                fprintf(stderr, "yetty: --telnet expects [HOST]:PORT or PORT (got '%s')\n", arg);
                exit(1);
            }
            set_config(impl, YETTY_YCONFIG_KEY_TELNET, "true");
            break;
        }
        case OPT_WEBSOCKET: {
            claim_session_mode(&session_mode, "websocket");
            /* Accept both --websocket=URL and --websocket URL (same
             * peek-next-argv pattern as --telnet). Bare --websocket
             * leaves websocket/url to the platform default. */
            const char *url = yetty_yplatform_optarg;
            if (!url && yetty_yplatform_optind < argc) {
                const char *next = argv[yetty_yplatform_optind];
                if (next && next[0] != '-') {
                    url = next;
                    yetty_yplatform_optind++;
                }
            }
            if (url) {
                set_config(impl, YETTY_YCONFIG_KEY_WEBSOCKET_URL, url);
            }
            set_config(impl, YETTY_YCONFIG_KEY_WEBSOCKET, "true");
            break;
        }
        case OPT_WEBSOCKET_URL:
            /* Endpoint only — no session-mode claim, so it can ride
             * along with --telnet (webasm telnet-over-websocket). */
            set_config(impl, YETTY_YCONFIG_KEY_WEBSOCKET_URL, yetty_yplatform_optarg);
            break;
        case OPT_NET_RELAY:
            /* Brings up the in-browser lwIP netstack against an L2 frame
             * relay (webasm). Independent of session mode. */
            set_config(impl, YETTY_YCONFIG_KEY_NET_RELAY, yetty_yplatform_optarg);
            break;
        case OPT_CMD:
            claim_session_mode(&session_mode, "cmd");
            set_config(impl, "shell/command", "cmd.exe");
            break;
        case OPT_POWERSHELL:
            claim_session_mode(&session_mode, "powershell");
            set_config(impl, "shell/command", "powershell.exe");
            break;
        case OPT_PWSH:
            claim_session_mode(&session_mode, "pwsh");
            set_config(impl, "shell/command", "pwsh.exe");
            break;
        case OPT_WSL: {
            claim_session_mode(&session_mode, "wsl");
            /* Accept both --wsl=DISTRO and --wsl DISTRO (matching --telnet's
             * peek-next-argv pattern). Bare --wsl uses the default distro. */
            const char *distro = yetty_yplatform_optarg;
            if (!distro && yetty_yplatform_optind < argc) {
                const char *next = argv[yetty_yplatform_optind];
                if (next && next[0] != '-') {
                    distro = next;
                    yetty_yplatform_optind++;
                }
            }
            if (distro) {
                char buf[256];
                snprintf(buf, sizeof(buf), "wsl.exe -d %s", distro);
                set_config(impl, "shell/command", buf);
            } else {
                set_config(impl, "shell/command", "wsl.exe");
            }
            break;
        }
        case 'h':
            print_usage(stdout, argv[0]);
            exit(0);
        default:
            print_usage(stderr, argv[0]);
            exit(1);
        }
    }
}

/* Public create function */

struct yetty_yconfig_result yetty_yconfig_create(int argc, char *argv[],
                                                 const struct yetty_yconfig_paths *paths)
{
    struct config_impl *impl = calloc(1, sizeof(struct config_impl));
    if (!impl) {
        return YETTY_ERR(yetty_yconfig, "failed to allocate config");
    }

    impl->base.ops = &config_ops;
    impl->root = node_create(NULL);
    if (!impl->root) {
        free(impl);
        return YETTY_ERR(yetty_yconfig, "failed to allocate config root");
    }

    try_load_config_file(impl, argc, argv, paths);
    store_platform_paths(impl, paths);

    /* $SHELL overrides shell/default from yaml */
    const char *env_shell = getenv("SHELL");
    if (env_shell && env_shell[0]) {
        set_config(impl, "shell/default", env_shell);
    }

    parse_cmdline(impl, argc, argv);

    return YETTY_OK(yetty_yconfig, &impl->base);
}
