/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YMUSIC_METHODS_GEN_H
#define YETTY_YCLASSGEN_YMUSIC_METHODS_GEN_H

#include "yetty/ymusic/music.h"

typedef struct yetty_ycore_void_result (*yetty_ymusic_configure_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_set_font_path_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymusic_parse_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymusic_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymusic_hit_test_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_ymusic_set_highlight_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_destroy_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);

#endif
