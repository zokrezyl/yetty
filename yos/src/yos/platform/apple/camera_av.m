/*
 * Apple camera backend — AVFoundation.
 *
 * Builds on macOS, iOS, iOS-sim, and tvOS. On tvOS the discovery session
 * returns an empty list (no built-in camera), every other call still
 * works and just hands back YDEV_OK / empty data.
 *
 * Lifecycle map:
 *   ydev_camera_open       -> alloc YdevCameraSession, find AVCaptureDevice
 *   ydev_camera_set_format -> pick AVCaptureDeviceFormat + pixel format
 *   ydev_camera_start      -> session startRunning
 *   delegate callback      -> push apple_cam_slot_t into vfd
 *   ydev_camera_acquire    -> pop slot, hand client a borrowed pointer
 *   ydev_camera_release    -> unlock + CFRelease the CVPixelBuffer
 *   ydev_camera_stop       -> session stopRunning
 *   ydev_camera_close      -> drop everything
 *
 * The CVPixelBufferRef is kept locked (and retained) from the delegate
 * callback until ydev_camera_release_frame. That lets the client read
 * frame bytes with zero copies.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/camera.h>

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

/* ── slot record stored in the vfd ring ──────────────────────────────── */

typedef struct {
    CVPixelBufferRef     cvpb;       /* retained + locked while in slot   */
    const uint8_t       *data;
    size_t               size;
    uint32_t             width;
    uint32_t             height;
    uint32_t             stride[4];
    size_t               plane_offset[4];
    int                  format;     /* ydev_pixel_format_t                */
    uint64_t             ts_ns;
    uint64_t             seq;
} apple_cam_slot_t;

static void apple_cam_drop_slot(void *rec)
{
    apple_cam_slot_t *s = (apple_cam_slot_t *)rec;
    if (s->cvpb) {
        CVPixelBufferUnlockBaseAddress(s->cvpb, kCVPixelBufferLock_ReadOnly);
        CFRelease(s->cvpb);
        s->cvpb = NULL;
    }
}

/* ── ObjC delegate that wraps the AVCaptureSession ───────────────────── */

@class YdevCameraSession;

@interface YdevCameraSession : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
@public
    AVCaptureSession         *session;
    AVCaptureDevice          *device;
    AVCaptureDeviceInput     *input;
    AVCaptureVideoDataOutput *output;
    dispatch_queue_t          queue;
    struct ydev_camera       *owner;     /* unretained back-pointer       */
}
@end

/* ── public-facing handle ────────────────────────────────────────────── */

struct ydev_camera {
    struct ydev_vfd      vfd;
    void                *session_obj;    /* CFBridgingRetain'd YdevCameraSession */
    char                 device_id[64];
    ydev_camera_format_t requested;
    OSType               cv_format;
    int                  started;
};

/* ── pixel-format mapping ────────────────────────────────────────────── */

static int to_cv_format(ydev_pixel_format_t f, OSType *out)
{
    switch (f) {
    case YDEV_PIX_NV12: *out = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange; return 0;
    case YDEV_PIX_I420: *out = kCVPixelFormatType_420YpCbCr8Planar;             return 0;
    case YDEV_PIX_BGRA: *out = kCVPixelFormatType_32BGRA;                       return 0;
    default: return -1;
    }
}

static ydev_pixel_format_t from_cv_format(OSType cv)
{
    switch (cv) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:  return YDEV_PIX_NV12;
    case kCVPixelFormatType_420YpCbCr8Planar:             return YDEV_PIX_I420;
    case kCVPixelFormatType_32BGRA:                       return YDEV_PIX_BGRA;
    default:                                              return YDEV_PIX_NV12;
    }
}

/* ── device enumeration ──────────────────────────────────────────────── */

