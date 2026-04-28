/*
 * yrich-command.c — undo/redo machinery.
 *
 * The base struct stores the operations a command produced; default undo
 * applies their inverses, default redo replays them. Subclass commands can
 * override either or both.
 */

#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-operation.h>

#include <stdlib.h>
#include <string.h>

#define YRICH_HISTORY_DEFAULT_MAX 100

/*=============================================================================
 * Command base
 *===========================================================================*/

int yetty_yrich_command_record_op(struct yetty_yrich_command *cmd, struct yetty_yrich_operation *op)
{
    if (!cmd || !op) {
        yetty_yrich_operation_destroy(op);
        return -1;
    }
    if (cmd->recorded_count == cmd->recorded_capacity) {
        size_t new_cap = cmd->recorded_capacity ? cmd->recorded_capacity * 2 : 4;
        struct yetty_yrich_operation **new_ops = realloc(cmd->recorded, new_cap * sizeof(*new_ops));
        if (!new_ops) {
            yetty_yrich_operation_destroy(op);
            return -1;
        }
        cmd->recorded = new_ops;
        cmd->recorded_capacity = new_cap;
    }
    cmd->recorded[cmd->recorded_count++] = op;
    return 0;
}

void yetty_yrich_command_default_undo(struct yetty_yrich_command *cmd,
                                      struct yetty_yrich_document *doc)
{
    if (!cmd) {
        return;
    }
    /* Apply inverses in reverse order. */
    for (size_t i = cmd->recorded_count; i > 0; i--) {
        struct yetty_yrich_operation *inv = yetty_yrich_operation_inverse(cmd->recorded[i - 1]);
        if (!inv) {
            continue;
        }
        yetty_yrich_document_apply_op(doc, inv, true);
        yetty_yrich_operation_destroy(inv);
    }
}

void yetty_yrich_command_default_redo(struct yetty_yrich_command *cmd,
                                      struct yetty_yrich_document *doc)
{
    if (!cmd) {
        return;
    }
    for (size_t i = 0; i < cmd->recorded_count; i++) {
        yetty_yrich_document_apply_op(doc, cmd->recorded[i], true);
    }
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

static int push(struct yetty_yrich_command ***stack, size_t *count, size_t *capacity,
                struct yetty_yrich_command *cmd)
{
    if (*count == *capacity) {
        size_t new_cap = *capacity ? *capacity * 2 : 8;
        struct yetty_yrich_command **new_stack = realloc(*stack, new_cap * sizeof(*new_stack));
        if (!new_stack) {
            return -1;
        }
        *stack = new_stack;
        *capacity = new_cap;
    }
    (*stack)[(*count)++] = cmd;
    return 0;
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
                                                           struct yetty_yrich_document *doc)
{
    if (!h || !cmd || !cmd->ops || !cmd->ops->execute) {
        yetty_yrich_command_destroy(cmd);
        return YETTY_ERR(yetty_ycore_void, "yrich history execute: invalid command");
    }

    struct yetty_ycore_void_result r = cmd->ops->execute(cmd, doc);
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

    if (push(&h->undo_stack, &h->undo_count, &h->undo_capacity, cmd) < 0) {
        yetty_yrich_command_destroy(cmd);
        return YETTY_ERR(yetty_ycore_void, "yrich history: push failed");
    }

    /* Trim oldest if over the limit. */
    if (h->undo_count > h->max_size) {
        yetty_yrich_command_destroy(h->undo_stack[0]);
        memmove(&h->undo_stack[0], &h->undo_stack[1], (h->undo_count - 1) * sizeof(*h->undo_stack));
        h->undo_count--;
    }
    return YETTY_OK_VOID();
}

void yetty_yrich_history_undo(struct yetty_yrich_history *h, struct yetty_yrich_document *doc)
{
    if (!h) {
        return;
    }
    struct yetty_yrich_command *cmd = pop(h->undo_stack, &h->undo_count);
    if (!cmd) {
        return;
    }

    if (cmd->ops->undo) {
        cmd->ops->undo(cmd, doc);
    } else {
        yetty_yrich_command_default_undo(cmd, doc);
    }

    if (push(&h->redo_stack, &h->redo_count, &h->redo_capacity, cmd) < 0) {
        /* On alloc failure, drop the command rather than leak. */
        yetty_yrich_command_destroy(cmd);
    }
}

void yetty_yrich_history_redo(struct yetty_yrich_history *h, struct yetty_yrich_document *doc)
{
    if (!h) {
        return;
    }
    struct yetty_yrich_command *cmd = pop(h->redo_stack, &h->redo_count);
    if (!cmd) {
        return;
    }

    if (cmd->ops->redo) {
        cmd->ops->redo(cmd, doc);
    } else {
        yetty_yrich_command_default_redo(cmd, doc);
    }

    if (push(&h->undo_stack, &h->undo_count, &h->undo_capacity, cmd) < 0) {
        yetty_yrich_command_destroy(cmd);
    }
}
