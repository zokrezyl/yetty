/*
 * ydev/camera.h — camera capture.
 *
 * Lifecycle:
 *   list                discover available cameras
 *   open                allocate handle, no I/O yet
 *   set_format          choose width/height/fps/pixel format
 *   start               begin producing frames, fd becomes pollable
 *   poll + acquire      wait for, then borrow, the next frame
 *   release             return the borrowed frame to the platform
 *   stop                stop producing
 *   close               free handle
 *
 * Acquire/release is a pair. On iOS the buffer is a CVPixelBufferRef with
 * locked base addresses; on Android it's an AImage; on Linux it's a V4L2
 * mmap'd buffer index. In every case the client may use frame.data until
 * release; afterwards the pointer is invalid.
 *
 * The actual format may differ from what set_format requested if the
 * hardware cannot match exactly. Read width/height/stride/format from
 * each frame, do not assume.
 */

#ifndef YOS_YDEV_CAMERA_H
#define YOS_YDEV_CAMERA_H

#include <yos/ydev/ydev.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_FACING_UNKNOWN  = 0,
    YDEV_FACING_BACK     = 1,
    YDEV_FACING_FRONT    = 2,
    YDEV_FACING_EXTERNAL = 3,
} ydev_camera_facing_t;

typedef enum {
    YDEV_PIX_NV12        = 1,    /* YUV 4:2:0 semi-planar, common on mobile */
    YDEV_PIX_I420        = 2,    /* YUV 4:2:0 planar, encoder-friendly       */
    YDEV_PIX_YUYV        = 3,    /* 4:2:2 packed, common on UVC webcams      */
    YDEV_PIX_BGRA        = 4,
    YDEV_PIX_RGBA        = 5,
    YDEV_PIX_MJPEG       = 10,   /* already-compressed frames                */
    YDEV_PIX_H264_ANNEXB = 11,   /* hardware-encoded stream                  */
} ydev_pixel_format_t;

typedef struct {
    char                 id[64];           /* opaque, pass to camera_open    */
    char                 display_name[128];
    ydev_camera_facing_t facing;
    uint32_t             supported_formats_count;
} ydev_camera_info_t;

typedef struct {
    uint32_t            width;
    uint32_t            height;
    uint32_t            min_fps;
    uint32_t            max_fps;
    ydev_pixel_format_t format;
} ydev_camera_format_t;

typedef struct ydev_camera ydev_camera_t;

typedef struct {
    const uint8_t      *data;          /* base of the buffer                 */
    size_t              size;          /* total bytes addressable from data  */
    uint32_t            width;
    uint32_t            height;
    uint32_t            stride[4];     /* per-plane row stride, 0 if unused  */
    size_t              plane_offset[4]; /* byte offset of plane i in data   */
    ydev_pixel_format_t format;
    uint64_t            ts_ns;         /* monotonic capture time             */
    uint64_t            seq;           /* monotonically increasing seq no.   */
    void               *opaque;        /* backend handle, do not touch       */
} ydev_frame_t;

/* Enumeration. Safe to call before any permission has been granted. */
ydev_result_t ydev_camera_list(ydev_camera_info_t *out, size_t cap, size_t *count);
ydev_result_t ydev_camera_query_formats(const char *id,
                                        ydev_camera_format_t *out, size_t cap,
                                        size_t *count);

/* Lifecycle. */
ydev_camera_t *ydev_camera_open(const char *id);
ydev_result_t  ydev_camera_set_format(ydev_camera_t *, const ydev_camera_format_t *);
ydev_result_t  ydev_camera_start(ydev_camera_t *);
ydev_result_t  ydev_camera_stop(ydev_camera_t *);
void           ydev_camera_close(ydev_camera_t *);

/* Data path. */
int            ydev_camera_fd(ydev_camera_t *);
ydev_result_t  ydev_camera_acquire_frame(ydev_camera_t *, ydev_frame_t *out, int timeout_ms);
ydev_result_t  ydev_camera_release_frame(ydev_camera_t *, ydev_frame_t *);

/* Optional in-library NV12 -> I420 plane shuffle. Encoders typically want
 * I420; many mobile cameras deliver NV12. dst must point to a buffer of
 * at least (width * height * 3 / 2) bytes. */
ydev_result_t ydev_convert_to_i420(const ydev_frame_t *src,
                                   uint8_t *dst, size_t dst_size,
                                   size_t *bytes_written);

#ifdef __cplusplus
}
#endif

#endif
