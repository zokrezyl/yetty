/*
 * yrich-operation.c — operation construction, inversion, and log.
 *
 * Operations own their string fields (raw char*). Helpers in this file are
 * the only place that copies / frees them.
 */

#include <yetty/yrich/yrich-operation.h>

#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Internal: dup a possibly-NULL char*
 *===========================================================================*/

static char *dup_str(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n + 1);
    return out;
}

/*=============================================================================
 * create / destroy
 *===========================================================================*/

struct yetty_yrich_operation_ptr_result yetty_yrich_operation_create(uint32_t type,
                                                                     uint64_t timestamp,
                                                                     yetty_yrich_session_id author)
{
    struct yetty_yrich_operation *op = calloc(1, sizeof(struct yetty_yrich_operation));
    if (!op) {
        return YETTY_ERR(yetty_yrich_operation_ptr, "yrich operation alloc failed");
    }
    op->type = type;
    op->timestamp = timestamp;
    op->author = author;
    return YETTY_OK(yetty_yrich_operation_ptr, op);
}

void yetty_yrich_operation_destroy(struct yetty_yrich_operation *op)
{
    if (!op) {
        return;
    }
    switch (op->type) {
    case YETTY_YRICH_OP_INSERT:
        free(op->u.insert.element_type);
        free(op->u.insert.data);
        break;
    case YETTY_YRICH_OP_DELETE:
        free(op->u.del.data);
        break;
    case YETTY_YRICH_OP_UPDATE:
        free(op->u.update.property);
        free(op->u.update.old_value);
        free(op->u.update.new_value);
        break;
    case YETTY_YRICH_OP_TEXT_INSERT:
        free(op->u.text_insert.text);
        break;
    case YETTY_YRICH_OP_TEXT_DELETE:
        free(op->u.text_delete.deleted_text);
        break;
    case YETTY_YRICH_OP_CELL_SET:
        free(op->u.cell_set.old_value);
        free(op->u.cell_set.new_value);
        break;
    case YETTY_YRICH_OP_STYLE_SET:
        free(op->u.style_set.property);
        free(op->u.style_set.old_value);
        free(op->u.style_set.new_value);
        break;
    default:
        break;
    }
    free(op);
}

bool yetty_yrich_operation_is_presence(const struct yetty_yrich_operation *op)
{
    if (!op) {
        return false;
    }
    return op->type == YETTY_YRICH_OP_CURSOR_MOVE || op->type == YETTY_YRICH_OP_SELECTION_SET;
}

/*=============================================================================
 * inverse — swap directions / flip insert<->delete pairs
 *===========================================================================*/

struct yetty_yrich_operation_ptr_result yetty_yrich_operation_inverse(
    const struct yetty_yrich_operation *op)
{
    if (!op) {
        return YETTY_ERR(yetty_yrich_operation_ptr, "yrich operation_inverse: NULL op");
    }
    if (yetty_yrich_operation_is_presence(op)) {
        return YETTY_ERR(yetty_yrich_operation_ptr,
                         "yrich operation_inverse: presence op has no inverse");
    }

    uint32_t inv_type = op->type;
    switch (op->type) {
    case YETTY_YRICH_OP_INSERT:
        inv_type = YETTY_YRICH_OP_DELETE;
        break;
    case YETTY_YRICH_OP_DELETE:
        inv_type = YETTY_YRICH_OP_INSERT;
        break;
    case YETTY_YRICH_OP_TEXT_INSERT:
        inv_type = YETTY_YRICH_OP_TEXT_DELETE;
        break;
    case YETTY_YRICH_OP_TEXT_DELETE:
        inv_type = YETTY_YRICH_OP_TEXT_INSERT;
        break;
    default:
        break; /* same type, swapped fields */
    }

    struct yetty_yrich_operation_ptr_result r =
        yetty_yrich_operation_create(inv_type, op->timestamp, op->author);
    YETTY_RETURN_IF_ERR(yetty_yrich_operation_ptr, r, "yrich operation_inverse: create failed");
    struct yetty_yrich_operation *inv = r.value;

    switch (op->type) {
    case YETTY_YRICH_OP_INSERT:
        inv->u.del.id = op->u.insert.id;
        inv->u.del.data = dup_str(op->u.insert.data);
        break;
    case YETTY_YRICH_OP_DELETE:
        inv->u.insert.id = op->u.del.id;
        inv->u.insert.element_type = NULL;
        inv->u.insert.data = dup_str(op->u.del.data);
        break;
    case YETTY_YRICH_OP_UPDATE:
        inv->u.update.id = op->u.update.id;
        inv->u.update.property = dup_str(op->u.update.property);
        inv->u.update.old_value = dup_str(op->u.update.new_value);
        inv->u.update.new_value = dup_str(op->u.update.old_value);
        break;
    case YETTY_YRICH_OP_TEXT_INSERT: {
        const char *text = op->u.text_insert.text;
        inv->u.text_delete.id = op->u.text_insert.id;
        inv->u.text_delete.position = op->u.text_insert.position;
        inv->u.text_delete.length = text ? (int32_t)strlen(text) : 0;
        inv->u.text_delete.deleted_text = dup_str(text);
        break;
    }
    case YETTY_YRICH_OP_TEXT_DELETE:
        inv->u.text_insert.id = op->u.text_delete.id;
        inv->u.text_insert.position = op->u.text_delete.position;
        inv->u.text_insert.text = dup_str(op->u.text_delete.deleted_text);
        break;
    case YETTY_YRICH_OP_CELL_SET:
        inv->u.cell_set.addr = op->u.cell_set.addr;
        inv->u.cell_set.old_value = dup_str(op->u.cell_set.new_value);
        inv->u.cell_set.new_value = dup_str(op->u.cell_set.old_value);
        break;
    case YETTY_YRICH_OP_STYLE_SET:
        inv->u.style_set.id = op->u.style_set.id;
        inv->u.style_set.property = dup_str(op->u.style_set.property);
        inv->u.style_set.old_value = dup_str(op->u.style_set.new_value);
        inv->u.style_set.new_value = dup_str(op->u.style_set.old_value);
        break;
    default:
        break;
    }
    return YETTY_OK(yetty_yrich_operation_ptr, inv);
}

/*=============================================================================
 * Log
 *===========================================================================*/

void yetty_yrich_op_log_init(struct yetty_yrich_op_log *log)
{
    if (!log) {
        return;
    }
    memset(log, 0, sizeof(*log));
}

void yetty_yrich_op_log_clear(struct yetty_yrich_op_log *log)
{
    if (!log) {
        return;
    }
    for (size_t i = 0; i < log->count; i++) {
        yetty_yrich_operation_destroy(log->ops[i]);
    }
    free(log->ops);
    memset(log, 0, sizeof(*log));
}

int yetty_yrich_op_log_append(struct yetty_yrich_op_log *log, struct yetty_yrich_operation *op)
{
    if (!log || !op) {
        yetty_yrich_operation_destroy(op);
        return -1;
    }
    if (log->count == log->capacity) {
        size_t new_cap = log->capacity ? log->capacity * 2 : 32;
        struct yetty_yrich_operation **new_ops = realloc(log->ops, new_cap * sizeof(*new_ops));
        if (!new_ops) {
            yetty_yrich_operation_destroy(op);
            return -1;
        }
        log->ops = new_ops;
        log->capacity = new_cap;
    }
    log->ops[log->count++] = op;
    if (op->timestamp > log->current_ts) {
        log->current_ts = op->timestamp;
    }
    return 0;
}
