/*
 * yrich-keymap — semantic editor commands + a remappable, modal keymap.
 *
 * Owns the command vocabulary, the binding table, lookup/bind, the built-in
 * default + vi bindings, and command<->name mapping. Behaviour of each command
 * lives in the editor controller that dispatches it.
 */

#include <yetty/yrich/yrich-keymap.h>

#include <stdlib.h>
#include <string.h>

void yetty_yrich_keymap_init(struct yetty_yrich_keymap *keymap)
{
    if (keymap) {
        memset(keymap, 0, sizeof(*keymap));
    }
}

void yetty_yrich_keymap_clear(struct yetty_yrich_keymap *keymap)
{
    if (!keymap) {
        return;
    }
    free(keymap->bindings);
    memset(keymap, 0, sizeof(*keymap));
}

static struct yetty_yrich_keybinding *find_binding(struct yetty_yrich_keymap *keymap,
                                                   enum yetty_yrich_edit_mode mode,
                                                   enum yetty_yrich_key key, uint32_t mods)
{
    for (size_t i = 0; i < keymap->count; i++) {
        if (keymap->bindings[i].mode == mode && keymap->bindings[i].key == key &&
            keymap->bindings[i].mods == mods) {
            return &keymap->bindings[i];
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_yrich_keymap_bind(struct yetty_yrich_keymap *keymap,
                                                       enum yetty_yrich_edit_mode mode,
                                                       enum yetty_yrich_key key, uint32_t mods,
                                                       enum yetty_yrich_command_id command)
{
    if (!keymap) {
        return YETTY_ERR(yetty_ycore_void, "keymap_bind: NULL keymap");
    }
    struct yetty_yrich_keybinding *existing = find_binding(keymap, mode, key, mods);
    if (existing) {
        if (command == YETTY_YRICH_CMD_NONE) {
            /* Remove: overwrite with the last entry and shrink. */
            *existing = keymap->bindings[keymap->count - 1];
            keymap->count--;
        } else {
            existing->command = command;
        }
        return YETTY_OK_VOID();
    }
    if (command == YETTY_YRICH_CMD_NONE) {
        return YETTY_OK_VOID();
    }
    if (keymap->count == keymap->capacity) {
        size_t new_capacity = keymap->capacity ? keymap->capacity * 2 : 32;
        struct yetty_yrich_keybinding *grown =
            realloc(keymap->bindings, new_capacity * sizeof(*grown));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "keymap_bind: binding array grow failed");
        }
        keymap->bindings = grown;
        keymap->capacity = new_capacity;
    }
    keymap->bindings[keymap->count].mode = mode;
    keymap->bindings[keymap->count].key = key;
    keymap->bindings[keymap->count].mods = mods;
    keymap->bindings[keymap->count].command = command;
    keymap->count++;
    return YETTY_OK_VOID();
}

enum yetty_yrich_command_id yetty_yrich_keymap_lookup(const struct yetty_yrich_keymap *keymap,
                                                      enum yetty_yrich_edit_mode mode,
                                                      enum yetty_yrich_key key, uint32_t mods)
{
    if (!keymap) {
        return YETTY_YRICH_CMD_NONE;
    }
    for (size_t i = 0; i < keymap->count; i++) {
        if (keymap->bindings[i].mode == mode && keymap->bindings[i].key == key &&
            keymap->bindings[i].mods == mods) {
            return keymap->bindings[i].command;
        }
    }
    return YETTY_YRICH_CMD_NONE;
}

struct yetty_ycore_void_result yetty_yrich_keymap_load_defaults(struct yetty_yrich_keymap *keymap)
{
    if (!keymap) {
        return YETTY_ERR(yetty_ycore_void, "keymap_load_defaults: NULL keymap");
    }
    static const struct yetty_yrich_keybinding defaults[] = {
        /* --- default mode: conventional desktop shortcuts --- */
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_S, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_SAVE},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_Q, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_QUIT},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_Q, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_QUIT},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_Z, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_UNDO},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_Z, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_REDO},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_Y, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_REDO},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_B, YETTY_YRICH_MOD_CTRL,
         YETTY_YRICH_CMD_TOGGLE_BOLD},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_I, YETTY_YRICH_MOD_CTRL,
         YETTY_YRICH_CMD_TOGGLE_ITALIC},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_U, YETTY_YRICH_MOD_CTRL,
         YETTY_YRICH_CMD_TOGGLE_UNDERLINE},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_A, YETTY_YRICH_MOD_CTRL,
         YETTY_YRICH_CMD_SELECT_ALL},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_C, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_COPY},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_X, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_CUT},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_V, YETTY_YRICH_MOD_CTRL, YETTY_YRICH_CMD_PASTE},
        /* Alignment — the conventional word-processor chords. */
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_L, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_ALIGN_LEFT},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_E, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_ALIGN_CENTER},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_R, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_ALIGN_RIGHT},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_J, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_ALIGN_JUSTIFY},
        /* Headings — Ctrl+Alt+0..3 (Google Docs parity). */
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_0, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT,
         YETTY_YRICH_CMD_HEADING_NORMAL},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_1, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT,
         YETTY_YRICH_CMD_HEADING_1},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_2, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT,
         YETTY_YRICH_CMD_HEADING_2},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_3, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT,
         YETTY_YRICH_CMD_HEADING_3},
        /* Lists — Ctrl+Shift+7 numbered / 8 bulleted / 9 checklist. */
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_7, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_LIST_NUMBERED},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_8, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_LIST_BULLET},
        {YETTY_YRICH_MODE_DEFAULT, YETTY_YRICH_KEY_9, YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_SHIFT,
         YETTY_YRICH_CMD_LIST_CHECKLIST},

        /* --- vi-normal: motions + mode switches (substrate; extend later) --- */
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_H, 0, YETTY_YRICH_CMD_CARET_LEFT},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_L, 0, YETTY_YRICH_CMD_CARET_RIGHT},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_K, 0, YETTY_YRICH_CMD_CARET_UP},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_J, 0, YETTY_YRICH_CMD_CARET_DOWN},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_HOME, 0, YETTY_YRICH_CMD_CARET_LINE_START},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_END, 0, YETTY_YRICH_CMD_CARET_LINE_END},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_I, 0, YETTY_YRICH_CMD_MODE_VI_INSERT},
        {YETTY_YRICH_MODE_VI_NORMAL, YETTY_YRICH_KEY_U, 0, YETTY_YRICH_CMD_UNDO},

        /* --- vi-insert: Escape returns to normal; text otherwise passes through --- */
        {YETTY_YRICH_MODE_VI_INSERT, YETTY_YRICH_KEY_ESCAPE, 0, YETTY_YRICH_CMD_MODE_VI_NORMAL},
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        struct yetty_ycore_void_result bind_res = yetty_yrich_keymap_bind(
            keymap, defaults[i].mode, defaults[i].key, defaults[i].mods, defaults[i].command);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bind_res, "keymap_load_defaults: bind failed");
    }
    return YETTY_OK_VOID();
}

