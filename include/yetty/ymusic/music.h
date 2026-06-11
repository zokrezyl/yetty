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

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymusic_music;
YETTY_YRESULT_DECLARE(yetty_ymusic_music_ptr, struct yetty_ymusic_music *);
struct yetty_ymusic_music_ptr_result yetty_ymusic_music_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ymusic_music_to(struct yetty_ymusic_music *data);

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
