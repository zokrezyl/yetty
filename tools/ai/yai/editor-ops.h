/*
 * editor-ops.h — shared, mode-independent line-editing primitives for
 * the yclass editor subclasses (yai:emacs, yai:vi).
 *
 * All ops act on `struct yai_app`'s line buffer (stdin_buf/len/cursor +
 * kill_buf/len). They are static-inline so both subclass translation
 * units share them without a separate object file. The ESC/CSI decoder
 * lives here too, since arrow/home/end/delete sequences are decoded the
 * same way in both modes.
 *
 * The editor's `feed_byte` slot returns one of these action codes (as
 * the int value of a yetty_ycore_int_result); main.c maps each to a UI
 * effect, keeping menu/history/submit policy out of the editor.
 */
#ifndef YAI_EDITOR_OPS_H
#define YAI_EDITOR_OPS_H

#include "app.h"

#include <stdlib.h>
#include <string.h>

#define YAI_EDIT_NONE 0           /* byte consumed, nothing to do */
#define YAI_EDIT_CHANGED 1        /* text changed: history reset + repaint + menu */
#define YAI_EDIT_MOVED 2          /* cursor moved only: repaint */
#define YAI_EDIT_SUBMIT 3         /* Enter */
#define YAI_EDIT_EOF 4            /* Ctrl-D on an empty line */
#define YAI_EDIT_INTERRUPT 5      /* Ctrl-C */
#define YAI_EDIT_NAV_PREV 6       /* up: menu-up or history-prev */
#define YAI_EDIT_NAV_NEXT 7       /* down: menu-down or history-next */
#define YAI_EDIT_COMPLETE 8       /* Tab: accept menu selection */
#define YAI_EDIT_HISTORY_SEARCH 9 /* Ctrl-R: fzy history search */

/* ESC/CSI decoder status (see editor_ops_csi). */
#define YAI_CSI_MID 0      /* byte consumed mid-sequence; emit NONE */
#define YAI_CSI_PLAIN 1    /* not part of an escape; handle the byte */
#define YAI_CSI_COMPLETE 2 /* a CSI sequence finished: *final / *param valid */
#define YAI_CSI_META 3     /* lone ESC then *final (a Meta/mode-exit byte) */

/*---------------------------------------------------------------------------
 * UTF-8 cursor motion (codepoint-aware; one cell per codepoint).
 *---------------------------------------------------------------------------*/

static inline size_t editor_ops_prev_char(const struct yai_app *app, size_t position)
{
    while (position > 0 && (app->stdin_buf[position - 1] & 0xC0) == 0x80) {
        position--;
    }
    return position > 0 ? position - 1 : 0;
}

static inline size_t editor_ops_next_char(const struct yai_app *app, size_t position)
{
    if (position >= app->stdin_len) {
        return app->stdin_len;
    }
    position++;
    while (position < app->stdin_len && (app->stdin_buf[position] & 0xC0) == 0x80) {
        position++;
    }
    return position;
}

/*---------------------------------------------------------------------------
 * Word motion (whitespace-delimited). A newline is a separator, not part of a
 * word, so word motions cross line breaks without merging the lines on either
 * side into one word.
 *---------------------------------------------------------------------------*/

static inline int editor_ops_is_word_sep(char byte)
{
    return byte == ' ' || byte == '\t' || byte == '\n';
}

/* Start of the word at/just before `position`: skip trailing separators,
 * then the word body. */
static inline size_t editor_ops_word_back(const struct yai_app *app, size_t position)
{
    while (position > 0 && editor_ops_is_word_sep(app->stdin_buf[position - 1])) {
        position--;
    }
    while (position > 0 && !editor_ops_is_word_sep(app->stdin_buf[position - 1])) {
        position--;
    }
    return position;
}

/* Just past the end of the word at/after `position`: skip leading
 * separators, then the word body. */
static inline size_t editor_ops_word_forward(const struct yai_app *app, size_t position)
{
    while (position < app->stdin_len && editor_ops_is_word_sep(app->stdin_buf[position])) {
        position++;
    }
    while (position < app->stdin_len && !editor_ops_is_word_sep(app->stdin_buf[position])) {
        position++;
    }
    return position;
}

/*---------------------------------------------------------------------------
 * Logical-line motion (lines split on '\n'; the buffer may be multi-line when
 * the user inserts newlines). Columns are counted in codepoints.
 *---------------------------------------------------------------------------*/

/* Byte offset of the start of the logical line containing `position` (just
 * after the preceding '\n', or 0). */
