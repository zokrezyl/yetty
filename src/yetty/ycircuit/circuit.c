/*
 * circuit.c — yclass class `ycircuit:circuit`: an electronic schematic renderer.
 *
 * `circuit` is a client-side object, modelled on `ymusic`: it parses a small
 * schematic DSL (or is filled programmatically through its add_* methods) into
 * an editable circuit model, lays that model out on a grid, and renders it to a
 * ydraw drawable list. It does NOT render itself — `yfigure` is the backend
 * that displays the list, shipped inline through the ycat / scrolling-layer
 * path or to a server figure.
 *
 * Symbols are drawn the classic ANSI way: wires, resistor zigzags, capacitor
 * plates, inductor humps, source circles, transistor bodies etc. are ydraw SDF
 * primitives (segments, circles, triangles); reference designators, values and
 * free labels are MSDF text spans using the receiver's default font. No
 * intermediate format — model straight to GPU primitives.
 *
 * The circuit model is a flat element list with a stable id on every element,
 * so an editor can hit-test, select and mutate elements in place;
 * `hit_test`/`set_highlight` already exercise that id surface.
 *
 * Being a yclass class, `make codegen` emits the public header (circuit.h),
 * the method dispatch, model.yaml, and the FFI / host-language binding
 * surface. The only hand-written file is this annotated .c; circuit.gen.c is
 * #included at the foot. Every slot is `local@` — a circuit model is an
 * in-process frontend, never proxied over RPC; the model still records the
 * methods so bindings emit them.
 *
 * DSL (line-based, `#` comments, coordinates in grid units; see README.md):
 *
 *   circuit Half-wave rectifier
 *   grid 14
 *   vsource   2  6  v  V1  9V
 *   diode     8  2  h  D1  1N4148
 *   resistor 14  6  v  R1  10k
 *   wire 2 2  5 2
 *   wire 11 2  14 2
 *   wire 2 10  14 10
 *   dot 14 10
 *   gnd 8 10
 *   label 15 2 Vout
 *
 * Component lines are `<kind> <x> <y> [<rot>] [<name>] [<value>]` with
 * rot ∈ {h, v, r0, r90, r180, r270}; `wire` takes 2+ points and emits one
 * element per segment. The generic `ic` kind additionally takes per-side pin
 * lists, e.g. `ic 14 8 h U1 NE555 l:GND,TRIG,OUT,RESET r:VCC,DIS,THR,CV`
 * (sides l/r/t/b; the body grows to fit the longest side).
 */
/* This TU deliberately does NOT include its own generated public header
 * `yetty/ycircuit/circuit.h` — that header is a downstream artifact for other
 * modules. The foundational types this TU and the appended circuit.gen.c need
 * (yclass identity, Result, core types) are pulled in directly below, and
 * this TU declares its own `yetty_ycircuit_circuit_ptr_result` (after the
 * class struct) — the same one circuit.h publishes for consumers. */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public constants. Defined here in the owning .c; codegen reproduces the
 * enum into the generated circuit.h for consumers. */
enum YETTY_ANNOTATE("expose") yetty_ycircuit_constant {
    YETTY_YCIRCUIT_NO_ELEMENT = -1, /* hit_test: no element under the point */
    YETTY_YCIRCUIT_FLAG_NONE = 0,   /* reserved render flags */
};

enum {
    YCIRCUIT_DEFAULT_GRID = 14, /* px per grid unit */
};

/* math.h only defines M_PI under POSIX feature macros; strict C23 omits it. */
#define YCIRCUIT_PI 3.14159265358979323846f

/* Ink (ABGR — byte0=R, byte1=G, byte2=B, byte3=A; see ymusic/music.c).
 * Brand palette roles: component bodies in off-white primary text, wires in
 * the cooler secondary gray, labels in the brand mint, selection in the
 * bright mint. */
enum {
    YCIRCUIT_INK = 0xFFE4E5E0u,       /* BRAND_TEXT_PRIMARY   #E0E5E4 */
    YCIRCUIT_WIRE_INK = 0xFFA8A79Fu,  /* BRAND_TEXT_SECONDARY #9FA7A8 */
    YCIRCUIT_LABEL_INK = 0xFF92A86Bu, /* BRAND_ACCENT         #6BA892 */
    YCIRCUIT_HIGHLIGHT = 0xFFA5C574u, /* BRAND_ACCENT_BRIGHT  #74C5A5 */
};

/*=============================================================================
 * Circuit model (internal — a flat element list, addressable by element id)
 *===========================================================================*/

enum ycircuit_component_kind {
    YCIRCUIT_COMP_RESISTOR = 0,
    YCIRCUIT_COMP_CAPACITOR,
    YCIRCUIT_COMP_INDUCTOR,
    YCIRCUIT_COMP_DIODE,
    YCIRCUIT_COMP_LED,
    YCIRCUIT_COMP_BATTERY,
    YCIRCUIT_COMP_VSOURCE,
    YCIRCUIT_COMP_ISOURCE,
    YCIRCUIT_COMP_ACSOURCE,
    YCIRCUIT_COMP_GND,
    YCIRCUIT_COMP_VCC,
    YCIRCUIT_COMP_NPN,
    YCIRCUIT_COMP_PNP,
    YCIRCUIT_COMP_SWITCH,
    YCIRCUIT_COMP_OPAMP,
    YCIRCUIT_COMP_IC,
    YCIRCUIT_COMP_KIND_COUNT,
};

enum ycircuit_element_type {
    YCIRCUIT_ELEM_COMPONENT = 0,
    YCIRCUIT_ELEM_WIRE,     /* one straight segment (x,y) -> (x2,y2) */
    YCIRCUIT_ELEM_JUNCTION, /* filled connection dot */
    YCIRCUIT_ELEM_LABEL,    /* free text anchored at (x,y) */
};

struct ycircuit_element {
    uint32_t id; /* stable handle for hit-test / editing (== index) */
    enum ycircuit_element_type type;
    enum ycircuit_component_kind kind; /* COMPONENT only */

    /* Model coordinates in grid units. (x,y) is the component centre (the
     * connection point for GND/VCC), the wire start, the junction centre or
     * the label anchor. (x2,y2) is the wire end. */
    float x, y;
    float x2, y2;
    int rot; /* quarter turns clockwise, 0..3 */

    char *name;  /* reference designator ("R1"); label text for LABEL */
    char *value; /* component value ("10k"); may be NULL */

    /* IC pin names per side, as comma-separated lists ("GND,TRIG,OUT").
     * NULL = no pins on that side. Only rendered for COMP_IC. */
    char *pins_left;
    char *pins_right;
    char *pins_top;
    char *pins_bottom;

    /* Scene-px bounding box, filled by the last render() (hit-test space). */
    float bbox_min_x, bbox_min_y;
    float bbox_max_x, bbox_max_y;
};

/*=============================================================================
 * Class data
 *===========================================================================*/

struct YETTY_ANNOTATE("class@ycircuit:circuit")
    YETTY_ANNOTATE("include@yetty/ydraw-core/drawable-list.h") yetty_ycircuit_circuit {
    /* Render configuration. 0 = default. */
    float grid;     /* px per grid unit (configure() wins over the DSL hint) */
    uint32_t flags; /* reserved */

    float grid_hint; /* from the DSL `grid` directive */
    char *title;     /* from the DSL `circuit` line; may be NULL */

    /* The elements, in creation order; element id == array index. */
    struct ycircuit_element *elements;
    size_t count;
    size_t cap;

    /* Interaction + last layout. Highlight is stored as id+1 so the
     * zero-initialised object means "no selection". */
    int32_t highlight_id_plus_one;
    float content_w;
    float content_h;
};

/* Result wrapper for the circuit handle. Declared here (not pulled from
 * circuit.h, which this TU does not include) so the appended circuit.gen.c —
 * which defines yetty_ycircuit_circuit_from() returning it — has the type in
 * scope. The public circuit.h publishes the identical declaration for other
 * modules. */
YETTY_YRESULT_DECLARE(yetty_ycircuit_circuit_ptr, struct yetty_ycircuit_circuit *);

/* Defined in the appended circuit.gen.c (foot of this TU). Forward-declared
 * here because this TU does not include its own generated header — the class
 * accessor backs circuit_from_obj below, and the obj→body downcast is part of
 * the generated public surface this TU must keep in scope. */
struct yetty_yclass_ptr_result yetty_ycircuit_circuit_class_get(void);
struct yetty_ycircuit_circuit_ptr_result yetty_ycircuit_circuit_from(
    struct yetty_yclass_object *obj);

static struct yetty_yclass_void_ptr_result circuit_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ycircuit_circuit_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "circuit_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/*=============================================================================
 * Model construction / teardown
 *===========================================================================*/

static void element_free_strings(struct ycircuit_element *element)
{
    free(element->name);
    free(element->value);
    free(element->pins_left);
    free(element->pins_right);
    free(element->pins_top);
    free(element->pins_bottom);
    element->name = NULL;
    element->value = NULL;
    element->pins_left = NULL;
    element->pins_right = NULL;
    element->pins_top = NULL;
    element->pins_bottom = NULL;
}

