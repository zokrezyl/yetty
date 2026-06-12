/*
 * commands.h — yai's slash-command table.
 *
 * Two sources merge into one table:
 *   - yai's own LOCAL commands (/exit, /quit, /shell, /config)
 *     dispatched by the line editor itself;
 *   - the CLI's commands, loaded at startup from the `initialize`
 *     control_response (name + description + argumentHint — the same
 *     list the interactive UI completes from). Non-local commands are
 *     forwarded as user messages; the CLI executes them.
 *
 * The table feeds the live completion menu under the prompt.
 */
#ifndef YAI_COMMANDS_H
#define YAI_COMMANDS_H

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yyjson.h>

struct yai_command {
    char name[64];          /* without the leading slash */
    char argument_hint[96]; /* e.g. "[issue description]" */
    char *description;      /* heap-owned; may be empty, never NULL */
    int local;              /* 1 = handled by yai, not forwarded */
};

struct yai_command_table {
    struct yai_command *items;
    size_t count;
    size_t capacity;
};

/* Seed the table with yai's local commands. */
struct yetty_ycore_void_result yai_command_table_init(struct yai_command_table *table);

void yai_command_table_destroy(struct yai_command_table *table);

/* Merge the `commands` array from the initialize control_response.
 * Existing names (the locals) win; the table is re-sorted by name. */
struct yetty_ycore_void_result yai_command_table_load(struct yai_command_table *table,
                                                      yyjson_val *commands_array);

/* Fill `out_indices` with up to `max` table indices whose name starts
 * with prefix[0..prefix_len). Returns the number filled. */
size_t yai_command_table_match(const struct yai_command_table *table, const char *prefix,
                               size_t prefix_len, size_t *out_indices, size_t max);

/* Exact-name lookup; NULL when absent. */
const struct yai_command *yai_command_table_find(const struct yai_command_table *table,
                                                 const char *name, size_t name_len);

#endif /* YAI_COMMANDS_H */
