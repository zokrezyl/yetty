/*
 * music.c — yclass class `ymusic:music`: a LilyPond-subset music renderer.
 *
 * `music` is a client-side object, modelled on `yflame`: it parses a subset of
 * LilyPond input into an editable score model, lays that model out, and renders
 * it to a ydraw drawable list. It does NOT render itself — like a flame, it is a
 * frontend that produces the picture; `yfigure` is the backend that displays it,
 * and `yview` ships the list to a server figure (YCOMPOSITOR_BIN) so it scrolls.
 *
 * It engraves the way LilyPond does: the *symbolic* shapes (clefs, noteheads,
 * rests, accidentals, flags, dots) are glyphs pulled from the Emmentaler music
 * font (SIL OFL, vendored in assets/fonts/Emmentaler-20.otf) via the MSDF text
 * path; the *geometric* shapes (staff lines, stems, ledger lines, barlines) are
 * drawn directly as ydraw SDF segments. No intermediate format — model straight
 * to GPU primitives.
 *
 * The score model (score -> staff -> measure -> element -> note) is a mutable
 * tree with a stable id on every element, so a later editor can hit-test, select
 * and mutate notes in place; `hit_test`/`set_highlight` already exercise that id
 * surface.
 *
 * Being a yclass class, `make codegen` emits the public header (music.h), the
 * method dispatch, model.yaml, and the FFI / host-language binding surface. The
 * only hand-written file is this annotated .c; music.gen.c is #included at the
 * foot. Every slot is `local@` — a music model is an in-process frontend, never
 * proxied over RPC; the model still records the methods so bindings emit them.
 */
/* This TU deliberately does NOT include its own generated public header
 * `yetty/ymusic/music.h` — that header is a downstream artifact for other
 * modules. The foundational types this TU and the appended music.gen.c need
 * (yclass identity, Result, core types) are pulled in directly below, and
 * this TU declares its own `yetty_ymusic_music_ptr_result` (after the class
 * struct) — the same one music.h publishes for consumers. */
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
 * enum into the generated music.h for consumers. */
enum YETTY_ANNOTATE("expose") yetty_ymusic_constant {
    YETTY_YMUSIC_NO_ELEMENT = -1, /* hit_test: no element under the point */
    YETTY_YMUSIC_FLAG_NONE = 0,   /* reserved render flags */
};

enum {
    YMUSIC_DEFAULT_WIDTH = 1200,
    YMUSIC_DEFAULT_STAFF_SPACE = 12, /* px between adjacent staff lines */
};

/* Emmentaler (SMuFL-PUA) glyph codepoints. Keeping these as named constants is
 * the swap point: re-point this table at Bravura's codepoints and nothing else
 * in the renderer changes. */
enum {
    GLYPH_CLEF_G = 0xE085u, /* treble */
    GLYPH_CLEF_F = 0xE083u, /* bass */
    GLYPH_CLEF_C = 0xE07Fu, /* alto / tenor */

    GLYPH_NOTEHEAD_WHOLE = 0xE0E8u, /* noteheads.s0 */
    GLYPH_NOTEHEAD_HALF = 0xE0E9u,  /* noteheads.s1 (void) */
    GLYPH_NOTEHEAD_BLACK = 0xE0EAu, /* noteheads.s2 */

    GLYPH_REST_WHOLE = 0xE000u,   /* rests.0 */
    GLYPH_REST_HALF = 0xE001u,    /* rests.1 */
    GLYPH_REST_QUARTER = 0xE008u, /* rests.2 */
    GLYPH_REST_EIGHTH = 0xE00Bu,  /* rests.3 */
    GLYPH_REST_16TH = 0xE00Cu,    /* rests.4 */
    GLYPH_REST_32ND = 0xE00Du,    /* rests.5 */

    GLYPH_ACC_SHARP = 0xE013u,    /* accidentals.sharp */
    GLYPH_ACC_FLAT = 0xE021u,     /* accidentals.flat */
    GLYPH_ACC_NATURAL = 0xE01Du,  /* accidentals.natural */
    GLYPH_ACC_DBLSHARP = 0xE01Cu, /* accidentals.doublesharp */
    GLYPH_ACC_DBLFLAT = 0xE02Au,  /* accidentals.flatflat */

    GLYPH_FLAG_UP_BASE = 0xE0D2u,   /* flags.u3 (eighth, up); + (dur_log - 3) */
    GLYPH_FLAG_DOWN_BASE = 0xE0DAu, /* flags.d3 (eighth, down) */

    GLYPH_DOT = 0xE038u, /* dots.dot */
};

/* Ink (ABGR — byte0=R, byte1=G, byte2=B, byte3=A; see yflame/flame.c).
 * Off-white on the dark brand canvas, with a teal-tinted muted staff. */
enum {
    YMUSIC_INK = 0xFFE4E5E0u,    /* BRAND_TEXT_PRIMARY #E0E5E4 */
    YMUSIC_STAFF = 0xFF626155u,  /* BRAND_TEXT_MUTED   #556162 */
    YMUSIC_ACCENT = 0xFFA5C574u, /* BRAND_ACCENT_BRIGHT #74C5A5 */
};

/*=============================================================================
 * Score model (internal — a mutable tree, addressable by element id)
 *===========================================================================*/

enum ymusic_clef {
    YMUSIC_CLEF_TREBLE = 0,
    YMUSIC_CLEF_BASS,
    YMUSIC_CLEF_ALTO,
    YMUSIC_CLEF_TENOR,
};

enum ymusic_element_type {
    YMUSIC_ELEM_NOTE = 0, /* one or more noteheads sharing a stem (chord) */
    YMUSIC_ELEM_REST,
};

struct ymusic_note {
    int step;   /* 0..6 = C..B */
    int octave; /* scientific: middle C = C4 */
    int alter;  /* -2..+2 (accidental) */
};

struct ymusic_element {
    uint32_t id; /* stable handle for hit-test / editing */
    enum ymusic_element_type type;

    struct ymusic_note *notes; /* empty for a rest */
    size_t note_count;
    size_t note_cap;

    int dur_log; /* 0=whole 1=half 2=quarter 3=eighth 4=16th ... */
    int dots;
    int tie; /* model-only for now: tie into the next element */

    /* Filled by the layout pass. */
    int system; /* index of the system (staff line) the element wraps onto */
    float x;    /* left edge of the element's column (scene coords) */
    float adv;  /* column advance to the next element */
};

struct ymusic_measure {
    uint32_t id;
    struct ymusic_element **elements;
    size_t count;
    size_t cap;
};

struct ymusic_staff {
    enum ymusic_clef clef;
    int key_fifths; /* -7..+7 (negative = flats) */
    int time_num, time_den;
    struct ymusic_measure **measures;
    size_t count;
    size_t cap;
};

/*=============================================================================
 * Class data
 *===========================================================================*/

