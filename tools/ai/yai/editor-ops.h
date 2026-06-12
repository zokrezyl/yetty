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

#include <string.h>

#define YAI_EDIT_NONE 0      /* byte consumed, nothing to do */
#define YAI_EDIT_CHANGED 1   /* text changed: history reset + repaint + menu */
#define YAI_EDIT_MOVED 2     /* cursor moved only: repaint */
#define YAI_EDIT_SUBMIT 3    /* Enter */
#define YAI_EDIT_EOF 4       /* Ctrl-D on an empty line */
#define YAI_EDIT_INTERRUPT 5 /* Ctrl-C */
#define YAI_EDIT_NAV_PREV 6  /* up: menu-up or history-prev */
#define YAI_EDIT_NAV_NEXT 7  /* down: menu-down or history-next */
#define YAI_EDIT_COMPLETE 8  /* Tab: accept menu selection */

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
 * Word motion (whitespace-delimited; matches the old Ctrl-W behaviour).
 *---------------------------------------------------------------------------*/

/* Start of the word at/just before `position`: skip trailing spaces,
 * then the word body. */
static inline size_t editor_ops_word_back(const struct yai_app *app, size_t position)
{
    while (position > 0 && app->stdin_buf[position - 1] == ' ') {
        position--;
    }
    while (position > 0 && app->stdin_buf[position - 1] != ' ') {
        position--;
    }
    return position;
}

/* Just past the end of the word at/after `position`: skip leading
 * spaces, then the word body. */
static inline size_t editor_ops_word_forward(const struct yai_app *app, size_t position)
{
    while (position < app->stdin_len && app->stdin_buf[position] == ' ') {
        position++;
    }
    while (position < app->stdin_len && app->stdin_buf[position] != ' ') {
        position++;
    }
    return position;
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
