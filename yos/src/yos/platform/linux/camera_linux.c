/*
 * Linux camera backend — V4L2.
 *
 * The V4L2 character device fd is itself pollable, so the public
 * ydev_camera_fd is a dup() of it. No vfd / no self-pipe needed on
 * this backend — the kernel already gives us what we want.
 *
 * Buffers are mmap'd from the kernel (VIDIOC_REQBUFS + VIDIOC_QUERYBUF +
 * mmap). acquire_frame dequeues one (VIDIOC_DQBUF) and hands the client
 * a borrowed pointer into the mmap region; release_frame requeues it
 * (VIDIOC_QBUF). The kernel recycles the same N buffers forever.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/camera.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define YDEV_V4L2_BUFS 4

struct v4l2_mmap_buf {
    void   *ptr;
    size_t  len;
};

struct ydev_camera {
    int                  dev_fd;
    int                  pub_fd;            /* dup of dev_fd, exposed       */
    char                 path[64];          /* /dev/videoN                   */
    ydev_pixel_format_t  format;
    uint32_t             width, height;
    uint32_t             stride;
    size_t               img_size;
    struct v4l2_mmap_buf buffers[YDEV_V4L2_BUFS];
    int                  buf_count;
    uint64_t             seq;
    int                  streaming;
};

static int v4l2_xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

static uint32_t to_v4l2_fmt(ydev_pixel_format_t f)
{
    switch (f) {
    case YDEV_PIX_NV12:        return V4L2_PIX_FMT_NV12;
    case YDEV_PIX_I420:        return V4L2_PIX_FMT_YUV420;
    case YDEV_PIX_YUYV:        return V4L2_PIX_FMT_YUYV;
    case YDEV_PIX_BGRA:        return V4L2_PIX_FMT_ABGR32;
    case YDEV_PIX_RGBA:        return V4L2_PIX_FMT_RGBA32;
    case YDEV_PIX_MJPEG:       return V4L2_PIX_FMT_MJPEG;
    case YDEV_PIX_H264_ANNEXB: return V4L2_PIX_FMT_H264;
    }
    return 0;
}

static ydev_pixel_format_t from_v4l2_fmt(uint32_t f)
{
    switch (f) {
    case V4L2_PIX_FMT_NV12:    return YDEV_PIX_NV12;
    case V4L2_PIX_FMT_YUV420:  return YDEV_PIX_I420;
    case V4L2_PIX_FMT_YUYV:    return YDEV_PIX_YUYV;
    case V4L2_PIX_FMT_ABGR32:  return YDEV_PIX_BGRA;
    case V4L2_PIX_FMT_RGBA32:  return YDEV_PIX_RGBA;
    case V4L2_PIX_FMT_MJPEG:   return YDEV_PIX_MJPEG;
    case V4L2_PIX_FMT_H264:    return YDEV_PIX_H264_ANNEXB;
    }
    return YDEV_PIX_NV12;
}

/* ── enumeration ─────────────────────────────────────────────────────── */

ydev_result_t ydev_camera_list(ydev_camera_info_t *out, size_t cap, size_t *count)
{
    DIR *d = opendir("/dev");
    size_t n = 0;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "video", 5) != 0) continue;
            char path[80];
            snprintf(path, sizeof path, "/dev/%s", de->d_name);
            int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            struct v4l2_capability cap2;
            memset(&cap2, 0, sizeof cap2);
            if (v4l2_xioctl(fd, VIDIOC_QUERYCAP, &cap2) == 0 &&
                (cap2.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                if (out && n < cap) {
                    memset(&out[n], 0, sizeof out[n]);
                    strncpy(out[n].id, path, sizeof(out[n].id) - 1);
                    strncpy(out[n].display_name, (char *)cap2.card,
                            sizeof(out[n].display_name) - 1);
                    out[n].facing = YDEV_FACING_EXTERNAL;
                }
                n++;
            }
            close(fd);
        }
        closedir(d);
    }
    if (count) *count = n;
    return YDEV_OK;
}

