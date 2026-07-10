/*
 * Android camera backend — NDK Camera2 + AImageReader.
 *
 * Camera2 is callback-driven. We allocate one ACameraDevice + one
 * AImageReader, and the reader's OnImageAvailable listener pushes the
 * AImage into our vfd ring. The client gets fd-level wakeups via the
 * vfd's self-pipe; acquire_frame pops the borrowed AImage, fills the
 * ydev_frame_t with the plane pointers, and release_frame calls
 * AImage_delete.
 *
 * Camera permissions on Android need to be granted by the *Java*
 * Activity before NDK code can open the device — that piece lives in
 * perm_android.c which uses JNI.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/camera.h>

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImageReader.h>
#include <android/log.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AImage             *img;      /* owned: AImage_delete on drop      */
    const uint8_t      *data;
    size_t              size;
    uint32_t            width, height;
    uint32_t            stride[4];
    size_t              plane_offset[4];
    int                 format;
    uint64_t            ts_ns;
    uint64_t            seq;
} android_cam_slot_t;

static void android_cam_drop(void *rec)
{
    android_cam_slot_t *s = rec;
    if (s->img) { AImage_delete(s->img); s->img = NULL; }
}

struct ydev_camera {
    struct ydev_vfd               vfd;
    ACameraManager               *mgr;
    ACameraDevice                *dev;
    AImageReader                 *reader;
    ANativeWindow                *window;
    ACaptureSessionOutputContainer *out_container;
    ACaptureSessionOutput        *out;
    ACameraOutputTarget          *target;
    ACaptureRequest              *req;
    ACameraCaptureSession        *session;
    char                          id[64];
    uint64_t                      seq;
    int                           started;
};

static int to_android_fmt(ydev_pixel_format_t f)
{
    switch (f) {
    case YDEV_PIX_NV12:        return 0x23; /* AIMAGE_FORMAT_YUV_420_888 */
    case YDEV_PIX_I420:        return 0x23;
    case YDEV_PIX_BGRA:
    case YDEV_PIX_RGBA:        return 0x1;  /* AIMAGE_FORMAT_RGBA_8888    */
    case YDEV_PIX_MJPEG:       return 0x100;/* AIMAGE_FORMAT_JPEG         */
    default:                   return 0x23;
    }
}

static void cb_image_available(void *ctx, AImageReader *reader)
{
    ydev_camera_t *c = ctx;
    AImage *img = NULL;
    if (AImageReader_acquireLatestImage(reader, &img) != AMEDIA_OK || !img) return;

    android_cam_slot_t slot;
    memset(&slot, 0, sizeof slot);
    slot.img = img;

    int32_t w = 0, h = 0;
    AImage_getWidth(img, &w);
    AImage_getHeight(img, &h);
    slot.width  = (uint32_t)w;
    slot.height = (uint32_t)h;

    int32_t fmt = 0;
    AImage_getFormat(img, &fmt);
    slot.format = (fmt == 0x23) ? YDEV_PIX_NV12 : YDEV_PIX_BGRA;

    int32_t n_planes = 0;
    AImage_getNumberOfPlanes(img, &n_planes);
    if (n_planes > 4) n_planes = 4;

    uint8_t *base0 = NULL;
    int      len0  = 0;
    AImage_getPlaneData(img, 0, &base0, &len0);
    slot.data = base0;
    size_t total = (size_t)len0;
    int32_t s0 = 0;
    AImage_getPlaneRowStride(img, 0, &s0);
    slot.stride[0]       = (uint32_t)s0;
    slot.plane_offset[0] = 0;
    for (int i = 1; i < n_planes; i++) {
        uint8_t *p = NULL; int len = 0; int32_t row = 0;
        AImage_getPlaneData(img, i, &p, &len);
        AImage_getPlaneRowStride(img, i, &row);
        slot.stride[i]       = (uint32_t)row;
        slot.plane_offset[i] = (size_t)(p - base0);
        total += (size_t)len;
    }
    slot.size = total;

    int64_t ts = 0;
    AImage_getTimestamp(img, &ts);
    slot.ts_ns = ts ? (uint64_t)ts : ydev_now_ns();

    slot.seq = ydev_vfd_push(&c->vfd, &slot);
}

