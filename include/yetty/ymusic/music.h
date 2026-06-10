/* GENERATED — do not edit. */
/* Public interface for regular class(es) `music` (module: ymusic).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YMUSIC_MUSIC_H
#define YETTY_YCLASSGEN_YMUSIC_MUSIC_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void);

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

struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymusic_register(void);

struct yetty_ydraw_drawable_list;

/* render() returns struct yetty_ydraw_drawable_list_result by value, so the
 * generated music.h (and the dispatch TU that includes it) needs the complete
 * type — pull its defining header into the public header. */
#include <yetty/ydraw-core/drawable-list.h>
/* Public constants — copied verbatim into the generated music.h. */
#define YETTY_YMUSIC_NO_ELEMENT (-1) /* hit_test: no element under the point */
#define YETTY_YMUSIC_FLAG_NONE 0x0u  /* reserved render flags */
struct yetty_ycore_void_result yetty_ymusic_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd);

#endif