static void model_clear(struct yetty_ycircuit_circuit *circuit)
{
    for (size_t i = 0; i < circuit->count; i++) {
        element_free_strings(&circuit->elements[i]);
    }
    free(circuit->elements);
    circuit->elements = NULL;
    circuit->count = 0;
    circuit->cap = 0;
    free(circuit->title);
    circuit->title = NULL;
    circuit->grid_hint = 0.0f;
    circuit->highlight_id_plus_one = 0;
}

/* Append a zeroed element and return it (id pre-set), or NULL on OOM. */
static struct ycircuit_element *model_append(struct yetty_ycircuit_circuit *circuit)
{
    if (circuit->count == circuit->cap) {
        size_t new_cap = circuit->cap ? circuit->cap * 2 : 16;
        struct ycircuit_element *grown =
            realloc(circuit->elements, new_cap * sizeof(struct ycircuit_element));
        if (!grown) {
            return NULL;
        }
        circuit->elements = grown;
        circuit->cap = new_cap;
    }
    struct ycircuit_element *element = &circuit->elements[circuit->count];
    memset(element, 0, sizeof(*element));
    element->id = (uint32_t)circuit->count;
    circuit->count++;
    return element;
}

/* strdup that maps NULL / empty to NULL (absent). */
static char *dup_or_null(const char *text)
{
    if (!text || !text[0]) {
        return NULL;
    }
    return strdup(text);
}

/*=============================================================================
 * Component kind table
 *===========================================================================*/

static int component_kind_of_name(const char *word, enum ycircuit_component_kind *kind_out)
{
    static const struct {
        const char *name;
        enum ycircuit_component_kind kind;
    } kinds[] = {
        {"resistor", YCIRCUIT_COMP_RESISTOR},
        {"r", YCIRCUIT_COMP_RESISTOR},
        {"capacitor", YCIRCUIT_COMP_CAPACITOR},
        {"cap", YCIRCUIT_COMP_CAPACITOR},
        {"c", YCIRCUIT_COMP_CAPACITOR},
        {"inductor", YCIRCUIT_COMP_INDUCTOR},
        {"coil", YCIRCUIT_COMP_INDUCTOR},
        {"l", YCIRCUIT_COMP_INDUCTOR},
        {"diode", YCIRCUIT_COMP_DIODE},
        {"d", YCIRCUIT_COMP_DIODE},
        {"led", YCIRCUIT_COMP_LED},
        {"battery", YCIRCUIT_COMP_BATTERY},
        {"bat", YCIRCUIT_COMP_BATTERY},
        {"vsource", YCIRCUIT_COMP_VSOURCE},
        {"v", YCIRCUIT_COMP_VSOURCE},
        {"isource", YCIRCUIT_COMP_ISOURCE},
        {"i", YCIRCUIT_COMP_ISOURCE},
        {"acsource", YCIRCUIT_COMP_ACSOURCE},
        {"ac", YCIRCUIT_COMP_ACSOURCE},
        {"gnd", YCIRCUIT_COMP_GND},
        {"ground", YCIRCUIT_COMP_GND},
        {"vcc", YCIRCUIT_COMP_VCC},
        {"npn", YCIRCUIT_COMP_NPN},
        {"pnp", YCIRCUIT_COMP_PNP},
        {"switch", YCIRCUIT_COMP_SWITCH},
        {"sw", YCIRCUIT_COMP_SWITCH},
        {"opamp", YCIRCUIT_COMP_OPAMP},
        {"ic", YCIRCUIT_COMP_IC},
        {"chip", YCIRCUIT_COMP_IC},
    };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (strcmp(word, kinds[i].name) == 0) {
            *kind_out = kinds[i].kind;
            return 1;
        }
    }
    return 0;
}

/* h / v / r0 / r90 / r180 / r270 / 0 / 90 / 180 / 270 → quarter turns. */
static int rotation_of_token(const char *token, int *rot_out)
{
    const char *digits = token;
    if (strcmp(token, "h") == 0) {
        *rot_out = 0;
        return 1;
    }
    if (strcmp(token, "v") == 0) {
        *rot_out = 1;
        return 1;
    }
    if (token[0] == 'r') {
        digits = token + 1;
    }
    if (strcmp(digits, "0") == 0) {
        *rot_out = 0;
        return 1;
    }
    if (strcmp(digits, "90") == 0) {
        *rot_out = 1;
        return 1;
    }
    if (strcmp(digits, "180") == 0) {
        *rot_out = 2;
        return 1;
    }
    if (strcmp(digits, "270") == 0) {
        *rot_out = 3;
        return 1;
    }
    return 0;
}

/*=============================================================================
 * Parser — a line-based schematic DSL.
 *===========================================================================*/

enum {
    YCIRCUIT_MAX_TOKENS = 34,
    YCIRCUIT_TOKEN_MAX = 64,
};

struct parsed_line {
    char tokens[YCIRCUIT_MAX_TOKENS][YCIRCUIT_TOKEN_MAX];
    size_t token_count;
    /* Byte range (within the line) of everything after the Nth token —
     * used by `circuit` / `label` to take the raw rest of the line. */
    const char *line;
    size_t line_len;
    size_t tail_offsets[YCIRCUIT_MAX_TOKENS]; /* tail after token i ends */
};

static int is_blank(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r';
}

/* Split [line, line+len) into whitespace-separated tokens; a `#` outside the
 * first column also terminates the line (inline comment). */
static void tokenize_line(const char *line, size_t len, struct parsed_line *out)
{
    out->token_count = 0;
    out->line = line;
    size_t pos = 0;
    while (pos < len && line[pos] != '#') {
        while (pos < len && is_blank(line[pos])) {
            pos++;
        }
        if (pos >= len || line[pos] == '#') {
            break;
        }
        size_t start = pos;
        while (pos < len && !is_blank(line[pos]) && line[pos] != '#') {
            pos++;
        }
        if (out->token_count < YCIRCUIT_MAX_TOKENS) {
            size_t token_len = pos - start;
            if (token_len >= YCIRCUIT_TOKEN_MAX) {
                token_len = YCIRCUIT_TOKEN_MAX - 1;
            }
            memcpy(out->tokens[out->token_count], line + start, token_len);
            out->tokens[out->token_count][token_len] = '\0';
            out->tail_offsets[out->token_count] = pos;
            out->token_count++;
        }
    }
    /* Trim the comment off the stored line length. */
    size_t effective = len;
    for (size_t i = 0; i < len; i++) {
        if (line[i] == '#') {
            effective = i;
            break;
        }
    }
    out->line_len = effective;
}

/* The raw rest of the line after token `index`, trimmed, optional quotes
 * stripped. Returns a malloc'd string or NULL when empty/OOM. */
static char *line_tail_dup(const struct parsed_line *parsed, size_t index)
{
    if (index >= parsed->token_count) {
        return NULL;
    }
    size_t start = parsed->tail_offsets[index];
    size_t end = parsed->line_len;
    while (start < end && is_blank(parsed->line[start])) {
        start++;
    }
    while (end > start && (is_blank(parsed->line[end - 1]) || parsed->line[end - 1] == '\n')) {
        end--;
    }
    if (end > start + 1 && parsed->line[start] == '"' && parsed->line[end - 1] == '"') {
        start++;
        end--;
    }
    if (end <= start) {
        return NULL;
    }
    char *copy = malloc(end - start + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, parsed->line + start, end - start);
    copy[end - start] = '\0';
    return copy;
}

static int parse_float(const char *token, float *value_out)
{
    char *end = NULL;
    float value = strtof(token, &end);
    if (!end || end == token || *end != '\0') {
        return 0;
    }
    *value_out = value;
    return 1;
}

/* IC pin-spec token (`l:GND,TRIG`, `r:VCC`, `t:...`, `b:...`) → side index
 * (0=left 1=right 2=top 3=bottom), or -1 when the token is not a pin spec. */
static int pin_spec_side(const char *token)
{
    if (token[0] == '\0' || token[1] != ':') {
        return -1;
    }
    switch (token[0]) {
    case 'l':
        return 0;
    case 'r':
        return 1;
    case 't':
        return 2;
    case 'b':
        return 3;
    default:
        return -1;
    }
}