struct command_name_entry {
    enum yetty_yrich_command_id command;
    const char *name;
};

static const struct command_name_entry *command_name_table(size_t *count_out)
{
    static const struct command_name_entry table[] = {
        {YETTY_YRICH_CMD_SAVE, "doc.save"},
        {YETTY_YRICH_CMD_QUIT, "app.quit"},
        {YETTY_YRICH_CMD_UNDO, "edit.undo"},
        {YETTY_YRICH_CMD_REDO, "edit.redo"},
        {YETTY_YRICH_CMD_TOGGLE_BOLD, "format.bold"},
        {YETTY_YRICH_CMD_TOGGLE_ITALIC, "format.italic"},
        {YETTY_YRICH_CMD_TOGGLE_UNDERLINE, "format.underline"},
        {YETTY_YRICH_CMD_TOGGLE_STRIKE, "format.strike"},
        {YETTY_YRICH_CMD_ALIGN_LEFT, "para.align_left"},
        {YETTY_YRICH_CMD_ALIGN_CENTER, "para.align_center"},
        {YETTY_YRICH_CMD_ALIGN_RIGHT, "para.align_right"},
        {YETTY_YRICH_CMD_ALIGN_JUSTIFY, "para.align_justify"},
        {YETTY_YRICH_CMD_HEADING_NORMAL, "para.heading_normal"},
        {YETTY_YRICH_CMD_HEADING_1, "para.heading_1"},
        {YETTY_YRICH_CMD_HEADING_2, "para.heading_2"},
        {YETTY_YRICH_CMD_HEADING_3, "para.heading_3"},
        {YETTY_YRICH_CMD_FONT_LARGER, "format.font_larger"},
        {YETTY_YRICH_CMD_FONT_SMALLER, "format.font_smaller"},
        {YETTY_YRICH_CMD_ADD_PARAGRAPH, "doc.add_paragraph"},
        {YETTY_YRICH_CMD_LIST_BULLET, "para.list_bullet"},
        {YETTY_YRICH_CMD_LIST_NUMBERED, "para.list_numbered"},
        {YETTY_YRICH_CMD_LIST_CHECKLIST, "para.list_checklist"},
        {YETTY_YRICH_CMD_CHECK_TOGGLE, "para.check_toggle"},
        {YETTY_YRICH_CMD_INSERT_HRULE, "insert.horizontal_rule"},
        {YETTY_YRICH_CMD_SELECT_ALL, "edit.select_all"},
        {YETTY_YRICH_CMD_COPY, "edit.copy"},
        {YETTY_YRICH_CMD_CUT, "edit.cut"},
        {YETTY_YRICH_CMD_PASTE, "edit.paste"},
        {YETTY_YRICH_CMD_CARET_LEFT, "caret.left"},
        {YETTY_YRICH_CMD_CARET_RIGHT, "caret.right"},
        {YETTY_YRICH_CMD_CARET_UP, "caret.up"},
        {YETTY_YRICH_CMD_CARET_DOWN, "caret.down"},
        {YETTY_YRICH_CMD_CARET_LINE_START, "caret.line_start"},
        {YETTY_YRICH_CMD_CARET_LINE_END, "caret.line_end"},
        {YETTY_YRICH_CMD_MODE_DEFAULT, "mode.default"},
        {YETTY_YRICH_CMD_MODE_VI_NORMAL, "mode.vi_normal"},
        {YETTY_YRICH_CMD_MODE_VI_INSERT, "mode.vi_insert"},
    };
    if (count_out) {
        *count_out = sizeof(table) / sizeof(table[0]);
    }
    return table;
}

const char *yetty_yrich_command_name(enum yetty_yrich_command_id command)
{
    size_t count = 0;
    const struct command_name_entry *table = command_name_table(&count);
    for (size_t i = 0; i < count; i++) {
        if (table[i].command == command) {
            return table[i].name;
        }
    }
    return "none";
}

enum yetty_yrich_command_id yetty_yrich_command_from_name(const char *name)
{
    if (!name) {
        return YETTY_YRICH_CMD_NONE;
    }
    size_t count = 0;
    const struct command_name_entry *table = command_name_table(&count);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return table[i].command;
        }
    }
    return YETTY_YRICH_CMD_NONE;
}
