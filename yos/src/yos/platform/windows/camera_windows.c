/*
 * Windows camera — UNSUPPORTED stub for the initial Windows port. A
 * future iteration may wrap Media Foundation (IMFSourceReader) but the
 * surface area is large enough to live behind its own milestone.
 */

#include "../../impl/ydev/internal.h"

#include <yos/ydev/camera.h>

ydev_result_t ydev_camera_list(ydev_camera_info_t *out, size_t cap, size_t *count)
{
    (void)out; (void)cap;
    if (count) *count = 0;
    return YDEV_OK;
}

ydev_result_t ydev_camera_query_formats(const char *id,
                                         ydev_camera_format_t *out,
                                         size_t cap, size_t *count)
{
    (void)id; (void)out; (void)cap;
    if (count) *count = 0;
    return YDEV_UNSUPPORTED;
}

ydev_camera_t *ydev_camera_open(const char *id)
{
    (void)id;
    return NULL;
}

ydev_result_t ydev_camera_set_format(ydev_camera_t *c, const ydev_camera_format_t *f)
{
    (void)c; (void)f;
    return YDEV_UNSUPPORTED;
}

ydev_result_t ydev_camera_start(ydev_camera_t *c) { (void)c; return YDEV_UNSUPPORTED; }
ydev_result_t ydev_camera_stop (ydev_camera_t *c) { (void)c; return YDEV_UNSUPPORTED; }
void          ydev_camera_close(ydev_camera_t *c) { (void)c; }
int           ydev_camera_fd   (ydev_camera_t *c) { (void)c; return -1; }

ydev_result_t ydev_camera_acquire_frame(ydev_camera_t *c, ydev_frame_t *out, int timeout_ms)
{
    (void)c; (void)out; (void)timeout_ms;
    return YDEV_UNSUPPORTED;
}

ydev_result_t ydev_camera_release_frame(ydev_camera_t *c, ydev_frame_t *fr)
{
    (void)c; (void)fr;
    return YDEV_UNSUPPORTED;
}