struct YETTY_ANNOTATE("class@ymusic:music")
    YETTY_ANNOTATE("include@yetty/ydraw-core/drawable-list.h") yetty_ymusic_music {
    /* Render configuration. */
    float width;
    float staff_space;
    uint32_t flags;

    /* The music font (Emmentaler) is referenced by name at render time and
     * resolved from the install by the receiver — ymusic never loads or ships
     * the font itself. */

    /* The score (single staff for now; the tree is shaped to grow). */
    struct ymusic_staff staff;

    /* Flat id -> element index, rebuilt on parse. */
    struct ymusic_element **index;
    uint32_t element_count;

    /* Interaction + last layout. */
    int32_t highlight_id;
    float content_w;
    float content_h;
};

/* Result wrapper for the music handle. Declared here (not pulled from
 * music.h, which this TU does not include) so the appended music.gen.c —
 * which defines yetty_ymusic_music_from() returning it — has the type in
 * scope. The public music.h publishes the identical declaration for other
 * modules. */
YETTY_YRESULT_DECLARE(yetty_ymusic_music_ptr, struct yetty_ymusic_music *);

/* Defined in the appended music.gen.c (foot of this TU). Forward-declared
 * here because this TU does not include its own generated header — the class
 * accessor backs music_from_obj below, and the obj→body downcast is part of
 * the generated public surface this TU must keep in scope. */
struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void);
struct yetty_ymusic_music_ptr_result yetty_ymusic_music_from(struct yetty_yclass_object *obj);

static struct yetty_yclass_void_ptr_result music_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymusic_music_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "music_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/*=============================================================================
 * Model construction / teardown
 *===========================================================================*/

static struct ymusic_element *element_create(enum ymusic_element_type type, uint32_t id)
{
    struct ymusic_element *element = calloc(1, sizeof(struct ymusic_element));
    if (!element) {
        return NULL;
    }
    element->type = type;
    element->id = id;
    element->dur_log = 2; /* quarter, overwritten by the parser */
    return element;
}

static int element_add_note(struct ymusic_element *element, int step, int octave, int alter)
{
    if (element->note_count == element->note_cap) {
        size_t new_cap = element->note_cap ? element->note_cap * 2 : 4;
        struct ymusic_note *grown = realloc(element->notes, new_cap * sizeof(struct ymusic_note));
        if (!grown) {
            return -1;
        }
        element->notes = grown;
        element->note_cap = new_cap;
    }
    element->notes[element->note_count].step = step;
    element->notes[element->note_count].octave = octave;
    element->notes[element->note_count].alter = alter;
    element->note_count++;
    return 0;
}

static void element_destroy(struct ymusic_element *element)
{
    if (!element) {
        return;
    }
    free(element->notes);
    free(element);
}

static struct ymusic_measure *measure_create(uint32_t id)
{
    struct ymusic_measure *measure = calloc(1, sizeof(struct ymusic_measure));
    if (measure) {
        measure->id = id;
    }
    return measure;
}

static int measure_add_element(struct ymusic_measure *measure, struct ymusic_element *element)
{
    if (measure->count == measure->cap) {
        size_t new_cap = measure->cap ? measure->cap * 2 : 8;
        struct ymusic_element **grown =
            realloc(measure->elements, new_cap * sizeof(struct ymusic_element *));
        if (!grown) {
            return -1;
        }
        measure->elements = grown;
        measure->cap = new_cap;
    }
    measure->elements[measure->count++] = element;
    return 0;
}

static void measure_destroy(struct ymusic_measure *measure)
{
    if (!measure) {
        return;
    }
    for (size_t i = 0; i < measure->count; i++) {
        element_destroy(measure->elements[i]);
    }
    free(measure->elements);
    free(measure);
}

static int staff_add_measure(struct ymusic_staff *staff, struct ymusic_measure *measure)
{
    if (staff->count == staff->cap) {
        size_t new_cap = staff->cap ? staff->cap * 2 : 8;
        struct ymusic_measure **grown =
            realloc(staff->measures, new_cap * sizeof(struct ymusic_measure *));
        if (!grown) {
            return -1;
        }
        staff->measures = grown;
        staff->cap = new_cap;
    }
    staff->measures[staff->count++] = measure;
    return 0;
}

static void staff_clear(struct ymusic_staff *staff)
{
    for (size_t i = 0; i < staff->count; i++) {
        measure_destroy(staff->measures[i]);
    }
    free(staff->measures);
    staff->measures = NULL;
    staff->count = 0;
    staff->cap = 0;
}

/*=============================================================================
 * Parser — a pragmatic LilyPond subset.
 *
 * Understood: \clef, \time N/D, \key <pitch> \major|\minor, \relative [<pitch>],
 * notes (a..g with is/es accidentals, '/, octave marks, optional duration and
 * dots), rests (r), chords (<...>), and bar checks (|). Unknown \commands,
 * strings and braces are skipped, so wrapping like \version / \header / \score /
 * { } / \new Staff is tolerated rather than parsed.
 *===========================================================================*/

struct parse_state {
    int prev_dur_log;
    int prev_dots;
    int relative; /* 1 once \relative seen */
    int rel_step; /* running relative reference */
    int rel_oct;
};

static int step_of_letter(char letter)
{
    switch (letter) {
    case 'c':
        return 0;
    case 'd':
        return 1;
    case 'e':
        return 2;
    case 'f':
        return 3;
    case 'g':
        return 4;
    case 'a':
        return 5;
    case 'b':
        return 6;
    default:
        return -1;
    }
}

static int dur_log_of_value(unsigned value)
{
    switch (value) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    case 16:
        return 4;
    case 32:
        return 5;
    case 64:
        return 6;
    default:
        return -1;
    }
}

static int relative_octave(int prev_step, int prev_oct, int step, int marks)
{
    int letter_delta = step - prev_step; /* -6..6 */
    int octave = prev_oct;
    if (letter_delta > 3) {
        octave -= 1;
    } else if (letter_delta < -3) {
        octave += 1;
    }
    return octave + marks;
}

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

/* Advance past whitespace, %{ ... %} block comments and % line comments. */
static void skip_trivia(const char *input, size_t len, size_t *pos)
{
    while (*pos < len) {
        char ch = input[*pos];
        if (is_space(ch)) {
            (*pos)++;
        } else if (ch == '%') {
            if (*pos + 1 < len && input[*pos + 1] == '{') {
                *pos += 2;
                while (*pos + 1 < len && !(input[*pos] == '%' && input[*pos + 1] == '}')) {
                    (*pos)++;
                }
                *pos += 2;
            } else {
                while (*pos < len && input[*pos] != '\n') {
                    (*pos)++;
                }
            }
        } else {
            break;
        }
    }
}

/* Read accidental suffixes (is/es pairs) and octave marks ('/,) after a letter.
 * Returns the alteration; writes the octave-mark sum (+1 per ', -1 per ,). */
static int read_accidentals_and_marks(const char *input, size_t len, size_t *pos, int *marks_out)
{
    int alter = 0;
    while (*pos + 1 < len) {
        if (input[*pos] == 'i' && input[*pos + 1] == 's') {
            alter += 1;
            *pos += 2;
        } else if (input[*pos] == 'e' && input[*pos + 1] == 's') {
            alter -= 1;
            *pos += 2;
        } else {
            break;
        }
    }
    int marks = 0;
    while (*pos < len && (input[*pos] == '\'' || input[*pos] == ',')) {
        marks += (input[*pos] == '\'') ? 1 : -1;
        (*pos)++;
    }
    *marks_out = marks;
    return alter;
}