static NSArray<AVCaptureDevice *> *enumerate_devices(void)
{
    NSMutableArray<AVCaptureDeviceType> *types = [NSMutableArray array];
    [types addObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
#if TARGET_OS_OSX
  #if defined(__MAC_14_0) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_14_0
    [types addObject:AVCaptureDeviceTypeExternal];
  #else
    [types addObject:AVCaptureDeviceTypeExternalUnknown];
  #endif
#endif
    AVCaptureDeviceDiscoverySession *d =
        [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:types
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];
    return d.devices ?: @[];
}

static ydev_camera_facing_t facing_of(AVCaptureDevice *dev)
{
    switch (dev.position) {
    case AVCaptureDevicePositionBack:        return YDEV_FACING_BACK;
    case AVCaptureDevicePositionFront:       return YDEV_FACING_FRONT;
    case AVCaptureDevicePositionUnspecified: return YDEV_FACING_EXTERNAL;
    }
    return YDEV_FACING_UNKNOWN;
}

ydev_result_t ydev_camera_list(ydev_camera_info_t *out, size_t cap, size_t *count)
{
    NSArray<AVCaptureDevice *> *devs = enumerate_devices();
    size_t n = 0;
    for (AVCaptureDevice *d in devs) {
        if (out && n < cap) {
            memset(&out[n], 0, sizeof out[n]);
            const char *uid = d.uniqueID.UTF8String ?: "";
            strncpy(out[n].id, uid, sizeof(out[n].id) - 1);
            const char *nm = d.localizedName.UTF8String ?: "";
            strncpy(out[n].display_name, nm, sizeof(out[n].display_name) - 1);
            out[n].facing = facing_of(d);
            out[n].supported_formats_count = (uint32_t)d.formats.count;
        }
        n++;
    }
    if (count) *count = n;
    return YDEV_OK;
}

static AVCaptureDevice *find_device(const char *id)
{
    NSString *target = [NSString stringWithUTF8String:id];
    for (AVCaptureDevice *d in enumerate_devices()) {
        if ([d.uniqueID isEqualToString:target]) return d;
    }
    return nil;
}

ydev_result_t ydev_camera_query_formats(const char *id,
                                        ydev_camera_format_t *out, size_t cap,
                                        size_t *count)
{
    AVCaptureDevice *dev = find_device(id);
    if (!dev) {
        ydev_set_error("camera_query_formats: no device with id %s", id ? id : "(null)");
        if (count) *count = 0;
        return YDEV_INVALID_ARG;
    }
    size_t n = 0;
    for (AVCaptureDeviceFormat *fmt in dev.formats) {
        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt.formatDescription);
        FourCharCode fcc = CMFormatDescriptionGetMediaSubType(fmt.formatDescription);
        for (AVFrameRateRange *r in fmt.videoSupportedFrameRateRanges) {
            if (out && n < cap) {
                memset(&out[n], 0, sizeof out[n]);
                out[n].width   = (uint32_t)dim.width;
                out[n].height  = (uint32_t)dim.height;
                out[n].min_fps = (uint32_t)r.minFrameRate;
                out[n].max_fps = (uint32_t)r.maxFrameRate;
                out[n].format  = from_cv_format(fcc);
            }
            n++;
        }
    }
    if (count) *count = n;
    return YDEV_OK;
}

/* ── delegate ────────────────────────────────────────────────────────── */

@implementation YdevCameraSession