static inline size_t editor_ops_line_start(const struct yai_app *app, size_t position)
{
    while (position > 0 && app->stdin_buf[position - 1] != '\n') {
        position--;
    }
    return position;
}

/* Byte offset of the end of the logical line containing `position` (the next
 * '\n', or stdin_len). */
static inline size_t editor_ops_line_end(const struct yai_app *app, size_t position)
{
    while (position < app->stdin_len && app->stdin_buf[position] != '\n') {
        position++;
    }
    return position;
}

/* Byte offset of the first non-blank on the line containing `position`
 * (vi `^` / `I`), or the line end when the line is all blanks. */
static inline size_t editor_ops_line_first_nonblank(const struct yai_app *app, size_t position)
{
    size_t cursor = editor_ops_line_start(app, position);
    while (cursor < app->stdin_len &&
           (app->stdin_buf[cursor] == ' ' || app->stdin_buf[cursor] == '\t')) {
        cursor++;
    }
    return cursor;
}

/* Codepoints between `from` and `to` (to >= from). */
static inline size_t editor_ops_column_span(const struct yai_app *app, size_t from, size_t to)
{
    size_t columns = 0;
    while (from < to) {
        from = editor_ops_next_char(app, from);
        columns++;
    }
    return columns;
}

/* Advance `columns` codepoints from `from`, stopping at `limit`. */
static inline size_t editor_ops_advance_columns(const struct yai_app *app, size_t from,
                                                size_t limit, size_t columns)
{
    while (columns > 0 && from < limit) {
        from = editor_ops_next_char(app, from);
        columns--;
    }
    return from > limit ? limit : from;
}

/*---------------------------------------------------------------------------
 * Buffer mutation.
 *---------------------------------------------------------------------------*/

/* Remove [from, to); cursor lands at `from`. */
static inline void editor_ops_delete_range(struct yai_app *app, size_t from, size_t to)
{
    if (to <= from || to > app->stdin_len) {
        return;
    }
    memmove(app->stdin_buf + from, app->stdin_buf + to, app->stdin_len - to);
    app->stdin_len -= to - from;
    app->stdin_cursor = from;
}

/* Copy [from, to) into the kill ring, then delete it. */
static inline void editor_ops_kill_range(struct yai_app *app, size_t from, size_t to)
{
    if (to <= from || to > app->stdin_len) {
        return;
    }
    size_t span = to - from;
    if (span >= sizeof(app->kill_buf)) {
        span = sizeof(app->kill_buf) - 1;
    }
    memcpy(app->kill_buf, app->stdin_buf + from, span);
    app->kill_len = span;
    editor_ops_delete_range(app, from, to);
}

/* Insert `len` bytes at the cursor. Returns 1 on success, 0 if it would
 * overflow the line buffer. */
static inline int editor_ops_insert(struct yai_app *app, const char *bytes, size_t len)
{
    if (len == 0) {
        return 1;
    }
    if (app->stdin_len + len + 1 > sizeof(app->stdin_buf)) {
        return 0;
    }
    memmove(app->stdin_buf + app->stdin_cursor + len, app->stdin_buf + app->stdin_cursor,
            app->stdin_len - app->stdin_cursor);
    memcpy(app->stdin_buf + app->stdin_cursor, bytes, len);
    app->stdin_len += len;
    app->stdin_cursor += len;
    return 1;
}

/* Insert the kill ring at the cursor. Returns 1 on success, 0 on
 * overflow. */
static inline int editor_ops_yank(struct yai_app *app)
{
    return editor_ops_insert(app, app->kill_buf, app->kill_len);
}

/*---------------------------------------------------------------------------
 * Multi-level undo, shared by both editor modes (vi `u`, emacs Ctrl-_ /
 * Ctrl-/). One undo step == one logical edit: every discrete command
 * (delete, kill, paste, replace, …) is its own step, and a run of typed
 * text is split into words (a space typed after a non-space ends a word).
 * Popping a step restores the line as it was before that edit, so repeated
 * undo walks back command by command / word by word.
 *
 * Each text-changing keystroke is recorded through one of two helpers, so
 * the granularity is identical in both modes:
 *   editor_ops_undo_note_insert   — extend the current typed-word group
 *   editor_ops_undo_note_discrete — a self-contained single-step edit
 *---------------------------------------------------------------------------*/

/* Free the whole undo stack and drop any open group. Call when the line
 * is reset (submitted / cancelled). */
