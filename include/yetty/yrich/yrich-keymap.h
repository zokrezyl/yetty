#ifndef YETTY_YRICH_YRICH_KEYMAP_H
#define YETTY_YRICH_YRICH_KEYMAP_H

/*
 * yrich-keymap — semantic editor commands + a remappable, modal keymap.
 *
 * Editor actions are NAMED COMMANDS (enum yetty_yrich_command_id). Keys never
 * invoke actions directly: a keymap maps (mode, key, mods) -> command, and
 * every binding is remappable at runtime. The keymap is MODAL, so the same
 * key means different commands in different modes — this is the substrate for
 * vi-style editing (default / vi-normal / vi-insert). Menus, toolbar buttons,
 * key input, and (later) scripting all dispatch the very same commands.
 *
 * This module owns only the command vocabulary and the binding table; the
 * per-command behaviour lives in the editor controller that dispatches them.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-types.h> /* enum yetty_yrich_key, mod flags */

#ifdef __cplusplus
extern "C" {
#endif

/* Editor input modes. The keymap is keyed on the active mode. */
enum yetty_yrich_edit_mode {
    YETTY_YRICH_MODE_DEFAULT = 0,
    YETTY_YRICH_MODE_VI_NORMAL,
    YETTY_YRICH_MODE_VI_INSERT,
    YETTY_YRICH_MODE_COUNT,
};

/* Semantic editor commands. Stable ids — append new commands before _COUNT.
 * Every menu item, toolbar button, keybinding, and future script call
 * resolves to one of these. */
enum yetty_yrich_command_id {
    YETTY_YRICH_CMD_NONE = 0,

    /* file / history */
    YETTY_YRICH_CMD_SAVE,
    YETTY_YRICH_CMD_UNDO,
    YETTY_YRICH_CMD_REDO,

    /* character formatting */
    YETTY_YRICH_CMD_TOGGLE_BOLD,
    YETTY_YRICH_CMD_TOGGLE_ITALIC,
    YETTY_YRICH_CMD_TOGGLE_UNDERLINE,
    YETTY_YRICH_CMD_TOGGLE_STRIKE,

    /* paragraph */
    YETTY_YRICH_CMD_ALIGN_LEFT,
    YETTY_YRICH_CMD_ALIGN_CENTER,
    YETTY_YRICH_CMD_ALIGN_RIGHT,
    YETTY_YRICH_CMD_HEADING_NORMAL,
    YETTY_YRICH_CMD_HEADING_1,
    YETTY_YRICH_CMD_HEADING_2,
    YETTY_YRICH_CMD_HEADING_3,
    YETTY_YRICH_CMD_FONT_LARGER,
    YETTY_YRICH_CMD_FONT_SMALLER,
    YETTY_YRICH_CMD_ADD_PARAGRAPH,
    YETTY_YRICH_CMD_LIST_BULLET,
    YETTY_YRICH_CMD_LIST_NUMBERED,

    /* selection / clipboard */
    YETTY_YRICH_CMD_SELECT_ALL,
    YETTY_YRICH_CMD_COPY,
    YETTY_YRICH_CMD_CUT,
    YETTY_YRICH_CMD_PASTE,

    /* caret motion (a vi-normal keymap binds these to letters) */
    YETTY_YRICH_CMD_CARET_LEFT,
    YETTY_YRICH_CMD_CARET_RIGHT,
    YETTY_YRICH_CMD_CARET_UP,
    YETTY_YRICH_CMD_CARET_DOWN,
    YETTY_YRICH_CMD_CARET_LINE_START,
    YETTY_YRICH_CMD_CARET_LINE_END,

    /* mode switches — themselves commands */
    YETTY_YRICH_CMD_MODE_DEFAULT,
    YETTY_YRICH_CMD_MODE_VI_NORMAL,
    YETTY_YRICH_CMD_MODE_VI_INSERT,

    YETTY_YRICH_CMD_COUNT,
};

struct yetty_yrich_keybinding {
    enum yetty_yrich_edit_mode mode;
    enum yetty_yrich_key key;
    uint32_t mods; /* enum yetty_yrich_mod_flags bitmask */
    enum yetty_yrich_command_id command;
};

struct yetty_yrich_keymap {
    struct yetty_yrich_keybinding *bindings;
    size_t count;
    size_t capacity;
};

void yetty_yrich_keymap_init(struct yetty_yrich_keymap *keymap);
void yetty_yrich_keymap_clear(struct yetty_yrich_keymap *keymap);

/* Bind (mode,key,mods) -> command, replacing any existing binding for the same
 * (mode,key,mods). command == YETTY_YRICH_CMD_NONE removes the binding. This
 * is the remap entry point — a config loader or a "rebind key" UI calls it. */
struct yetty_ycore_void_result yetty_yrich_keymap_bind(struct yetty_yrich_keymap *keymap,
                                                       enum yetty_yrich_edit_mode mode,
                                                       enum yetty_yrich_key key, uint32_t mods,
                                                       enum yetty_yrich_command_id command);

/* Resolve a key event in `mode` to a command, or YETTY_YRICH_CMD_NONE. */
enum yetty_yrich_command_id yetty_yrich_keymap_lookup(const struct yetty_yrich_keymap *keymap,
                                                      enum yetty_yrich_edit_mode mode,
                                                      enum yetty_yrich_key key, uint32_t mods);

/* Install the built-in default + vi bindings. */
struct yetty_ycore_void_result yetty_yrich_keymap_load_defaults(struct yetty_yrich_keymap *keymap);

/* Stable machine name for a command (config / scripting / remap-by-name),
 * e.g. "doc.save". Returns "none" for CMD_NONE / out of range. */
const char *yetty_yrich_command_name(enum yetty_yrich_command_id command);

/* Inverse of yetty_yrich_command_name; YETTY_YRICH_CMD_NONE if unknown. */
enum yetty_yrich_command_id yetty_yrich_command_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_KEYMAP_H */
