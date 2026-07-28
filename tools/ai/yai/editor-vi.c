/*
 * editor-vi.c — yclass class `yai:vi`: vi-mode keystroke interpretation
 * for the line editor (subclass of yai:editor).
 *
 * Two sub-modes, like readline vi-mode: the editor starts in INSERT
 * (type straight away); ESC switches to NORMAL. The prompt shows the
 * current sub-mode via app->edit_status ("[I]" / "[N]").
 *
 * NORMAL commands (single-line subset; no repeat counts yet):
 *   h l  ← →          char left / right        (space = l)
 *   0 ^  $            start / end of line
 *   w b e             word forward / back / end
 *   x                 delete char under cursor (into the register)
 *   i a A I           insert: here / after / at $ / at 0
 *   D C               delete / change to end of line
 *   d{motion} dd      delete motion / whole line   (dw db de d$ d0 dd)
 *   c{motion} cc      change motion / whole line   (enters insert)
 *   r{char}           replace the char under cursor
 *   p P               paste the register after / before the cursor
 *   u                 undo the last change (repeat to walk back)
 *   Enter             submit;  Ctrl-C interrupt;  Ctrl-D (empty) quit
 *   Ctrl-R            fuzzy history search (fzy; no redo here, so the
 *                     readline-ish binding wins in both sub-modes)
 *
 * INSERT: printable → insert, Backspace, Ctrl-W (delete word before
 *   cursor), Enter (submit), Tab (menu), ESC → NORMAL, Ctrl-C / Ctrl-D
 *   / Ctrl-R as in NORMAL.
 */
#include "app.h"
#include "editor-ops.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdio.h>

struct YETTY_ANNOTATE("class@yai:vi") YETTY_ANNOTATE("parent@yai:editor") yetty_yai_vi {
    int normal;     /* 0 = insert sub-mode, 1 = normal */
    int pending_op; /* a pending operator byte: 'd', 'c', 'r', or 0 */
    /* `.` repeat: the byte sequence of the last text-changing normal
     * command that stayed in normal mode (x, dd, dw, D, r<c>, p, …).
     * `acc` accumulates the in-progress command's bytes. */
    char acc[16];
    int acc_len;
    char repeat[16];
    int repeat_len;
};

YETTY_YRESULT_DECLARE(yetty_yai_vi_ptr, struct yetty_yai_vi *);

/* Defined in the appended editor-vi.gen.c. */
struct yetty_yai_vi_ptr_result yetty_yai_vi_from(struct yetty_yclass_object *obj);

/* vi $ / e land ON the last char of a span, not past it. */
static size_t vi_last_char(const struct yai_app *app, size_t past_end)
{
    return past_end > 0 ? editor_ops_prev_char(app, past_end) : 0;
}