static inline void editor_ops_undo_clear(struct yai_app *app)
{
    for (int index = 0; index < app->undo_depth; index++) {
        free(app->undo_stack[index].bytes);
        app->undo_stack[index].bytes = NULL;
    }
    app->undo_depth = 0;
    app->undo_group_open = 0;
    app->undo_group_dirty = 0;
}

/* Push a pre-change snapshot onto the stack. Drops the oldest entry when
 * full so recent history survives. Best-effort: silently no-ops on OOM. */
static inline void editor_ops_undo_push(struct yai_app *app, const char *bytes, size_t len,
                                        size_t cursor)
{
    char *copy = malloc(len ? len : 1);
    if (!copy) {
        return;
    }
    memcpy(copy, bytes, len);
    if (app->undo_depth == YAI_UNDO_MAX) {
        free(app->undo_stack[0].bytes);
        memmove(&app->undo_stack[0], &app->undo_stack[1],
                (YAI_UNDO_MAX - 1) * sizeof(app->undo_stack[0]));
        app->undo_depth--;
    }
    app->undo_stack[app->undo_depth].bytes = copy;
    app->undo_stack[app->undo_depth].len = len;
    app->undo_stack[app->undo_depth].cursor = cursor;
    app->undo_depth++;
}

/* Open an undo group with the given pre-change snapshot, unless one is
 * already open (so an insert session keeps its original pre-state). */
static inline void editor_ops_undo_begin(struct yai_app *app, const char *bytes, size_t len,
                                         size_t cursor)
{
    if (app->undo_group_open) {
        return;
    }
    size_t span = len <= sizeof(app->undo_group_buf) ? len : sizeof(app->undo_group_buf);
    memcpy(app->undo_group_buf, bytes, span);
    app->undo_group_len = span;
    app->undo_group_cursor = cursor;
    app->undo_group_open = 1;
    app->undo_group_dirty = 0;
}

/* Mark the open group as having actually changed text. */
static inline void editor_ops_undo_mark_dirty(struct yai_app *app)
{
    if (app->undo_group_open) {
        app->undo_group_dirty = 1;
    }
}

/* Commit the open group: push its pre-state only if text actually changed,
 * then close the group. */
static inline void editor_ops_undo_commit(struct yai_app *app)
{
    if (app->undo_group_open && app->undo_group_dirty) {
        editor_ops_undo_push(app, app->undo_group_buf, app->undo_group_len, app->undo_group_cursor);
    }
    app->undo_group_open = 0;
    app->undo_group_dirty = 0;
}

/* Record a self-inserted byte into the current typed-word group. Consecutive
 * inserts accumulate into one group (one word); a space typed right after a
 * non-space first commits the finished word, so each word is its own step.
 * The group stays open — it is committed when the typing run ends (a word
 * boundary, a discrete edit, leaving insert mode, or an undo). */
static inline void editor_ops_undo_note_insert(struct yai_app *app, const char *pre_bytes,
                                               size_t pre_len, size_t pre_cursor,
                                               char inserted_byte)
{
    if (inserted_byte == ' ' && pre_cursor > 0 && pre_bytes[pre_cursor - 1] != ' ') {
        editor_ops_undo_commit(app);
    }
    editor_ops_undo_begin(app, pre_bytes, pre_len, pre_cursor);
    editor_ops_undo_mark_dirty(app);
}

/* Record a discrete edit (delete, kill, paste, replace, …) as its own undo
 * step: close any open typed-word group first, then push this operation's
 * pre-state alone, so one undo reverses exactly this one command. */
static inline void editor_ops_undo_note_discrete(struct yai_app *app, const char *pre_bytes,
                                                 size_t pre_len, size_t pre_cursor)
{
    editor_ops_undo_commit(app);
    editor_ops_undo_begin(app, pre_bytes, pre_len, pre_cursor);
    editor_ops_undo_mark_dirty(app);
    editor_ops_undo_commit(app);
}

/* Pop one undo level into the live buffer. Returns 1 if something was
 * restored, 0 if the stack was empty. */
static inline int editor_ops_undo_pop(struct yai_app *app)
{
    /* A typed-word group may still be open (emacs has no mode switch to
     * close it) — commit it first so undo reverses the in-progress word
     * before walking into earlier steps. */
    editor_ops_undo_commit(app);
    if (app->undo_depth == 0) {
        return 0;
    }
    app->undo_depth--;
    struct yai_undo_entry *entry = &app->undo_stack[app->undo_depth];
    size_t span = entry->len <= sizeof(app->stdin_buf) ? entry->len : sizeof(app->stdin_buf);
    memcpy(app->stdin_buf, entry->bytes, span);
    app->stdin_len = span;
    app->stdin_cursor = entry->cursor <= span ? entry->cursor : span;
    free(entry->bytes);
    entry->bytes = NULL;
    return 1;
}