/* Read an optional duration (digits + dots) at *pos. Returns 1 if a duration was
 * present (and fills dur_log/dots), 0 otherwise. */
static int read_duration(const char *input, size_t len, size_t *pos, int *dur_log_out,
                         int *dots_out)
{
    if (*pos >= len || input[*pos] < '0' || input[*pos] > '9') {
        return 0;
    }
    unsigned value = 0;
    while (*pos < len && input[*pos] >= '0' && input[*pos] <= '9') {
        value = value * 10 + (unsigned)(input[*pos] - '0');
        (*pos)++;
    }
    int dur_log = dur_log_of_value(value);
    if (dur_log < 0) {
        dur_log = 2; /* unrecognised → quarter */
    }
    int dots = 0;
    while (*pos < len && input[*pos] == '.') {
        dots++;
        (*pos)++;
    }
    *dur_log_out = dur_log;
    *dots_out = dots;
    return 1;
}

/* Read a bare word of ASCII letters into out (NUL-terminated, truncated). */
static void read_word(const char *input, size_t len, size_t *pos, char *out, size_t out_cap)
{
    size_t written = 0;
    while (*pos < len && ((input[*pos] >= 'a' && input[*pos] <= 'z') ||
                          (input[*pos] >= 'A' && input[*pos] <= 'Z'))) {
        if (written + 1 < out_cap) {
            out[written++] = input[*pos];
        }
        (*pos)++;
    }
    out[written] = '\0';
}

static void skip_string(const char *input, size_t len, size_t *pos)
{
    (*pos)++; /* opening quote */
    while (*pos < len && input[*pos] != '"') {
        if (input[*pos] == '\\' && *pos + 1 < len) {
            (*pos)++;
        }
        (*pos)++;
    }
    if (*pos < len) {
        (*pos)++; /* closing quote */
    }
}

static int fifths_for_key(int step, int alter, int minor)
{
    /* fifths of the natural major tonic, by letter. */
    static const int natural[7] = {0, 2, 4, -1, 1, 3, 5}; /* C D E F G A B */
    int fifths = natural[step] + 7 * alter;
    if (minor) {
        fifths -= 3; /* relative major is three fifths up */
    }
    if (fifths > 7) {
        fifths = 7;
    }
    if (fifths < -7) {
        fifths = -7;
    }
    return fifths;
}

/* Parse one note/chord pitch token's letter; caller has already matched a..g. */
static int parse_pitch_letter(const char *input, size_t len, size_t *pos, int *step_out,
                              int *alter_out, int *marks_out)
{
    int step = step_of_letter(input[*pos]);
    if (step < 0) {
        return -1;
    }
    (*pos)++;
    *alter_out = read_accidentals_and_marks(input, len, pos, marks_out);
    *step_out = step;
    return 0;
}

static enum ymusic_clef clef_of_name(const char *name)
{
    if (strcmp(name, "bass") == 0) {
        return YMUSIC_CLEF_BASS;
    }
    if (strcmp(name, "alto") == 0) {
        return YMUSIC_CLEF_ALTO;
    }
    if (strcmp(name, "tenor") == 0) {
        return YMUSIC_CLEF_TENOR;
    }
    return YMUSIC_CLEF_TREBLE; /* treble / violin / G / default */
}

static struct ymusic_measure *ensure_measure(struct ymusic_staff *staff, uint32_t *next_measure_id)
{
    if (staff->count == 0) {
        struct ymusic_measure *measure = measure_create((*next_measure_id)++);
        if (!measure || staff_add_measure(staff, measure) != 0) {
            measure_destroy(measure);
            return NULL;
        }
    }
    return staff->measures[staff->count - 1];
}

