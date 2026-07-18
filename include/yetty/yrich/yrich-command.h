#ifndef YETTY_YRICH_YRICH_COMMAND_H
#define YETTY_YRICH_YRICH_COMMAND_H

/*
 * yrich-command — undo/redo facade.
 *
 * A command bundles the operations that result from one user action so they
 * can be undone as a unit. The C port keeps the vtable shape (description,
 * execute, undo, redo, mergeWith) but stores each command's operations in a
 * flat array and reuses yetty_yrich_operation_inverse for undo.
 */

#include <stdbool.h>
#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-operation.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Documents are yclass objects (class yrich:document and subclasses). */
struct yetty_yclass_object;
struct yetty_yrich_command;

struct yetty_yrich_command_ops {
    void (*destroy)(struct yetty_yrich_command *self);
    const char *(*description)(const struct yetty_yrich_command *self);

    /* Execute the command against the document. Each operation produced is
	 * appended to self->ops via yetty_yrich_command_record_op. Returns
	 * YETTY_OK_VOID() / error. */
    struct yetty_ycore_void_result (*execute)(struct yetty_yrich_command *self,
                                              struct yetty_yclass_object *doc_obj);

    /* Optional — default uses inverse(ops). */
    struct yetty_ycore_void_result (*undo)(struct yetty_yrich_command *self,
                                           struct yetty_yclass_object *doc_obj);

    /* Optional — default re-applies ops. */
    struct yetty_ycore_void_result (*redo)(struct yetty_yrich_command *self,
                                           struct yetty_yclass_object *doc_obj);

    bool (*can_merge_with)(const struct yetty_yrich_command *self,
                           const struct yetty_yrich_command *other);
    /* Coalesce `other` into `self`. Returns true if merged (the caller then
     * destroys `other`); false if the merge could not complete (e.g. an
     * allocation failure) — the caller must then keep `other` as its own
     * undo entry so the already-applied edit is never lost. */
    bool (*merge_with)(struct yetty_yrich_command *self, struct yetty_yrich_command *other);
};

struct yetty_yrich_command {
    const struct yetty_yrich_command_ops *ops;

    /* Operations recorded during execute(); owned by the command. Used by
	 * the default undo/redo paths and freed in destroy. */
    struct yetty_yrich_operation **recorded;
    size_t recorded_count;
    size_t recorded_capacity;
};

YETTY_YRESULT_DECLARE(yetty_yrich_command_ptr, struct yetty_yrich_command *);

/* Append op to self->recorded. Takes ownership; errors on alloc failure
 * (op destroyed). */
struct yetty_ycore_void_result yetty_yrich_command_record_op(struct yetty_yrich_command *cmd,
                                                             struct yetty_yrich_operation *op);

/* Generic operation command — execute applies every recorded op against
 * the document (via the document_apply_op slot), undo applies the
 * inverses in reverse order (the default undo path). Build one, record
 * the ops of a user action, then hand it to yetty_yrich_document_execute
 * so the action lands on the undo stack. Consecutive single-character
 * text inserts on the same element merge into one undo step. */
struct yetty_yrich_command_ptr_result yetty_yrich_op_command_create(void);

/* Default helpers exported for vtables that don't customise undo/redo. */
struct yetty_ycore_void_result yetty_yrich_command_default_undo(
    struct yetty_yrich_command *cmd, struct yetty_yclass_object *doc_obj);
struct yetty_ycore_void_result yetty_yrich_command_default_redo(
    struct yetty_yrich_command *cmd, struct yetty_yclass_object *doc_obj);

void yetty_yrich_command_destroy(struct yetty_yrich_command *cmd);

/*=============================================================================
 * History
 *===========================================================================*/

struct yetty_yrich_history {
    struct yetty_yrich_command **undo_stack;
    size_t undo_count;
    size_t undo_capacity;

    struct yetty_yrich_command **redo_stack;
    size_t redo_count;
    size_t redo_capacity;

    size_t max_size; /* default 100 */
};

void yetty_yrich_history_init(struct yetty_yrich_history *h);
void yetty_yrich_history_clear(struct yetty_yrich_history *h);

/* Run cmd, push onto undo stack, drop redo stack. Takes ownership of cmd. */
struct yetty_ycore_void_result yetty_yrich_history_execute(struct yetty_yrich_history *h,
                                                           struct yetty_yrich_command *cmd,
                                                           struct yetty_yclass_object *doc_obj);

bool yetty_yrich_history_can_undo(const struct yetty_yrich_history *h);
bool yetty_yrich_history_can_redo(const struct yetty_yrich_history *h);

struct yetty_ycore_void_result yetty_yrich_history_undo(struct yetty_yrich_history *h,
                                                        struct yetty_yclass_object *doc_obj);
struct yetty_ycore_void_result yetty_yrich_history_redo(struct yetty_yrich_history *h,
                                                        struct yetty_yclass_object *doc_obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_COMMAND_H */