/*---------------------------------------------------------------------------
 * Abstract editing commands — the unit of undo.
 *
 * Keystrokes in EVERY mode (vi, emacs) translate to these calls; each command
 * performs its edit AND records its own undo step, so undo is identical
 * regardless of editor mode: one undo reverses exactly one command. The mode
 * layers are pure key→command mappers — they never touch the undo stack.
 *
 * A self-inserted character accumulates into a word (a space after a
 * non-space ends the word); every other command is one discrete undo step.
 * Each returns a YAI_EDIT_* action code (CHANGED on success, NONE on no-op).
 *---------------------------------------------------------------------------*/

/* Insert one typed character at the cursor (word-grouped undo). */
static inline int editor_cmd_insert_char(struct yai_app *app, char byte)
{
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    if (!editor_ops_insert(app, &byte, 1)) {
        return YAI_EDIT_NONE;
    }
    editor_ops_undo_note_insert(app, pre, pre_len, pre_cursor, byte);
    return YAI_EDIT_CHANGED;
}

/* Delete the byte range [from, to) (one discrete undo step). */
static inline int editor_cmd_delete(struct yai_app *app, size_t from, size_t to)
{
    if (to <= from || to > app->stdin_len) {
        return YAI_EDIT_NONE;
    }
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    editor_ops_delete_range(app, from, to);
    editor_ops_undo_note_discrete(app, pre, pre_len, pre_cursor);
    return YAI_EDIT_CHANGED;
}

/* Kill the byte range [from, to) into the register (one discrete undo step). */
static inline int editor_cmd_kill(struct yai_app *app, size_t from, size_t to)
{
    if (to <= from || to > app->stdin_len) {
        return YAI_EDIT_NONE;
    }
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    editor_ops_kill_range(app, from, to);
    editor_ops_undo_note_discrete(app, pre, pre_len, pre_cursor);
    return YAI_EDIT_CHANGED;
}

/* Paste the register at the cursor (one discrete undo step). */
static inline int editor_cmd_yank(struct yai_app *app)
{
    if (app->kill_len == 0) {
        return YAI_EDIT_NONE;
    }
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    if (!editor_ops_yank(app)) {
        return YAI_EDIT_NONE;
    }
    editor_ops_undo_note_discrete(app, pre, pre_len, pre_cursor);
    return YAI_EDIT_CHANGED;
}

/* Replace the char range [from, to) with one byte and land the cursor on it
 * (vi `r`; one discrete undo step). */
static inline int editor_cmd_replace_char(struct yai_app *app, size_t from, size_t to, char byte)
{
    if (to <= from || to > app->stdin_len) {
        return YAI_EDIT_NONE;
    }
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    editor_ops_delete_range(app, from, to);
    if (!editor_ops_insert(app, &byte, 1)) {
        return YAI_EDIT_NONE;
    }
    app->stdin_cursor = editor_ops_prev_char(app, app->stdin_cursor);
    editor_ops_undo_note_discrete(app, pre, pre_len, pre_cursor);
    return YAI_EDIT_CHANGED;
}

/* Undo the last command (pop one step). */
static inline int editor_cmd_undo(struct yai_app *app)
{
    return editor_ops_undo_pop(app) ? YAI_EDIT_CHANGED : YAI_EDIT_NONE;
}

/* Resolve the goal column for a vertical move: a fresh run (the cursor is not
 * where the last vertical move left it) seeds the goal from the current column;
 * a continuation keeps it, so up/down across short lines returns to the column
 * the run started from. */
static inline void editor_ops_vmotion_seed_goal(struct yai_app *app)
{
    if (app->stdin_cursor != app->edit_vmotion_cursor) {
        size_t line_start = editor_ops_line_start(app, app->stdin_cursor);
        app->edit_goal_column = editor_ops_column_span(app, line_start, app->stdin_cursor);
    }
}

/* Move the cursor up one logical line at the goal column. Returns MOVED when a
 * line above exists, or NAV_PREV at the top (the caller falls back to history /
 * the menu — the up-line-or-history idiom). */
static inline int editor_cmd_line_up(struct yai_app *app)
{
    size_t line_start = editor_ops_line_start(app, app->stdin_cursor);
    if (line_start == 0) {
        app->edit_vmotion_cursor = (size_t)-1; /* boundary → end the run */
        return YAI_EDIT_NAV_PREV;
    }
    editor_ops_vmotion_seed_goal(app);
    size_t prev_start = editor_ops_line_start(app, line_start - 1);
    app->stdin_cursor =
        editor_ops_advance_columns(app, prev_start, line_start - 1, app->edit_goal_column);
    app->edit_vmotion_cursor = app->stdin_cursor;
    return YAI_EDIT_MOVED;
}