ydev_result_t ydev_camera_query_formats(const char *id,
                                        ydev_camera_format_t *out, size_t cap,
                                        size_t *count)
{
    if (!id) { if (count) *count = 0; return YDEV_INVALID_ARG; }
    int fd = open(id, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        ydev_set_error("camera_query_formats: open %s: %s", id, strerror(errno));
        if (count) *count = 0;
        return YDEV_IO;
    }
    size_t n = 0;
    struct v4l2_fmtdesc fdesc;
    memset(&fdesc, 0, sizeof fdesc);
    fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (uint32_t i = 0; v4l2_xioctl(fd, VIDIOC_ENUM_FMT, &fdesc) == 0; i++) {
        fdesc.index = i + 1;
        struct v4l2_frmsizeenum fs;
        memset(&fs, 0, sizeof fs);
        fs.pixel_format = fdesc.pixelformat;
        for (uint32_t j = 0; v4l2_xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; j++) {
            fs.index = j + 1;
            if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) continue;
            struct v4l2_frmivalenum fi;
            memset(&fi, 0, sizeof fi);
            fi.pixel_format = fdesc.pixelformat;
            fi.width  = fs.discrete.width;
            fi.height = fs.discrete.height;
            for (uint32_t k = 0; v4l2_xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; k++) {
                fi.index = k + 1;
                if (fi.type != V4L2_FRMIVAL_TYPE_DISCRETE) continue;
                if (out && n < cap) {
                    memset(&out[n], 0, sizeof out[n]);
                    out[n].width   = fs.discrete.width;
                    out[n].height  = fs.discrete.height;
                    uint32_t fps   = fi.discrete.denominator / (fi.discrete.numerator ?: 1);
                    out[n].min_fps = fps;
                    out[n].max_fps = fps;
                    out[n].format  = from_v4l2_fmt(fdesc.pixelformat);
                }
                n++;
            }
        }
    }
    close(fd);
    if (count) *count = n;
    return YDEV_OK;
}

/* ── lifecycle ───────────────────────────────────────────────────────── */

ydev_camera_t *ydev_camera_open(const char *id)
{
    if (!id) { ydev_set_error("camera_open: id is NULL"); return NULL; }
    int fd = open(id, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        ydev_set_error("camera_open: open %s: %s", id, strerror(errno));
        return NULL;
    }
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (v4l2_xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0 ||
        !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING)) {
        ydev_set_error("camera_open: %s lacks VIDEO_CAPTURE+STREAMING", id);
        close(fd);
        return NULL;
    }

    ydev_camera_t *c = calloc(1, sizeof *c);
    if (!c) { close(fd); return NULL; }
    c->dev_fd = fd;
    c->pub_fd = dup(fd);
    strncpy(c->path, id, sizeof(c->path) - 1);
    return c;
}

ydev_result_t ydev_camera_set_format(ydev_camera_t *c, const ydev_camera_format_t *f)
{
    if (!c || !f) return YDEV_INVALID_ARG;
    uint32_t pix = to_v4l2_fmt(f->format);
    if (pix == 0) return YDEV_UNSUPPORTED;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = f->width;
    fmt.fmt.pix.height      = f->height;
    fmt.fmt.pix.pixelformat = pix;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (v4l2_xioctl(c->dev_fd, VIDIOC_S_FMT, &fmt) != 0) {
        ydev_set_error("camera_set_format: VIDIOC_S_FMT: %s", strerror(errno));
        return YDEV_IO;
    }

    /* Set frame rate (best effort). */
    if (f->max_fps > 0) {
        struct v4l2_streamparm sp;
        memset(&sp, 0, sizeof sp);
        sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        sp.parm.capture.timeperframe.numerator   = 1;
        sp.parm.capture.timeperframe.denominator = f->max_fps;
        v4l2_xioctl(c->dev_fd, VIDIOC_S_PARM, &sp);
    }

    c->format   = from_v4l2_fmt(fmt.fmt.pix.pixelformat);
    c->width    = fmt.fmt.pix.width;
    c->height   = fmt.fmt.pix.height;
    c->stride   = fmt.fmt.pix.bytesperline;
    c->img_size = fmt.fmt.pix.sizeimage;
    return YDEV_OK;
}

