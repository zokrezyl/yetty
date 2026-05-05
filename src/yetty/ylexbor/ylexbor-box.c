/*
 * ylexbor-box — DOM tree → flat box vector.
 *
 * MVP scope: walk the lexbor DOM, classify each element as block /
 * inline / skip purely from its tag name, and emit a flat list of
 * boxes with style derived from a tiny built-in user-agent stylesheet
 * (HTML default rendering — same shape every browser ships out of the
 * box). lexbor's full CSS cascade (lxb_style_value) is wired in
 * later — for now we get correct font weights / sizes for the common
 * HTML5 sectioning elements without touching CSS at all, which is
 * already enough to make documentation pages legible.
 */

#include "ylexbor-internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/tag/const.h>


/* ===========================================================================
 * Built-in user-agent style table.
 *
 * Indexed by lxb_tag_id_t so lookup is O(1). For tags we don't list
 * (custom elements, the unknown-tag default) we fall back to inline
 * with default font + black color.
 * ===========================================================================*/

enum yl_disp { YL_DISP_INLINE, YL_DISP_BLOCK, YL_DISP_NONE };

struct yl_default_style {
	enum yl_disp disp;
	float font_size_em;     /* multiplied by parent font size */
	int   font_weight;      /* 0 = inherit, else 100..900 */
	int   font_italic;      /* 0/1; -1 = inherit */
	float margin_top_em;
	float margin_bottom_em;
	uint32_t fg_rgb;        /* 0xRRGGBB; 0xffffffff = inherit */
};

#define INHERIT_RGB 0xffffffffu

static const struct yl_default_style YL_DEFAULT_INLINE = {
	YL_DISP_INLINE, 1.0f, 0, -1, 0.0f, 0.0f, INHERIT_RGB,
};
static const struct yl_default_style YL_DEFAULT_BLOCK = {
	YL_DISP_BLOCK, 1.0f, 0, -1, 1.0f, 1.0f, INHERIT_RGB,
};

/* Match lexbor tag IDs; values pulled from <lexbor/tag/const.h>. */
static const struct yl_default_style *default_for(lxb_tag_id_t tag)
{
	static struct yl_default_style h1 = { YL_DISP_BLOCK, 2.00f, 700, -1, 0.67f, 0.67f, INHERIT_RGB };
	static struct yl_default_style h2 = { YL_DISP_BLOCK, 1.50f, 700, -1, 0.83f, 0.83f, INHERIT_RGB };
	static struct yl_default_style h3 = { YL_DISP_BLOCK, 1.17f, 700, -1, 1.00f, 1.00f, INHERIT_RGB };
	static struct yl_default_style h4 = { YL_DISP_BLOCK, 1.00f, 700, -1, 1.33f, 1.33f, INHERIT_RGB };
	static struct yl_default_style h5 = { YL_DISP_BLOCK, 0.83f, 700, -1, 1.67f, 1.67f, INHERIT_RGB };
	static struct yl_default_style h6 = { YL_DISP_BLOCK, 0.67f, 700, -1, 2.33f, 2.33f, INHERIT_RGB };

	static struct yl_default_style strong = { YL_DISP_INLINE, 1.0f, 700, -1, 0, 0, INHERIT_RGB };
	static struct yl_default_style em     = { YL_DISP_INLINE, 1.0f, 0,    1, 0, 0, INHERIT_RGB };
	static struct yl_default_style anchor = { YL_DISP_INLINE, 1.0f, 0,   -1, 0, 0, 0x0000eeu };
	static struct yl_default_style none   = { YL_DISP_NONE,   1.0f, 0,   -1, 0, 0, INHERIT_RGB };

	switch (tag) {
	case LXB_TAG_H1: return &h1;
	case LXB_TAG_H2: return &h2;
	case LXB_TAG_H3: return &h3;
	case LXB_TAG_H4: return &h4;
	case LXB_TAG_H5: return &h5;
	case LXB_TAG_H6: return &h6;

	case LXB_TAG_STRONG: case LXB_TAG_B:
		return &strong;
	case LXB_TAG_EM: case LXB_TAG_I: case LXB_TAG_CITE:
		return &em;
	case LXB_TAG_A:
		return &anchor;

	case LXB_TAG_HEAD:    case LXB_TAG_TITLE:  case LXB_TAG_META:
	case LXB_TAG_LINK:    case LXB_TAG_SCRIPT: case LXB_TAG_STYLE:
	case LXB_TAG_NOSCRIPT:
		return &none;

	case LXB_TAG_HTML:    case LXB_TAG_BODY:    case LXB_TAG_DIV:
	case LXB_TAG_P:       case LXB_TAG_SECTION: case LXB_TAG_ARTICLE:
	case LXB_TAG_HEADER:  case LXB_TAG_FOOTER:  case LXB_TAG_NAV:
	case LXB_TAG_ASIDE:   case LXB_TAG_MAIN:    case LXB_TAG_FIGURE:
	case LXB_TAG_FIGCAPTION:
	case LXB_TAG_UL:      case LXB_TAG_OL:      case LXB_TAG_LI:
	case LXB_TAG_DL:      case LXB_TAG_DT:      case LXB_TAG_DD:
	case LXB_TAG_BLOCKQUOTE: case LXB_TAG_PRE:  case LXB_TAG_HR:
	case LXB_TAG_TABLE:   case LXB_TAG_THEAD:   case LXB_TAG_TBODY:
	case LXB_TAG_TR:      case LXB_TAG_TD:      case LXB_TAG_TH:
	case LXB_TAG_FORM:
		return &YL_DEFAULT_BLOCK;

	default:
		return &YL_DEFAULT_INLINE;
	}
}

