/*
 * sequence.c — Mermaid sequenceDiagram → ydraw buffer.
 *
 * Sequence diagrams are temporal, not a layered graph, so they own their
 * layout: participants are columns across the top with vertical lifelines;
 * messages are time-ordered horizontal arrows flowing top to bottom.
 *
 * Supported:
 *   participant ID [as Label]   /   actor ID [as Label]
 *   A->>B: text     solid arrow       A-->>B: text   dashed arrow (return)
 *   A->B:  text     solid, open       A-->B:  text   dashed, open
 *   A->>A: text     self-message loop
 *   Note right of A: text   /   Note left of A: text   /   Note over A,B: text
 *   loop/alt/opt/par/else/end and activate/deactivate are accepted and their
 *   inner messages still render (frames are a future addition).
 */

#include <yetty/ydiagram/diagrams.h>

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/types.h>
#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/*=============================================================================
 * Model
 *===========================================================================*/

struct participant {
    char *id;
    char *label;
    float x; /* column centre */
};

enum ev_kind { EV_MESSAGE, EV_NOTE };

struct event {
    enum ev_kind kind;
    int src; /* participant index */
    int tgt; /* participant index (== src for notes anchored to one) */
    char *text;
    bool dashed;
    bool arrow; /* draw an arrowhead at the target */
    float y;    /* assigned in the measure pass */
};

struct seq {
    struct participant *parts;
    size_t part_count, part_cap;
    struct event *events;
    size_t event_count, event_cap;
};

/* Layout constants (pixels). */
enum {
    SEQ_MARGIN = 24,
    SEQ_HEADER_H = 38,
    SEQ_MSG_GAP = 40,
    SEQ_SELF_GAP = 52,
    SEQ_NOTE_H = 36,
};
#define SEQ_PART_FS 14.0f
#define SEQ_MSG_FS 12.5f

/*=============================================================================
 * Helpers
 *===========================================================================*/

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static float measure(yetty_ydiagram_measure_text_fn fn, void *ud, const char *t, float fs)
{
    if (!t || !t[0]) {
        return 0.0f;
    }
    if (fn) {
        return fn(t, strlen(t), fs, ud);
    }
    return fs * 0.6f * (float)strlen(t);
}

static int find_or_add_participant(struct seq *s, const char *id, const char *label)
{
    for (size_t i = 0; i < s->part_count; i++) {
        if (strcmp(s->parts[i].id, id) == 0) {
            if (label && label[0] && strcmp(s->parts[i].label, id) == 0) {
                char *nl = strdup(label);
                if (nl) {
                    free(s->parts[i].label);
                    s->parts[i].label = nl;
                }
            }
            return (int)i;
        }
    }
    if (s->part_count == s->part_cap) {
        size_t nc = s->part_cap ? s->part_cap * 2 : 8;
        struct participant *np = realloc(s->parts, nc * sizeof(*np));
        if (!np) {
            return -1;
        }
        s->parts = np;
        s->part_cap = nc;
    }
    struct participant *p = &s->parts[s->part_count];
    p->id = strdup(id);
    p->label = strdup(label && label[0] ? label : id);
    p->x = 0.0f;
    if (!p->id || !p->label) {
        free(p->id);
        free(p->label);
        return -1;
    }
    return (int)s->part_count++;
}

static struct event *push_event(struct seq *s)
{
    if (s->event_count == s->event_cap) {
        size_t nc = s->event_cap ? s->event_cap * 2 : 16;
        struct event *ne = realloc(s->events, nc * sizeof(*ne));
        if (!ne) {
            return NULL;
        }
        s->events = ne;
        s->event_cap = nc;
    }
    struct event *e = &s->events[s->event_count++];
    memset(e, 0, sizeof(*e));
    return e;
}

static void seq_destroy(struct seq *s)
{
    for (size_t i = 0; i < s->part_count; i++) {
        free(s->parts[i].id);
        free(s->parts[i].label);
    }
    free(s->parts);
    for (size_t i = 0; i < s->event_count; i++) {
        free(s->events[i].text);
    }
    free(s->events);
}