- (void)captureOutput:(AVCaptureOutput *)captureOutput
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection
{
    if (!owner) return;

    CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pb) return;

    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    CFRetain(pb);

    apple_cam_slot_t slot;
    memset(&slot, 0, sizeof slot);
    slot.cvpb   = pb;
    slot.width  = (uint32_t)CVPixelBufferGetWidth(pb);
    slot.height = (uint32_t)CVPixelBufferGetHeight(pb);
    slot.format = from_cv_format(CVPixelBufferGetPixelFormatType(pb));

    if (CVPixelBufferIsPlanar(pb)) {
        size_t np = CVPixelBufferGetPlaneCount(pb);
        if (np > 4) np = 4;
        uint8_t *base0 = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
        slot.data = base0;
        size_t total = 0;
        for (size_t i = 0; i < np; i++) {
            uint8_t *bp = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, i);
            slot.stride[i]       = (uint32_t)CVPixelBufferGetBytesPerRowOfPlane(pb, i);
            slot.plane_offset[i] = (size_t)(bp - base0);
            total += slot.stride[i] * CVPixelBufferGetHeightOfPlane(pb, i);
        }
        slot.size = total;
    } else {
        slot.data       = CVPixelBufferGetBaseAddress(pb);
        slot.stride[0]  = (uint32_t)CVPixelBufferGetBytesPerRow(pb);
        slot.size       = slot.stride[0] * slot.height;
    }

    CMTime t = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    if (CMTIME_IS_VALID(t) && t.timescale != 0) {
        slot.ts_ns = (uint64_t)((double)t.value * 1000000000.0 / (double)t.timescale);
    } else {
        slot.ts_ns = ydev_now_ns();
    }

    slot.seq = ydev_vfd_push(&owner->vfd, &slot);
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
    didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection
{
    /* Framework dropped a frame upstream of us — nothing to do. */
}

@end

/* ── lifecycle ───────────────────────────────────────────────────────── */

ydev_camera_t *ydev_camera_open(const char *id)
{
    if (!id) { ydev_set_error("camera_open: id is NULL"); return NULL; }
    AVCaptureDevice *dev = find_device(id);
    if (!dev) { ydev_set_error("camera_open: no device with id %s", id); return NULL; }

    ydev_camera_t *cam = calloc(1, sizeof *cam);
    if (!cam) return NULL;
    if (ydev_vfd_init(&cam->vfd, sizeof(apple_cam_slot_t), 3, apple_cam_drop_slot) != 0) {
        free(cam);
        return NULL;
    }
    strncpy(cam->device_id, id, sizeof(cam->device_id) - 1);
    cam->cv_format = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    cam->requested.format = YDEV_PIX_NV12;

    YdevCameraSession *s = [[YdevCameraSession alloc] init];
    s->owner   = cam;
    s->session = [[AVCaptureSession alloc] init];
    s->device  = dev;
    s->queue   = dispatch_queue_create("ydev.camera", DISPATCH_QUEUE_SERIAL);

    NSError *err = nil;
    s->input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if (!s->input) {
        ydev_set_error("camera_open: AVCaptureDeviceInput failed: %s",
                       err.localizedDescription.UTF8String ?: "");
        ydev_vfd_destroy(&cam->vfd);
        free(cam);
        return NULL;
    }
    [s->session addInput:s->input];

    s->output = [[AVCaptureVideoDataOutput alloc] init];
    s->output.alwaysDiscardsLateVideoFrames = YES;
    [s->output setSampleBufferDelegate:s queue:s->queue];
    [s->session addOutput:s->output];

    cam->session_obj = (void *)CFBridgingRetain(s);
    return cam;
}

ydev_result_t ydev_camera_set_format(ydev_camera_t *cam, const ydev_camera_format_t *f)
{
    if (!cam || !f) return YDEV_INVALID_ARG;
    OSType cvfmt;
    if (to_cv_format(f->format, &cvfmt) != 0) {
        ydev_set_error("camera_set_format: unsupported pixel format %d", (int)f->format);
        return YDEV_UNSUPPORTED;
    }
    YdevCameraSession *s = (__bridge YdevCameraSession *)cam->session_obj;

    AVCaptureDeviceFormat *best = nil;
    for (AVCaptureDeviceFormat *fmt in s->device.formats) {
        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt.formatDescription);
        if ((uint32_t)dim.width == f->width && (uint32_t)dim.height == f->height) {
            for (AVFrameRateRange *r in fmt.videoSupportedFrameRateRanges) {
                if (f->min_fps >= (uint32_t)r.minFrameRate &&
                    f->max_fps <= (uint32_t)r.maxFrameRate + 1) {
                    best = fmt;
                    break;
                }
            }
            if (best) break;
        }
    }
    if (!best) {
        ydev_set_error("camera_set_format: no matching format for %ux%u @ %u-%u fps",
                       f->width, f->height, f->min_fps, f->max_fps);
        return YDEV_UNSUPPORTED;
    }

    NSError *err = nil;
    if (![s->device lockForConfiguration:&err]) {
        ydev_set_error("camera_set_format: lockForConfiguration failed: %s",
                       err.localizedDescription.UTF8String ?: "");
        return YDEV_IO;
    }
    s->device.activeFormat = best;
    if (f->max_fps > 0) {
        CMTime dur = CMTimeMake(1, (int32_t)f->max_fps);
        s->device.activeVideoMinFrameDuration = dur;
        s->device.activeVideoMaxFrameDuration = dur;
    }
    [s->device unlockForConfiguration];

    s->output.videoSettings = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(cvfmt)
    };

    cam->cv_format = cvfmt;
    cam->requested = *f;
    return YDEV_OK;
}