/* Move the cursor down one logical line at the goal column. Returns MOVED when a
 * line below exists, or NAV_NEXT at the bottom (history / menu fallback). */
static inline int editor_cmd_line_down(struct yai_app *app)
{
    size_t line_end = editor_ops_line_end(app, app->stdin_cursor);
    if (line_end >= app->stdin_len) {
        app->edit_vmotion_cursor = (size_t)-1; /* boundary → end the run */
        return YAI_EDIT_NAV_NEXT;
    }
    editor_ops_vmotion_seed_goal(app);
    size_t next_start = line_end + 1;
    size_t next_end = editor_ops_line_end(app, next_start);
    app->stdin_cursor =
        editor_ops_advance_columns(app, next_start, next_end, app->edit_goal_column);
    app->edit_vmotion_cursor = app->stdin_cursor;
    return YAI_EDIT_MOVED;
}

/* Insert a literal newline at the cursor (one discrete undo step). The buffer
 * may then span several lines; the renderer wraps it across rows and the whole
 * multi-line text is sent as one prompt on submit. */
static inline int editor_cmd_newline(struct yai_app *app)
{
    char pre[sizeof(app->stdin_buf)];
    size_t pre_len = app->stdin_len;
    size_t pre_cursor = app->stdin_cursor;
    memcpy(pre, app->stdin_buf, pre_len);
    char newline = '\n';
    if (!editor_ops_insert(app, &newline, 1)) {
        return YAI_EDIT_NONE;
    }
    editor_ops_undo_note_discrete(app, pre, pre_len, pre_cursor);
    return YAI_EDIT_CHANGED;
}

/* Resolve an Enter keystroke to its action under the configured submit-key
 * policy. `modified` is the Alt/Meta-Enter variant, which always does the
 * opposite of a plain Enter. app->enter_submits selects which one submits:
 * when set, plain Enter submits and Alt+Enter inserts a newline; when clear,
 * the two swap. */
static inline int editor_cmd_enter(struct yai_app *app, int modified)
{
    /* Inside a paste burst every line break is literal content, never a submit
     * — otherwise a pasted N-line block fires N messages. */
    if (app->in_paste) {
        return editor_cmd_newline(app);
    }
    int submit = modified ? !app->enter_submits : app->enter_submits;
    return submit ? YAI_EDIT_SUBMIT : editor_cmd_newline(app);
}

/*---------------------------------------------------------------------------
 * ESC/CSI decoder. Drives app->escape_state / app->escape_param.
 *
 *   state 0: normal. ESC (0x1B) → state 1.
 *   state 1: saw ESC. '[' → state 2 (CSI); else → YAI_CSI_META with the
 *            following byte (a Meta chord in emacs, a mode-exit + command
 *            in vi). The lone ESC is thus deferred until the next byte —
 *            the usual single-line-editor compromise.
 *   state 2: in CSI. digits/';' accumulate the parameter; a final byte
 *            (0x40..0x7E) completes the sequence.
 *---------------------------------------------------------------------------*/

static inline int editor_ops_csi(struct yai_app *app, char byte, char *out_final, int *out_param)
{
    if (app->escape_state == 1) {
        if (byte == '[') {
            app->escape_state = 2;
            app->escape_param = 0;
            return YAI_CSI_MID;
        }
        app->escape_state = 0;
        *out_final = byte;
        return YAI_CSI_META;
    }
    if (app->escape_state == 2) {
        if (byte >= '0' && byte <= '9') {
            app->escape_param = app->escape_param * 10 + (byte - '0');
            return YAI_CSI_MID;
        }
        if (byte == ';') {
            app->escape_param = 0; /* keep only the last parameter group */
            return YAI_CSI_MID;
        }
        if ((unsigned char)byte >= 0x40 && (unsigned char)byte <= 0x7E) {
            app->escape_state = 0;
            *out_final = byte;
            *out_param = app->escape_param;
            return YAI_CSI_COMPLETE;
        }
        return YAI_CSI_MID; /* stray byte inside CSI: swallow */
    }
    if ((unsigned char)byte == 0x1B) {
        app->escape_state = 1;
        return YAI_CSI_MID;
    }
    return YAI_CSI_PLAIN;
}

#endif /* YAI_EDITOR_OPS_H */
