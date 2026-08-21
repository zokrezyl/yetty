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
    char path[YCOMPLEX2_PATH_LIMIT];
};

YETTY_YRESULT_DECLARE(yetty_ycomplex2_video_ptr, struct yetty_ycomplex2_video *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_VIDEO_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ycomplex2_video_class_get(void);

static struct yetty_yclass_void_ptr_result video_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ycomplex2_video_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "video_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "video_from_obj: object_data");
    return slice_r;
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
    struct yetty_yclass_void_ptr_result video_r = video_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, video_r, "ycomplex2 set_h264: object");
    struct yetty_ycomplex2_video *video = (struct yetty_ycomplex2_video *)video_r.value;
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
    struct yetty_yclass_void_ptr_result video_r = video_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, video_r, "ycomplex2 video pack: object");
    struct yetty_ycomplex2_video *video = (struct yetty_ycomplex2_video *)video_r.value;
    if (video->path[0] == '\0') {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: h264 path not set");
    }
    uint8_t *nal_bytes = NULL;
    struct yetty_ycore_size_result read_r = ycomplex2_read_file(video->path, &nal_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, read_r, "ycomplex2 video pack: read");
    uint32_t video_w = video->video_w;
    uint32_t video_h = video->video_h;
    if (video_w == 0 || video_h == 0) {
        if (!yetty_yvideo_h264_dimensions(nal_bytes, read_r.value, &video_w, &video_h)) {
            free(nal_bytes);
            return YETTY_ERR(yetty_ycore_void,
                             "ycomplex2 video pack: no SPS in stream — set video_w/video_h");
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
    struct yetty_ydraw_drawable_list_result rendered_r =
        yetty_yvideo_render(nal_bytes, read_r.value, NULL, 0, &config);
    free(nal_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rendered_r, "ycomplex2 video pack: render");
    /* A nonzero id wraps the record in CMD_GROUP(id) so later CMD_UPDATE
     * envelopes (frame streaming) can address the live instance. */
    uint32_t group_marker_offset = 0;
    if (video->id != 0) {
        struct yetty_ydraw_id_result group_r =
            yetty_ydraw_drawable_list_begin_group(list, video->id);
        if (YETTY_IS_ERR(group_r)) {
            yetty_ydraw_drawable_list_destroy(rendered_r.value);
            return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: begin_group", group_r);
        }
        group_marker_offset = group_r.value;
    }
    struct yetty_ycore_void_result append_r = ycomplex2_append_rendered(list, rendered_r.value);
    yetty_ydraw_drawable_list_destroy(rendered_r.value);
    if (YETTY_IS_ERR(append_r)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 video pack: append", append_r);
    }
    if (video->id != 0) {
        struct yetty_ycore_void_result end_r =
            yetty_ydraw_drawable_list_end_group(list, group_marker_offset);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_r, "ycomplex2 video pack: end_group");
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ycomplex2/video.c"

#endif /* YETTY_YCOMPLEX2_HAS_YVIDEO */
