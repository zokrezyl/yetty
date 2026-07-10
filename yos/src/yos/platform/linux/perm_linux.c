/*
 * Linux permissions — derived from filesystem ACLs on the underlying
 * device node, not from a runtime prompt. For v1 we report GRANTED
 * for every capability; if the actual open() fails, the per-device
 * open path returns YDEV_DENIED.
 */

#include "../../impl/ydev/internal.h"

ydev_perm_status_t ydev_perm_query_platform(ydev_capability_t cap)
{
    (void)cap;
    return YDEV_PERM_GRANTED;
}

ydev_result_t ydev_perm_request_platform(ydev_capability_t cap)
{
    /* Nothing to ask. The check is implicit in open(). */
    ydev_perm_set(cap, YDEV_PERM_GRANTED);
    return YDEV_OK;
}
