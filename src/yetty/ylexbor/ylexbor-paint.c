/*
 * ylexbor-paint — emit ypaint primitives from a laid-out box vector.
 *
 * Every YL_BOX_BLOCK with non-zero alpha background → ysdf box.
 * Every YL_BOX_INLINE_TEXT → TEXT_SPAN flyweight prim.
 * Every YL_BOX_INLINE_IMAGE → grey placeholder box.
 *
 * Color packing matches what ypaint's shader expects: low byte = R,
 * high byte = A. Same convention ynetsurf-plotters.c uses.
 */

#include "ylexbor-internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>


static uint32_t pack_rgba(struct yetty_ylexbor_color c)
{
	if (c.a == 0) return 0;
	return ((uint32_t)c.r) | ((uint32_t)c.g << 8) |
	       ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

struct yetty_ycore_void_result yetty_ylexbor_paint(
	struct yetty_ylexbor *r, struct yetty_ypaint_core_buffer *buf)
{
	if (r == NULL || buf == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_paint: null");

	const int debug = getenv("YLEXBOR_DEBUG_PAINT") != NULL;
	uint32_t z = 0;

	if (debug) {
		fprintf(stderr, "[ylexbor:paint] total boxes=%u\n",
			r->boxes.size);
	}
	for (uint32_t i = 0; i < r->boxes.size; i++) {
		struct yetty_ylexbor_box *b = &r->boxes.data[i];
		if (b->w <= 0 || b->h <= 0) {
			if (debug) {
				fprintf(stderr,
				    "[ylexbor:paint] skip  i=%u kind=%d xy=%.0f,%.0f wh=%.0fx%.0f\n",
				    i, b->kind, b->x, b->y, b->w, b->h);
			}
			continue;
		}

		switch (b->kind) {
		case YL_BOX_BLOCK: {
			if (debug) {
				fprintf(stderr,
				    "[ylexbor:paint] block i=%u xy=%.0f,%.0f wh=%.0fx%.0f bg=%02x%02x%02x%02x\n",
				    i, b->x, b->y, b->w, b->h,
				    b->bg.r, b->bg.g, b->bg.b, b->bg.a);
			}
			/* Skip transparent backgrounds — most blocks. */
			if (b->bg.a == 0) break;
			struct yetty_ysdf_box box = {
				.center_x = b->x + b->w * 0.5f,
				.center_y = b->y + b->h * 0.5f,
				.half_width = b->w * 0.5f,
				.half_height = b->h * 0.5f,
				.corner_radius = 0,
			};
			(void)yetty_ysdf_add_box(buf, z++,
				pack_rgba(b->bg), 0, 0, &box);
			break;
		}

		case YL_BOX_INLINE_IMAGE: {
			struct yetty_ysdf_box box = {
				.center_x = b->x + b->w * 0.5f,
				.center_y = b->y + b->h * 0.5f,
				.half_width = b->w * 0.5f,
				.half_height = b->h * 0.5f,
				.corner_radius = 0,
			};
			uint32_t fill = 0xc0c0c0ffu;
			(void)yetty_ysdf_add_box(buf, z++, fill, 0, 0, &box);
			break;
		}

		case YL_BOX_INLINE_TEXT: {
			if (debug && b->text_len) {
				int n = b->text_len > 40 ? 40 : (int)b->text_len;
				fprintf(stderr,
				    "[ylexbor:paint] text  i=%u xy=%.0f,%.0f wh=%.0fx%.0f fg=%02x%02x%02x%02x \"%.*s\"\n",
				    i, b->x, b->y, b->w, b->h,
				    b->fg.r, b->fg.g, b->fg.b, b->fg.a,
				    n, b->text);
			}
			if (b->text == NULL || b->text_len == 0) break;
			struct yetty_ycore_buffer txt = {
				.data = (uint8_t *)b->text,
				.capacity = b->text_len,
				.size = b->text_len,
			};
			/* Baseline approximation: top + 0.8 * line height.
			 * Real metric needs FreeType ascent. */
			float baseline_y = b->y + b->font_size * 0.8f;
			(void)yetty_ypaint_core_buffer_add_text(
				buf, b->x, baseline_y, &txt,
				b->font_size, pack_rgba(b->fg),
				z++, /*font_id=*/-1, /*rotation=*/0.0f);
			break;
		}
		}
	}

	return YETTY_OK_VOID();
}