ydev_result_t ydev_camera_start(ydev_camera_t *cam)
{
    if (!cam) return YDEV_INVALID_ARG;

    ydev_perm_status_t pst = ydev_perm_query_platform(YDEV_CAP_CAMERA);
    if (pst == YDEV_PERM_DENIED || pst == YDEV_PERM_RESTRICTED) {
        ydev_set_error("camera_start: permission %s",
                       pst == YDEV_PERM_DENIED ? "denied" : "restricted");
        return YDEV_DENIED;
    }

    YdevCameraSession *s = (__bridge YdevCameraSession *)cam->session_obj;
    [s->session startRunning];
    cam->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_camera_stop(ydev_camera_t *cam)
{
    if (!cam) return YDEV_INVALID_ARG;
    YdevCameraSession *s = (__bridge YdevCameraSession *)cam->session_obj;
    [s->session stopRunning];
    cam->started = 0;
    ydev_vfd_close(&cam->vfd);
    return YDEV_OK;
}

void ydev_camera_close(ydev_camera_t *cam)
{
    if (!cam) return;
    if (cam->started) ydev_camera_stop(cam);
    if (cam->session_obj) {
        YdevCameraSession *s = (YdevCameraSession *)CFBridgingRelease(cam->session_obj);
        s->owner = NULL;
        s = nil;
    }
    ydev_vfd_destroy(&cam->vfd);
    free(cam);
}

int ydev_camera_fd(ydev_camera_t *cam)
{
    if (!cam) return -1;
    return ydev_vfd_fd(&cam->vfd);
}

ydev_result_t ydev_camera_acquire_frame(ydev_camera_t *cam, ydev_frame_t *out,
                                        int timeout_ms)
{
    if (!cam || !out) return YDEV_INVALID_ARG;
    apple_cam_slot_t slot;
    ydev_result_t r = ydev_vfd_pop(&cam->vfd, &slot, timeout_ms);
    if (r != YDEV_OK) return r;

    memset(out, 0, sizeof *out);
    out->data   = slot.data;
    out->size   = slot.size;
    out->width  = slot.width;
    out->height = slot.height;
    memcpy(out->stride, slot.stride, sizeof out->stride);
    memcpy(out->plane_offset, slot.plane_offset, sizeof out->plane_offset);
    out->format = (ydev_pixel_format_t)slot.format;
    out->ts_ns  = slot.ts_ns;
    out->seq    = slot.seq;
    out->opaque = slot.cvpb;     /* CVPixelBufferRef, retained + locked   */
    return YDEV_OK;
}

ydev_result_t ydev_camera_release_frame(ydev_camera_t *cam, ydev_frame_t *fr)
{
    (void)cam;
    if (!fr) return YDEV_INVALID_ARG;
    CVPixelBufferRef pb = (CVPixelBufferRef)fr->opaque;
    if (pb) {
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        CFRelease(pb);
    }
    fr->opaque = NULL;
    fr->data   = NULL;
    return YDEV_OK;
}