/* Known message arrow tokens, longest first so the leftmost-longest wins. */
static const char *seq_arrow_token(const char *s, size_t *out_len, bool *dashed, bool *arrow)
{
    static const char *const toks[] = {"-->>", "--x", "--)", "-->", "->>", "-x", "-)", "->"};
    const char *best = NULL;
    size_t best_pos = (size_t)-1;
    size_t best_len = 0;
    for (size_t i = 0; i < sizeof(toks) / sizeof(toks[0]); i++) {
        const char *hit = strstr(s, toks[i]);
        if (!hit) {
            continue;
        }
        size_t pos = (size_t)(hit - s);
        size_t l = strlen(toks[i]);
        if (pos < best_pos || (pos == best_pos && l > best_len)) {
            best = hit;
            best_pos = pos;
            best_len = l;
        }
    }
    if (!best) {
        return NULL;
    }
    *out_len = best_len;
    *dashed = (best_len >= 2 && best[0] == '-' && best[1] == '-');
    *arrow = (strchr(best, '>') != NULL) || best[best_len - 1] == 'x' || best[best_len - 1] == ')';
    return best;
}

/*=============================================================================
 * Parse
 *===========================================================================*/

static struct yetty_ycore_void_result seq_parse(struct seq *s, const char *input, size_t len)
{
    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "sequence: oom");
    }
    memcpy(buf, input, len);
    buf[len] = '\0';

    char *save = NULL;
    for (char *raw = strtok_r(buf, "\n", &save); raw; raw = strtok_r(NULL, "\n", &save)) {
        char *line = trim(raw);
        if (!line[0] || (line[0] == '%' && line[1] == '%')) {
            continue;
        }
        if (strncmp(line, "sequenceDiagram", 15) == 0) {
            continue;
        }
        if (strncmp(line, "participant ", 12) == 0 || strncmp(line, "actor ", 6) == 0) {
            char *rest = trim(strchr(line, ' '));
            char *as = strstr(rest, " as ");
            char *id = rest;
            char *label = NULL;
            if (as) {
                *as = '\0';
                id = trim(rest);
                label = trim(as + 4);
            }
            if (find_or_add_participant(s, trim(id), label) < 0) {
                free(buf);
                return YETTY_ERR(yetty_ycore_void, "sequence: participant alloc failed");
            }
            continue;
        }
        if (strncmp(line, "Note ", 5) == 0 || strncmp(line, "note ", 5) == 0) {
            /* Note right of A: text | Note left of A: text | Note over A,B: text */
            char *colon = strchr(line, ':');
            char *text = colon ? trim(colon + 1) : (char *)"";
            char *anchor = NULL;
            char *over = strstr(line, " of ");
            if (over) {
                anchor = over + 4;
            } else if ((over = strstr(line, "over ")) != NULL) {
                anchor = over + 5;
            }
            if (colon) {
                *colon = '\0';
            }
            if (anchor) {
                char *comma = strchr(anchor, ',');
                if (comma) {
                    *comma = '\0';
                }
                int pi = find_or_add_participant(s, trim(anchor), NULL);
                if (pi < 0) {
                    free(buf);
                    return YETTY_ERR(yetty_ycore_void, "sequence: note anchor alloc failed");
                }
                struct event *e = push_event(s);
                if (!e) {
                    free(buf);
                    return YETTY_ERR(yetty_ycore_void, "sequence: event alloc failed");
                }
                e->kind = EV_NOTE;
                e->src = e->tgt = pi;
                e->text = strdup(text);
            }
            continue;
        }
        /* Block keywords: render inner content, frames are future work. */
        if (strncmp(line, "loop", 4) == 0 || strncmp(line, "alt", 3) == 0 ||
            strncmp(line, "opt", 3) == 0 || strncmp(line, "par", 3) == 0 ||
            strncmp(line, "else", 4) == 0 || strncmp(line, "and", 3) == 0 ||
            strncmp(line, "end", 3) == 0 || strncmp(line, "rect", 4) == 0 ||
            strncmp(line, "critical", 8) == 0 || strncmp(line, "break", 5) == 0 ||
            strncmp(line, "activate", 8) == 0 || strncmp(line, "deactivate", 10) == 0 ||
            strncmp(line, "autonumber", 10) == 0) {
            continue;
        }

        /* Message line: SRC<arrow>TGT: text */
        size_t alen = 0;
        bool dashed = false, arrow = false;
        char *colon = strchr(line, ':');
        char *text = NULL;
        if (colon) {
            *colon = '\0';
            text = trim(colon + 1);
        }
        const char *atok = seq_arrow_token(line, &alen, &dashed, &arrow);
        if (!atok) {
            continue;
        }
        char *arrow_at = (char *)atok;
        *arrow_at = '\0';
        char *src = trim(line);
        char *tgt = trim(arrow_at + alen);
        if (!src[0] || !tgt[0]) {
            continue;
        }
        int si = find_or_add_participant(s, src, NULL);
        int ti = find_or_add_participant(s, tgt, NULL);
        if (si < 0 || ti < 0) {
            free(buf);
            return YETTY_ERR(yetty_ycore_void, "sequence: message participant alloc failed");
        }
        struct event *e = push_event(s);
        if (!e) {
            free(buf);
            return YETTY_ERR(yetty_ycore_void, "sequence: event alloc failed");
        }
        e->kind = EV_MESSAGE;
        e->src = si;
        e->tgt = ti;
        e->dashed = dashed;
        e->arrow = arrow;
        e->text = strdup(text ? text : "");
    }

    free(buf);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Render
 *===========================================================================*/

