/*
 * ylexbor — top-level lifecycle. Wires lexbor's HTML+CSS parsing to the
 * box-build → layout → paint pipeline implemented in the sibling files.
 */

#include "ylexbor-internal.h"

#include <stdlib.h>
#include <string.h>

#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/html/html.h>


/* ===========================================================================
 * Box vector — small dynamic array.
 * ===========================================================================*/

static struct yetty_ycore_void_result box_vec_reserve(
	struct yetty_ylexbor_box_vec *v, uint32_t want)
{
	if (want <= v->cap) return YETTY_OK_VOID();
	uint32_t new_cap = v->cap ? v->cap * 2 : 16;
	while (new_cap < want) new_cap *= 2;
	void *p = realloc(v->data, new_cap * sizeof(*v->data));
	if (p == NULL) return YETTY_ERR(yetty_ycore_void, "box vec OOM");
	v->data = p;
	v->cap = new_cap;
	return YETTY_OK_VOID();
}

static void box_vec_clear(struct yetty_ylexbor_box_vec *v)
{
	v->size = 0;
}

static void box_vec_destroy(struct yetty_ylexbor_box_vec *v)
{
	free(v->data);
	v->data = NULL;
	v->size = v->cap = 0;
}

/* ===========================================================================
 * Text arena
 * ===========================================================================*/

const char *yetty_ylexbor_arena_dup(struct yetty_ylexbor *r,
				    const char *bytes, size_t len)
{
	if (len == 0) return "";
	if (r->text_arena_size + len > r->text_arena_cap) {
		size_t new_cap = r->text_arena_cap ? r->text_arena_cap * 2 : 4096;
		while (new_cap < r->text_arena_size + len) new_cap *= 2;
		char *p = realloc(r->text_arena, new_cap);
		if (p == NULL) return NULL;
		r->text_arena = p;
		r->text_arena_cap = new_cap;
	}
	char *out = r->text_arena + r->text_arena_size;
	memcpy(out, bytes, len);
	r->text_arena_size += len;
	return out;
}

static void arena_reset(struct yetty_ylexbor *r) { r->text_arena_size = 0; }

/* ===========================================================================
 * Naive text width — placeholder, will become FreeType-driven later.
 * Good enough for the same MVP layout shape ynetsurf uses.
 * ===========================================================================*/

float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size)
{
	int n = 0;
	for (size_t i = 0; i < len;) {
		unsigned char c = (unsigned char)s[i];
		if      (c < 0x80) i += 1;
		else if ((c & 0xE0) == 0xC0) i += 2;
		else if ((c & 0xF0) == 0xE0) i += 3;
		else if ((c & 0xF8) == 0xF0) i += 4;
		else                          i += 1;
		n++;
	}
	float per_glyph = font_size * 0.55f;
	if (per_glyph < 1.0f) per_glyph = 1.0f;
	return n * per_glyph;
}

/* ===========================================================================
 * Public lifecycle
 * ===========================================================================*/

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(
	const struct yetty_ylexbor_config *cfg)
{
	struct yetty_ylexbor *r = calloc(1, sizeof(*r));
	if (r == NULL)
		return YETTY_ERR(yetty_ylexbor_ptr, "ylexbor alloc");

	r->viewport_w = cfg && cfg->viewport_width  > 0 ? cfg->viewport_width  : 1024;
	r->viewport_h = cfg && cfg->viewport_height > 0 ? cfg->viewport_height : 768;
	r->default_font_size =
		cfg && cfg->default_font_size > 0 ? cfg->default_font_size : 16.0f;

	r->document = lxb_html_document_create();
	if (r->document == NULL) {
		free(r);
		return YETTY_ERR(yetty_ylexbor_ptr, "html_document_create");
	}
	if (lxb_style_init(r->document) != LXB_STATUS_OK) {
		lxb_html_document_destroy(r->document);
		free(r);
		return YETTY_ERR(yetty_ylexbor_ptr, "lxb_style_init");
	}

	r->css_parser = lxb_css_parser_create();
	if (r->css_parser == NULL ||
	    lxb_css_parser_init(r->css_parser, NULL) != LXB_STATUS_OK) {
		if (r->css_parser) lxb_css_parser_destroy(r->css_parser, true);
		lxb_html_document_destroy(r->document);
		free(r);
		return YETTY_ERR(yetty_ylexbor_ptr, "css_parser_init");
	}

	return YETTY_OK(yetty_ylexbor_ptr, r);
}

struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r)
{
	if (r == NULL) return YETTY_OK_VOID();
	if (r->css_parser)
		lxb_css_parser_destroy(r->css_parser, true);
	if (r->document)
		lxb_html_document_destroy(r->document);
	box_vec_destroy(&r->boxes);
	free(r->text_arena);
	free(r);
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_load_html(
	struct yetty_ylexbor *r, const char *html, size_t html_len)
{
	if (r == NULL || html == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_load_html: null");

	/* Replace the document — fresh parser state, drop any prior boxes. */
	box_vec_clear(&r->boxes);
	arena_reset(r);
	r->content_height = 0;

	lxb_status_t s = lxb_html_document_parse(
		r->document, (const lxb_char_t *)html, html_len);
	if (s != LXB_STATUS_OK)
		return YETTY_ERR(yetty_ycore_void, "html_document_parse failed");

	struct yetty_ycore_void_result br = yetty_ylexbor_box_build(r);
	if (YETTY_IS_ERR(br)) return br;

	struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
	if (YETTY_IS_ERR(lr)) return lr;

	(void)box_vec_reserve;  /* used by box-build */
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_add_css(
	struct yetty_ylexbor *r, const char *css, size_t css_len)
{
	if (r == NULL || css == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_add_css: null");

	lxb_css_stylesheet_t *sheet = lxb_css_stylesheet_create(NULL);
	if (sheet == NULL)
		return YETTY_ERR(yetty_ycore_void, "stylesheet_create");
	lxb_status_t s = lxb_css_stylesheet_parse(
		sheet, r->css_parser, (const lxb_char_t *)css, css_len);
	if (s != LXB_STATUS_OK) {
		lxb_css_stylesheet_destroy(sheet, true);
		return YETTY_ERR(yetty_ycore_void, "stylesheet_parse");
	}
	s = lxb_html_document_stylesheet_attach(r->document, sheet);
	if (s != LXB_STATUS_OK) {
		lxb_css_stylesheet_destroy(sheet, true);
		return YETTY_ERR(yetty_ycore_void, "stylesheet_attach");
	}
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_set_viewport(
	struct yetty_ylexbor *r, int width, int height)
{
	if (r == NULL) return YETTY_ERR(yetty_ycore_void, "null");
	r->viewport_w = width  > 0 ? width  : r->viewport_w;
	r->viewport_h = height > 0 ? height : r->viewport_h;
	if (r->boxes.size > 0) {
		struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
		if (YETTY_IS_ERR(lr)) return lr;
	}
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_render(
	struct yetty_ylexbor *r, struct yetty_ypaint_core_buffer *buf)
{
	if (r == NULL || buf == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_render: null");
	return yetty_ylexbor_paint(r, buf);
}

int yetty_ylexbor_content_height(const struct yetty_ylexbor *r)
{
	return r ? r->content_height : 0;
}

/* Make box_vec_reserve visible to box-build. Static-but-shared via
 * attribute would be cleaner; this single-TU project uses a header
 * shim. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(
	struct yetty_ylexbor_box_vec *v, uint32_t want)
{
	return box_vec_reserve(v, want);
}