static void cb_dev_disconnected(void *ctx, ACameraDevice *dev) { (void)ctx; (void)dev; }
static void cb_dev_error       (void *ctx, ACameraDevice *dev, int err) { (void)ctx; (void)dev; (void)err; }
static void cb_sess_closed     (void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; }
static void cb_sess_ready      (void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; }
static void cb_sess_active     (void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; }

ydev_result_t ydev_camera_list(ydev_camera_info_t *out, size_t cap, size_t *count)
{
    ACameraManager *m = ACameraManager_create();
    if (!m) { if (count) *count = 0; return YDEV_IO; }
    ACameraIdList *ids = NULL;
    ACameraManager_getCameraIdList(m, &ids);
    size_t n = ids ? (size_t)ids->numCameras : 0;
    for (size_t i = 0; i < n && out && i < cap; i++) {
        memset(&out[i], 0, sizeof out[i]);
        strncpy(out[i].id, ids->cameraIds[i], sizeof(out[i].id) - 1);
        ACameraMetadata *meta = NULL;
        if (ACameraManager_getCameraCharacteristics(m, ids->cameraIds[i], &meta) == ACAMERA_OK && meta) {
            ACameraMetadata_const_entry e;
            if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &e) == ACAMERA_OK) {
                switch (e.data.u8[0]) {
                case 0: out[i].facing = YDEV_FACING_FRONT; break;
                case 1: out[i].facing = YDEV_FACING_BACK;  break;
                case 2: out[i].facing = YDEV_FACING_EXTERNAL; break;
                }
            }
            ACameraMetadata_free(meta);
        }
        snprintf(out[i].display_name, sizeof out[i].display_name,
                 "Camera %s", ids->cameraIds[i]);
    }
    if (ids) ACameraManager_deleteCameraIdList(ids);
    ACameraManager_delete(m);
    if (count) *count = n;
    return YDEV_OK;
}

ydev_result_t ydev_camera_query_formats(const char *id,
                                        ydev_camera_format_t *out, size_t cap,
                                        size_t *count)
{
    (void)id; (void)out; (void)cap;
    if (count) *count = 0;
    /* Camera2 reports streams via ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
     * decoding that into a format table is straightforward but unused by the
     * camera server so we leave it as a future addition. */
    return YDEV_UNSUPPORTED;
}

ydev_camera_t *ydev_camera_open(const char *id)
{
    if (!id) { ydev_set_error("camera_open: id NULL"); return NULL; }
    ydev_camera_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    if (ydev_vfd_init(&c->vfd, sizeof(android_cam_slot_t), 4, android_cam_drop) != 0) {
        free(c); return NULL;
    }
    strncpy(c->id, id, sizeof(c->id) - 1);

    c->mgr = ACameraManager_create();
    if (!c->mgr) { ydev_camera_close(c); return NULL; }

    ACameraDevice_StateCallbacks dev_cb = {
        .context = c,
        .onDisconnected = cb_dev_disconnected,
        .onError        = cb_dev_error,
    };
    if (ACameraManager_openCamera(c->mgr, id, &dev_cb, &c->dev) != ACAMERA_OK) {
        ydev_set_error("camera_open: ACameraManager_openCamera failed");
        ydev_camera_close(c);
        return NULL;
    }
    return c;
}

