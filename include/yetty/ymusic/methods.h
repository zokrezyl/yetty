/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YMUSIC_METHODS_H
#define YETTY_YCLASSGEN_YMUSIC_METHODS_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/ymusic/types.h"

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;

struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float staff_space, uint32_t flags);
struct yetty_ycore_void_result yetty_ymusic_set_font_path(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          const char *path);
struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y);
struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          int32_t element_id);
struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);

#endif