/* ===========================================================================
 * Style stack — propagates inheritable properties down the tree.
 * ===========================================================================*/

struct yl_style_state {
	float font_size;
	int   font_weight;
	bool  font_italic;
	struct yetty_ylexbor_color fg;
};

static struct yetty_ylexbor_color rgb_to_color(uint32_t rgb, uint8_t a)
{
	struct yetty_ylexbor_color c = {
		.r = (uint8_t)(rgb >> 16),
		.g = (uint8_t)(rgb >> 8),
		.b = (uint8_t)(rgb),
		.a = a,
	};
	return c;
}

static struct yl_style_state apply_default(
	const struct yl_style_state *parent,
	const struct yl_default_style *d)
{
	struct yl_style_state s = *parent;
	s.font_size = parent->font_size * d->font_size_em;
	if (d->font_weight) s.font_weight = d->font_weight;
	if (d->font_italic >= 0) s.font_italic = d->font_italic ? true : false;
	if (d->fg_rgb != INHERIT_RGB) s.fg = rgb_to_color(d->fg_rgb, 0xff);
	return s;
}

/* ===========================================================================
 * Inline text accumulator. We collect a block's inline contents as a
 * single concatenated UTF-8 run + a list of "style segments" that the
 * layout pass uses for line-by-line wrapping. For the MVP we collapse
 * all inline children into a single text run with the most recently
 * propagated style — i.e. <p>plain <b>bold</b> plain</p> renders as one
 * line of plain text without distinguishing the bold span. Visible
 * weight differences come back when we add per-run style segments
 * (next iteration).
 * ===========================================================================*/

struct yl_inline_buf {
	char  *buf;
	size_t len, cap;
	int last_was_space;
};

static int inline_buf_append(struct yl_inline_buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 256;
		while (nc < b->len + n + 1) nc *= 2;
		char *p = realloc(b->buf, nc);
		if (p == NULL) return -1;
		b->buf = p; b->cap = nc;
	}
	/* Whitespace collapsing per CSS normal — runs of WS become a single
	 * space; leading WS is dropped. Same default handful of HTML defaults
	 * use. Doesn't honor white-space:pre yet. */
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		int is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
		if (is_ws) {
			if (b->last_was_space || b->len == 0) continue;
			b->buf[b->len++] = ' ';
			b->last_was_space = 1;
		} else {
			b->buf[b->len++] = (char)c;
			b->last_was_space = 0;
		}
	}
	return 0;
}

/* ===========================================================================
 * DOM walker — recursive
 * ===========================================================================*/

static int box_alloc(struct yetty_ylexbor *r, uint32_t *out_idx)
{
	struct yetty_ycore_void_result rr =
		_yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
	if (YETTY_IS_ERR(rr)) return -1;
	struct yetty_ylexbor_box *b = &r->boxes.data[r->boxes.size];
	memset(b, 0, sizeof(*b));
	*out_idx = r->boxes.size++;
	return 0;
}

static void style_to_box(struct yetty_ylexbor_box *b,
			 const struct yl_style_state *s)
{
	b->font_size   = s->font_size;
	b->font_weight = s->font_weight;
	b->font_italic = s->font_italic;
	b->fg          = s->fg;
}

static void walk(struct yetty_ylexbor *r,
		 lxb_dom_node_t *node,
		 const struct yl_style_state *parent_style,
		 uint32_t parent_idx,
		 struct yl_inline_buf *inline_collect);

/* Flush an accumulated inline-text run as one or more YL_BOX_INLINE_TEXT
 * children of `parent_idx`. Layout will wrap it into lines; box-build
 * stores the un-wrapped string. */
static void flush_inline(struct yetty_ylexbor *r,
			 const struct yl_style_state *style,
			 uint32_t parent_idx,
			 struct yl_inline_buf *coll)
{
	if (coll->buf == NULL || coll->len == 0) return;

