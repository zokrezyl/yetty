/*
 * ydev/perm.h — runtime permission state for camera/mic/location/motion.
 *
 * iOS and Android ask the user at runtime. Linux derives the answer from
 * filesystem ACLs on the underlying device node. macOS sits in between
 * (TCC dialog for camera/mic/location, granted-by-default for sensors).
 *
 * ydev_perm_request kicks off whatever asynchronous prompt the platform
 * uses; status moves UNKNOWN → PENDING → GRANTED|DENIED|RESTRICTED. The
 * status fd becomes readable each time any capability transitions.
 *
 * iOS quirk: the camera/mic prompt only appears when the first capture
 * session actually starts, not when access is requested. The library
 * therefore runs a one-shot dummy session inside ydev_perm_request to
 * provoke the dialog. From the client's perspective the lifecycle is
 * still request → poll(perm_fd) → status.
 */

#ifndef YOS_YDEV_PERM_H
#define YOS_YDEV_PERM_H

#include <yos/ydev/ydev.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_PERM_UNKNOWN    = 0,
    YDEV_PERM_PENDING    = 1,
    YDEV_PERM_GRANTED    = 2,
    YDEV_PERM_DENIED     = 3,
    YDEV_PERM_RESTRICTED = 4,
} ydev_perm_status_t;

ydev_perm_status_t ydev_perm_status(ydev_capability_t cap);
ydev_result_t      ydev_perm_request(ydev_capability_t cap);

/* Becomes readable when any capability's status changes. Single fd shared
 * across capabilities; clients re-poll the individual statuses on wake. */
int                ydev_perm_fd(void);

#ifdef __cplusplus
}
#endif

#endif
