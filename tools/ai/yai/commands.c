/*
 * commands.c — yai's slash-command table.
 */
#include "commands.h"

#include <stdlib.h>
#include <string.h>

static struct yetty_ycore_void_result table_reserve(struct yai_command_table *table, size_t wanted)
{
    if (wanted <= table->capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_capacity = table->capacity ? table->capacity * 2 : 16;
    while (new_capacity < wanted) {
        new_capacity *= 2;
    }
    struct yai_command *grown = realloc(table->items, new_capacity * sizeof(struct yai_command));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "table_reserve: realloc");
    }
    table->items = grown;
    table->capacity = new_capacity;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result table_add(struct yai_command_table *table, const char *name,
                                                const char *description, const char *argument_hint,
                                                int local)
{
    if (yai_command_table_find(table, name, strlen(name))) {
        return YETTY_OK_VOID(); /* existing entry (a local) wins */
    }
    struct yetty_ycore_void_result reserve_res = table_reserve(table, table->count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "table_add: reserve");
    struct yai_command *entry = &table->items[table->count];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->argument_hint, sizeof(entry->argument_hint), "%s",
             argument_hint ? argument_hint : "");
    entry->description = strdup(description ? description : "");
    if (!entry->description) {
        return YETTY_ERR(yetty_ycore_void, "table_add: strdup");
    }
    entry->local = local;
    table->count++;
    return YETTY_OK_VOID();
}

static int command_compare(const void *first, const void *second)
{
    const struct yai_command *left = first;
    const struct yai_command *right = second;
    return strcmp(left->name, right->name);
}

struct yetty_ycore_void_result yai_command_table_init(struct yai_command_table *table)
{
    memset(table, 0, sizeof(*table));
    struct yetty_ycore_void_result res;
    res = table_add(table, "exit", "exit yai", "", 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "command_table_init: exit");
    res = table_add(table, "quit", "exit yai", "", 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "command_table_init: quit");
    res = table_add(table, "shell", "run a shell; the prompt continues when it exits", "[command…]",
                    1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "command_table_init: shell");
    res = table_add(table, "config", "toggle the engine + yai configuration dialog", "", 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "command_table_init: config");
    res = table_add(table, "usage", "show this session's token usage and cost", "", 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "command_table_init: usage");
    qsort(table->items, table->count, sizeof(table->items[0]), command_compare);
    return YETTY_OK_VOID();
}

void yai_command_table_destroy(struct yai_command_table *table)
{
    for (size_t index = 0; index < table->count; index++) {
        free(table->items[index].description);
    }
    free(table->items);
    memset(table, 0, sizeof(*table));
}

struct yetty_ycore_void_result yai_command_table_load(struct yai_command_table *table,
                                                      yyjson_val *commands_array)
{
    if (!yyjson_is_arr(commands_array)) {
        return YETTY_ERR(yetty_ycore_void, "command_table_load: not an array");
    }
    yyjson_val *entry;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(commands_array, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *name = yyjson_get_str(yyjson_obj_get(entry, "name"));
        if (!name || !name[0]) {
            continue;
        }
        const char *description = yyjson_get_str(yyjson_obj_get(entry, "description"));
        const char *argument_hint = yyjson_get_str(yyjson_obj_get(entry, "argumentHint"));
        struct yetty_ycore_void_result add_res =
            table_add(table, name, description, argument_hint, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "command_table_load: add");
    }
    qsort(table->items, table->count, sizeof(table->items[0]), command_compare);
    return YETTY_OK_VOID();
}

size_t yai_command_table_match(const struct yai_command_table *table, const char *prefix,
                               size_t prefix_len, size_t *out_indices, size_t max)
{
    size_t found = 0;
    for (size_t index = 0; index < table->count && found < max; index++) {
        if (strncmp(table->items[index].name, prefix, prefix_len) == 0) {
            out_indices[found++] = index;
        }
    }
    return found;
}

const struct yai_command *yai_command_table_find(const struct yai_command_table *table,
                                                 const char *name, size_t name_len)
{
    for (size_t index = 0; index < table->count; index++) {
        if (strlen(table->items[index].name) == name_len &&
            strncmp(table->items[index].name, name, name_len) == 0) {
            return &table->items[index];
        }
    }
    return NULL;
}
