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
 *   Enter             submit;  Ctrl-C interrupt;  Ctrl-D (empty) quit
 *
 * INSERT: printable → insert, Backspace, Enter (submit), Tab (menu),
 *   ESC → NORMAL, Ctrl-C / Ctrl-D as in NORMAL.
 */
#include "app.h"
#include "editor-ops.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdio.h>

struct [[clang::annotate("class@yai:vi")]] [[clang::annotate("parent@yai:editor")]] yetty_yai_vi {
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
    case 'A':
        return YAI_EDIT_NAV_PREV;
    case 'B':
        return YAI_EDIT_NAV_NEXT;
    case 'C':
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'D':
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'H':
        app->stdin_cursor = 0;
        return YAI_EDIT_MOVED;
    case 'F':
        app->stdin_cursor = app->stdin_len;
        return YAI_EDIT_MOVED;
    case '~':
        if (param == 3) {
            editor_ops_delete_range(app, app->stdin_cursor,
                                    editor_ops_next_char(app, app->stdin_cursor));
            return YAI_EDIT_CHANGED;
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
    if (motion == op) { /* dd / cc: the whole line */
        *from = 0;
        *to = app->stdin_len;
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
        *to = app->stdin_len;
        return 1;
    case '0':
    case '^':
        *from = 0;
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
            editor_ops_delete_range(app, app->stdin_cursor,
                                    editor_ops_next_char(app, app->stdin_cursor));
            if (!editor_ops_insert(app, &byte, 1)) {
                return YAI_EDIT_NONE;
            }
            app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
            return YAI_EDIT_CHANGED;
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
        editor_ops_kill_range(app, from, to);
        if (op == 'c') {
            vi->normal = 0; /* change → insert */
        }
        return YAI_EDIT_CHANGED;
    }

    switch (byte) {
    case 'h':
        app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'l':
    case ' ':
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case '0':
    case '^':
        app->stdin_cursor = 0;
        return YAI_EDIT_MOVED;
    case '$':
        app->stdin_cursor = vi_last_char(app, app->stdin_len);
        return YAI_EDIT_MOVED;
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
            editor_ops_kill_range(app, app->stdin_cursor,
                                  editor_ops_next_char(app, app->stdin_cursor));
            return YAI_EDIT_CHANGED;
        }
        return YAI_EDIT_NONE;
    case 'i':
        vi->normal = 0;
        return YAI_EDIT_NONE;
    case 'a':
        vi->normal = 0;
        app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        return YAI_EDIT_MOVED;
    case 'A':
        vi->normal = 0;
        app->stdin_cursor = app->stdin_len;
        return YAI_EDIT_MOVED;
    case 'I':
        vi->normal = 0;
        app->stdin_cursor = 0;
        return YAI_EDIT_MOVED;
    case 'D':
        editor_ops_kill_range(app, app->stdin_cursor, app->stdin_len);
        return YAI_EDIT_CHANGED;
    case 'C':
        editor_ops_kill_range(app, app->stdin_cursor, app->stdin_len);
        vi->normal = 0;
        return YAI_EDIT_CHANGED;
    case 'd':
    case 'c':
    case 'r':
        vi->pending_op = (unsigned char)byte;
        return YAI_EDIT_NONE;
    case 'p': /* paste the register after the cursor */
        if (app->stdin_cursor < app->stdin_len) {
            app->stdin_cursor = editor_ops_next_char(app, app->stdin_cursor);
        }
        return editor_ops_yank(app) ? YAI_EDIT_CHANGED : YAI_EDIT_NONE;
    case 'P': /* paste before the cursor */
        return editor_ops_yank(app) ? YAI_EDIT_CHANGED : YAI_EDIT_NONE;
    case 'u': /* undo the previous edit; repeat to walk further back */
        return editor_ops_undo_pop(app) ? YAI_EDIT_CHANGED : YAI_EDIT_NONE;
    case '.': { /* repeat the last text-changing command */
        int action = YAI_EDIT_NONE;
        for (int index = 0; index < vi->repeat_len; index++) {
            action = vi_normal_cmd(app, vi, vi->repeat[index]);
        }
        return action;
    }
    case '\r':
    case '\n':
        return YAI_EDIT_SUBMIT;
    case 0x03: /* Ctrl-C */
        return YAI_EDIT_INTERRUPT;
    case 0x04: /* Ctrl-D */
        return app->stdin_len == 0 ? YAI_EDIT_EOF : YAI_EDIT_NONE;
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
        return YAI_EDIT_SUBMIT;
    case '\t':
        return YAI_EDIT_COMPLETE;
    case 0x7F:
    case 0x08:
        if (app->stdin_cursor > 0) {
            editor_ops_delete_range(app, editor_ops_prev_char(app, app->stdin_cursor),
                                    app->stdin_cursor);
            return YAI_EDIT_CHANGED;
        }
        return YAI_EDIT_NONE;
    case 0x03:
        return YAI_EDIT_INTERRUPT;
    case 0x04:
        return app->stdin_len == 0 ? YAI_EDIT_EOF : YAI_EDIT_NONE;
    default:
        break;
    }
    if ((unsigned char)byte < 0x20) {
        return YAI_EDIT_NONE;
    }
    return editor_ops_insert(app, &byte, 1) ? YAI_EDIT_CHANGED : YAI_EDIT_NONE;
}

[[clang::annotate("override@yai:vi:feed_byte")]]
static struct yetty_ycore_int_result vi_feed_byte(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  struct yai_app *app, int byte)
{
    (void)ctx;
    struct yetty_yai_vi_ptr_result vi_res = yetty_yai_vi_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, vi_res, "vi feed_byte: data slice");
    struct yetty_yai_vi *vi = vi_res.value;

    /* ESC leaves insert mode immediately, so the indicator flips at
     * once. A following '[' is still decoded as a CSI arrow below. */
    if ((unsigned char)byte == 0x1B && !vi->normal) {
        vi->normal = 1;
        vi->pending_op = 0;
    }

    /* Snapshot the line before this keystroke mutates it, for undo. */
    char undo_pre[sizeof(app->stdin_buf)];
    size_t undo_pre_len = app->stdin_len;
    size_t undo_pre_cursor = app->stdin_cursor;
    memcpy(undo_pre, app->stdin_buf, undo_pre_len);

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
        /* lone ESC then a byte: already in normal mode — run it there. */
        vi->normal = 1;
        action = vi_normal_cmd(app, vi, final_byte);
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

    /* Undo bookkeeping. `u` is the pop itself, excluded here. A text change
     * opens (or continues) the current group and marks it dirty; resolving
     * back to NORMAL mode commits it — so a normal-mode command is one undo
     * step, and an insert session (group stays open until ESC) collapses
     * into a single step. Line resets (submit / cancel) clear the stack. */
    if ((char)byte != 'u') {
        if (action == YAI_EDIT_CHANGED) {
            editor_ops_undo_begin(app, undo_pre, undo_pre_len, undo_pre_cursor);
            editor_ops_undo_mark_dirty(app);
        }
        if (vi->normal) {
            editor_ops_undo_commit(app);
        }
    }
    if (action == YAI_EDIT_SUBMIT || action == YAI_EDIT_INTERRUPT || action == YAI_EDIT_EOF) {
        editor_ops_undo_clear(app);
    }

    snprintf(app->edit_status, sizeof(app->edit_status), "%s", vi->normal ? "[N]" : "[I]");
    return YETTY_OK(yetty_ycore_int, action);
}

#include "editor-vi.gen.c"
