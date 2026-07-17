/*
 * yrich command/history invariants (Phase-0A steps 3–4).
 *
 * Pins the two transactional-integrity fixes in yrich-command.c using
 * synthetic commands whose vtable behaviour is test-controlled — no document
 * context needed, deterministic, no libc allocation-injection fragility:
 *
 *   - A merge that cannot complete (merge_with returns false — the same signal
 *     the real op_command emits on realloc failure) must NOT drop the
 *     already-applied edit: it becomes its own undo entry.
 *   - A command whose undo/redo *application* fails must stay on its original
 *     stack, never silently moving to the opposite one.
 */

#include <yetty/yrich/yrich-command.h>

#include "ytest.h"

#include <stdlib.h>

/*--- synthetic command vtable behaviours -----------------------------------*/

static int g_undo_calls;
static int g_redo_calls;

static struct yetty_ycore_void_result ok_execute(struct yetty_yrich_command *self,
                                                 struct yetty_yclass_object *doc)
{
    (void)self;
    (void)doc;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ok_undo(struct yetty_yrich_command *self,
                                              struct yetty_yclass_object *doc)
{
    (void)self;
    (void)doc;
    g_undo_calls++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ok_redo(struct yetty_yrich_command *self,
                                              struct yetty_yclass_object *doc)
{
    (void)self;
    (void)doc;
    g_redo_calls++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result fail_undo(struct yetty_yrich_command *self,
                                                struct yetty_yclass_object *doc)
{
    (void)self;
    (void)doc;
    return YETTY_ERR(yetty_ycore_void, "synthetic undo failure");
}

static struct yetty_ycore_void_result fail_redo(struct yetty_yrich_command *self,
                                                struct yetty_yclass_object *doc)
{
    (void)self;
    (void)doc;
    return YETTY_ERR(yetty_ycore_void, "synthetic redo failure");
}

static bool always_can_merge(const struct yetty_yrich_command *self,
                             const struct yetty_yrich_command *other)
{
    (void)self;
    (void)other;
    return true;
}

/* Simulate a merge that cannot complete (the real op_command returns false
 * here on realloc failure). */
static bool merge_declines(struct yetty_yrich_command *self, struct yetty_yrich_command *other)
{
    (void)self;
    (void)other;
    return false;
}

static const struct yetty_yrich_command_ops merge_decline_ops = {
    .execute = ok_execute,
    .undo = ok_undo,
    .redo = ok_redo,
    .can_merge_with = always_can_merge,
    .merge_with = merge_declines,
};

static const struct yetty_yrich_command_ops undo_fail_ops = {
    .execute = ok_execute,
    .undo = fail_undo,
    .redo = ok_redo,
};

static const struct yetty_yrich_command_ops redo_fail_ops = {
    .execute = ok_execute,
    .undo = ok_undo,
    .redo = fail_redo,
};

static struct yetty_yrich_command *make_cmd(const struct yetty_yrich_command_ops *ops)
{
    struct yetty_yrich_command *cmd = calloc(1, sizeof(*cmd));
    if (cmd) {
        cmd->ops = ops;
    }
    return cmd;
}

/*--- step 3: a declined merge keeps the edit as its own undo entry ----------*/
static void test_merge_declined_keeps_separate_entry(struct ytest *test)
{
    struct yetty_yrich_history history;
    yetty_yrich_history_init(&history);
    g_undo_calls = 0;

    struct yetty_ycore_void_result first =
        yetty_yrich_history_execute(&history, make_cmd(&merge_decline_ops), NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(first));
    struct yetty_ycore_void_result second =
        yetty_yrich_history_execute(&history, make_cmd(&merge_decline_ops), NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(second));

    /* Merge could not complete → the second edit must be its OWN undo entry,
     * never dropped. */
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 2);

    /* Both edits remain undoable. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_history_undo(&history, NULL)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_history_undo(&history, NULL)));
    YTEST_CHECK_EQ_INT(test, g_undo_calls, 2);
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 0);

    yetty_yrich_history_clear(&history);
}

/*--- step 4a: a failed undo application stays on the undo stack -------------*/
static void test_failed_undo_stays_on_undo_stack(struct ytest *test)
{
    struct yetty_yrich_history history;
    yetty_yrich_history_init(&history);

    YTEST_CHECK(
        test, !YETTY_IS_ERR(yetty_yrich_history_execute(&history, make_cmd(&undo_fail_ops), NULL)));
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 1);

    struct yetty_ycore_void_result undo_res = yetty_yrich_history_undo(&history, NULL);
    YTEST_CHECK(test, YETTY_IS_ERR(undo_res)); /* application failed */
    if (YETTY_IS_ERR(undo_res)) {
        yetty_ycore_error_destroy(undo_res.error);
    }

    /* Command must remain on the undo stack, not migrate to redo. */
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 1);
    YTEST_CHECK_EQ_INT(test, (int)history.redo_count, 0);

    yetty_yrich_history_clear(&history);
}

/*--- step 4b: a failed redo application stays on the redo stack -------------*/
static void test_failed_redo_stays_on_redo_stack(struct ytest *test)
{
    struct yetty_yrich_history history;
    yetty_yrich_history_init(&history);

    /* Execute + undo successfully so a command sits on the redo stack. */
    YTEST_CHECK(
        test, !YETTY_IS_ERR(yetty_yrich_history_execute(&history, make_cmd(&redo_fail_ops), NULL)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_history_undo(&history, NULL)));
    YTEST_CHECK_EQ_INT(test, (int)history.redo_count, 1);
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 0);

    struct yetty_ycore_void_result redo_res = yetty_yrich_history_redo(&history, NULL);
    YTEST_CHECK(test, YETTY_IS_ERR(redo_res)); /* application failed */
    if (YETTY_IS_ERR(redo_res)) {
        yetty_ycore_error_destroy(redo_res.error);
    }

    /* Command must remain on the redo stack, not migrate to undo. */
    YTEST_CHECK_EQ_INT(test, (int)history.redo_count, 1);
    YTEST_CHECK_EQ_INT(test, (int)history.undo_count, 0);

    yetty_yrich_history_clear(&history);
}

int main(void)
{
    struct ytest test = ytest_begin("yrich_command");
    YTEST_RUN(&test, test_merge_declined_keeps_separate_entry);
    YTEST_RUN(&test, test_failed_undo_stays_on_undo_stack);
    YTEST_RUN(&test, test_failed_redo_stays_on_redo_stack);
    return ytest_end(&test);
}
