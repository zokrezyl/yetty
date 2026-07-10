/* aliases — thin trampolines for libc names that share an
 * implementation with another libc name in this tree.
 *
 * Kept separate from impl/proc.c etc. so the wiring is greppable
 * (every alias here is a deliberate "foo IS bar in our model"
 * decision, not an accident of subsystem layout). */

#include "yos/types.h"
#include "impl/proc/proc.h"

/* _exit and _Exit are POSIX synonyms for exit (no atexit handlers,
 * no stdio flush). yos's process model treats all three identically:
 * mark this proc ZOMBIE, broadcast wait_cond. */
int32_t yos__exit(struct yos_exec_ctx *ctx, int32_t status)
{
    return yos_exit(ctx, status);
}

int32_t yos__Exit(struct yos_exec_ctx *ctx, int32_t status)
{
    return yos_exit(ctx, status);
}
