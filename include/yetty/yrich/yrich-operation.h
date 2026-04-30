#ifndef YETTY_YRICH_YRICH_OPERATION_H
#define YETTY_YRICH_YRICH_OPERATION_H

/*
 * yrich-operation — atomic document changes.
 *
 * The C++ POC layered a CRDT-ish sync model on top of operations. The C port
 * keeps the operation type and inversion logic (so undo works) but elides
 * Lamport timestamps and SessionManager — sync isn't yet ported. This means
 * the operation log is a flat in-memory list and timestamps are simple
 * monotonic counters.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t yetty_yrich_session_id;
#define YETTY_YRICH_LOCAL_SESSION ((yetty_yrich_session_id)0)

/*=============================================================================
 * Operation types
 *===========================================================================*/

enum yetty_yrich_op_type {
    YETTY_YRICH_OP_INSERT = 0,
    YETTY_YRICH_OP_DELETE,
    YETTY_YRICH_OP_UPDATE,
    YETTY_YRICH_OP_TEXT_INSERT,
    YETTY_YRICH_OP_TEXT_DELETE,
    YETTY_YRICH_OP_TEXT_FORMAT,
    YETTY_YRICH_OP_CELL_SET,
    YETTY_YRICH_OP_CELL_CLEAR,
    YETTY_YRICH_OP_CELL_FORMAT,
    YETTY_YRICH_OP_STYLE_SET,
    YETTY_YRICH_OP_CURSOR_MOVE,
    YETTY_YRICH_OP_SELECTION_SET,
};

struct yetty_yrich_op_insert {
    yetty_yrich_element_id id;
    char *element_type; /* owned, may be NULL */
    char *data;         /* owned serialized payload, may be NULL */
};

struct yetty_yrich_op_delete {
    yetty_yrich_element_id id;
    char *data; /* serialized for undo, owned */
};

struct yetty_yrich_op_update {
    yetty_yrich_element_id id;
    char *property;  /* owned */
    char *old_value; /* owned */
    char *new_value; /* owned */
};

struct yetty_yrich_op_text_insert {
    yetty_yrich_element_id id;
    int32_t position;
    char *text; /* owned */
};

struct yetty_yrich_op_text_delete {
    yetty_yrich_element_id id;
    int32_t position;
    int32_t length;
    char *deleted_text; /* owned, for undo */
};

struct yetty_yrich_op_text_format {
    yetty_yrich_element_id id;
    int32_t start_pos;
    int32_t end_pos;
    uint32_t add_format;
    uint32_t remove_format;
    uint32_t old_format;
};

struct yetty_yrich_op_cell_set {
    struct yetty_yrich_cell_addr addr;
    char *old_value; /* owned */
    char *new_value; /* owned */
};

struct yetty_yrich_op_style_set {
    yetty_yrich_element_id id;
    char *property;
    char *old_value;
    char *new_value;
};

struct yetty_yrich_op_cursor_move {
    yetty_yrich_element_id element_id;
    int32_t position;
};

/*=============================================================================
 * Operation — tagged-union body. Owns all heap-allocated string fields; copy
 * semantics are explicit (yetty_yrich_op_clone). Free with op_destroy.
 *===========================================================================*/

struct yetty_yrich_operation {
    uint32_t type;      /* enum yetty_yrich_op_type */
    uint64_t timestamp; /* monotonic counter (logical clock) */
    yetty_yrich_session_id author;

    union {
        struct yetty_yrich_op_insert insert;
        struct yetty_yrich_op_delete del;
        struct yetty_yrich_op_update update;
        struct yetty_yrich_op_text_insert text_insert;
        struct yetty_yrich_op_text_delete text_delete;
        struct yetty_yrich_op_text_format text_format;
        struct yetty_yrich_op_cell_set cell_set;
        struct yetty_yrich_op_style_set style_set;
        struct yetty_yrich_op_cursor_move cursor_move;
    } u;
};

YETTY_YRESULT_DECLARE(yetty_yrich_operation_ptr, struct yetty_yrich_operation *);

/* Allocate empty operation of given type at timestamp/author. Caller fills the
 * union body and string fields (transferring ownership). */
struct yetty_yrich_operation_ptr_result yetty_yrich_operation_create(uint32_t type,
                                                                     uint64_t timestamp,
                                                                     yetty_yrich_session_id author);

/* Free an operation including all owned strings. NULL-safe. */
void yetty_yrich_operation_destroy(struct yetty_yrich_operation *op);

/* True for cursor / selection ops — these are not persisted in the log. */
bool yetty_yrich_operation_is_presence(const struct yetty_yrich_operation *op);

/* Build the inverse operation (for undo). Errors on alloc failure or for
 * ops that have no inverse (presence). */
struct yetty_yrich_operation_ptr_result yetty_yrich_operation_inverse(
    const struct yetty_yrich_operation *op);

/*=============================================================================
 * Operation log — append-only history.
 *===========================================================================*/

struct yetty_yrich_op_log {
    struct yetty_yrich_operation **ops;
    size_t count;
    size_t capacity;
    uint64_t current_ts;
};

void yetty_yrich_op_log_init(struct yetty_yrich_op_log *log);
void yetty_yrich_op_log_clear(struct yetty_yrich_op_log *log);

/* Take ownership of op. Returns -1 on alloc failure (op is destroyed). */
int yetty_yrich_op_log_append(struct yetty_yrich_op_log *log, struct yetty_yrich_operation *op);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_OPERATION_H */