static struct yetty_ycore_void_result parse_component_line(struct yetty_ycircuit_circuit *circuit,
                                                           const struct parsed_line *parsed,
                                                           enum ycircuit_component_kind kind)
{
    if (parsed->token_count < 3) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit parse: component needs `<kind> <x> <y>`");
    }
    float x = 0.0f, y = 0.0f;
    if (!parse_float(parsed->tokens[1], &x) || !parse_float(parsed->tokens[2], &y)) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit parse: bad component coordinates");
    }
    size_t next = 3;
    int rot = 0;
    if (next < parsed->token_count && rotation_of_token(parsed->tokens[next], &rot)) {
        next++;
    }
    /* Remaining tokens: name, then value, with `l:`/`r:`/`t:`/`b:` pin specs
     * (any position) routed to their side. */
    const char *name = NULL;
    const char *value = NULL;
    const char *pin_specs[4] = {NULL, NULL, NULL, NULL};
    for (; next < parsed->token_count; next++) {
        const char *token = parsed->tokens[next];
        int side = pin_spec_side(token);
        if (side >= 0) {
            pin_specs[side] = token + 2;
        } else if (!name) {
            name = token;
        } else if (!value) {
            value = token;
        }
    }

    struct ycircuit_element *element = model_append(circuit);
    if (!element) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit parse: out of memory (component)");
    }
    element->type = YCIRCUIT_ELEM_COMPONENT;
    element->kind = kind;
    element->x = x;
    element->y = y;
    element->rot = rot;
    element->name = dup_or_null(name);
    element->value = dup_or_null(value);
    element->pins_left = dup_or_null(pin_specs[0]);
    element->pins_right = dup_or_null(pin_specs[1]);
    element->pins_top = dup_or_null(pin_specs[2]);
    element->pins_bottom = dup_or_null(pin_specs[3]);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_wire_line(struct yetty_ycircuit_circuit *circuit,
                                                      const struct parsed_line *parsed)
{
    size_t coord_count = parsed->token_count - 1;
    if (coord_count < 4 || (coord_count % 2) != 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "ycircuit parse: wire needs `wire x0 y0 x1 y1 [x2 y2 ...]`");
    }
    float prev_x = 0.0f, prev_y = 0.0f;
    for (size_t pair = 0; pair * 2 + 1 < coord_count; pair++) {
        float x = 0.0f, y = 0.0f;
        if (!parse_float(parsed->tokens[1 + pair * 2], &x) ||
            !parse_float(parsed->tokens[2 + pair * 2], &y)) {
            return YETTY_ERR(yetty_ycore_void, "ycircuit parse: bad wire coordinate");
        }
        if (pair > 0) {
            struct ycircuit_element *element = model_append(circuit);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ycircuit parse: out of memory (wire)");
            }
            element->type = YCIRCUIT_ELEM_WIRE;
            element->x = prev_x;
            element->y = prev_y;
            element->x2 = x;
            element->y2 = y;
        }
        prev_x = x;
        prev_y = y;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_circuit_text(struct yetty_ycircuit_circuit *circuit,
                                                         const char *input, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && input[pos] != '\n') {
            pos++;
        }
        size_t line_len = pos - line_start;
        if (pos < len) {
            pos++; /* consume the newline */
        }

        struct parsed_line parsed;
        tokenize_line(input + line_start, line_len, &parsed);
        if (parsed.token_count == 0) {
            continue;
        }
        const char *keyword = parsed.tokens[0];

        if (strcmp(keyword, "circuit") == 0) {
            free(circuit->title);
            circuit->title = line_tail_dup(&parsed, 0);
            continue;
        }
        if (strcmp(keyword, "grid") == 0) {
            float grid = 0.0f;
            if (parsed.token_count < 2 || !parse_float(parsed.tokens[1], &grid) || grid <= 0.0f) {
                return YETTY_ERR(yetty_ycore_void, "ycircuit parse: bad `grid` directive");
            }
            circuit->grid_hint = grid;
            continue;
        }
        if (strcmp(keyword, "wire") == 0) {
            struct yetty_ycore_void_result wire_res = parse_wire_line(circuit, &parsed);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wire_res, "ycircuit parse: wire");
            continue;
        }
        if (strcmp(keyword, "dot") == 0 || strcmp(keyword, "junction") == 0) {
            float x = 0.0f, y = 0.0f;
            if (parsed.token_count < 3 || !parse_float(parsed.tokens[1], &x) ||
                !parse_float(parsed.tokens[2], &y)) {
                return YETTY_ERR(yetty_ycore_void, "ycircuit parse: dot needs `dot <x> <y>`");
            }
            struct ycircuit_element *element = model_append(circuit);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ycircuit parse: out of memory (dot)");
            }
            element->type = YCIRCUIT_ELEM_JUNCTION;
            element->x = x;
            element->y = y;
            continue;
        }
        if (strcmp(keyword, "label") == 0) {
            float x = 0.0f, y = 0.0f;
            if (parsed.token_count < 4 || !parse_float(parsed.tokens[1], &x) ||
                !parse_float(parsed.tokens[2], &y)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "ycircuit parse: label needs `label <x> <y> <text>`");
            }
            struct ycircuit_element *element = model_append(circuit);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ycircuit parse: out of memory (label)");
            }
            element->type = YCIRCUIT_ELEM_LABEL;
            element->x = x;
            element->y = y;
            element->name = line_tail_dup(&parsed, 2);
            continue;
        }

        enum ycircuit_component_kind kind;
        if (component_kind_of_name(keyword, &kind)) {
            struct yetty_ycore_void_result comp_res = parse_component_line(circuit, &parsed, kind);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, comp_res, "ycircuit parse: component");
            continue;
        }

        ydebug("ycircuit parse: unknown keyword '%s'", keyword);
        return YETTY_ERR(yetty_ycore_void, "ycircuit parse: unknown keyword");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Layout geometry — local symbol space.
 *
 * Two-terminal components span 6 grid units pin-to-pin: pins at local (-3,0)
 * and (+3,0), the body within roughly ±2 units. Local coordinates rotate in
 * quarter turns around the element centre (screen y grows downward, so rot=1
 * turns the +x axis to point down). GND and VCC anchor their connection point
 * at the element position itself.
 *===========================================================================*/

struct emit_geom {
    struct yetty_ydraw_drawable_list *buf;
    float grid;     /* px per grid unit */
    float origin_x; /* scene-px offset applied to all model coordinates */
    float origin_y;
    float font_size; /* label font in px (IC pin labels derive from it) */
    uint32_t *z_order;
};

static void rotate_local(int rot, float dx, float dy, float *out_x, float *out_y)
{
    switch (rot & 3) {
    case 1:
        *out_x = -dy;
        *out_y = dx;
        break;
    case 2:
        *out_x = -dx;
        *out_y = -dy;
        break;
    case 3:
        *out_x = dy;
        *out_y = -dx;
        break;
    case 0:
    default:
        *out_x = dx;
        *out_y = dy;
        break;
    }
}

/* Scene-px position of the element-local point (dx,dy) in grid units. */
static void scene_point(const struct emit_geom *geom, const struct ycircuit_element *element,
                        float dx, float dy, float *out_x, float *out_y)
{
    float rotated_x = 0.0f, rotated_y = 0.0f;
    rotate_local(element->rot, dx, dy, &rotated_x, &rotated_y);
    *out_x = geom->origin_x + (element->x + rotated_x) * geom->grid;
    *out_y = geom->origin_y + (element->y + rotated_y) * geom->grid;
}

static float line_thickness(const struct emit_geom *geom)
{
    float thickness = geom->grid * 0.12f;
    return thickness < 1.0f ? 1.0f : thickness;
}

static struct yetty_ycore_void_result emit_scene_line(const struct emit_geom *geom, float x0,
                                                      float y0, float x1, float y1, uint32_t color,
                                                      float thickness)
{
    struct yetty_ysdf_segment segment = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    return yetty_ydraw_drawable_list_add_cmd_add_segment(geom->buf, /*id=*/0,
                                                         /*z_order=*/(*geom->z_order)++,
                                                         /*fill=*/0u, color, thickness, &segment);
}

/* One straight stroke between two element-local points. */
static struct yetty_ycore_void_result emit_local_line(const struct emit_geom *geom,
                                                      const struct ycircuit_element *element,
                                                      float x0, float y0, float x1, float y1,
                                                      uint32_t color)
{
    float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
    scene_point(geom, element, x0, y0, &sx0, &sy0);
    scene_point(geom, element, x1, y1, &sx1, &sy1);
    return emit_scene_line(geom, sx0, sy0, sx1, sy1, color, line_thickness(geom));
}

/* A polyline through element-local points. */
static struct yetty_ycore_void_result emit_local_polyline(const struct emit_geom *geom,
                                                          const struct ycircuit_element *element,
                                                          const float *points, size_t point_count,
                                                          uint32_t color)
{
    for (size_t i = 0; i + 1 < point_count; i++) {
        struct yetty_ycore_void_result line_res =
            emit_local_line(geom, element, points[i * 2], points[i * 2 + 1], points[i * 2 + 2],
                            points[i * 2 + 3], color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "ycircuit: polyline");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_local_circle(const struct emit_geom *geom,
                                                        const struct ycircuit_element *element,
                                                        float cx, float cy, float radius_units,
                                                        uint32_t color, int filled)
{
    float sx = 0.0f, sy = 0.0f;
    scene_point(geom, element, cx, cy, &sx, &sy);
    struct yetty_ysdf_circle circle = {
        .center_x = sx,
        .center_y = sy,
        .radius = radius_units * geom->grid,
    };
    uint32_t fill_color = filled ? color : 0u;
    uint32_t stroke_color = filled ? 0u : color;
    float stroke_width = filled ? 0.0f : line_thickness(geom);
    return yetty_ydraw_drawable_list_add_cmd_add_circle(geom->buf, /*id=*/0,
                                                        /*z_order=*/(*geom->z_order)++, fill_color,
                                                        stroke_color, stroke_width, &circle);
}

static struct yetty_ycore_void_result emit_local_triangle(const struct emit_geom *geom,
                                                          const struct ycircuit_element *element,
                                                          float ax, float ay, float bx, float by,
                                                          float cx, float cy, uint32_t color)
{
    struct yetty_ysdf_triangle triangle;
    scene_point(geom, element, ax, ay, &triangle.vertex_a_x, &triangle.vertex_a_y);
    scene_point(geom, element, bx, by, &triangle.vertex_b_x, &triangle.vertex_b_y);
    scene_point(geom, element, cx, cy, &triangle.vertex_c_x, &triangle.vertex_c_y);
    return yetty_ydraw_drawable_list_add_cmd_add_triangle(geom->buf, /*id=*/0,
                                                          /*z_order=*/(*geom->z_order)++, color,
                                                          /*stroke=*/0u, 0.0f, &triangle);
}

/* Arrow: shaft (x0,y0)->(x1,y1) plus a two-stroke head at the tip. */
static struct yetty_ycore_void_result emit_local_arrow(const struct emit_geom *geom,
                                                       const struct ycircuit_element *element,
                                                       float x0, float y0, float x1, float y1,
                                                       uint32_t color)
{
    struct yetty_ycore_void_result shaft_res =
        emit_local_line(geom, element, x0, y0, x1, y1, color);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shaft_res, "ycircuit: arrow shaft");

    float dir_x = x1 - x0;
    float dir_y = y1 - y0;
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length <= 0.0f) {
        return YETTY_OK_VOID();
    }
    dir_x /= length;
    dir_y /= length;
    float perp_x = -dir_y;
    float perp_y = dir_x;
    float head = 0.35f;
    struct yetty_ycore_void_result head_a_res =
        emit_local_line(geom, element, x1, y1, x1 - dir_x * head + perp_x * head * 0.6f,
                        y1 - dir_y * head + perp_y * head * 0.6f, color);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, head_a_res, "ycircuit: arrow head");
    return emit_local_line(geom, element, x1, y1, x1 - dir_x * head - perp_x * head * 0.6f,
                           y1 - dir_y * head - perp_y * head * 0.6f, color);
}

