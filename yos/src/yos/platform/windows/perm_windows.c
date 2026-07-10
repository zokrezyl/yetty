/*
 * Windows permissions — there is no per-app capability prompt for the
 * desktop Win32 surface yos targets here. Return GRANTED unconditionally
 * and let the underlying device open() decide.
 */

#include "../../impl/ydev/internal.h"

ydev_perm_status_t ydev_perm_query_platform(ydev_capability_t cap)
{
    (void)cap;
    return YDEV_PERM_GRANTED;
}

ydev_result_t ydev_perm_request_platform(ydev_capability_t cap)
{
    ydev_perm_set(cap, YDEV_PERM_GRANTED);
    return YDEV_OK;
}
