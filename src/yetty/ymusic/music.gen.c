/* GENERATED — do not edit. */
#include "yetty/ymusic/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ymusic_configure_fn yetty_ymusic_music_yetty_ymusic_configure_check = music_configure;
[[maybe_unused]]
static yetty_ymusic_set_font_path_fn yetty_ymusic_music_yetty_ymusic_set_font_path_check =
    music_set_font_path;
[[maybe_unused]]
static yetty_ymusic_parse_fn yetty_ymusic_music_yetty_ymusic_parse_check = music_parse;
[[maybe_unused]]
static yetty_ymusic_render_fn yetty_ymusic_music_yetty_ymusic_render_check = music_render;
[[maybe_unused]]
static yetty_ymusic_hit_test_fn yetty_ymusic_music_yetty_ymusic_hit_test_check = music_hit_test;
[[maybe_unused]]
static yetty_ymusic_set_highlight_fn yetty_ymusic_music_yetty_ymusic_set_highlight_check =
    music_set_highlight;
[[maybe_unused]]
static yetty_ymusic_destroy_fn yetty_ymusic_music_yetty_ymusic_destroy_check = music_obj_destroy;

struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ymusic_music");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymusic_music",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymusic_music),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ymusic", "configure", (yetty_yclass_method_id_t)yetty_ymusic_configure,
         (yetty_yclass_impl_t)music_configure},
        {"yetty_ymusic", "set_font_path", (yetty_yclass_method_id_t)yetty_ymusic_set_font_path,
         (yetty_yclass_impl_t)music_set_font_path},
        {"yetty_ymusic", "parse", (yetty_yclass_method_id_t)yetty_ymusic_parse,
         (yetty_yclass_impl_t)music_parse},
        {"yetty_ymusic", "render", (yetty_yclass_method_id_t)yetty_ymusic_render,
         (yetty_yclass_impl_t)music_render},
        {"yetty_ymusic", "hit_test", (yetty_yclass_method_id_t)yetty_ymusic_hit_test,
         (yetty_yclass_impl_t)music_hit_test},
        {"yetty_ymusic", "set_highlight", (yetty_yclass_method_id_t)yetty_ymusic_set_highlight,
         (yetty_yclass_impl_t)music_set_highlight},
        {"yetty_ymusic", "destroy", (yetty_yclass_method_id_t)yetty_ymusic_destroy,
         (yetty_yclass_impl_t)music_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ymusic_music_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymusic_music_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ymusic_music_ptr_result yetty_ymusic_music_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymusic_music_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ymusic_music_ptr, "yetty_ymusic_music_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ymusic_music_ptr, "yetty_ymusic_music_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ymusic_music_ptr, (struct yetty_ymusic_music *)slice_r.value);
}