/* MSDF text span at a scene-px anchor (default font). */
static struct yetty_ycore_void_result emit_scene_text(const struct emit_geom *geom,
                                                      const char *text, float x, float y,
                                                      float font_size, uint32_t color)
{
    size_t text_len = strlen(text);
    struct yetty_ycore_buffer text_view = {
        .data = (uint8_t *)text,
        .capacity = text_len,
        .size = text_len,
    };
    return yetty_ydraw_drawable_list_add_text(geom->buf, x, y, &text_view, font_size, color,
                                              (*geom->z_order)++, /*font_id=*/-1,
                                              /*rotation=*/0.0f);
}

/* Crude advance estimate for centring labels (no shaping on this side). */
static float estimate_text_width(const char *text, float font_size)
{
    return text ? (float)strlen(text) * font_size * 0.55f : 0.0f;
}

/*=============================================================================
 * Symbol emitters — one per component kind, drawing in local units.
 *===========================================================================*/

static struct yetty_ycore_void_result emit_resistor(const struct emit_geom *geom,
                                                    const struct ycircuit_element *element,
                                                    uint32_t ink)
{
    static const float zigzag[] = {
        -3.0f, 0.0f,   -1.8f, 0.0f,  -1.5f, 0.55f,  -0.9f, -0.55f, -0.3f, 0.55f,
        0.3f,  -0.55f, 0.9f,  0.55f, 1.5f,  -0.55f, 1.8f,  0.0f,   3.0f,  0.0f,
    };
    return emit_local_polyline(geom, element, zigzag, sizeof(zigzag) / (2 * sizeof(float)), ink);
}

static struct yetty_ycore_void_result emit_capacitor(const struct emit_geom *geom,
                                                     const struct ycircuit_element *element,
                                                     uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -0.35f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: capacitor lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 0.35f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: capacitor lead");
    struct yetty_ycore_void_result plate_a =
        emit_local_line(geom, element, -0.35f, -1.0f, -0.35f, 1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plate_a, "ycircuit: capacitor plate");
    return emit_local_line(geom, element, 0.35f, -1.0f, 0.35f, 1.0f, ink);
}