static struct yetty_ycore_void_result parse_lilypond(struct yetty_ymusic_music *music,
                                                     const char *input, size_t len)
{
    struct ymusic_staff *staff = &music->staff;
    struct parse_state state = {.prev_dur_log = 2,
                                .prev_dots = 0,
                                .relative = 0,
                                .rel_step = 0,
                                .rel_oct = 4 /* c' reference */};
    uint32_t next_element_id = 0;
    uint32_t next_measure_id = 0;
    size_t pos = 0;

    while (pos < len) {
        skip_trivia(input, len, &pos);
        if (pos >= len) {
            break;
        }
        char ch = input[pos];

        if (ch == '\\') {
            pos++;
            char word[32];
            read_word(input, len, &pos, word, sizeof(word));
            if (strcmp(word, "clef") == 0) {
                skip_trivia(input, len, &pos);
                if (pos < len && input[pos] == '"') {
                    /* read quoted name without the skip-string helper. */
                    pos++;
                    char name[32];
                    size_t written = 0;
                    while (pos < len && input[pos] != '"') {
                        if (written + 1 < sizeof(name)) {
                            name[written++] = input[pos];
                        }
                        pos++;
                    }
                    name[written] = '\0';
                    if (pos < len) {
                        pos++;
                    }
                    staff->clef = clef_of_name(name);
                } else {
                    char name[32];
                    read_word(input, len, &pos, name, sizeof(name));
                    staff->clef = clef_of_name(name);
                }
            } else if (strcmp(word, "time") == 0) {
                skip_trivia(input, len, &pos);
                int numerator = 0, denominator = 0;
                while (pos < len && input[pos] >= '0' && input[pos] <= '9') {
                    numerator = numerator * 10 + (input[pos] - '0');
                    pos++;
                }
                if (pos < len && input[pos] == '/') {
                    pos++;
                }
                while (pos < len && input[pos] >= '0' && input[pos] <= '9') {
                    denominator = denominator * 10 + (input[pos] - '0');
                    pos++;
                }
                if (numerator > 0 && denominator > 0) {
                    staff->time_num = numerator;
                    staff->time_den = denominator;
                }
            } else if (strcmp(word, "key") == 0) {
                skip_trivia(input, len, &pos);
                int step = 0, alter = 0, marks = 0;
                if (pos < len && step_of_letter(input[pos]) >= 0) {
                    parse_pitch_letter(input, len, &pos, &step, &alter, &marks);
                }
                skip_trivia(input, len, &pos);
                int minor = 0;
                if (pos < len && input[pos] == '\\') {
                    pos++;
                    char mode[16];
                    read_word(input, len, &pos, mode, sizeof(mode));
                    minor = (strcmp(mode, "minor") == 0);
                }
                staff->key_fifths = fifths_for_key(step, alter, minor);
            } else if (strcmp(word, "relative") == 0) {
                state.relative = 1;
                skip_trivia(input, len, &pos);
                if (pos < len && step_of_letter(input[pos]) >= 0) {
                    int step = 0, alter = 0, marks = 0;
                    parse_pitch_letter(input, len, &pos, &step, &alter, &marks);
                    state.rel_step = step;
                    state.rel_oct = 3 + marks; /* absolute octave of the reference */
                }
            } else if (strcmp(word, "version") == 0) {
                skip_trivia(input, len, &pos);
                if (pos < len && input[pos] == '"') {
                    skip_string(input, len, &pos);
                }
            }
            /* Any other \command is ignored. */
            continue;
        }

        if (ch == '"') {
            skip_string(input, len, &pos);
            continue;
        }

        if (ch == '|') {
            /* Explicit bar check → start a fresh measure (if the current one has
             * content). */
            pos++;
            if (staff->count > 0 && staff->measures[staff->count - 1]->count > 0) {
                struct ymusic_measure *measure = measure_create(next_measure_id++);
                if (!measure || staff_add_measure(staff, measure) != 0) {
                    measure_destroy(measure);
                    return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (measure)");
                }
            }
            continue;
        }

        if (ch == 'r' || ch == 'R') {
            pos++;
            int dur_log = state.prev_dur_log, dots = state.prev_dots;
            if (read_duration(input, len, &pos, &dur_log, &dots)) {
                state.prev_dur_log = dur_log;
                state.prev_dots = dots;
            }
            struct ymusic_measure *measure = ensure_measure(staff, &next_measure_id);
            if (!measure) {
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (measure)");
            }
            struct ymusic_element *element = element_create(YMUSIC_ELEM_REST, next_element_id++);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (rest)");
            }
            element->dur_log = dur_log;
            element->dots = dots;
            if (measure_add_element(measure, element) != 0) {
                element_destroy(element);
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (rest add)");
            }
            continue;
        }

        if (ch == '<') {
            /* Chord: <pitch pitch ...> duration. */
            pos++;
            struct ymusic_element *element = element_create(YMUSIC_ELEM_NOTE, next_element_id++);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (chord)");
            }
            int chord_prev_step = state.rel_step;
            int chord_prev_oct = state.rel_oct;
            int first_step = -1, first_oct = 0;
            while (pos < len && input[pos] != '>') {
                skip_trivia(input, len, &pos);
                if (pos < len && input[pos] == '>') {
                    break;
                }
                if (pos >= len || step_of_letter(input[pos]) < 0) {
                    pos++; /* tolerate stray tokens inside the chord */
                    continue;
                }
                int step = 0, alter = 0, marks = 0;
                parse_pitch_letter(input, len, &pos, &step, &alter, &marks);
                int octave;
                if (state.relative) {
                    octave = relative_octave(chord_prev_step, chord_prev_oct, step, marks);
                    chord_prev_step = step;
                    chord_prev_oct = octave;
                } else {
                    octave = 3 + marks;
                }
                if (first_step < 0) {
                    first_step = step;
                    first_oct = octave;
                }
                if (element_add_note(element, step, octave, alter) != 0) {
                    element_destroy(element);
                    return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (chord note)");
                }
            }
            if (pos < len && input[pos] == '>') {
                pos++;
            }
            int dur_log = state.prev_dur_log, dots = state.prev_dots;
            if (read_duration(input, len, &pos, &dur_log, &dots)) {
                state.prev_dur_log = dur_log;
                state.prev_dots = dots;
            }
            element->dur_log = dur_log;
            element->dots = dots;
            if (state.relative && first_step >= 0) {
                state.rel_step = first_step;
                state.rel_oct = first_oct;
            }
            if (element->note_count == 0) {
                element_destroy(element);
                continue;
            }
            struct ymusic_measure *measure = ensure_measure(staff, &next_measure_id);
            if (!measure || measure_add_element(measure, element) != 0) {
                element_destroy(element);
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (chord add)");
            }
            continue;
        }

        if (step_of_letter(ch) >= 0) {
            int step = 0, alter = 0, marks = 0;
            parse_pitch_letter(input, len, &pos, &step, &alter, &marks);
            int octave;
            if (state.relative) {
                octave = relative_octave(state.rel_step, state.rel_oct, step, marks);
                state.rel_step = step;
                state.rel_oct = octave;
            } else {
                octave = 3 + marks;
            }
            int dur_log = state.prev_dur_log, dots = state.prev_dots;
            if (read_duration(input, len, &pos, &dur_log, &dots)) {
                state.prev_dur_log = dur_log;
                state.prev_dots = dots;
            }
            /* Skip a trailing tie marker (~) if present. */
            int tie = 0;
            skip_trivia(input, len, &pos);
            if (pos < len && input[pos] == '~') {
                tie = 1;
                pos++;
            }
            struct ymusic_measure *measure = ensure_measure(staff, &next_measure_id);
            if (!measure) {
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (measure)");
            }
            struct ymusic_element *element = element_create(YMUSIC_ELEM_NOTE, next_element_id++);
            if (!element) {
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (note)");
            }
            element->dur_log = dur_log;
            element->dots = dots;
            element->tie = tie;
            if (element_add_note(element, step, octave, alter) != 0 ||
                measure_add_element(measure, element) != 0) {
                element_destroy(element);
                return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (note add)");
            }
            continue;
        }

        /* Braces, '=', stray punctuation: skip one char. */
        pos++;
    }

    /* Build the flat id -> element index. */
    music->element_count = next_element_id;
    free(music->index);
    music->index = NULL;
    if (next_element_id > 0) {
        music->index = calloc(next_element_id, sizeof(struct ymusic_element *));
        if (!music->index) {
            return YETTY_ERR(yetty_ycore_void, "ymusic: out of memory (index)");
        }
        for (size_t mi = 0; mi < staff->count; mi++) {
            struct ymusic_measure *measure = staff->measures[mi];
            for (size_t ei = 0; ei < measure->count; ei++) {
                struct ymusic_element *element = measure->elements[ei];
                if (element->id < next_element_id) {
                    music->index[element->id] = element;
                }
            }
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Layout — diatonic geometry helpers.
 *===========================================================================*/

struct layout_geom {
    float staff_space;
    float half_space;
    float glyph_em;
    float staff_top_y; /* y of the top staff line */
    float y_middle;    /* y of the middle (3rd) staff line */
};

static int diatonic_index(int step, int octave)
{
    return octave * 7 + step;
}

static int clef_middle_diatonic(enum ymusic_clef clef)
{
    switch (clef) {
    case YMUSIC_CLEF_BASS:
        return diatonic_index(1, 3); /* D3 */
    case YMUSIC_CLEF_ALTO:
        return diatonic_index(0, 4); /* C4 */
    case YMUSIC_CLEF_TENOR:
        return diatonic_index(5, 3); /* A3 */
    case YMUSIC_CLEF_TREBLE:
    default:
        return diatonic_index(6, 4); /* B4 */
    }
}

/* Vertical position of a half-space offset from the middle line (up = +). */
static float y_of_offset(const struct layout_geom *geom, int offset_halfspaces)
{
    return geom->y_middle - (float)offset_halfspaces * geom->half_space;
}

static float y_of_note(const struct layout_geom *geom, enum ymusic_clef clef,
                       const struct ymusic_note *note)
{
    int offset = diatonic_index(note->step, note->octave) - clef_middle_diatonic(clef);
    return y_of_offset(geom, offset);
}

static float duration_quarters(int dur_log, int dots)
{
    float quarters = 4.0f / (float)(1 << dur_log);
    float increment = quarters * 0.5f;
    for (int i = 0; i < dots; i++) {
        quarters += increment;
        increment *= 0.5f;
    }
    return quarters;
}

static int element_has_accidental(const struct ymusic_element *element)
{
    for (size_t i = 0; i < element->note_count; i++) {
        if (element->notes[i].alter != 0) {
            return 1;
        }
    }
    return 0;
}

static float element_advance(const struct yetty_ymusic_music *music,
                             const struct ymusic_element *element)
{
    float quarters = duration_quarters(element->dur_log, element->dots);
    float advance = music->staff_space * (2.4f + 1.6f * sqrtf(quarters));
    if (element_has_accidental(element)) {
        advance += music->staff_space * 1.1f;
    }
    return advance;
}

/*=============================================================================
 * Emit helpers — glyphs (Emmentaler) and SDF lines.
 *===========================================================================*/

static size_t utf8_encode(uint32_t codepoint, char out[4])
{
    if (codepoint < 0x80u) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800u) {
        out[0] = (char)(0xC0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint < 0x10000u) {
        out[0] = (char)(0xE0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
    return 4;
}

static struct yetty_ycore_void_result emit_glyph(struct yetty_ydraw_drawable_list *buf, float x,
                                                 float y, uint32_t codepoint, float em,
                                                 uint32_t color, int32_t font_id, uint32_t *z)
{
    char encoded[4];
    size_t encoded_len = utf8_encode(codepoint, encoded);
    struct yetty_ycore_buffer text_view = {
        .data = (uint8_t *)encoded,
        .capacity = encoded_len,
        .size = encoded_len,
    };
    return yetty_ydraw_drawable_list_add_text(buf, x, y, &text_view, em, color, (*z)++, font_id,
                                              0.0f);
}

static struct yetty_ycore_void_result emit_line(struct yetty_ydraw_drawable_list *buf, float x0,
                                                float y0, float x1, float y1, uint32_t color,
                                                float thickness, uint32_t *z)
{
    struct yetty_ysdf_segment segment = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    return yetty_ydraw_drawable_list_add_cmd_add_segment(buf, /*id=*/0, /*z_order=*/(*z)++,
                                                         /*fill=*/0u, color, thickness, &segment);
}

static uint32_t notehead_glyph(int dur_log)
{
    if (dur_log <= 0) {
        return GLYPH_NOTEHEAD_WHOLE;
    }
    if (dur_log == 1) {
        return GLYPH_NOTEHEAD_HALF;
    }
    return GLYPH_NOTEHEAD_BLACK;
}

static uint32_t rest_glyph(int dur_log)
{
    switch (dur_log) {
    case 0:
        return GLYPH_REST_WHOLE;
    case 1:
        return GLYPH_REST_HALF;
    case 2:
        return GLYPH_REST_QUARTER;
    case 3:
        return GLYPH_REST_EIGHTH;
    case 4:
        return GLYPH_REST_16TH;
    default:
        return GLYPH_REST_32ND;
    }
}

static uint32_t accidental_glyph(int alter)
{
    switch (alter) {
    case 2:
        return GLYPH_ACC_DBLSHARP;
    case 1:
        return GLYPH_ACC_SHARP;
    case -1:
        return GLYPH_ACC_FLAT;
    case -2:
        return GLYPH_ACC_DBLFLAT;
    default:
        return GLYPH_ACC_NATURAL;
    }
}

/* Ledger lines for a single note offset, drawn around the notehead column. */
static struct yetty_ycore_void_result emit_ledgers(struct yetty_ydraw_drawable_list *buf,
                                                   const struct layout_geom *geom, int offset,
                                                   float notehead_x, float notehead_w,
                                                   float thickness, uint32_t *z)
{
    float pad = geom->staff_space * 0.28f;
    float x0 = notehead_x - pad;
    float x1 = notehead_x + notehead_w + pad;
    if (offset > 4) {
        for (int line_offset = 6; line_offset <= offset; line_offset += 2) {
            float y = y_of_offset(geom, line_offset);
            struct yetty_ycore_void_result line_r =
                emit_line(buf, x0, y, x1, y, YMUSIC_STAFF, thickness, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, line_r, "ymusic: ledger above");
        }
    } else if (offset < -4) {
        for (int line_offset = -6; line_offset >= offset; line_offset -= 2) {
            float y = y_of_offset(geom, line_offset);
            struct yetty_ycore_void_result line_r =
                emit_line(buf, x0, y, x1, y, YMUSIC_STAFF, thickness, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, line_r, "ymusic: ledger below");
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Emit — clef, key signature, time signature, elements.
 *===========================================================================*/

static uint32_t clef_glyph(enum ymusic_clef clef)
{
    switch (clef) {
    case YMUSIC_CLEF_BASS:
        return GLYPH_CLEF_F;
    case YMUSIC_CLEF_ALTO:
    case YMUSIC_CLEF_TENOR:
        return GLYPH_CLEF_C;
    case YMUSIC_CLEF_TREBLE:
    default:
        return GLYPH_CLEF_G;
    }
}

/* The clef glyph's baseline sits on a reference staff line (G clef curls around
 * the 2nd line from the bottom = offset -2; F clef dots straddle offset +2; C
 * clef centres on the middle line). */
static int clef_baseline_offset(enum ymusic_clef clef)
{
    switch (clef) {
    case YMUSIC_CLEF_BASS:
        return 2;
    case YMUSIC_CLEF_ALTO:
    case YMUSIC_CLEF_TENOR:
        return 0;
    case YMUSIC_CLEF_TREBLE:
    default:
        return -2;
    }
}

/* Key-signature accidental positions (half-space offsets from the middle line),
 * in circle-of-fifths order. Alto/tenor reuse the treble pattern for now. */
static void key_offsets(enum ymusic_clef clef, int sharps, const int **offsets_out)
{
    static const int treble_sharps[7] = {4, 1, 5, 2, -1, 3, 0};
    static const int treble_flats[7] = {0, 3, -1, 2, -2, 1, -3};
    static const int bass_sharps[7] = {2, -1, 3, 0, -3, 1, -2};
    static const int bass_flats[7] = {-2, 1, -3, 0, -4, -1, -5};
    if (clef == YMUSIC_CLEF_BASS) {
        *offsets_out = sharps ? bass_sharps : bass_flats;
    } else {
        *offsets_out = sharps ? treble_sharps : treble_flats;
    }
}

static struct yetty_ycore_void_result emit_number(struct yetty_ydraw_drawable_list *buf,
                                                  const struct layout_geom *geom, float center_x,
                                                  float baseline_y, int value, int32_t font_id,
                                                  uint32_t *z)
{
    char digits[12];
    int written = snprintf(digits, sizeof(digits), "%d", value);
    if (written <= 0) {
        return YETTY_OK_VOID();
    }
    float digit_w = geom->staff_space * 1.1f;
    float start_x = center_x - (float)written * digit_w * 0.5f;
    for (int i = 0; i < written; i++) {
        struct yetty_ycore_void_result glyph_r =
            emit_glyph(buf, start_x + (float)i * digit_w, baseline_y, (uint32_t)digits[i],
                       geom->glyph_em, YMUSIC_INK, font_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, glyph_r, "ymusic: time digit");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_element(struct yetty_ydraw_drawable_list *buf,
                                                   const struct yetty_ymusic_music *music,
                                                   const struct layout_geom *geom,
                                                   const struct ymusic_element *element,
                                                   int32_t font_id, uint32_t *z)
{
    enum ymusic_clef clef = music->staff.clef;
    float notehead_w = geom->staff_space * 1.18f;
    float stem_thickness = geom->staff_space * 0.13f;
    float line_thickness = geom->staff_space * 0.12f;
    float notehead_x =
        element->x + (element_has_accidental(element) ? geom->staff_space * 1.1f : 0.0f);

    if (element->type == YMUSIC_ELEM_REST) {
        /* Rests hang from / sit on the middle line by convention. */
        float baseline = (element->dur_log == 0) ? y_of_offset(geom, 2) : geom->y_middle;
        struct yetty_ycore_void_result rest_r =
            emit_glyph(buf, notehead_x, baseline, rest_glyph(element->dur_log), geom->glyph_em,
                       YMUSIC_INK, font_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rest_r, "ymusic: rest");
        return YETTY_OK_VOID();
    }

    /* Find the vertical span of the chord and the mean position (for stem dir). */
    int min_offset = 1000, max_offset = -1000;
    float min_y = 1e9f, max_y = -1e9f;
    for (size_t i = 0; i < element->note_count; i++) {
        const struct ymusic_note *note = &element->notes[i];
        int offset = diatonic_index(note->step, note->octave) - clef_middle_diatonic(clef);
        float y = y_of_note(geom, clef, note);
        if (offset < min_offset) {
            min_offset = offset;
        }
        if (offset > max_offset) {
            max_offset = offset;
        }
        if (y < min_y) {
            min_y = y;
        }
        if (y > max_y) {
            max_y = y;
        }
    }
    /* Mean offset decides stem direction: below the middle line → stem up. */
    int stem_up = ((min_offset + max_offset) < 0);

    /* Ledger lines, then accidentals, then noteheads. */
    for (size_t i = 0; i < element->note_count; i++) {
        const struct ymusic_note *note = &element->notes[i];
        int offset = diatonic_index(note->step, note->octave) - clef_middle_diatonic(clef);
        struct yetty_ycore_void_result ledger_r =
            emit_ledgers(buf, geom, offset, notehead_x, notehead_w, line_thickness, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ledger_r, "ymusic: ledgers");
    }
    for (size_t i = 0; i < element->note_count; i++) {
        const struct ymusic_note *note = &element->notes[i];
        if (note->alter != 0) {
            float y = y_of_note(geom, clef, note);
            struct yetty_ycore_void_result acc_r =
                emit_glyph(buf, element->x, y, accidental_glyph(note->alter), geom->glyph_em,
                           YMUSIC_INK, font_id, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, acc_r, "ymusic: accidental");
        }
    }
    for (size_t i = 0; i < element->note_count; i++) {
        const struct ymusic_note *note = &element->notes[i];
        float y = y_of_note(geom, clef, note);
        struct yetty_ycore_void_result head_r =
            emit_glyph(buf, notehead_x, y, notehead_glyph(element->dur_log), geom->glyph_em,
                       YMUSIC_INK, font_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, head_r, "ymusic: notehead");
    }

    /* Stem + flag (whole notes have neither). */
    if (element->dur_log >= 1) {
        float stem_len = geom->staff_space * 3.5f;
        float stem_x;
        float stem_y0, stem_y1;
        if (stem_up) {
            stem_x = notehead_x + notehead_w - stem_thickness * 0.5f;
            stem_y0 = max_y;            /* lowest notehead */
            stem_y1 = min_y - stem_len; /* up past the highest */
        } else {
            stem_x = notehead_x + stem_thickness * 0.5f;
            stem_y0 = min_y;            /* highest notehead */
            stem_y1 = max_y + stem_len; /* down past the lowest */
        }
        struct yetty_ycore_void_result stem_r =
            emit_line(buf, stem_x, stem_y0, stem_x, stem_y1, YMUSIC_INK, stem_thickness, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stem_r, "ymusic: stem");

        if (element->dur_log >= 3) {
            uint32_t flag = (stem_up ? GLYPH_FLAG_UP_BASE : GLYPH_FLAG_DOWN_BASE) +
                            (uint32_t)(element->dur_log - 3);
            struct yetty_ycore_void_result flag_r =
                emit_glyph(buf, stem_x, stem_y1, flag, geom->glyph_em, YMUSIC_INK, font_id, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flag_r, "ymusic: flag");
        }
    }

    /* Augmentation dots after the noteheads. */
    if (element->dots > 0) {
        for (size_t i = 0; i < element->note_count; i++) {
            const struct ymusic_note *note = &element->notes[i];
            int offset = diatonic_index(note->step, note->octave) - clef_middle_diatonic(clef);
            float dot_y = y_of_note(geom, clef, note);
            if ((offset % 2) == 0) {
                dot_y -= geom->half_space; /* nudge a line-note's dot into the space */
            }
            for (int d = 0; d < element->dots; d++) {
                float dot_x =
                    notehead_x + notehead_w + geom->staff_space * (0.35f + 0.5f * (float)d);
                struct yetty_ycore_void_result dot_r = emit_glyph(
                    buf, dot_x, dot_y, GLYPH_DOT, geom->glyph_em, YMUSIC_INK, font_id, z);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, dot_r, "ymusic: dot");
            }
        }
    }

    /* Highlight overlay for the selected element (editor affordance). */
    if ((int32_t)element->id == music->highlight_id) {
        struct yetty_ysdf_box box = {
            .center_x = element->x + element->adv * 0.5f,
            .center_y = geom->y_middle,
            .half_width = element->adv * 0.5f,
            .half_height = geom->staff_space * 3.0f,
            .corner_radius = geom->staff_space * 0.3f,
        };
        struct yetty_ycore_void_result hl_r = yetty_ydraw_drawable_list_add_cmd_add_box(
            buf, /*id=*/0, /*z_order=*/(*z)++, /*fill=*/0u, YMUSIC_ACCENT,
            /*stroke_width=*/geom->staff_space * 0.16f, &box);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hl_r, "ymusic: highlight");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* configure: set system width, staff-space (line gap) in px and render flags.
 * 0 selects the default for each. Call after create(), before render(). */
YETTY_ANNOTATE("virtual@ymusic:music:configure")
YETTY_ANNOTATE("local@ymusic:configure")
static struct yetty_ycore_void_result
    music_configure(struct yetty_yclass_object *obj, float width, float staff_space, uint32_t flags)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, music_r, "ymusic configure: from_obj");
    struct yetty_ymusic_music *music = music_r.value;
    music->width = width;
    music->staff_space = staff_space;
    music->flags = flags;
    return YETTY_OK_VOID();
}

/* parse: ingest LilyPond-subset text and build the score model. Resets the model
 * and clears any selection. */
YETTY_ANNOTATE("virtual@ymusic:music:parse")
YETTY_ANNOTATE("local@ymusic:parse")
static struct yetty_ycore_void_result
    music_parse(struct yetty_yclass_object *obj, const char *input, size_t len)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, music_r, "ymusic parse: from_obj");
    struct yetty_ymusic_music *music = music_r.value;
    if (!input && len > 0) {
        return YETTY_ERR(yetty_ycore_void, "ymusic parse: NULL input");
    }

    staff_clear(&music->staff);
    free(music->index);
    music->index = NULL;
    music->element_count = 0;
    music->highlight_id = -1;
    music->staff.clef = YMUSIC_CLEF_TREBLE;
    music->staff.key_fifths = 0;
    music->staff.time_num = 0;
    music->staff.time_den = 0;

    struct yetty_ycore_void_result parsed = parse_lilypond(music, input, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parsed, "ymusic parse: failed");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * System layout — wrap measures into stacked systems (staff lines) so a long
 * score flows down the page and scrolls vertically, like printed sheet music.
 *===========================================================================*/

static float system_band_height(float staff_space)
{
    return staff_space * 12.0f;
}

static float layout_top_margin(float staff_space)
{
    return staff_space * 2.0f;
}

/* Per-system geometry: the staff sits inside its band with headroom above (for
 * high notes / ledger lines) and below. */
static struct layout_geom system_geom(float staff_space, int system)
{
    float staff_top = layout_top_margin(staff_space) +
                      (float)system * system_band_height(staff_space) + staff_space * 4.0f;
    struct layout_geom geom = {
        .staff_space = staff_space,
        .half_space = staff_space * 0.5f,
        .glyph_em = staff_space * 4.0f, /* SMuFL: em = 4 staff spaces */
        .staff_top_y = staff_top,
        .y_middle = staff_top + staff_space * 2.0f,
    };
    return geom;
}

static int key_count_of(const struct yetty_ymusic_music *music)
{
    return music->staff.key_fifths < 0 ? -music->staff.key_fifths : music->staff.key_fifths;
}

/* Width of a system's leading clef + key signature (+ time signature on the
 * first system only). Mirrors what emit_system_prefix lays down. */
static float prefix_width(const struct yetty_ymusic_music *music, float staff_space, int show_time)
{
    float width = staff_space * 3.0f; /* clef */
    int key_count = key_count_of(music);
    if (key_count > 0) {
        width += (float)key_count * staff_space * 0.95f + staff_space * 0.6f;
    }
    if (show_time && music->staff.time_num > 0 && music->staff.time_den > 0) {
        width += staff_space * 3.0f;
    }
    return width;
}

static float measure_width(const struct yetty_ymusic_music *music,
                           const struct ymusic_measure *measure, float bar_gutter)
{
    float width = 0.0f;
    for (size_t i = 0; i < measure->count; i++) {
        width += element_advance(music, measure->elements[i]);
    }
    return width + bar_gutter;
}

static struct yetty_ycore_void_result emit_staff_lines(struct yetty_ydraw_drawable_list *buf,
                                                       const struct layout_geom *geom, float x0,
                                                       float x1, uint32_t *z)
{
    float thickness = geom->staff_space * 0.12f;
    for (int line = 0; line < 5; line++) {
        float y = geom->staff_top_y + (float)line * geom->staff_space;
        struct yetty_ycore_void_result line_r =
            emit_line(buf, x0, y, x1, y, YMUSIC_STAFF, thickness, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_r, "ymusic: staff line");
    }
    return YETTY_OK_VOID();
}

/* Clef + key signature (+ time signature when show_time) at a system's left
 * edge. The staff lines already span from left_x. */
static struct yetty_ycore_void_result emit_system_prefix(struct yetty_ydraw_drawable_list *buf,
                                                         const struct yetty_ymusic_music *music,
                                                         const struct layout_geom *geom,
                                                         float left_x, int show_time,
                                                         int32_t font_id, uint32_t *z)
{
    float staff_space = geom->staff_space;
    float x = left_x;

    float clef_y = y_of_offset(geom, clef_baseline_offset(music->staff.clef));
    struct yetty_ycore_void_result clef_r =
        emit_glyph(buf, x + staff_space * 0.4f, clef_y, clef_glyph(music->staff.clef),
                   geom->glyph_em, YMUSIC_INK, font_id, z);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clef_r, "ymusic: clef");
    x += staff_space * 3.0f;

    int key_count = key_count_of(music);
    if (key_count > 0) {
        int sharps = music->staff.key_fifths > 0;
        const int *offsets = NULL;
        key_offsets(music->staff.clef, sharps, &offsets);
        uint32_t glyph = sharps ? GLYPH_ACC_SHARP : GLYPH_ACC_FLAT;
        float key_step = staff_space * 0.95f;
        for (int i = 0; i < key_count; i++) {
            struct yetty_ycore_void_result acc_r =
                emit_glyph(buf, x + (float)i * key_step, y_of_offset(geom, offsets[i]), glyph,
                           geom->glyph_em, YMUSIC_INK, font_id, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, acc_r, "ymusic: key sig");
        }
        x += (float)key_count * key_step + staff_space * 0.6f;
    }

    if (show_time && music->staff.time_num > 0 && music->staff.time_den > 0) {
        float time_x = x + staff_space * 1.2f;
        struct yetty_ycore_void_result num_r =
            emit_number(buf, geom, time_x, y_of_offset(geom, 2), music->staff.time_num, font_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, num_r, "ymusic: time num");
        struct yetty_ycore_void_result den_r = emit_number(buf, geom, time_x, y_of_offset(geom, -2),
                                                           music->staff.time_den, font_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, den_r, "ymusic: time den");
    }
    return YETTY_OK_VOID();
}

/* render: lay out the score and emit it as a fresh ydraw drawable list (caller
 * owns it). Pointer return -> local-only. */
YETTY_ANNOTATE("virtual@ymusic:music:render")
YETTY_ANNOTATE("local@ymusic:render")
static struct yetty_ydraw_drawable_list_result music_render(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, music_r, "ymusic render: from_obj");
    struct yetty_ymusic_music *music = music_r.value;

    float staff_space =
        music->staff_space > 0.0f ? music->staff_space : (float)YMUSIC_DEFAULT_STAFF_SPACE;
    float width = music->width > 0.0f ? music->width : (float)YMUSIC_DEFAULT_WIDTH;

    float left_margin = staff_space * 1.5f;
    float right_margin = staff_space * 1.5f;
    float bar_gutter = staff_space * 1.2f;
    float right_edge = width - right_margin;

    /* Pass 1 — wrap measures into systems; assign each element its system + x.
     * Measures are kept whole (never split across a line break). */
    int system = 0;
    float system_start = left_margin + prefix_width(music, staff_space, /*show_time=*/1);
    float cursor = system_start;
    for (size_t mi = 0; mi < music->staff.count; mi++) {
        struct ymusic_measure *measure = music->staff.measures[mi];
        if (measure->count == 0) {
            continue;
        }
        if (cursor > system_start &&
            cursor + measure_width(music, measure, bar_gutter) > right_edge) {
            system++;
            system_start = left_margin + prefix_width(music, staff_space, /*show_time=*/0);
            cursor = system_start;
        }
        for (size_t ei = 0; ei < measure->count; ei++) {
            struct ymusic_element *element = measure->elements[ei];
            element->system = system;
            element->x = cursor;
            element->adv = element_advance(music, element);
            cursor += element->adv;
        }
        cursor += bar_gutter;
    }
    int num_systems = system + 1;

    float content_w = width;
    float content_h = layout_top_margin(staff_space) +
                      (float)num_systems * system_band_height(staff_space) + staff_space * 2.0f;
    music->content_w = content_w;
    music->content_h = content_h;

    struct yetty_ydraw_drawable_list_config buffer_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = content_w,
        .scene_max_y = content_h,
    };
    struct yetty_ydraw_drawable_list_result list_r =
        yetty_ydraw_drawable_list_config_buffer_create(&buffer_config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_r, "ymusic render: list create");
    struct yetty_ydraw_drawable_list *buf = list_r.value;
    uint32_t z = 0;

    /* Reference the Emmentaler music font by name — it ships with the install
     * as a pre-generated MSDF atlas (msdf-fonts/Emmentaler.cdb), so a score
     * never carries the font's ~200 KB of bytes. Text spans reference it by
     * the returned id. */
    struct yetty_ycore_int_result font_id_r =
        yetty_ydraw_drawable_list_add_font_named(buf, "Emmentaler");
    if (YETTY_IS_ERR(font_id_r)) {
        yetty_ydraw_drawable_list_destroy(buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymusic render: add_font_named", font_id_r);
    }
    int32_t font_id = (int32_t)font_id_r.value;

#define YMUSIC_FAIL(msg, res)                                                                      \
    do {                                                                                           \
        yetty_ydraw_drawable_list_destroy(buf);                                                    \
        return YETTY_ERR(yetty_ydraw_drawable_list, msg, res);                                     \
    } while (0)

    /* Each system: five staff lines spanning the width, then the leading clef /
     * key signature (and the time signature on the first system). */
    for (int sys = 0; sys < num_systems; sys++) {
        struct layout_geom geom = system_geom(staff_space, sys);
        struct yetty_ycore_void_result lines_r =
            emit_staff_lines(buf, &geom, left_margin, right_edge, &z);
        if (YETTY_IS_ERR(lines_r)) {
            YMUSIC_FAIL("ymusic render: staff", lines_r);
        }
        struct yetty_ycore_void_result prefix_r =
            emit_system_prefix(buf, music, &geom, left_margin, /*show_time=*/sys == 0, font_id, &z);
        if (YETTY_IS_ERR(prefix_r)) {
            YMUSIC_FAIL("ymusic render: prefix", prefix_r);
        }
    }

    /* Barline at each measure's right edge, on that measure's system. */
    for (size_t mi = 0; mi < music->staff.count; mi++) {
        struct ymusic_measure *measure = music->staff.measures[mi];
        if (measure->count == 0) {
            continue;
        }
        struct ymusic_element *last = measure->elements[measure->count - 1];
        struct layout_geom geom = system_geom(staff_space, last->system);
        float bar_x = last->x + last->adv + staff_space * 0.4f;
        struct yetty_ycore_void_result bar_r =
            emit_line(buf, bar_x, geom.staff_top_y, bar_x, geom.staff_top_y + 4.0f * staff_space,
                      YMUSIC_STAFF, staff_space * 0.13f, &z);
        if (YETTY_IS_ERR(bar_r)) {
            YMUSIC_FAIL("ymusic render: barline", bar_r);
        }
    }

    /* Elements, each drawn on its own system's staff. */
    for (size_t mi = 0; mi < music->staff.count; mi++) {
        struct ymusic_measure *measure = music->staff.measures[mi];
        for (size_t ei = 0; ei < measure->count; ei++) {
            struct ymusic_element *element = measure->elements[ei];
            struct layout_geom geom = system_geom(staff_space, element->system);
            struct yetty_ycore_void_result element_r =
                emit_element(buf, music, &geom, element, font_id, &z);
            if (YETTY_IS_ERR(element_r)) {
                YMUSIC_FAIL("ymusic render: element", element_r);
            }
        }
    }

#undef YMUSIC_FAIL
    ydebug("ymusic render: measures=%zu elements=%u systems=%d content=%.0fx%.0f",
           music->staff.count, music->element_count, num_systems, (double)content_w,
           (double)content_h);
    return YETTY_OK(yetty_ydraw_drawable_list, buf);
}

/* hit_test: id of the element whose column + system band contains content
 * (x,y), or YETTY_YMUSIC_NO_ELEMENT. Requires a prior render() for the layout. */
YETTY_ANNOTATE("virtual@ymusic:music:hit_test")
YETTY_ANNOTATE("local@ymusic:hit_test")
static struct yetty_ycore_int_result
    music_hit_test(struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, music_r, "ymusic hit_test: from_obj");
    struct yetty_ymusic_music *music = music_r.value;
    float staff_space =
        music->staff_space > 0.0f ? music->staff_space : (float)YMUSIC_DEFAULT_STAFF_SPACE;
    for (size_t mi = 0; mi < music->staff.count; mi++) {
        struct ymusic_measure *measure = music->staff.measures[mi];
        for (size_t ei = 0; ei < measure->count; ei++) {
            struct ymusic_element *element = measure->elements[ei];
            struct layout_geom geom = system_geom(staff_space, element->system);
            float band_top = geom.staff_top_y - staff_space * 3.0f;
            float band_bottom = geom.staff_top_y + 4.0f * staff_space + staff_space * 3.0f;
            if (x >= element->x && x < element->x + element->adv && y >= band_top &&
                y <= band_bottom) {
                return YETTY_OK(yetty_ycore_int, (int)element->id);
            }
        }
    }
    return YETTY_OK(yetty_ycore_int, YETTY_YMUSIC_NO_ELEMENT);
}

/* set_highlight: mark an element as selected (-1 clears) for the next render. */
YETTY_ANNOTATE("virtual@ymusic:music:set_highlight")
YETTY_ANNOTATE("local@ymusic:set_highlight")
static struct yetty_ycore_void_result
    music_set_highlight(struct yetty_yclass_object *obj, int32_t element_id)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, music_r, "ymusic set_highlight: from_obj");
    struct yetty_ymusic_music *music = music_r.value;
    music->highlight_id =
        (element_id >= 0 && (uint32_t)element_id < music->element_count) ? element_id : -1;
    return YETTY_OK_VOID();
}

/* destroy: free the score model and the object. */
YETTY_ANNOTATE("virtual@ymusic:music:destroy")
YETTY_ANNOTATE("local@ymusic:destroy")
static struct yetty_ycore_void_result music_obj_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result music_r = music_from_obj(obj);
    if (YETTY_IS_ERR(music_r)) {
        yetty_ycore_error_destroy(music_r.error);
    } else {
        struct yetty_ymusic_music *music = music_r.value;
        staff_clear(&music->staff);
        free(music->index);
        music->index = NULL;
        music->element_count = 0;
    }
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * One-shot helper — serialize a rendered drawable list as a YDRAW_BIN OSC
 * envelope (the ycat / scrolling-layer path). Exposed for a CLI front-end.
 *===========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymusic_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd)
{
    if (!list) {
        return YETTY_ERR(yetty_ycore_void, "ymusic emit_osc: NULL list");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ymusic emit_osc: empty serialize");
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
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "ymusic emit_osc: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

#include "music.gen.c"