struct seq_render {
    struct yetty_ydraw_drawable_list *buf;
    uint32_t z;
};

static void seq_text(struct seq_render *r, float x, float y, const char *t, float fs, uint32_t color)
{
    if (!t || !t[0]) {
        return;
    }
    size_t n = strlen(t);
    struct yetty_ycore_buffer view = {.data = (uint8_t *)(uintptr_t)t, .capacity = n, .size = n};
    (void)yetty_ydraw_drawable_list_add_text(r->buf, x, y, &view, fs, color, r->z++, -1, 0.0f);
}

static void seq_seg(struct seq_render *r, float x0, float y0, float x1, float y1, uint32_t color,
                    float w)
{
    struct yetty_ysdf_segment g = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    yetty_ydraw_drawable_list_add_cmd_add_segment(r->buf, 0, r->z++, 0, color, w, &g);
}

static void seq_dashed(struct seq_render *r, float x0, float y0, float x1, float y1, uint32_t color,
                       float w)
{
    float dx = x1 - x0, dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) {
        return;
    }
    float ux = dx / length, uy = dy / length;
    float dash = 7.0f, gap = 5.0f, step = dash + gap;
    uint32_t z = r->z;
    for (float pos = 0.0f; pos < length; pos += step) {
        float end = pos + dash;
        if (end > length) {
            end = length;
        }
        struct yetty_ysdf_segment g = {.start_x = x0 + ux * pos,
                                       .start_y = y0 + uy * pos,
                                       .end_x = x0 + ux * end,
                                       .end_y = y0 + uy * end};
        yetty_ydraw_drawable_list_add_cmd_add_segment(r->buf, 0, z, 0, color, w, &g);
    }
    r->z = z + 1;
}

static void seq_arrowhead(struct seq_render *r, float x, float y, float dir, uint32_t color)
{
    float size = 9.0f;
    float ax = dir > 0 ? x - size : x + size;
    struct yetty_ysdf_triangle g = {
        .vertex_a_x = x,        .vertex_a_y = y,
        .vertex_b_x = ax,       .vertex_b_y = y - size * 0.45f,
        .vertex_c_x = ax,       .vertex_c_y = y + size * 0.45f,
    };
    yetty_ydraw_drawable_list_add_cmd_add_triangle(r->buf, 0, r->z++, color, 0, 0.0f, &g);
}

