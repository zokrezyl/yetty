/*
 * result.c — chain helpers for the Result error type.
 *
 * The top-of-chain error lives by value inside a Result; the cause chain is
 * heap-allocated. yetty_ycore_error_chain copies an inner error onto the heap
 * so a wrapping YETTY_ERR can take ownership of its chain.
 * yetty_ycore_error_destroy walks and frees the chain.
 */

#include <yetty/ycore/result.h>

#include <stdlib.h>

struct yetty_ycore_error *yetty_ycore_error_chain(struct yetty_ycore_error prev)
{
    struct yetty_ycore_error *p = malloc(sizeof(*p));
    if (!p) {
        /* OOM during error wrapping: drop the inner chain so we don't
         * leak it. The outer error still surfaces; debug context is lost. */
        yetty_ycore_error_destroy(prev);
        return NULL;
    }
    *p = prev;
    return p;
}

void yetty_ycore_error_destroy(struct yetty_ycore_error err)
{
    struct yetty_ycore_error *p = err.cause;
    while (p) {
        struct yetty_ycore_error *next = p->cause;
        free(p);
        p = next;
    }
}
