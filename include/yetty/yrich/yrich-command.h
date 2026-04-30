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

struct yetty_yrich_document;
struct yetty_yrich_command;

struct yetty_yrich_command_ops {
    void (*destroy)(struct yetty_yrich_command *self);
    const char *(*description)(const struct yetty_yrich_command *self);

    /* Execute the command against the document. Each operation produced is
	 * appended to self->ops via yetty_yrich_command_record_op. Returns
	 * YETTY_OK_VOID() / error. */
    struct yetty_ycore_void_result (*execute)(struct yetty_yrich_command *self,
                                              struct yetty_yrich_document *doc);

    /* Optional — default uses inverse(ops). */
    struct yetty_ycore_void_result (*undo)(struct yetty_yrich_command *self,
                                           struct yetty_yrich_document *doc);

    /* Optional — default re-applies ops. */
    struct yetty_ycore_void_result (*redo)(struct yetty_yrich_command *self,
                                           struct yetty_yrich_document *doc);

    bool (*can_merge_with)(const struct yetty_yrich_command *self,
                           const struct yetty_yrich_command *other);
    void (*merge_with)(struct yetty_yrich_command *self, struct yetty_yrich_command *other);
};

struct yetty_yrich_command {
    const struct yetty_yrich_command_ops *ops;

    /* Operations recorded during execute(); owned by the command. Used by
	 * the default undo/redo paths and freed in destroy. */
    struct yetty_yrich_operation **recorded;
    size_t recorded_count;
    size_t recorded_capacity;
};

/* Append op to self->recorded. Takes ownership; returns -1 on alloc failure
 * (op destroyed). */
int yetty_yrich_command_record_op(struct yetty_yrich_command *cmd,
                                  struct yetty_yrich_operation *op);

/* Default helpers exported for vtables that don't customise undo/redo. */
struct yetty_ycore_void_result yetty_yrich_command_default_undo(struct yetty_yrich_command *cmd,
                                                                struct yetty_yrich_document *doc);
struct yetty_ycore_void_result yetty_yrich_command_default_redo(struct yetty_yrich_command *cmd,
                                                                struct yetty_yrich_document *doc);

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
                                                           struct yetty_yrich_document *doc);

bool yetty_yrich_history_can_undo(const struct yetty_yrich_history *h);
bool yetty_yrich_history_can_redo(const struct yetty_yrich_history *h);

struct yetty_ycore_void_result yetty_yrich_history_undo(struct yetty_yrich_history *h,
                                                        struct yetty_yrich_document *doc);
struct yetty_ycore_void_result yetty_yrich_history_redo(struct yetty_yrich_history *h,
                                                        struct yetty_yrich_document *doc);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_COMMAND_H */
