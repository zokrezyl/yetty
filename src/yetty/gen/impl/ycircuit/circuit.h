/* GENERATED — do not edit. */
/* Public interface for regular class(es) `circuit` (module: ycircuit).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YCIRCUIT_CIRCUIT_H
#define YETTY_YCLASSGEN_YCIRCUIT_CIRCUIT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCIRCUIT_CONSTANT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCIRCUIT_CONSTANT
/* Public constants. Defined here in the owning .c; codegen reproduces the
 * enum into the generated circuit.h for consumers. */
enum yetty_ycircuit_constant {
    YETTY_YCIRCUIT_NO_ELEMENT = -1,
    YETTY_YCIRCUIT_FLAG_NONE = 0,
};
#endif

struct yetty_yclass_ptr_result yetty_ycircuit_circuit_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ycircuit_circuit;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCIRCUIT_CIRCUIT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCIRCUIT_CIRCUIT_PTR_RESULT
struct yetty_ycircuit_circuit_ptr_result {
    int ok;
    union {
        struct yetty_ycircuit_circuit *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ycircuit_circuit_ptr_result yetty_ycircuit_circuit_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_to(
    struct yetty_ycircuit_circuit *data);

/* configure: set the grid pitch in px and render flags. 0 selects the
 * default. A `grid` directive in the parsed document takes precedence. Call
 * after create(), before render(). */
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object *obj,
                                                        float grid_px, uint32_t flags);
/* parse: ingest schematic-DSL text and build the circuit model. Resets the
 * model and clears any selection. */
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object *obj,
                                                    const char *input, size_t len);
/* clear: drop every element, the title and the selection (configuration is
 * kept). The programmatic counterpart of parse(""). */
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object *obj);
/* add_component: append a component (kind by name: "resistor", "diode",
 * "npn", ...; rotation in degrees, multiples of 90; name/value may be NULL or
 * empty). Returns the new element id. */
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object *obj,
                                                           const char *kind, float x, float y,
                                                           int32_t rotation_deg, const char *name,
                                                           const char *value);
/* add_ic: append a generic IC body with named pins per side (comma-separated
 * lists like "GND,TRIG,OUT"; NULL or empty = no pins on that side). The body
 * grows to fit the longest side. Returns the new element id. */
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object *obj, float x,
                                                    float y, int32_t rotation_deg, const char *name,
                                                    const char *value, const char *pins_left,
                                                    const char *pins_right, const char *pins_top,
                                                    const char *pins_bottom);
/* add_wire: append one straight wire segment. Returns the new element id. */
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object *obj, float x0,
                                                      float y0, float x1, float y1);
/* add_junction: append a connection dot. Returns the new element id. */
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object *obj, float x,
                                                          float y);
/* add_label: append a free text label anchored at (x,y). Returns the new
 * element id. */
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object *obj, float x,
                                                       float y, const char *text);
/* render: lay out the circuit and emit it as a fresh ydraw drawable list
 * (caller owns it). Pointer return -> local-only. */
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object *obj);
/* hit_test: id of the element whose bounding box contains content (x,y), or
 * YETTY_YCIRCUIT_NO_ELEMENT. Requires a prior render() for the layout. On
 * overlap the most recently added element wins (it draws on top). */
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object *obj, float x,
                                                      float y);
/* set_highlight: mark an element as selected (-1 clears) for the next
 * render. */
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object *obj,
                                                            int32_t element_id);
/* destroy: free the circuit model and the object. */
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ycircuit_configure_fn)(struct yetty_yclass_object *,
                                                                      float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_parse_fn)(struct yetty_yclass_object *,
                                                                  const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_component_fn)(
    struct yetty_yclass_object *, const char *, float, float, int32_t, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_ic_fn)(struct yetty_yclass_object *,
                                                                  float, float, int32_t,
                                                                  const char *, const char *,
                                                                  const char *, const char *,
                                                                  const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_wire_fn)(struct yetty_yclass_object *,
                                                                    float, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_junction_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_label_fn)(struct yetty_yclass_object *,
                                                                     float, float, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycircuit_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_hit_test_fn)(struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_set_highlight_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ycircuit_register(void);

struct yetty_ycore_void_result yetty_ycircuit_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                       int fd);

#ifdef __cplusplus
}
#endif

#endif