ydev_result_t ydev_camera_start(ydev_camera_t *c)
{
    if (!c) return YDEV_INVALID_ARG;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count  = YDEV_V4L2_BUFS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (v4l2_xioctl(c->dev_fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) {
        ydev_set_error("camera_start: REQBUFS: %s", strerror(errno));
        return YDEV_IO;
    }
    c->buf_count = req.count;

    for (int i = 0; i < c->buf_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (v4l2_xioctl(c->dev_fd, VIDIOC_QUERYBUF, &buf) != 0) return YDEV_IO;

        c->buffers[i].len = buf.length;
        c->buffers[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, c->dev_fd, buf.m.offset);
        if (c->buffers[i].ptr == MAP_FAILED) {
            ydev_set_error("camera_start: mmap: %s", strerror(errno));
            return YDEV_IO;
        }
        if (v4l2_xioctl(c->dev_fd, VIDIOC_QBUF, &buf) != 0) return YDEV_IO;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_xioctl(c->dev_fd, VIDIOC_STREAMON, &type) != 0) {
        ydev_set_error("camera_start: STREAMON: %s", strerror(errno));
        return YDEV_IO;
    }
    c->streaming = 1;
    return YDEV_OK;
}

ydev_result_t ydev_camera_stop(ydev_camera_t *c)
{
    if (!c) return YDEV_INVALID_ARG;
    if (!c->streaming) return YDEV_OK;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_xioctl(c->dev_fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < c->buf_count; i++) {
        if (c->buffers[i].ptr && c->buffers[i].ptr != MAP_FAILED) {
            munmap(c->buffers[i].ptr, c->buffers[i].len);
            c->buffers[i].ptr = NULL;
        }
    }
    c->streaming = 0;
    return YDEV_OK;
}

void ydev_camera_close(ydev_camera_t *c)
{
    if (!c) return;
    if (c->streaming) ydev_camera_stop(c);
    if (c->pub_fd >= 0) close(c->pub_fd);
    if (c->dev_fd >= 0) close(c->dev_fd);
    free(c);
}

int ydev_camera_fd(ydev_camera_t *c) { return c ? c->pub_fd : -1; }

ydev_result_t ydev_camera_acquire_frame(ydev_camera_t *c, ydev_frame_t *out,
                                        int timeout_ms)
{
    if (!c || !out) return YDEV_INVALID_ARG;
    if (!c->streaming) return YDEV_IO;

    if (timeout_ms != 0) {
        struct pollfd p = { .fd = c->dev_fd, .events = POLLIN };
        int r = poll(&p, 1, timeout_ms);
        if (r == 0)  return YDEV_AGAIN;
        if (r <  0)  return YDEV_IO;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof buf);
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_xioctl(c->dev_fd, VIDIOC_DQBUF, &buf) != 0) {
        if (errno == EAGAIN) return YDEV_AGAIN;
        ydev_set_error("DQBUF: %s", strerror(errno));
        return YDEV_IO;
    }

    memset(out, 0, sizeof *out);
    out->data         = (const uint8_t *)c->buffers[buf.index].ptr;
    out->size         = buf.bytesused;
    out->width        = c->width;
    out->height       = c->height;
    out->stride[0]    = c->stride;
    out->plane_offset[0] = 0;
    if (c->format == YDEV_PIX_NV12 || c->format == YDEV_PIX_I420) {
        out->stride[1]       = c->stride / (c->format == YDEV_PIX_NV12 ? 1 : 2);
        out->plane_offset[1] = c->stride * c->height;
        if (c->format == YDEV_PIX_I420) {
            out->stride[2]       = c->stride / 2;
            out->plane_offset[2] = out->plane_offset[1] + (c->stride / 2) * (c->height / 2);
        }
    }
    out->format = c->format;
    out->ts_ns  = (uint64_t)buf.timestamp.tv_sec * 1000000000ull +
                  (uint64_t)buf.timestamp.tv_usec * 1000ull;
    if (out->ts_ns == 0) out->ts_ns = ydev_now_ns();
    out->seq    = ++c->seq;
    out->opaque = (void *)(uintptr_t)buf.index;
    return YDEV_OK;
}

ydev_result_t ydev_camera_release_frame(ydev_camera_t *c, ydev_frame_t *fr)
{
    if (!c || !fr) return YDEV_INVALID_ARG;
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof buf);
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = (uint32_t)(uintptr_t)fr->opaque;
    if (v4l2_xioctl(c->dev_fd, VIDIOC_QBUF, &buf) != 0) {
        ydev_set_error("QBUF: %s", strerror(errno));
        return YDEV_IO;
    }
    fr->data   = NULL;
    fr->opaque = NULL;
    return YDEV_OK;
}