/* CSI arrow / home / end / delete — works in both sub-modes. */
static int vi_csi(struct yai_app *app, char final_byte, int param)
{
    switch (final_byte) {
    case 'A': /* up: previous line, or history at the top line */
        return editor_cmd_line_up(app);
    case 'B': /* down: next line, or history at the bottom line */
        return editor_cmd_line_down(app);
    case 'C':
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'D':
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'H': /* home: start of the current line */
        app->stdin_cursor = editor_ops_line_start(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'F': /* end: end of the current line */
        app->stdin_cursor = editor_ops_line_end(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case '~':
        if (param == 3) {
            return editor_cmd_delete(app, app->stdin_cursor,
                                     editor_ops_next_char(app, app->stdin_cursor));
        }
        return YAI_EDIT_NONE;
    default:
        return YAI_EDIT_NONE;
    }
}

/* Resolve a `d`/`c` motion byte to a [from, to) byte span. Returns 1 on
 * a recognized motion, 0 otherwise. `op` is the operator (to detect the
 * doubled dd / cc whole-line form). */
static int vi_motion_span(struct yai_app *app, char op, char motion, size_t *from, size_t *to)
{
    size_t cursor = app->stdin_cursor;
    if (motion == op) { /* dd / cc: the whole current line */
        size_t line_start = editor_ops_line_start(app, cursor);
        size_t line_end = editor_ops_line_end(app, cursor);
        if (op == 'c') {
            /* cc clears the line's text but keeps it as a line (newline stays). */
            *from = line_start;
            *to = line_end;
        } else if (line_end < app->stdin_len) {
            /* dd removes the line and its trailing newline (joins the next up). */
            *from = line_start;
            *to = line_end + 1;
        } else if (line_start > 0) {
            /* Last line: take the preceding newline so no blank line is left. */
            *from = line_start - 1;
            *to = line_end;
        } else {
            *from = line_start; /* the only line */
            *to = line_end;
        }
        return 1;
    }
    switch (motion) {
    case 'w':
        *from = cursor;
        *to = editor_ops_word_forward(app, cursor);
        return 1;
    case 'e':
        *from = cursor;
        *to = editor_ops_word_forward(app, cursor);
        return 1;
    case 'b':
        *from = editor_ops_word_back(app, cursor);
        *to = cursor;
        return 1;
    case '$':
        *from = cursor;
        *to = editor_ops_line_end(app, cursor);
        return 1;
    case '0':
        *from = editor_ops_line_start(app, cursor);
        *to = cursor;
        return 1;
    case '^':
        *from = editor_ops_line_first_nonblank(app, cursor);
        *to = cursor;
        return 1;
    case 'l':
    case ' ':
        *from = cursor;
        *to = editor_ops_next_char(app, cursor);
        return 1;
    case 'h':
        *from = editor_ops_prev_char(app, cursor);
        *to = cursor;
        return 1;
    default:
        return 0;
    }
}

static int vi_normal_cmd(struct yai_app *app, struct yetty_yai_vi *vi, char byte)
{
    /* A pending operator consumes this byte. */
    if (vi->pending_op == 'r') {
        vi->pending_op = 0;
        if ((unsigned char)byte >= 0x20 && app->stdin_cursor < app->stdin_len) {
            return editor_cmd_replace_char(app, app->stdin_cursor,
                                           editor_ops_next_char(app, app->stdin_cursor), byte);
        }
        return YAI_EDIT_NONE;
    }
    if (vi->pending_op == 'd' || vi->pending_op == 'c') {
        char op = (char)vi->pending_op;
        vi->pending_op = 0;
        size_t from = 0;
        size_t to = 0;
        if (!vi_motion_span(app, op, byte, &from, &to)) {
            return YAI_EDIT_NONE;
        }
        int action = editor_cmd_kill(app, from, to);
        if (op == 'c') {
            vi->normal = 0; /* change → insert */
            if (action == YAI_EDIT_NONE) {
                action = YAI_EDIT_MOVED; /* empty change still flips the indicator */
            }
        }
        return action;
    }

    switch (byte) {
    case 'h':
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'l':
    case ' ':
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'k': /* up: previous line, or history at the top line */
        return editor_cmd_line_up(app);
    case 'j': /* down: next line, or history at the bottom line */
        return editor_cmd_line_down(app);
    case '0': /* first column of the current line */
        app->stdin_cursor = editor_ops_line_start(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case '^': /* first non-blank of the current line */
        app->stdin_cursor = editor_ops_line_first_nonblank(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case '$': { /* last char of the current line */
        size_t line_start = editor_ops_line_start(app, app->stdin_cursor);
        size_t line_end = editor_ops_line_end(app, app->stdin_cursor);
        app->stdin_cursor = line_end > line_start ? vi_last_char(app, line_end) : line_start;
        return YAI_EDIT_MOVED;
    }
    case 'w':
        app->stdin_cursor = editor_ops_word_forward(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'b':
        app->stdin_cursor = editor_ops_word_back(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'e':
        app->stdin_cursor = vi_last_char(app, editor_ops_word_forward(app, app->stdin_cursor));
        return YAI_EDIT_MOVED;
    case 'x':
        if (app->stdin_cursor < app->stdin_len) {
            return editor_cmd_kill(app, app->stdin_cursor,
                                   editor_ops_next_char(app, app->stdin_cursor));
        }
        return YAI_EDIT_NONE;
    case 'i':
        vi->normal = 0;
        return YAI_EDIT_NONE;
    case 'a':
        vi->normal = 0;
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'A': /* append at end of the current line */
        vi->normal = 0;
        app->stdin_cursor = editor_ops_line_end(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'I': /* insert before the first non-blank of the current line */
        vi->normal = 0;
        app->stdin_cursor = editor_ops_line_first_nonblank(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'D': /* kill to end of the current line */
        return editor_cmd_kill(app, app->stdin_cursor, editor_ops_line_end(app, app->stdin_cursor));
    case 'C': { /* change to end of the current line */
        int action =
            editor_cmd_kill(app, app->stdin_cursor, editor_ops_line_end(app, app->stdin_cursor));
        vi->normal = 0;
        return action == YAI_EDIT_NONE ? YAI_EDIT_MOVED : action;
    }
    case 'd':
    case 'c':
    case 'r':
        vi->pending_op = (unsigned char)byte;
        return YAI_EDIT_NONE;
    case 'p': /* paste the register after the cursor */
        if (app->stdin_cursor < app->stdin_len) {
            app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        }
        return editor_cmd_yank(app);
    case 'P': /* paste before the cursor */
        return editor_cmd_yank(app);
    case 'u': /* undo the previous command; repeat to walk further back */
        return editor_cmd_undo(app);
    case '.': { /* repeat the last text-changing command */
        int action = YAI_EDIT_NONE;
        for (int index = 0; index < vi->repeat_len; index++) {
            action = vi_normal_cmd(app, vi, vi->repeat[index]);
        }
        return action;
    }
    case '\r':
    case '\n':
        return editor_cmd_enter(app, /*modified=*/0);
    case 0x03: /* Ctrl-C */
        return YAI_EDIT_INTERRUPT;
    case 0x04: /* Ctrl-D */
        return app->stdin_len == 0 ? YAI_EDIT_EOF : YAI_EDIT_NONE;
    case 0x12: /* Ctrl-R: fuzzy history search */
        return YAI_EDIT_HISTORY_SEARCH;
    default:
        return YAI_EDIT_NONE; /* unknown command / repeat counts: ignored */
    }
}

static int vi_insert(struct yai_app *app, struct yetty_yai_vi *vi, char byte)
{
    (void)vi;
    switch ((unsigned char)byte) {
    case '\r':
    case '\n':
        return editor_cmd_enter(app, /*modified=*/0);
    case '\t':
        return YAI_EDIT_COMPLETE;
    case 0x7F:
    case 0x08:
        if (app->stdin_cursor > 0) {
            return editor_cmd_delete(app, editor_ops_prev_char(app, app->stdin_cursor),
                                     app->stdin_cursor);
        }
        return YAI_EDIT_NONE;
    case 0x17: /* Ctrl-W: delete the word before the cursor (vi insert) */
        if (app->stdin_cursor > 0) {
            return editor_cmd_kill(app, editor_ops_word_back(app, app->stdin_cursor),
                                   app->stdin_cursor);
        }
        return YAI_EDIT_NONE;
    case 0x03:
        return YAI_EDIT_INTERRUPT;
    case 0x04:
        return app->stdin_len == 0 ? YAI_EDIT_EOF : YAI_EDIT_NONE;
    case 0x12: /* Ctrl-R: fuzzy history search */
        return YAI_EDIT_HISTORY_SEARCH;
    default:
        break;
    }
    if ((unsigned char)byte < 0x20) {
        return YAI_EDIT_NONE;
    }
    return editor_cmd_insert_char(app, byte);
}

YETTY_ANNOTATE("override@yai:vi:feed_byte")
static struct yetty_ycore_int_result vi_feed_byte(struct yetty_yclass_object *obj,
                                                  struct yai_app *app, int byte)
{
    struct yetty_yai_vi_ptr_result vi_res = yetty_yai_vi_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, vi_res, "vi feed_byte: data slice");
    struct yetty_yai_vi *vi = vi_res.value;

    /* ESC leaves insert mode immediately, so the indicator flips at
     * once. A following '[' is still decoded as a CSI arrow below. */
    if ((unsigned char)byte == 0x1B && !vi->normal) {
        vi->normal = 1;
        vi->pending_op = 0;
    }

    char final_byte = 0;
    int param = 0;
    int status = editor_ops_csi(app, (char)byte, &final_byte, &param);
    int action = YAI_EDIT_NONE;
    switch (status) {
    case YAI_CSI_MID:
        action = YAI_EDIT_NONE;
        break;
    case YAI_CSI_COMPLETE:
        action = vi_csi(app, final_byte, param);
        break;
    case YAI_CSI_META:
        /* lone ESC then a byte: already in normal mode — run it there.
         * ESC-then-Enter and Alt+Enter are the same bytes, so a Meta Enter
         * is treated as the modified Enter (the opposite of a plain Enter). */
        vi->normal = 1;
        if (final_byte == '\r' || final_byte == '\n') {
            action = editor_cmd_enter(app, /*modified=*/1);
        } else {
            action = vi_normal_cmd(app, vi, final_byte);
        }
        break;
    default: /* YAI_CSI_PLAIN */
        if (vi->normal) {
            /* Record the command's bytes so `.` can replay it. The '.'
             * command itself is never recorded (it replays `repeat`). */
            if ((char)byte != '.' && vi->acc_len < (int)sizeof(vi->acc)) {
                vi->acc[vi->acc_len++] = (char)byte;
            }
            action = vi_normal_cmd(app, vi, (char)byte);
            if (vi->pending_op == 0) {
                /* Command resolved: keep it as the repeat sequence only
                 * if it changed text and stayed in normal mode (an edit
                 * that drops into insert can't be replayed verbatim). */
                if (action == YAI_EDIT_CHANGED && vi->normal && (char)byte != '.' &&
                    (char)byte != 'u' && vi->acc_len > 0) {
                    memcpy(vi->repeat, vi->acc, (size_t)vi->acc_len);
                    vi->repeat_len = vi->acc_len;
                }
                vi->acc_len = 0;
            }
        } else {
            action = vi_insert(app, vi, (char)byte);
        }
        break;
    }

    /* No undo bookkeeping here: each command recorded its own step when it
     * ran (editor_cmd_*), so undo is mode-independent. This layer only maps
     * keystrokes to commands and tracks the `.`-repeat sequence above. */
    snprintf(app->edit_status, sizeof(app->edit_status), "%s", vi->normal ? "[N]" : "[I]");
    return YETTY_OK(yetty_ycore_int, action);
}

#include "yetty/gen/impl/yai/editor-vi.c"