	/* Trim trailing single space introduced by whitespace collapsing. */
	while (coll->len > 0 && coll->buf[coll->len - 1] == ' ') coll->len--;
	if (coll->len == 0) { coll->last_was_space = 0; return; }

	uint32_t cidx;
	if (box_alloc(r, &cidx) != 0) return;
	struct yetty_ylexbor_box *b = &r->boxes.data[cidx];
	b->kind = YL_BOX_INLINE_TEXT;
	style_to_box(b, style);
	b->text = yetty_ylexbor_arena_dup(r, coll->buf, coll->len);
	b->text_len = coll->len;

	/* Splice into parent's children range. The box vector stores
	 * children contiguously: parent's first_child..first_child+count-1
	 * are this child if first_child wasn't set yet. We rely on the
	 * walker visiting children in order with no other allocations
	 * mixed in — true here since we flush at the end of a child group
	 * before recursing into the next block. */
	struct yetty_ylexbor_box *p = &r->boxes.data[parent_idx];
	if (p->child_count == 0) p->first_child = cidx;
	p->child_count++;

	coll->len = 0;
	coll->last_was_space = 0;
}

static void walk(struct yetty_ylexbor *r,
		 lxb_dom_node_t *node,
		 const struct yl_style_state *parent_style,
		 uint32_t parent_idx,
		 struct yl_inline_buf *inline_collect)
{
	for (lxb_dom_node_t *child = node->first_child; child != NULL;
	     child = child->next) {
		if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
			lxb_dom_text_t *t = lxb_dom_interface_text(child);
			size_t len = t->char_data.data.length;
			const char *bytes = (const char *)t->char_data.data.data;
			if (inline_collect != NULL && len > 0) {
				inline_buf_append(inline_collect, bytes, len);
			}
			continue;
		}
		if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;

		lxb_dom_element_t *el = lxb_dom_interface_element(child);
		const struct yl_default_style *d = default_for(child->local_name);
		if (d->disp == YL_DISP_NONE) continue;

		struct yl_style_state s = apply_default(parent_style, d);

		if (d->disp == YL_DISP_BLOCK) {
			/* Flush any inline text accumulated for the parent
			 * block before opening a new child block. */
			flush_inline(r, parent_style, parent_idx, inline_collect);

			uint32_t bidx;
			if (box_alloc(r, &bidx) != 0) return;
			struct yetty_ylexbor_box *b = &r->boxes.data[bidx];
			b->kind = YL_BOX_BLOCK;
			b->element = el;
			style_to_box(b, &s);
			b->margin_top    = d->margin_top_em    * s.font_size;
			b->margin_bottom = d->margin_bottom_em * s.font_size;

			/* Splice into parent. */
			struct yetty_ylexbor_box *p = &r->boxes.data[parent_idx];
			if (p->child_count == 0) p->first_child = bidx;
			p->child_count++;

			/* Recurse with a fresh inline accumulator for this
			 * block's children. */
			struct yl_inline_buf ib = {0};
			walk(r, child, &s, bidx, &ib);
			flush_inline(r, &s, bidx, &ib);
			free(ib.buf);
		} else {
			/* Inline element: recurse, accumulating into the
			 * parent block's inline buffer. We pass parent_style
			 * for inheritance, not s — because only the *style*
			 * of the inline run as a whole is captured by the
			 * surrounding text, not per-segment styling (MVP). */
			walk(r, child, &s, parent_idx, inline_collect);
		}
	}
}

/* ===========================================================================
 * Entry point.
 * ===========================================================================*/

struct yetty_ycore_void_result yetty_ylexbor_box_build(struct yetty_ylexbor *r)
{
	if (r == NULL || r->document == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_box_build: null");

	r->boxes.size = 0;

	/* Root box wraps the whole viewport. */
	uint32_t root_idx;
	if (box_alloc(r, &root_idx) != 0)
		return YETTY_ERR(yetty_ycore_void, "alloc root");
	struct yetty_ylexbor_box *root = &r->boxes.data[root_idx];
	root->kind = YL_BOX_BLOCK;
	root->font_size = r->default_font_size;
	root->font_weight = 400;
	root->fg.r = root->fg.g = root->fg.b = 0;
	root->fg.a = 0xff;
	root->bg.r = root->bg.g = root->bg.b = 0xff;
	root->bg.a = 0xff;

	struct yl_style_state initial = {
		.font_size = r->default_font_size,
		.font_weight = 400,
		.font_italic = false,
		.fg = root->fg,
	};

	struct yl_inline_buf ib = {0};
	walk(r, lxb_dom_interface_node(r->document), &initial, root_idx, &ib);
	flush_inline(r, &initial, root_idx, &ib);
	free(ib.buf);

	return YETTY_OK_VOID();
}