static struct yetty_ycore_void_result emit_inductor(const struct emit_geom *geom,
                                                    const struct ycircuit_element *element,
                                                    uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -2.4f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: inductor lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 2.4f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: inductor lead");

    /* Four upper semicircle humps, each approximated by 8 chords. */
    enum { HUMP_STEPS = 8 };
    for (int hump = 0; hump < 4; hump++) {
        float center = -1.8f + 1.2f * (float)hump;
        float prev_x = center - 0.6f;
        float prev_y = 0.0f;
        for (int step = 1; step <= HUMP_STEPS; step++) {
            float angle = YCIRCUIT_PI * (1.0f - (float)step / (float)HUMP_STEPS);
            float x = center + 0.6f * cosf(angle);
            float y = -0.6f * sinf(angle);
            struct yetty_ycore_void_result chord_res =
                emit_local_line(geom, element, prev_x, prev_y, x, y, ink);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, chord_res, "ycircuit: inductor hump");
            prev_x = x;
            prev_y = y;
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_diode_body(const struct emit_geom *geom,
                                                      const struct ycircuit_element *element,
                                                      uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -0.9f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: diode lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 0.9f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: diode lead");
    struct yetty_ycore_void_result body_res =
        emit_local_triangle(geom, element, -0.9f, -0.9f, -0.9f, 0.9f, 0.9f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: diode body");
    return emit_local_line(geom, element, 0.9f, -0.9f, 0.9f, 0.9f, ink);
}

static struct yetty_ycore_void_result emit_led(const struct emit_geom *geom,
                                               const struct ycircuit_element *element, uint32_t ink)
{
    struct yetty_ycore_void_result diode_res = emit_diode_body(geom, element, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, diode_res, "ycircuit: led body");
    struct yetty_ycore_void_result ray_a =
        emit_local_arrow(geom, element, 0.1f, -1.0f, 0.8f, -1.7f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ray_a, "ycircuit: led ray");
    return emit_local_arrow(geom, element, 0.8f, -0.7f, 1.5f, -1.4f, ink);
}

static struct yetty_ycore_void_result emit_battery(const struct emit_geom *geom,
                                                   const struct ycircuit_element *element,
                                                   uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -0.3f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: battery lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 0.3f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: battery lead");
    /* Short plate = negative pole, long plate = positive pole. */
    struct yetty_ycore_void_result short_plate =
        emit_local_line(geom, element, -0.3f, -0.55f, -0.3f, 0.55f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, short_plate, "ycircuit: battery plate");
    struct yetty_ycore_void_result long_plate =
        emit_local_line(geom, element, 0.3f, -1.1f, 0.3f, 1.1f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, long_plate, "ycircuit: battery plate");
    /* Plus marker beside the long plate. */
    struct yetty_ycore_void_result plus_h =
        emit_local_line(geom, element, 0.75f, -1.3f, 1.25f, -1.3f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plus_h, "ycircuit: battery plus");
    return emit_local_line(geom, element, 1.0f, -1.55f, 1.0f, -1.05f, ink);
}

static struct yetty_ycore_void_result emit_vsource(const struct emit_geom *geom,
                                                   const struct ycircuit_element *element,
                                                   uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -1.2f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: vsource lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 1.2f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: vsource lead");
    struct yetty_ycore_void_result body_res =
        emit_local_circle(geom, element, 0.0f, 0.0f, 1.2f, ink, /*filled=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: vsource body");
    /* Plus on the (x2) side, minus on the (x) side. */
    struct yetty_ycore_void_result plus_h =
        emit_local_line(geom, element, 0.3f, 0.0f, 0.9f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plus_h, "ycircuit: vsource plus");
    struct yetty_ycore_void_result plus_v =
        emit_local_line(geom, element, 0.6f, -0.3f, 0.6f, 0.3f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plus_v, "ycircuit: vsource plus");
    return emit_local_line(geom, element, -0.9f, 0.0f, -0.3f, 0.0f, ink);
}

static struct yetty_ycore_void_result emit_isource(const struct emit_geom *geom,
                                                   const struct ycircuit_element *element,
                                                   uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -1.2f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: isource lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 1.2f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: isource lead");
    struct yetty_ycore_void_result body_res =
        emit_local_circle(geom, element, 0.0f, 0.0f, 1.2f, ink, /*filled=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: isource body");
    return emit_local_arrow(geom, element, -0.7f, 0.0f, 0.7f, 0.0f, ink);
}

static struct yetty_ycore_void_result emit_acsource(const struct emit_geom *geom,
                                                    const struct ycircuit_element *element,
                                                    uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -1.2f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: acsource lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 1.2f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: acsource lead");
    struct yetty_ycore_void_result body_res =
        emit_local_circle(geom, element, 0.0f, 0.0f, 1.2f, ink, /*filled=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: acsource body");

    /* One sine period across the circle, as chords. */
    enum { SINE_STEPS = 12 };
    float prev_x = -0.75f;
    float prev_y = 0.0f;
    for (int step = 1; step <= SINE_STEPS; step++) {
        float t = (float)step / (float)SINE_STEPS;
        float x = -0.75f + 1.5f * t;
        float y = -0.5f * sinf(2.0f * YCIRCUIT_PI * t);
        struct yetty_ycore_void_result chord_res =
            emit_local_line(geom, element, prev_x, prev_y, x, y, ink);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, chord_res, "ycircuit: acsource sine");
        prev_x = x;
        prev_y = y;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_gnd(const struct emit_geom *geom,
                                               const struct ycircuit_element *element, uint32_t ink)
{
    struct yetty_ycore_void_result stem_res =
        emit_local_line(geom, element, 0.0f, 0.0f, 0.0f, 0.9f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stem_res, "ycircuit: gnd stem");
    struct yetty_ycore_void_result bar_a =
        emit_local_line(geom, element, -0.8f, 0.9f, 0.8f, 0.9f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bar_a, "ycircuit: gnd bar");
    struct yetty_ycore_void_result bar_b =
        emit_local_line(geom, element, -0.5f, 1.25f, 0.5f, 1.25f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bar_b, "ycircuit: gnd bar");
    return emit_local_line(geom, element, -0.2f, 1.6f, 0.2f, 1.6f, ink);
}

static struct yetty_ycore_void_result emit_vcc(const struct emit_geom *geom,
                                               const struct ycircuit_element *element, uint32_t ink)
{
    struct yetty_ycore_void_result stem_res =
        emit_local_line(geom, element, 0.0f, 0.0f, 0.0f, -1.2f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stem_res, "ycircuit: vcc stem");
    return emit_local_line(geom, element, -0.6f, -1.2f, 0.6f, -1.2f, ink);
}

static struct yetty_ycore_void_result emit_transistor(const struct emit_geom *geom,
                                                      const struct ycircuit_element *element,
                                                      uint32_t ink, int is_pnp)
{
    struct yetty_ycore_void_result body_res =
        emit_local_circle(geom, element, 0.2f, 0.0f, 1.6f, ink, /*filled=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: transistor body");
    struct yetty_ycore_void_result base_lead =
        emit_local_line(geom, element, -3.0f, 0.0f, -0.6f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, base_lead, "ycircuit: transistor base");
    struct yetty_ycore_void_result base_bar =
        emit_local_line(geom, element, -0.6f, -1.0f, -0.6f, 1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, base_bar, "ycircuit: transistor bar");
    /* Collector up, emitter down; both legs end at local (1,±3). */
    struct yetty_ycore_void_result collector_diag =
        emit_local_line(geom, element, -0.6f, -0.4f, 1.0f, -1.3f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, collector_diag, "ycircuit: collector");
    struct yetty_ycore_void_result collector_leg =
        emit_local_line(geom, element, 1.0f, -1.3f, 1.0f, -3.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, collector_leg, "ycircuit: collector leg");
    struct yetty_ycore_void_result emitter_leg =
        emit_local_line(geom, element, 1.0f, 1.3f, 1.0f, 3.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emitter_leg, "ycircuit: emitter leg");
    /* The emitter arrow points out for NPN, in for PNP. */
    if (is_pnp) {
        return emit_local_arrow(geom, element, 1.0f, 1.3f, -0.6f, 0.4f, ink);
    }
    return emit_local_arrow(geom, element, -0.6f, 0.4f, 1.0f, 1.3f, ink);
}

static struct yetty_ycore_void_result emit_switch(const struct emit_geom *geom,
                                                  const struct ycircuit_element *element,
                                                  uint32_t ink)
{
    struct yetty_ycore_void_result lead_a =
        emit_local_line(geom, element, -3.0f, 0.0f, -1.2f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_a, "ycircuit: switch lead");
    struct yetty_ycore_void_result lead_b =
        emit_local_line(geom, element, 1.2f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_b, "ycircuit: switch lead");
    struct yetty_ycore_void_result pivot_a =
        emit_local_circle(geom, element, -1.2f, 0.0f, 0.14f, ink, /*filled=*/1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pivot_a, "ycircuit: switch pivot");
    struct yetty_ycore_void_result pivot_b =
        emit_local_circle(geom, element, 1.2f, 0.0f, 0.14f, ink, /*filled=*/1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pivot_b, "ycircuit: switch pivot");
    return emit_local_line(geom, element, -1.2f, 0.0f, 1.05f, -1.1f, ink);
}

/* Operational amplifier: triangle body, inverting (-) input top, the
 * non-inverting (+) input bottom, output right. Pins at local (-3,∓1) and
 * (3,0). */
static struct yetty_ycore_void_result emit_opamp(const struct emit_geom *geom,
                                                 const struct ycircuit_element *element,
                                                 uint32_t ink)
{
    static const float body[] = {
        -1.8f, -2.0f, 2.2f, 0.0f, -1.8f, 2.0f, -1.8f, -2.0f,
    };
    struct yetty_ycore_void_result body_res =
        emit_local_polyline(geom, element, body, sizeof(body) / (2 * sizeof(float)), ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: opamp body");
    struct yetty_ycore_void_result lead_minus =
        emit_local_line(geom, element, -3.0f, -1.0f, -1.8f, -1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_minus, "ycircuit: opamp lead");
    struct yetty_ycore_void_result lead_plus =
        emit_local_line(geom, element, -3.0f, 1.0f, -1.8f, 1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_plus, "ycircuit: opamp lead");
    struct yetty_ycore_void_result lead_out =
        emit_local_line(geom, element, 2.2f, 0.0f, 3.0f, 0.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_out, "ycircuit: opamp lead");
    /* Input polarity marks just inside the body. */
    struct yetty_ycore_void_result minus_mark =
        emit_local_line(geom, element, -1.5f, -1.0f, -1.0f, -1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, minus_mark, "ycircuit: opamp minus");
    struct yetty_ycore_void_result plus_mark_h =
        emit_local_line(geom, element, -1.5f, 1.0f, -1.0f, 1.0f, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plus_mark_h, "ycircuit: opamp plus");
    return emit_local_line(geom, element, -1.25f, 0.75f, -1.25f, 1.25f, ink);
}

/*-----------------------------------------------------------------------------
 * Generic IC — a DIP-style body with named pins on any side.
 *---------------------------------------------------------------------------*/

enum {
    YCIRCUIT_IC_PIN_NAME_MAX = 32,
};

/* Pin pitch and body sizing, in grid units. */
#define YCIRCUIT_IC_PIN_PITCH 1.6f
#define YCIRCUIT_IC_PIN_LEAD 1.2f
#define YCIRCUIT_IC_BODY_MARGIN 1.0f
#define YCIRCUIT_IC_MIN_HALF_WIDTH 2.6f
#define YCIRCUIT_IC_MIN_HALF_HEIGHT 1.6f

static int pin_list_count(const char *list)
{
    if (!list || !list[0]) {
        return 0;
    }
    int count = 1;
    for (const char *ch = list; *ch; ch++) {
        if (*ch == ',') {
            count++;
        }
    }
    return count;
}

/* Copy the index-th comma-separated pin name into out (NUL-terminated,
 * truncated). Returns 0 when the list has no such entry. */
static int pin_list_get(const char *list, int index, char *out, size_t out_cap)
{
    if (!list) {
        return 0;
    }
    const char *cursor = list;
    for (int i = 0; i < index; i++) {
        cursor = strchr(cursor, ',');
        if (!cursor) {
            return 0;
        }
        cursor++;
    }
    size_t written = 0;
    while (cursor[written] && cursor[written] != ',' && written + 1 < out_cap) {
        out[written] = cursor[written];
        written++;
    }
    out[written] = '\0';
    return 1;
}

struct ic_geom {
    float half_width;  /* body half-extent along the local x axis */
    float half_height; /* body half-extent along the local y axis */
    int left_count, right_count, top_count, bottom_count;
};

static struct ic_geom ic_geometry_of(const struct ycircuit_element *element)
{
    struct ic_geom ic = {
        .left_count = pin_list_count(element->pins_left),
        .right_count = pin_list_count(element->pins_right),
        .top_count = pin_list_count(element->pins_top),
        .bottom_count = pin_list_count(element->pins_bottom),
    };
    int side_max = ic.left_count > ic.right_count ? ic.left_count : ic.right_count;
    int topbot_max = ic.top_count > ic.bottom_count ? ic.top_count : ic.bottom_count;
    ic.half_height =
        (float)(side_max - 1) * (YCIRCUIT_IC_PIN_PITCH * 0.5f) + YCIRCUIT_IC_BODY_MARGIN;
    if (ic.half_height < YCIRCUIT_IC_MIN_HALF_HEIGHT) {
        ic.half_height = YCIRCUIT_IC_MIN_HALF_HEIGHT;
    }
    ic.half_width =
        (float)(topbot_max - 1) * (YCIRCUIT_IC_PIN_PITCH * 0.5f) + YCIRCUIT_IC_BODY_MARGIN;
    if (ic.half_width < YCIRCUIT_IC_MIN_HALF_WIDTH) {
        ic.half_width = YCIRCUIT_IC_MIN_HALF_WIDTH;
    }
    return ic;
}

/* First pin coordinate along a side with `count` pins (pins are centred). */
static float ic_pin_start(int count)
{
    return -(float)(count - 1) * (YCIRCUIT_IC_PIN_PITCH * 0.5f);
}

/* One side's pin leads + inside labels. side: 0=left 1=right 2=top 3=bottom. */
static struct yetty_ycore_void_result emit_ic_side(const struct emit_geom *geom,
                                                   const struct ycircuit_element *element,
                                                   const struct ic_geom *ic, const char *list,
                                                   int side, uint32_t ink)
{
    int count = pin_list_count(list);
    float pin_font = geom->font_size * 0.62f;
    for (int i = 0; i < count; i++) {
        char pin_name[YCIRCUIT_IC_PIN_NAME_MAX];
        if (!pin_list_get(list, i, pin_name, sizeof(pin_name))) {
            break;
        }
        float along = ic_pin_start(count) + (float)i * YCIRCUIT_IC_PIN_PITCH;
        float lead_x0, lead_y0, lead_x1, lead_y1; /* local lead endpoints */
        float label_x, label_y;                   /* local label anchor */
        float label_units = estimate_text_width(pin_name, pin_font) / geom->grid;
        switch (side) {
        case 0: /* left */
            lead_x0 = -ic->half_width - YCIRCUIT_IC_PIN_LEAD;
            lead_y0 = along;
            lead_x1 = -ic->half_width;
            lead_y1 = along;
            label_x = -ic->half_width + 0.3f;
            label_y = along + 0.3f;
            break;
        case 1: /* right */
            lead_x0 = ic->half_width;
            lead_y0 = along;
            lead_x1 = ic->half_width + YCIRCUIT_IC_PIN_LEAD;
            lead_y1 = along;
            label_x = ic->half_width - 0.3f - label_units;
            label_y = along + 0.3f;
            break;
        case 2: /* top */
            lead_x0 = along;
            lead_y0 = -ic->half_height - YCIRCUIT_IC_PIN_LEAD;
            lead_x1 = along;
            lead_y1 = -ic->half_height;
            label_x = along - label_units * 0.5f;
            label_y = -ic->half_height + 1.0f;
            break;
        case 3: /* bottom */
        default:
            lead_x0 = along;
            lead_y0 = ic->half_height;
            lead_x1 = along;
            lead_y1 = ic->half_height + YCIRCUIT_IC_PIN_LEAD;
            label_x = along - label_units * 0.5f;
            label_y = ic->half_height - 0.5f;
            break;
        }
        struct yetty_ycore_void_result lead_res =
            emit_local_line(geom, element, lead_x0, lead_y0, lead_x1, lead_y1, ink);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lead_res, "ycircuit: ic pin lead");
        float scene_x = 0.0f, scene_y = 0.0f;
        scene_point(geom, element, label_x, label_y, &scene_x, &scene_y);
        struct yetty_ycore_void_result label_res =
            emit_scene_text(geom, pin_name, scene_x, scene_y, pin_font, ink);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "ycircuit: ic pin label");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_ic(const struct emit_geom *geom,
                                              const struct ycircuit_element *element, uint32_t ink)
{
    struct ic_geom ic = ic_geometry_of(element);
    /* Body outline (axis-aligned box prim; a quarter turn just swaps the
     * half-extents). */
    float scene_cx = 0.0f, scene_cy = 0.0f;
    scene_point(geom, element, 0.0f, 0.0f, &scene_cx, &scene_cy);
    int swapped = element->rot & 1;
    struct yetty_ysdf_box body = {
        .center_x = scene_cx,
        .center_y = scene_cy,
        .half_width = (swapped ? ic.half_height : ic.half_width) * geom->grid,
        .half_height = (swapped ? ic.half_width : ic.half_height) * geom->grid,
        .corner_radius = 0.2f * geom->grid,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        geom->buf, /*id=*/0, /*z_order=*/(*geom->z_order)++, /*fill=*/0u, ink, line_thickness(geom),
        &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ycircuit: ic body");

    struct yetty_ycore_void_result left_res =
        emit_ic_side(geom, element, &ic, element->pins_left, 0, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, left_res, "ycircuit: ic left pins");
    struct yetty_ycore_void_result right_res =
        emit_ic_side(geom, element, &ic, element->pins_right, 1, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, right_res, "ycircuit: ic right pins");
    struct yetty_ycore_void_result top_res =
        emit_ic_side(geom, element, &ic, element->pins_top, 2, ink);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, top_res, "ycircuit: ic top pins");
    return emit_ic_side(geom, element, &ic, element->pins_bottom, 3, ink);
}

static struct yetty_ycore_void_result emit_component_symbol(const struct emit_geom *geom,
                                                            const struct ycircuit_element *element,
                                                            uint32_t ink)
{
    switch (element->kind) {
    case YCIRCUIT_COMP_RESISTOR:
        return emit_resistor(geom, element, ink);
    case YCIRCUIT_COMP_CAPACITOR:
        return emit_capacitor(geom, element, ink);
    case YCIRCUIT_COMP_INDUCTOR:
        return emit_inductor(geom, element, ink);
    case YCIRCUIT_COMP_DIODE:
        return emit_diode_body(geom, element, ink);
    case YCIRCUIT_COMP_LED:
        return emit_led(geom, element, ink);
    case YCIRCUIT_COMP_BATTERY:
        return emit_battery(geom, element, ink);
    case YCIRCUIT_COMP_VSOURCE:
        return emit_vsource(geom, element, ink);
    case YCIRCUIT_COMP_ISOURCE:
        return emit_isource(geom, element, ink);
    case YCIRCUIT_COMP_ACSOURCE:
        return emit_acsource(geom, element, ink);
    case YCIRCUIT_COMP_GND:
        return emit_gnd(geom, element, ink);
    case YCIRCUIT_COMP_VCC:
        return emit_vcc(geom, element, ink);
    case YCIRCUIT_COMP_NPN:
        return emit_transistor(geom, element, ink, /*is_pnp=*/0);
    case YCIRCUIT_COMP_PNP:
        return emit_transistor(geom, element, ink, /*is_pnp=*/1);
    case YCIRCUIT_COMP_SWITCH:
        return emit_switch(geom, element, ink);
    case YCIRCUIT_COMP_OPAMP:
        return emit_opamp(geom, element, ink);
    case YCIRCUIT_COMP_IC:
        return emit_ic(geom, element, ink);
    default:
        return YETTY_ERR(yetty_ycore_void, "ycircuit: unknown component kind");
    }
}

/*=============================================================================
 * Extents + labels
 *===========================================================================*/

/* Half-extents of a component's symbol+labels footprint, in local units
 * BEFORE rotation (x = along the pin axis, y = across it). */
static void component_half_extents(const struct ycircuit_element *element, float *out_half_x,
                                   float *out_half_y)
{
    switch (element->kind) {
    case YCIRCUIT_COMP_GND:
    case YCIRCUIT_COMP_VCC:
        *out_half_x = 1.2f;
        *out_half_y = 2.0f;
        return;
    case YCIRCUIT_COMP_NPN:
    case YCIRCUIT_COMP_PNP:
        *out_half_x = 3.2f;
        *out_half_y = 3.2f;
        return;
    case YCIRCUIT_COMP_OPAMP:
        *out_half_x = 3.2f;
        *out_half_y = 2.6f;
        return;
    case YCIRCUIT_COMP_IC: {
        struct ic_geom ic = ic_geometry_of(element);
        *out_half_x = ic.half_width + YCIRCUIT_IC_PIN_LEAD + 0.2f;
        *out_half_y = ic.half_height + YCIRCUIT_IC_PIN_LEAD + 0.2f;
        return;
    }
    default:
        *out_half_x = 3.2f;
        *out_half_y = 2.2f;
        return;
    }
}

/* Raw (origin-less) scene-px bounding box of an element, including its text
 * labels. Written into the element; render() shifts it by the origin. */
static void element_compute_raw_bbox(struct ycircuit_element *element, float grid, float font_size)
{
    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    switch (element->type) {
    case YCIRCUIT_ELEM_WIRE: {
        float slop = 0.45f * grid;
        min_x = fminf(element->x, element->x2) * grid - slop;
        max_x = fmaxf(element->x, element->x2) * grid + slop;
        min_y = fminf(element->y, element->y2) * grid - slop;
        max_y = fmaxf(element->y, element->y2) * grid + slop;
        break;
    }
    case YCIRCUIT_ELEM_JUNCTION: {
        float slop = 0.5f * grid;
        min_x = element->x * grid - slop;
        max_x = element->x * grid + slop;
        min_y = element->y * grid - slop;
        max_y = element->y * grid + slop;
        break;
    }
    case YCIRCUIT_ELEM_LABEL: {
        float width = estimate_text_width(element->name, font_size);
        min_x = element->x * grid;
        max_x = element->x * grid + (width > 0.0f ? width : font_size);
        min_y = element->y * grid - font_size;
        max_y = element->y * grid + font_size * 0.35f;
        break;
    }
    case YCIRCUIT_ELEM_COMPONENT:
    default: {
        float half_x = 0.0f, half_y = 0.0f;
        component_half_extents(element, &half_x, &half_y);
        /* Quarter-turn rotation swaps the axes for odd rotations. */
        float eff_half_x = (element->rot & 1) ? half_y : half_x;
        float eff_half_y = (element->rot & 1) ? half_x : half_y;
        min_x = (element->x - eff_half_x) * grid;
        max_x = (element->x + eff_half_x) * grid;
        min_y = (element->y - eff_half_y) * grid;
        max_y = (element->y + eff_half_y) * grid;
        break;
    }
    }
    element->bbox_min_x = min_x;
    element->bbox_min_y = min_y;
    element->bbox_max_x = max_x;
    element->bbox_max_y = max_y;
}

/* Designator + value text beside a component. Text stays horizontal for
 * readability: above/below the body for horizontal components, to the right
 * of it for vertical ones. */
static struct yetty_ycore_void_result emit_component_labels(const struct emit_geom *geom,
                                                            const struct ycircuit_element *element,
                                                            float font_size)
{
    float center_x = geom->origin_x + element->x * geom->grid;
    float center_y = geom->origin_y + element->y * geom->grid;
    int vertical = element->rot & 1;

    /* IC: the designator sits above the body (clear of any top pins), the
     * part number is centred inside it (the pin labels hug the edges). */
    if (element->kind == YCIRCUIT_COMP_IC) {
        struct ic_geom ic = ic_geometry_of(element);
        float body_half_up = (vertical ? ic.half_width : ic.half_height) +
                             (ic.top_count > 0 ? YCIRCUIT_IC_PIN_LEAD : 0.0f);
        if (element->name) {
            float width = estimate_text_width(element->name, font_size);
            struct yetty_ycore_void_result name_res = emit_scene_text(
                geom, element->name, center_x - width * 0.5f,
                center_y - (body_half_up + 0.5f) * geom->grid, font_size, YCIRCUIT_LABEL_INK);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, name_res, "ycircuit: ic name");
        }
        if (element->value) {
            float width = estimate_text_width(element->value, font_size);
            struct yetty_ycore_void_result value_res =
                emit_scene_text(geom, element->value, center_x - width * 0.5f,
                                center_y + font_size * 0.35f, font_size, YCIRCUIT_LABEL_INK);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, value_res, "ycircuit: ic value");
        }
        return YETTY_OK_VOID();
    }

    if (element->name) {
        float width = estimate_text_width(element->name, font_size);
        float x = vertical ? center_x + 1.6f * geom->grid : center_x - width * 0.5f;
        float y = vertical ? center_y - 0.3f * geom->grid : center_y - 1.5f * geom->grid;
        struct yetty_ycore_void_result name_res =
            emit_scene_text(geom, element->name, x, y, font_size, YCIRCUIT_LABEL_INK);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, name_res, "ycircuit: name label");
    }
    if (element->value) {
        float width = estimate_text_width(element->value, font_size);
        float x = vertical ? center_x + 1.6f * geom->grid : center_x - width * 0.5f;
        float y = vertical ? center_y + 1.1f * geom->grid : center_y + 2.4f * geom->grid;
        struct yetty_ycore_void_result value_res =
            emit_scene_text(geom, element->value, x, y, font_size, YCIRCUIT_LABEL_INK);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, value_res, "ycircuit: value label");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* configure: set the grid pitch in px and render flags. 0 selects the
 * default. A `grid` directive in the parsed document takes precedence. Call
 * after create(), before render(). */
YETTY_ANNOTATE("virtual@ycircuit:circuit:configure")
YETTY_ANNOTATE("local@ycircuit:configure")
static struct yetty_ycore_void_result
    circuit_configure(struct yetty_yclass_object *obj, float grid_px, uint32_t flags)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, circuit_r, "ycircuit configure: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;
    circuit->grid = grid_px;
    circuit->flags = flags;
    return YETTY_OK_VOID();
}

/* parse: ingest schematic-DSL text and build the circuit model. Resets the
 * model and clears any selection. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:parse")
YETTY_ANNOTATE("local@ycircuit:parse")
static struct yetty_ycore_void_result
    circuit_parse(struct yetty_yclass_object *obj, const char *input, size_t len)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, circuit_r, "ycircuit parse: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;
    if (!input && len > 0) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit parse: NULL input");
    }
    model_clear(circuit);
    struct yetty_ycore_void_result parsed = parse_circuit_text(circuit, input, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parsed, "ycircuit parse: failed");
    return YETTY_OK_VOID();
}

/* clear: drop every element, the title and the selection (configuration is
 * kept). The programmatic counterpart of parse(""). */
YETTY_ANNOTATE("virtual@ycircuit:circuit:clear")
YETTY_ANNOTATE("local@ycircuit:clear")
static struct yetty_ycore_void_result circuit_clear(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, circuit_r, "ycircuit clear: from_obj");
    model_clear(circuit_r.value);
    return YETTY_OK_VOID();
}

/* add_component: append a component (kind by name: "resistor", "diode",
 * "npn", ...; rotation in degrees, multiples of 90; name/value may be NULL or
 * empty). Returns the new element id. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:add_component")
YETTY_ANNOTATE("local@ycircuit:add_component")
static struct yetty_ycore_int_result
    circuit_add_component(struct yetty_yclass_object *obj, const char *kind, float x, float y,
                          int32_t rotation_deg, const char *name, const char *value)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit add_component: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;
    if (!kind) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_component: NULL kind");
    }
    enum ycircuit_component_kind component_kind;
    if (!component_kind_of_name(kind, &component_kind)) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_component: unknown kind");
    }
    struct ycircuit_element *element = model_append(circuit);
    if (!element) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_component: out of memory");
    }
    element->type = YCIRCUIT_ELEM_COMPONENT;
    element->kind = component_kind;
    element->x = x;
    element->y = y;
    element->rot = ((rotation_deg / 90) % 4 + 4) % 4;
    element->name = dup_or_null(name);
    element->value = dup_or_null(value);
    return YETTY_OK(yetty_ycore_int, (int)element->id);
}

/* add_ic: append a generic IC body with named pins per side (comma-separated
 * lists like "GND,TRIG,OUT"; NULL or empty = no pins on that side). The body
 * grows to fit the longest side. Returns the new element id. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:add_ic")
YETTY_ANNOTATE("local@ycircuit:add_ic")
static struct yetty_ycore_int_result
    circuit_add_ic(struct yetty_yclass_object *obj, float x, float y, int32_t rotation_deg,
                   const char *name, const char *value, const char *pins_left,
                   const char *pins_right, const char *pins_top, const char *pins_bottom)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit add_ic: from_obj");
    struct ycircuit_element *element = model_append(circuit_r.value);
    if (!element) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_ic: out of memory");
    }
    element->type = YCIRCUIT_ELEM_COMPONENT;
    element->kind = YCIRCUIT_COMP_IC;
    element->x = x;
    element->y = y;
    element->rot = ((rotation_deg / 90) % 4 + 4) % 4;
    element->name = dup_or_null(name);
    element->value = dup_or_null(value);
    element->pins_left = dup_or_null(pins_left);
    element->pins_right = dup_or_null(pins_right);
    element->pins_top = dup_or_null(pins_top);
    element->pins_bottom = dup_or_null(pins_bottom);
    return YETTY_OK(yetty_ycore_int, (int)element->id);
}

/* add_wire: append one straight wire segment. Returns the new element id. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:add_wire")
YETTY_ANNOTATE("local@ycircuit:add_wire")
static struct yetty_ycore_int_result
    circuit_add_wire(struct yetty_yclass_object *obj, float x0, float y0, float x1, float y1)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit add_wire: from_obj");
    struct ycircuit_element *element = model_append(circuit_r.value);
    if (!element) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_wire: out of memory");
    }
    element->type = YCIRCUIT_ELEM_WIRE;
    element->x = x0;
    element->y = y0;
    element->x2 = x1;
    element->y2 = y1;
    return YETTY_OK(yetty_ycore_int, (int)element->id);
}

/* add_junction: append a connection dot. Returns the new element id. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:add_junction")
YETTY_ANNOTATE("local@ycircuit:add_junction")
static struct yetty_ycore_int_result
    circuit_add_junction(struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit add_junction: from_obj");
    struct ycircuit_element *element = model_append(circuit_r.value);
    if (!element) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_junction: out of memory");
    }
    element->type = YCIRCUIT_ELEM_JUNCTION;
    element->x = x;
    element->y = y;
    return YETTY_OK(yetty_ycore_int, (int)element->id);
}

/* add_label: append a free text label anchored at (x,y). Returns the new
 * element id. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:add_label")
YETTY_ANNOTATE("local@ycircuit:add_label")
static struct yetty_ycore_int_result
    circuit_add_label(struct yetty_yclass_object *obj, float x, float y, const char *text)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit add_label: from_obj");
    if (!text || !text[0]) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_label: empty text");
    }
    struct ycircuit_element *element = model_append(circuit_r.value);
    if (!element) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_label: out of memory");
    }
    element->type = YCIRCUIT_ELEM_LABEL;
    element->x = x;
    element->y = y;
    element->name = dup_or_null(text);
    if (!element->name) {
        return YETTY_ERR(yetty_ycore_int, "ycircuit add_label: out of memory (text)");
    }
    return YETTY_OK(yetty_ycore_int, (int)element->id);
}

/* render: lay out the circuit and emit it as a fresh ydraw drawable list
 * (caller owns it). Pointer return -> local-only. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:render")
YETTY_ANNOTATE("local@ycircuit:render")
static struct yetty_ydraw_drawable_list_result circuit_render(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, circuit_r, "ycircuit render: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;

    /* The document's explicit `grid` directive wins over configure()'s
     * (typically cell-derived) pitch; both win over the default. */
    float grid = circuit->grid_hint > 0.0f
                     ? circuit->grid_hint
                     : (circuit->grid > 0.0f ? circuit->grid : (float)YCIRCUIT_DEFAULT_GRID);
    float font_size = grid * 1.1f;
    float title_size = grid * 1.6f;
    float margin = grid * 1.5f;

    /* Pass 1 — raw (origin-less) bounding boxes + overall extents. */
    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    int have_extent = 0;
    for (size_t i = 0; i < circuit->count; i++) {
        struct ycircuit_element *element = &circuit->elements[i];
        element_compute_raw_bbox(element, grid, font_size);
        if (!have_extent) {
            min_x = element->bbox_min_x;
            min_y = element->bbox_min_y;
            max_x = element->bbox_max_x;
            max_y = element->bbox_max_y;
            have_extent = 1;
        } else {
            min_x = fminf(min_x, element->bbox_min_x);
            min_y = fminf(min_y, element->bbox_min_y);
            max_x = fmaxf(max_x, element->bbox_max_x);
            max_y = fmaxf(max_y, element->bbox_max_y);
        }
    }
    if (!have_extent) {
        min_x = 0.0f;
        min_y = 0.0f;
        max_x = 8.0f * grid;
        max_y = 4.0f * grid;
    }

    float title_band = circuit->title ? title_size * 1.8f : 0.0f;
    float origin_x = margin - min_x;
    float origin_y = margin + title_band - min_y;
    float content_w = (max_x - min_x) + 2.0f * margin;
    float title_width = circuit->title ? estimate_text_width(circuit->title, title_size) : 0.0f;
    if (title_width + 2.0f * margin > content_w) {
        content_w = title_width + 2.0f * margin;
    }
    float content_h = (max_y - min_y) + 2.0f * margin + title_band;
    circuit->content_w = content_w;
    circuit->content_h = content_h;

    /* Shift the stored bboxes into content space (hit-test space). */
    for (size_t i = 0; i < circuit->count; i++) {
        struct ycircuit_element *element = &circuit->elements[i];
        element->bbox_min_x += origin_x;
        element->bbox_max_x += origin_x;
        element->bbox_min_y += origin_y;
        element->bbox_max_y += origin_y;
    }

    struct yetty_ydraw_drawable_list_config buffer_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = content_w,
        .scene_max_y = content_h,
    };
    struct yetty_ydraw_drawable_list_result list_r =
        yetty_ydraw_drawable_list_config_buffer_create(&buffer_config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_r, "ycircuit render: list create");
    struct yetty_ydraw_drawable_list *buf = list_r.value;
    uint32_t z_order = 0;
    struct emit_geom geom = {
        .buf = buf,
        .grid = grid,
        .origin_x = origin_x,
        .origin_y = origin_y,
        .font_size = font_size,
        .z_order = &z_order,
    };

#define YCIRCUIT_FAIL(msg, res)                                                                    \
    do {                                                                                           \
        yetty_ydraw_drawable_list_destroy(buf);                                                    \
        return YETTY_ERR(yetty_ydraw_drawable_list, msg, res);                                     \
    } while (0)

    if (circuit->title) {
        struct yetty_ycore_void_result title_res = emit_scene_text(
            &geom, circuit->title, margin, margin + title_size * 0.8f, title_size, YCIRCUIT_INK);
        if (YETTY_IS_ERR(title_res)) {
            YCIRCUIT_FAIL("ycircuit render: title", title_res);
        }
    }

    /* Wires + junctions first (under the component bodies). */
    for (size_t i = 0; i < circuit->count; i++) {
        const struct ycircuit_element *element = &circuit->elements[i];
        if (element->type == YCIRCUIT_ELEM_WIRE) {
            float x0 = origin_x + element->x * grid;
            float y0 = origin_y + element->y * grid;
            float x1 = origin_x + element->x2 * grid;
            float y1 = origin_y + element->y2 * grid;
            struct yetty_ycore_void_result wire_res =
                emit_scene_line(&geom, x0, y0, x1, y1, YCIRCUIT_WIRE_INK, line_thickness(&geom));
            if (YETTY_IS_ERR(wire_res)) {
                YCIRCUIT_FAIL("ycircuit render: wire", wire_res);
            }
        } else if (element->type == YCIRCUIT_ELEM_JUNCTION) {
            struct yetty_ycore_void_result dot_res =
                emit_local_circle(&geom, element, 0.0f, 0.0f, 0.18f, YCIRCUIT_WIRE_INK,
                                  /*filled=*/1);
            if (YETTY_IS_ERR(dot_res)) {
                YCIRCUIT_FAIL("ycircuit render: junction", dot_res);
            }
        }
    }

    /* Component symbols + their labels, then free labels. */
    for (size_t i = 0; i < circuit->count; i++) {
        const struct ycircuit_element *element = &circuit->elements[i];
        if (element->type == YCIRCUIT_ELEM_COMPONENT) {
            struct yetty_ycore_void_result symbol_res =
                emit_component_symbol(&geom, element, YCIRCUIT_INK);
            if (YETTY_IS_ERR(symbol_res)) {
                YCIRCUIT_FAIL("ycircuit render: symbol", symbol_res);
            }
            struct yetty_ycore_void_result labels_res =
                emit_component_labels(&geom, element, font_size);
            if (YETTY_IS_ERR(labels_res)) {
                YCIRCUIT_FAIL("ycircuit render: labels", labels_res);
            }
        } else if (element->type == YCIRCUIT_ELEM_LABEL && element->name) {
            float x = origin_x + element->x * grid;
            float y = origin_y + element->y * grid;
            struct yetty_ycore_void_result label_res =
                emit_scene_text(&geom, element->name, x, y, font_size, YCIRCUIT_LABEL_INK);
            if (YETTY_IS_ERR(label_res)) {
                YCIRCUIT_FAIL("ycircuit render: label", label_res);
            }
        }
    }

    /* Highlight overlay for the selected element (editor affordance). */
    if (circuit->highlight_id_plus_one > 0 &&
        (size_t)(circuit->highlight_id_plus_one - 1) < circuit->count) {
        const struct ycircuit_element *element =
            &circuit->elements[circuit->highlight_id_plus_one - 1];
        struct yetty_ysdf_box box = {
            .center_x = (element->bbox_min_x + element->bbox_max_x) * 0.5f,
            .center_y = (element->bbox_min_y + element->bbox_max_y) * 0.5f,
            .half_width = (element->bbox_max_x - element->bbox_min_x) * 0.5f,
            .half_height = (element->bbox_max_y - element->bbox_min_y) * 0.5f,
            .corner_radius = grid * 0.3f,
        };
        struct yetty_ycore_void_result highlight_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            buf, /*id=*/0, /*z_order=*/z_order++, /*fill=*/0u, YCIRCUIT_HIGHLIGHT,
            /*stroke_width=*/grid * 0.16f, &box);
        if (YETTY_IS_ERR(highlight_res)) {
            YCIRCUIT_FAIL("ycircuit render: highlight", highlight_res);
        }
    }

#undef YCIRCUIT_FAIL
    ydebug("ycircuit render: elements=%zu grid=%.1f content=%.0fx%.0f", circuit->count,
           (double)grid, (double)content_w, (double)content_h);
    return YETTY_OK(yetty_ydraw_drawable_list, buf);
}

/* hit_test: id of the element whose bounding box contains content (x,y), or
 * YETTY_YCIRCUIT_NO_ELEMENT. Requires a prior render() for the layout. On
 * overlap the most recently added element wins (it draws on top). */
YETTY_ANNOTATE("virtual@ycircuit:circuit:hit_test")
YETTY_ANNOTATE("local@ycircuit:hit_test")
static struct yetty_ycore_int_result
    circuit_hit_test(struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, circuit_r, "ycircuit hit_test: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;
    for (size_t i = circuit->count; i > 0; i--) {
        const struct ycircuit_element *element = &circuit->elements[i - 1];
        if (x >= element->bbox_min_x && x <= element->bbox_max_x && y >= element->bbox_min_y &&
            y <= element->bbox_max_y) {
            return YETTY_OK(yetty_ycore_int, (int)element->id);
        }
    }
    return YETTY_OK(yetty_ycore_int, YETTY_YCIRCUIT_NO_ELEMENT);
}

/* set_highlight: mark an element as selected (-1 clears) for the next
 * render. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:set_highlight")
YETTY_ANNOTATE("local@ycircuit:set_highlight")
static struct yetty_ycore_void_result
    circuit_set_highlight(struct yetty_yclass_object *obj, int32_t element_id)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, circuit_r, "ycircuit set_highlight: from_obj");
    struct yetty_ycircuit_circuit *circuit = circuit_r.value;
    circuit->highlight_id_plus_one =
        (element_id >= 0 && (size_t)element_id < circuit->count) ? element_id + 1 : 0;
    return YETTY_OK_VOID();
}

/* destroy: free the circuit model and the object. */
YETTY_ANNOTATE("virtual@ycircuit:circuit:destroy")
YETTY_ANNOTATE("local@ycircuit:destroy")
static struct yetty_ycore_void_result circuit_obj_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result circuit_r = circuit_from_obj(obj);
    if (YETTY_IS_ERR(circuit_r)) {
        yetty_ycore_error_destroy(circuit_r.error);
    } else {
        model_clear(circuit_r.value);
    }
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * One-shot helper — serialize a rendered drawable list as a YDRAW_BIN OSC
 * envelope (the ycat / scrolling-layer path). Exposed for CLI / binding
 * front-ends.
 *===========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ycircuit_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                       int fd)
{
    if (!list) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit emit_osc: NULL list");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ycircuit emit_osc: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result emit_result = yetty_yface_emit_to_fd(
        fd, YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "ycircuit emit_osc: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

#include "circuit.gen.c"
