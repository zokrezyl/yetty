/*
 * video.c — yclass class `ycomplex2:video`: one yvideo complex record as a
 * v2 drawable. pack() reads the raw H.264 Annex-B stream and appends the
 * record carrying it as the initial chunk. video_w/video_h are REQUIRED
 * (the SPS dimensions; the class does no SPS parsing). Frame STREAMING —
 * CMD_UPDATE deltas to a live instance — stays with the yvideo tool; this
 * class covers the one-shot record, video-only (no audio).
 *
 * Feature-gated with the yvideo kind itself (YETTY_YCOMPLEX2_HAS_YVIDEO,
 * from YETTY_ENABLE_FEATURE_YVIDEO).
 */
#ifdef YETTY_YCOMPLEX2_HAS_YVIDEO

#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yvideo/yvideo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "complex2-internal.h"

struct YETTY_ANNOTATE("class@ycomplex2:video") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ycomplex2_video {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    YETTY_ANNOTATE("property") float width;
    YETTY_ANNOTATE("property") float height;
    YETTY_ANNOTATE("property") uint32_t id;
    YETTY_ANNOTATE("property") uint32_t video_w;
    YETTY_ANNOTATE("property") uint32_t video_h;
    YETTY_ANNOTATE("property") float fps;
    /* Stacking depth (z-order), uniform with every drawable's `layer`. */
    YETTY_ANNOTATE("property") int32_t layer;
    char path[YCOMPLEX2_PATH_LIMIT];
};

YETTY_YRESULT_DECLARE(yetty_ycomplex2_video_ptr, struct yetty_ycomplex2_video *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_VIDEO_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ycomplex2_video_class_get(void);

static struct yetty_yclass_void_ptr_result video_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ycomplex2_video_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "video_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "video_from_obj: object_data");
    return slice_res;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* set_h264: the raw Annex-B H.264 byte stream file (.h264/.264). */
YETTY_ANNOTATE("virtual@ycomplex2:video:set_h264")
YETTY_ANNOTATE("primary@ycomplex2:set_h264")
YETTY_ANNOTATE("local@ycomplex2:set_h264")
static struct yetty_ycore_void_result video_set_h264(struct yetty_yclass_object *obj,
                                                     const char *path YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result video_res = video_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, video_res, "ycomplex2 set_h264: object");
    struct yetty_ycomplex2_video *video = (struct yetty_ycomplex2_video *)video_res.value;
    if (!path || strlen(path) >= sizeof(video->path)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_h264: missing or too-long path");
    }
    snprintf(video->path, sizeof(video->path), "%s", path);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result video_pack(struct yetty_yclass_object *obj,
                                                 struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result video_res = video_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, video_res, "ycomplex2 video pack: object");
    struct yetty_ycomplex2_video *video = (struct yetty_ycomplex2_video *)video_res.value;
    if (video->path[0] == '\0') {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: h264 path not set");
    }
    uint8_t *nal_bytes = NULL;
    struct yetty_ycore_size_result read_res = ycomplex2_read_file(video->path, &nal_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "ycomplex2 video pack: read");
    uint32_t video_w = video->video_w;
    uint32_t video_h = video->video_h;
    if (video_w == 0 || video_h == 0) {
        uint32_t sps_w = 0;
        uint32_t sps_h = 0;
        if (!yetty_yvideo_h264_dimensions(nal_bytes, read_res.value, &sps_w, &sps_h)) {
            free(nal_bytes);
            return YETTY_ERR(yetty_ycore_void,
                             "ycomplex2 video pack: no SPS in stream — set video_w/video_h");
        }
        /* A partial override survives: only unset dims come from the SPS. */
        if (video_w == 0) {
            video_w = sps_w;
        }
        if (video_h == 0) {
            video_h = sps_h;
        }
    }
    struct yetty_yvideo_render_config config = {
        .bounds_x = video->x,
        .bounds_y = video->y,
        .bounds_w = video->width,
        .bounds_h = video->height,
        .video_w = video_w,
        .video_h = video_h,
        .fps = video->fps > 0.0f ? video->fps : 30.0f,
    };
    struct yetty_ydraw_drawable_list_result rendered_res =
        yetty_yvideo_render(nal_bytes, read_res.value, NULL, 0, &config);
    free(nal_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rendered_res, "ycomplex2 video pack: render");
    struct yetty_ycore_void_result paint_open_res = ycomplex2_paint_z_open(list, video->layer);
    if (YETTY_IS_ERR(paint_open_res)) {
        yetty_ydraw_drawable_list_destroy(rendered_res.value);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: paint_z open", paint_open_res);
    }
    /* A nonzero id makes the complex ITSELF the addressable node (its own
     * id, no wrapper group) so later CMD_UPDATE envelopes (frame streaming)
     * address the live instance. */
    struct yetty_ycore_void_result node_id_res = ycomplex2_node_id(list, video->id);
    if (YETTY_IS_ERR(node_id_res)) {
        yetty_ydraw_drawable_list_destroy(rendered_res.value);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: node_id", node_id_res);
    }
    struct yetty_ycore_void_result append_res = ycomplex2_append_rendered(list, rendered_res.value);
    yetty_ydraw_drawable_list_destroy(rendered_res.value);
    if (YETTY_IS_ERR(append_res)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: append", append_res);
    }
    struct yetty_ycore_void_result paint_close_res = ycomplex2_paint_z_close(list, video->layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paint_close_res, "ycomplex2 video pack: paint_z close");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ycomplex2/video.c"

#endif /* YETTY_YCOMPLEX2_HAS_YVIDEO */
