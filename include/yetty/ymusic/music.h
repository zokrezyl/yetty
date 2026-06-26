/* GENERATED — do not edit. */
/* Public interface for regular class(es) `music` (module: ymusic).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUSIC_MUSIC_H
#define YETTY_YCLASSGEN_YMUSIC_MUSIC_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* Public constants. Defined here in the owning .c; codegen reproduces the
 * enum into the generated music.h for consumers. */
enum yetty_ymusic_constant {
    YETTY_YMUSIC_NO_ELEMENT = -1,
    YETTY_YMUSIC_FLAG_NONE = 0,
};

struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymusic_music;
struct yetty_ymusic_music_ptr_result {
    int ok;
    union {
        struct yetty_ymusic_music *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ymusic_music_ptr_result yetty_ymusic_music_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymusic_music_to(struct yetty_ymusic_music *data);

/* configure: set system width, staff-space (line gap) in px and render flags.
 * 0 selects the default for each. Call after create(), before render(). */
struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_object *obj, float width,
                                                      float staff_space, uint32_t flags);
/* parse: ingest LilyPond-subset text and build the score model. Resets the model
 * and clears any selection. */
struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
/* render: lay out the score and emit it as a fresh ydraw drawable list (caller
 * owns it). Pointer return -> local-only. */
struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_object *obj);
/* hit_test: id of the element whose column + system band contains content
 * (x,y), or YETTY_YMUSIC_NO_ELEMENT. Requires a prior render() for the layout. */
struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_object *obj, float x,
                                                    float y);
/* set_highlight: mark an element as selected (-1 clears) for the next render. */
struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_object *obj,
                                                          int32_t element_id);
/* destroy: free the score model and the object. */
struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ymusic_configure_fn)(struct yetty_yclass_object *,
                                                                    float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_parse_fn)(struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymusic_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymusic_hit_test_fn)(struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_ymusic_set_highlight_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymusic_register(void);

struct yetty_ycore_void_result yetty_ymusic_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd);

#ifdef __cplusplus
}
#endif

#endif
