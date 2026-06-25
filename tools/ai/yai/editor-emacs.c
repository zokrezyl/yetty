/*
 * editor-emacs.c — yclass class `yai:emacs`: emacs-mode keystroke
 * interpretation for the line editor (subclass of yai:editor).
 *
 * Bindings (a readline-ish subset):
 *   Ctrl-A / Ctrl-E       start / end of line
 *   Ctrl-B / Ctrl-F       back / forward one char   (also ← / →)
 *   Ctrl-P / Ctrl-N       previous / next history   (also ↑ / ↓)
 *   Ctrl-D                delete char under cursor (EOF when line empty)
 *   Ctrl-H / Backspace    delete char before cursor
 *   Ctrl-K                kill to end of line
 *   Ctrl-U                kill whole line
 *   Ctrl-W               kill word before cursor
 *   Ctrl-Y                yank the kill ring
 *   Ctrl-_ / Ctrl-/       undo the last change (repeat to walk back)
 *   Ctrl-C                interrupt / quit
 *   Tab                   accept the completion menu selection
 *   Meta-B / Meta-F       back / forward one word
 *   Meta-D                kill word forward
 *   Meta-Backspace        kill word backward
 *   Home/End/Delete       via CSI
 */
#include "app.h"
#include "editor-ops.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

struct YETTY_ANNOTATE("class@yai:emacs") YETTY_ANNOTATE("parent@yai:editor")
yetty_yai_emacs {
    char unused;
};

YETTY_YRESULT_DECLARE(yetty_yai_emacs_ptr, struct yetty_yai_emacs *);

/* A completed CSI sequence (arrow / home / end / delete). */
static int emacs_csi(struct yai_app *app, char final_byte, int param)
{
    switch (final_byte) {
    case 'A':
        return YAI_EDIT_NAV_PREV; /* up */
    case 'B':
        return YAI_EDIT_NAV_NEXT; /* down */
    case 'C': /* right */
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'D': /* left */
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'H': /* home */
        app->stdin_cursor = 0;
        return YAI_EDIT_MOVED;
    case 'F': /* end */
        app->stdin_cursor = app->stdin_len;
        return YAI_EDIT_MOVED;
    case '~':
        if (param == 3) { /* delete: codepoint under the cursor */
            return editor_cmd_delete(app, app->stdin_cursor,
                                     editor_ops_next_char(app, app->stdin_cursor));
        }
        if (param == 1 || param == 7) {
            app->stdin_cursor = 0;
            return YAI_EDIT_MOVED;
        }
        if (param == 4 || param == 8) {
            app->stdin_cursor = app->stdin_len;
            return YAI_EDIT_MOVED;
        }
        return YAI_EDIT_NONE;
    default:
        return YAI_EDIT_NONE;
    }
}

/* A Meta chord (ESC then `byte`). */
static int emacs_meta(struct yai_app *app, char byte)
{
    switch (byte) {
    case 'b':
    case 'B': /* back one word */
        app->stdin_cursor = editor_ops_word_back(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'f':
    case 'F': /* forward one word */
        app->stdin_cursor = editor_ops_word_forward(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'd':
    case 'D': /* kill word forward */
        return editor_cmd_kill(app, app->stdin_cursor,
                               editor_ops_word_forward(app, app->stdin_cursor));
    case 0x7F:
    case 0x08: /* Meta-Backspace: kill word backward */
        return editor_cmd_kill(app, editor_ops_word_back(app, app->stdin_cursor),
                               app->stdin_cursor);
    default:
        return YAI_EDIT_NONE;
    }
}

/* A plain byte (not part of an escape sequence). */
static int emacs_plain(struct yai_app *app, char byte)
{
    switch ((unsigned char)byte) {
    case '\r':
    case '\n':
        return YAI_EDIT_SUBMIT;
    case '\t':
        return YAI_EDIT_COMPLETE;
    case 0x01: /* Ctrl-A */
        app->stdin_cursor = 0;
        return YAI_EDIT_MOVED;
    case 0x05: /* Ctrl-E */
        app->stdin_cursor = app->stdin_len;
        return YAI_EDIT_MOVED;
    case 0x02: /* Ctrl-B */
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 0x06: /* Ctrl-F */
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 0x10: /* Ctrl-P */
        return YAI_EDIT_NAV_PREV;
    case 0x0E: /* Ctrl-N */
        return YAI_EDIT_NAV_NEXT;
    case 0x04: /* Ctrl-D: delete under cursor, or EOF on empty line */
        if (app->stdin_len == 0) {
            return YAI_EDIT_EOF;
        }
        return editor_cmd_delete(app, app->stdin_cursor,
                                 editor_ops_next_char(app, app->stdin_cursor));
    case 0x7F:
    case 0x08: /* Backspace / Ctrl-H */
        if (app->stdin_cursor > 0) {
            return editor_cmd_delete(app, editor_ops_prev_char(app, app->stdin_cursor),
                                     app->stdin_cursor);
        }
        return YAI_EDIT_NONE;
    case 0x0B: /* Ctrl-K: kill to end of line */
        return editor_cmd_kill(app, app->stdin_cursor, app->stdin_len);
    case 0x15: /* Ctrl-U: kill the line */
        return editor_cmd_kill(app, 0, app->stdin_len);
    case 0x17: /* Ctrl-W: kill word before cursor */
        return editor_cmd_kill(app, editor_ops_word_back(app, app->stdin_cursor),
                               app->stdin_cursor);
    case 0x19: /* Ctrl-Y: yank */
        return editor_cmd_yank(app);
    case 0x1F: /* Ctrl-_ / Ctrl-/ : undo the last command */
        return editor_cmd_undo(app);
    case 0x03: /* Ctrl-C */
        return YAI_EDIT_INTERRUPT;
    default:
        break;
    }
    if ((unsigned char)byte < 0x20) {
        return YAI_EDIT_NONE; /* other control bytes: ignore */
    }
    return editor_cmd_insert_char(app, byte);
}

YETTY_ANNOTATE("override@yai:emacs:feed_byte")
static struct yetty_ycore_int_result emacs_feed_byte(struct yetty_yclass_object *obj,
                                                     struct yai_app *app, int byte)
{
    (void)obj;
    app->edit_status[0] = '\0'; /* emacs has no modal indicator */
    /* No undo bookkeeping here: each command records its own step when it
     * runs (editor_cmd_*), so undo is mode-independent. This layer only
     * decodes the byte and maps it to a command. */
    char final_byte = 0;
    int param = 0;
    int status = editor_ops_csi(app, (char)byte, &final_byte, &param);
    switch (status) {
    case YAI_CSI_MID:
        return YETTY_OK(yetty_ycore_int, YAI_EDIT_NONE);
    case YAI_CSI_COMPLETE:
        return YETTY_OK(yetty_ycore_int, emacs_csi(app, final_byte, param));
    case YAI_CSI_META:
        return YETTY_OK(yetty_ycore_int, emacs_meta(app, final_byte));
    default: /* YAI_CSI_PLAIN */
        return YETTY_OK(yetty_ycore_int, emacs_plain(app, (char)byte));
    }
}

#include "editor-emacs.gen.c"
