/*
 * yrich-command.c — undo/redo machinery.
 *
 * The base struct stores the operations a command produced; default undo
 * applies their inverses, default redo replays them. Subclass commands can
 * override either or both.
 */

#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/yrich-operation.h>

#include <yetty/yclass/class.h>
#include <yetty/yrich/document.h>

#include <stdlib.h>
#include <string.h>

#define YRICH_HISTORY_DEFAULT_MAX 100

/*=============================================================================
 * Command base
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yrich_command_record_op(struct yetty_yrich_command *cmd,
                                                             struct yetty_yrich_operation *op)
{
    if (!cmd || !op) {
        yetty_yrich_operation_destroy(op);
        return YETTY_ERR(yetty_ycore_void, "yrich command_record_op: NULL cmd/op");
    }
    if (cmd->recorded_count == cmd->recorded_capacity) {
        size_t new_cap = cmd->recorded_capacity ? cmd->recorded_capacity * 2 : 4;
        struct yetty_yrich_operation **new_ops = realloc(cmd->recorded, new_cap * sizeof(*new_ops));
        if (!new_ops) {
            yetty_yrich_operation_destroy(op);
            return YETTY_ERR(yetty_ycore_void, "yrich command_record_op: array grow failed");
        }
        cmd->recorded = new_ops;
        cmd->recorded_capacity = new_cap;
    }
    cmd->recorded[cmd->recorded_count++] = op;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_command_default_undo(struct yetty_yrich_command *cmd,
                                                                struct yetty_yclass_object *doc_obj)
{
    if (!cmd) {
        return YETTY_ERR(yetty_ycore_void, "command_default_undo: NULL cmd");
    }
    /* Apply inverses in reverse order; bail on the first failure. */
    for (size_t i = cmd->recorded_count; i > 0; i--) {
        struct yetty_yrich_operation_ptr_result inv_r =
            yetty_yrich_operation_inverse(cmd->recorded[i - 1]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, inv_r, "command_default_undo: inverse failed");
        struct yetty_ycore_void_result apply_res =
            yetty_yrich_document_apply_op(doc_obj, inv_r.value, 1);
        if (YETTY_IS_ERR(apply_res)) {
            yetty_yrich_operation_destroy(inv_r.value);
            return YETTY_ERR(yetty_ycore_void, "command_default_undo: apply_op failed", apply_res);
        }
        yetty_yrich_operation_destroy(inv_r.value);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_command_default_redo(struct yetty_yrich_command *cmd,
                                                                struct yetty_yclass_object *doc_obj)
{
    if (!cmd) {
        return YETTY_ERR(yetty_ycore_void, "command_default_redo: NULL cmd");
    }
    for (size_t i = 0; i < cmd->recorded_count; i++) {
        struct yetty_ycore_void_result apply_res =
            yetty_yrich_document_apply_op(doc_obj, cmd->recorded[i], 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "command_default_redo: apply_op failed");
    }
    return YETTY_OK_VOID();
}

void yetty_yrich_command_destroy(struct yetty_yrich_command *cmd)
{
    if (!cmd) {
        return;
    }
    if (cmd->ops && cmd->ops->destroy) {
        cmd->ops->destroy(cmd);
        return;
    }
    for (size_t i = 0; i < cmd->recorded_count; i++) {
        yetty_yrich_operation_destroy(cmd->recorded[i]);
    }
    free(cmd->recorded);
    free(cmd);
}

/*=============================================================================
 * Operation command — the generic undoable edit. Execute applies the
 * recorded ops in order; the default undo path applies their inverses in
 * reverse order. Consecutive text inserts merge so undo works per burst
 * of typing instead of per keystroke.
 *===========================================================================*/

static struct yetty_ycore_void_result op_command_execute(struct yetty_yrich_command *cmd,
                                                         struct yetty_yclass_object *doc_obj)
{
    for (size_t i = 0; i < cmd->recorded_count; i++) {
        struct yetty_ycore_void_result apply_res =
            yetty_yrich_document_apply_op(doc_obj, cmd->recorded[i], 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "op_command_execute: apply_op failed");
    }
    return YETTY_OK_VOID();
}

static const struct yetty_yrich_command_ops *op_command_ops(void);

/* Merge rule: both are op commands, the receiver's last op and the donor's
 * only op are TEXT_INSERTs on the same element, and the donor continues
 * exactly where the receiver left off. */
static bool op_command_can_merge_with(const struct yetty_yrich_command *self,
                                      const struct yetty_yrich_command *other)
{
    if (!self || !other || self->ops != op_command_ops() || other->ops != op_command_ops()) {
        return false;
    }
    if (self->recorded_count == 0 || other->recorded_count != 1) {
        return false;
    }
    const struct yetty_yrich_operation *last = self->recorded[self->recorded_count - 1];
    const struct yetty_yrich_operation *next = other->recorded[0];
    if (last->type != YETTY_YRICH_OP_TEXT_INSERT || next->type != YETTY_YRICH_OP_TEXT_INSERT) {
        return false;
    }
    if (last->u.text_insert.id != next->u.text_insert.id) {
        return false;
    }
    if (!last->u.text_insert.text || !next->u.text_insert.text) {
        return false;
    }
    size_t last_len = strlen(last->u.text_insert.text);
    return next->u.text_insert.position == last->u.text_insert.position + (int32_t)last_len;
}

static void op_command_merge_with(struct yetty_yrich_command *self,
                                  struct yetty_yrich_command *other)
{
    struct yetty_yrich_operation *last = self->recorded[self->recorded_count - 1];
    const struct yetty_yrich_operation *next = other->recorded[0];
    size_t last_len = strlen(last->u.text_insert.text);
    size_t next_len = strlen(next->u.text_insert.text);
    char *merged = realloc(last->u.text_insert.text, last_len + next_len + 1);
    if (!merged) {
        /* Best-effort: leave the commands unmerged-looking; the caller
		 * destroys `other` either way, so just keep the receiver as-is.
		 * The donor's op stays with the donor and dies with it — the
		 * keystroke was already applied to the document. */
        return;
    }
    memcpy(merged + last_len, next->u.text_insert.text, next_len + 1);
    last->u.text_insert.text = merged;
}

static const struct yetty_yrich_command_ops *op_command_ops(void)
{
    static const struct yetty_yrich_command_ops ops = {
        .execute = op_command_execute,
        .can_merge_with = op_command_can_merge_with,
        .merge_with = op_command_merge_with,
    };
    return &ops;
}

struct yetty_yrich_command_ptr_result yetty_yrich_op_command_create(void)
{
    struct yetty_yrich_command *cmd = calloc(1, sizeof(struct yetty_yrich_command));
    if (!cmd) {
        return YETTY_ERR(yetty_yrich_command_ptr, "op_command_create: alloc failed");
    }
    cmd->ops = op_command_ops();
    return YETTY_OK(yetty_yrich_command_ptr, cmd);
}

/*=============================================================================
 * History
 *===========================================================================*/

void yetty_yrich_history_init(struct yetty_yrich_history *h)
{
    if (!h) {
        return;
    }
    memset(h, 0, sizeof(*h));
    h->max_size = YRICH_HISTORY_DEFAULT_MAX;
}

void yetty_yrich_history_clear(struct yetty_yrich_history *h)
{
    if (!h) {
        return;
    }
    for (size_t i = 0; i < h->undo_count; i++) {
        yetty_yrich_command_destroy(h->undo_stack[i]);
    }
    for (size_t i = 0; i < h->redo_count; i++) {
        yetty_yrich_command_destroy(h->redo_stack[i]);
    }
    free(h->undo_stack);
    free(h->redo_stack);
    memset(h, 0, sizeof(*h));
    h->max_size = YRICH_HISTORY_DEFAULT_MAX;
}

static struct yetty_ycore_void_result push(struct yetty_yrich_command ***stack, size_t *count,
                                           size_t *capacity, struct yetty_yrich_command *cmd)
{
    if (*count == *capacity) {
        size_t new_cap = *capacity ? *capacity * 2 : 8;
        struct yetty_yrich_command **new_stack = realloc(*stack, new_cap * sizeof(*new_stack));
        if (!new_stack) {
            return YETTY_ERR(yetty_ycore_void, "yrich history: stack grow failed");
        }
        *stack = new_stack;
        *capacity = new_cap;
    }
    (*stack)[(*count)++] = cmd;
    return YETTY_OK_VOID();
}

static struct yetty_yrich_command *pop(struct yetty_yrich_command **stack, size_t *count)
{
    if (*count == 0) {
        return NULL;
    }
    (*count)--;
    return stack[*count];
}

static void clear_redo(struct yetty_yrich_history *h)
{
    for (size_t i = 0; i < h->redo_count; i++) {
        yetty_yrich_command_destroy(h->redo_stack[i]);
    }
    h->redo_count = 0;
}

bool yetty_yrich_history_can_undo(const struct yetty_yrich_history *h)
{
    return h && h->undo_count > 0;
}

bool yetty_yrich_history_can_redo(const struct yetty_yrich_history *h)
{
    return h && h->redo_count > 0;
}

struct yetty_ycore_void_result yetty_yrich_history_execute(struct yetty_yrich_history *h,
                                                           struct yetty_yrich_command *cmd,
                                                           struct yetty_yclass_object *doc_obj)
{
    if (!h || !cmd || !cmd->ops || !cmd->ops->execute) {
        yetty_yrich_command_destroy(cmd);
        return YETTY_ERR(yetty_ycore_void, "yrich history execute: invalid command");
    }

    struct yetty_ycore_void_result r = cmd->ops->execute(cmd, doc_obj);
    if (YETTY_IS_ERR(r)) {
        yetty_yrich_command_destroy(cmd);
        return r;
    }

    clear_redo(h);

    /* Try to merge with the top of the undo stack. */
    if (h->undo_count > 0 && cmd->ops->can_merge_with) {
        struct yetty_yrich_command *top = h->undo_stack[h->undo_count - 1];
        if (top->ops->can_merge_with && top->ops->can_merge_with(top, cmd) &&
            top->ops->merge_with) {
            top->ops->merge_with(top, cmd);
            yetty_yrich_command_destroy(cmd);
            return YETTY_OK_VOID();
        }
    }

    struct yetty_ycore_void_result push_res =
        push(&h->undo_stack, &h->undo_count, &h->undo_capacity, cmd);
    if (YETTY_IS_ERR(push_res)) {
        yetty_yrich_command_destroy(cmd);
        return YETTY_ERR(yetty_ycore_void, "yrich history execute: push failed", push_res);
    }

    /* Trim oldest if over the limit. */
    if (h->undo_count > h->max_size) {
        yetty_yrich_command_destroy(h->undo_stack[0]);
        memmove(&h->undo_stack[0], &h->undo_stack[1], (h->undo_count - 1) * sizeof(*h->undo_stack));
        h->undo_count--;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_history_undo(struct yetty_yrich_history *h,
                                                        struct yetty_yclass_object *doc_obj)
{
    if (!h) {
        return YETTY_ERR(yetty_ycore_void, "history_undo: NULL history");
    }
    struct yetty_yrich_command *cmd = pop(h->undo_stack, &h->undo_count);
    if (!cmd) {
        return YETTY_ERR(yetty_ycore_void, "history_undo: stack empty");
    }

    struct yetty_ycore_void_result undo_res;
    if (cmd->ops->undo) {
        undo_res = cmd->ops->undo(cmd, doc_obj);
    } else {
        undo_res = yetty_yrich_command_default_undo(cmd, doc_obj);
    }

    struct yetty_ycore_void_result push_res =
        push(&h->redo_stack, &h->redo_count, &h->redo_capacity, cmd);
    if (YETTY_IS_ERR(push_res)) {
        /* On alloc failure, drop the command rather than leak. The undo
         * error (if any) stays the primary failure. */
        yetty_yrich_command_destroy(cmd);
        if (YETTY_IS_ERR(undo_res)) {
            yetty_ycore_error_destroy(push_res.error);
            return undo_res;
        }
        return YETTY_ERR(yetty_ycore_void, "history_undo: redo-stack push failed", push_res);
    }

    return undo_res;
}

struct yetty_ycore_void_result yetty_yrich_history_redo(struct yetty_yrich_history *h,
                                                        struct yetty_yclass_object *doc_obj)
{
    if (!h) {
        return YETTY_ERR(yetty_ycore_void, "history_redo: NULL history");
    }
    struct yetty_yrich_command *cmd = pop(h->redo_stack, &h->redo_count);
    if (!cmd) {
        return YETTY_ERR(yetty_ycore_void, "history_redo: stack empty");
    }

    struct yetty_ycore_void_result redo_res;
    if (cmd->ops->redo) {
        redo_res = cmd->ops->redo(cmd, doc_obj);
    } else {
        redo_res = yetty_yrich_command_default_redo(cmd, doc_obj);
    }

    struct yetty_ycore_void_result push_res =
        push(&h->undo_stack, &h->undo_count, &h->undo_capacity, cmd);
    if (YETTY_IS_ERR(push_res)) {
        yetty_yrich_command_destroy(cmd);
        if (YETTY_IS_ERR(redo_res)) {
            yetty_ycore_error_destroy(push_res.error);
            return redo_res;
        }
        return YETTY_ERR(yetty_ycore_void, "history_redo: undo-stack push failed", push_res);
    }

    return redo_res;
}