ydev_result_t ydev_camera_set_format(ydev_camera_t *c, const ydev_camera_format_t *f)
{
    if (!c || !f) return YDEV_INVALID_ARG;
    int afmt = to_android_fmt(f->format);
    if (AImageReader_new((int32_t)f->width, (int32_t)f->height, afmt, 4, &c->reader) != AMEDIA_OK) {
        ydev_set_error("camera_set_format: AImageReader_new failed");
        return YDEV_UNSUPPORTED;
    }
    AImageReader_ImageListener listener = { .context = c, .onImageAvailable = cb_image_available };
    AImageReader_setImageListener(c->reader, &listener);
    AImageReader_getWindow(c->reader, &c->window);

    ACaptureSessionOutputContainer_create(&c->out_container);
    ACaptureSessionOutput_create(c->window, &c->out);
    ACaptureSessionOutputContainer_add(c->out_container, c->out);
    ACameraOutputTarget_create(c->window, &c->target);
    ACameraDevice_createCaptureRequest(c->dev, TEMPLATE_PREVIEW, &c->req);
    ACaptureRequest_addTarget(c->req, c->target);
    return YDEV_OK;
}

ydev_result_t ydev_camera_start(ydev_camera_t *c)
{
    if (!c) return YDEV_INVALID_ARG;
    ACameraCaptureSession_stateCallbacks sess_cb = {
        .context = c,
        .onClosed = cb_sess_closed,
        .onReady  = cb_sess_ready,
        .onActive = cb_sess_active,
    };
    if (ACameraDevice_createCaptureSession(c->dev, c->out_container, &sess_cb, &c->session)
        != ACAMERA_OK) {
        ydev_set_error("camera_start: createCaptureSession failed");
        return YDEV_IO;
    }
    ACaptureRequest *reqs[1] = { c->req };
    if (ACameraCaptureSession_setRepeatingRequest(c->session, NULL, 1, reqs, NULL) != ACAMERA_OK) {
        ydev_set_error("camera_start: setRepeatingRequest failed");
        return YDEV_IO;
    }
    c->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_camera_stop(ydev_camera_t *c)
{
    if (!c) return YDEV_INVALID_ARG;
    if (c->session) { ACameraCaptureSession_stopRepeating(c->session); }
    c->started = 0;
    ydev_vfd_close(&c->vfd);
    return YDEV_OK;
}

void ydev_camera_close(ydev_camera_t *c)
{
    if (!c) return;
    if (c->started) ydev_camera_stop(c);
    if (c->session)       ACameraCaptureSession_close(c->session);
    if (c->req)           ACaptureRequest_free(c->req);
    if (c->target)        ACameraOutputTarget_free(c->target);
    if (c->out)           ACaptureSessionOutput_free(c->out);
    if (c->out_container) ACaptureSessionOutputContainer_free(c->out_container);
    if (c->reader)        AImageReader_delete(c->reader);
    if (c->dev)           ACameraDevice_close(c->dev);
    if (c->mgr)           ACameraManager_delete(c->mgr);
    ydev_vfd_destroy(&c->vfd);
    free(c);
}

int ydev_camera_fd(ydev_camera_t *c) { return c ? ydev_vfd_fd(&c->vfd) : -1; }

ydev_result_t ydev_camera_acquire_frame(ydev_camera_t *c, ydev_frame_t *out, int timeout_ms)
{
    if (!c || !out) return YDEV_INVALID_ARG;
    android_cam_slot_t slot;
    ydev_result_t r = ydev_vfd_pop(&c->vfd, &slot, timeout_ms);
    if (r != YDEV_OK) return r;

    memset(out, 0, sizeof *out);
    out->data   = slot.data;
    out->size   = slot.size;
    out->width  = slot.width;
    out->height = slot.height;
    memcpy(out->stride,       slot.stride,       sizeof out->stride);
    memcpy(out->plane_offset, slot.plane_offset, sizeof out->plane_offset);
    out->format = (ydev_pixel_format_t)slot.format;
    out->ts_ns  = slot.ts_ns;
    out->seq    = slot.seq;
    out->opaque = slot.img;
    return YDEV_OK;
}

ydev_result_t ydev_camera_release_frame(ydev_camera_t *c, ydev_frame_t *fr)
{
    (void)c;
    if (!fr) return YDEV_INVALID_ARG;
    AImage *img = (AImage *)fr->opaque;
    if (img) AImage_delete(img);
    fr->opaque = NULL;
    fr->data   = NULL;
    return YDEV_OK;
}