static void seq_box(struct seq_render *r, float cx, float cy, float w, float h, uint32_t fill,
                    uint32_t stroke, float sw, float radius)
{
    struct yetty_ysdf_box g = {.center_x = cx,
                               .center_y = cy,
                               .half_width = w * 0.5f,
                               .half_height = h * 0.5f,
                               .corner_radius = radius};
    yetty_ydraw_drawable_list_add_cmd_add_box(r->buf, 0, r->z++, fill, stroke, sw, &g);
}

struct yetty_ydiagram_seq_buffer_result yetty_ydiagram_sequence_render(
    const char *input, size_t len, yetty_ydiagram_measure_text_fn measure_fn, void *measure_ud,
    bool clear_canvas)
{
    if (!input) {
        return YETTY_ERR(yetty_ydiagram_seq_buffer, "sequence_render: NULL input");
    }

    struct seq s = {0};
    struct yetty_ycore_void_result pr = seq_parse(&s, input, len);
    if (YETTY_IS_ERR(pr)) {
        seq_destroy(&s);
        return YETTY_ERR(yetty_ydiagram_seq_buffer, "sequence_render: parse failed", pr);
    }
    if (s.part_count == 0) {
        seq_destroy(&s);
        return YETTY_ERR(yetty_ydiagram_seq_buffer, "sequence_render: no participants");
    }

    struct yetty_ydiagram_node_style ns = yetty_ydiagram_default_node_style();
    uint32_t header_fill = ns.fill_color;
    uint32_t stroke = ns.stroke_color;
    uint32_t text_color = ns.text_color;
    uint32_t lifeline_color = 0xFF888888u;
    uint32_t note_fill = 0xFF1A1A2Eu;

    /* Column geometry: uniform step sized to the widest participant label. */
    float max_label_w = 0.0f;
    for (size_t i = 0; i < s.part_count; i++) {
        float w = measure(measure_fn, measure_ud, s.parts[i].label, SEQ_PART_FS);
        if (w > max_label_w) {
            max_label_w = w;
        }
    }
    float step = max_label_w + 70.0f;
    if (step < 150.0f) {
        step = 150.0f;
    }
    float col_half = step * 0.5f;
    for (size_t i = 0; i < s.part_count; i++) {
        s.parts[i].x = (float)SEQ_MARGIN + col_half + (float)i * step;
    }
    float total_w = (float)SEQ_MARGIN * 2.0f + (float)s.part_count * step;

    /* Measure pass: assign a y to each event, find the total height. */
    float life_top = (float)SEQ_MARGIN + (float)SEQ_HEADER_H;
    float y = life_top + 28.0f;
    for (size_t i = 0; i < s.event_count; i++) {
        struct event *e = &s.events[i];
        e->y = y;
        if (e->kind == EV_NOTE) {
            y += (float)SEQ_NOTE_H + 10.0f;
        } else if (e->src == e->tgt) {
            y += (float)SEQ_SELF_GAP;
        } else {
            y += (float)SEQ_MSG_GAP;
        }
    }
    float life_bottom = y + 6.0f;
    float total_h = life_bottom + (float)SEQ_HEADER_H + (float)SEQ_MARGIN;

    struct yetty_ydraw_drawable_list_config cfg = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = total_w, .scene_max_y = total_h};
    struct yetty_ydraw_drawable_list_result br = yetty_ydraw_drawable_list_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(br)) {
        seq_destroy(&s);
        return YETTY_ERR(yetty_ydiagram_seq_buffer, "sequence_render: buffer create failed", br);
    }

    struct seq_render r = {.buf = br.value, .z = 0};
    if (clear_canvas) {
        (void)yetty_ydraw_drawable_list_add_cmd_zero(br.value);
    }
    yetty_ydraw_drawable_list_set_scene_bounds(br.value, 0.0f, 0.0f, total_w, total_h);

    /* Lifelines (under everything). */
    for (size_t i = 0; i < s.part_count; i++) {
        float x = s.parts[i].x;
        seq_dashed(&r, x, life_top, x, life_bottom, lifeline_color, 1.2f);
    }

    /* Participant header boxes (top and bottom). */
    for (size_t i = 0; i < s.part_count; i++) {
        struct participant *p = &s.parts[i];
        float lw = measure(measure_fn, measure_ud, p->label, SEQ_PART_FS);
        float bw = lw + 24.0f;
        if (bw < 70.0f) {
            bw = 70.0f;
        }
        float top_cy = (float)SEQ_MARGIN + (float)SEQ_HEADER_H * 0.5f;
        float bot_cy = life_bottom + (float)SEQ_HEADER_H * 0.5f;
        seq_box(&r, p->x, top_cy, bw, (float)SEQ_HEADER_H, header_fill, stroke, 2.0f, 4.0f);
        seq_box(&r, p->x, bot_cy, bw, (float)SEQ_HEADER_H, header_fill, stroke, 2.0f, 4.0f);
        seq_text(&r, p->x - lw * 0.5f, top_cy + SEQ_PART_FS / 3.0f, p->label, SEQ_PART_FS,
                 text_color);
        seq_text(&r, p->x - lw * 0.5f, bot_cy + SEQ_PART_FS / 3.0f, p->label, SEQ_PART_FS,
                 text_color);
    }

    /* Events. */
    for (size_t i = 0; i < s.event_count; i++) {
        struct event *e = &s.events[i];
        if (e->kind == EV_NOTE) {
            float x = s.parts[e->src].x;
            float tw = measure(measure_fn, measure_ud, e->text, SEQ_MSG_FS);
            float bw = tw + 20.0f;
            float cy = e->y + (float)SEQ_NOTE_H * 0.5f;
            seq_box(&r, x, cy, bw, (float)SEQ_NOTE_H, note_fill, stroke, 1.2f, 2.0f);
            seq_text(&r, x - tw * 0.5f, cy + SEQ_MSG_FS / 3.0f, e->text, SEQ_MSG_FS, text_color);
            continue;
        }

        float xs = s.parts[e->src].x;
        float xt = s.parts[e->tgt].x;
        if (e->src == e->tgt) {
            /* Self-message: a small loop to the right of the lifeline. */
            float loop_w = 36.0f;
            float y0 = e->y, y1 = e->y + 20.0f;
            seq_seg(&r, xs, y0, xs + loop_w, y0, stroke, 1.4f);
            seq_seg(&r, xs + loop_w, y0, xs + loop_w, y1, stroke, 1.4f);
            if (e->dashed) {
                seq_dashed(&r, xs + loop_w, y1, xs + 6.0f, y1, stroke, 1.4f);
            } else {
                seq_seg(&r, xs + loop_w, y1, xs + 6.0f, y1, stroke, 1.4f);
            }
            if (e->arrow) {
                seq_arrowhead(&r, xs + 6.0f, y1, -1.0f, stroke);
            }
            seq_text(&r, xs + loop_w + 8.0f, y0 + SEQ_MSG_FS / 3.0f, e->text, SEQ_MSG_FS,
                     text_color);
            continue;
        }

        float dir = xt > xs ? 1.0f : -1.0f;
        float tip = xt - dir * 1.0f;
        if (e->dashed) {
            seq_dashed(&r, xs, e->y, tip, e->y, stroke, 1.4f);
        } else {
            seq_seg(&r, xs, e->y, tip, e->y, stroke, 1.4f);
        }
        if (e->arrow) {
            seq_arrowhead(&r, tip, e->y, dir, stroke);
        }
        float tw = measure(measure_fn, measure_ud, e->text, SEQ_MSG_FS);
        seq_text(&r, (xs + xt) * 0.5f - tw * 0.5f, e->y - 6.0f, e->text, SEQ_MSG_FS, text_color);
    }

    seq_destroy(&s);
    return YETTY_OK(yetty_ydiagram_seq_buffer, br.value);
}
