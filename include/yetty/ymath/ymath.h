#ifndef YETTY_YMATH_YMATH_H
#define YETTY_YMATH_YMATH_H

/*
 * ymath — TeX-subset math rendering to ydraw drawables. GPU-less, same
 * architecture as ychart/ydiagram: a layout engine placing MSDF glyphs
 * (default font, Unicode math codepoints) plus SDF rules for fraction
 * bars and radical strokes into a drawable list the terminal renders
 * like any other figure.
 *
 * Supported subset (see math.c for the token table):
 *   grouping    { }            scripts      x^2  a_i  x_i^2
 *   fractions   \frac{a}{b}    radicals     \sqrt{x}
 *   big ops     \sum \int \prod with _ ^ limits
 *   greek       \alpha ... \Omega
 *   symbols     \pm \mp \times \cdot \le \ge \ne \approx \infty
 *               \partial \nabla \to \pi \hbar \ell \dots
 *   functions   \sin \cos \tan \log \ln \exp (roman)
 *
 * NOT full LaTeX: no environments, no macros, no text mode.
 */

#include <stddef.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymath_render_config {
    float font_size; /* base glyph size in px; 0 → 28 */
    uint32_t color;  /* text color 0xAABBGGRR; 0 → off-white */
    float origin_x;  /* top-left of the figure; usually 0 */
    float origin_y;
};

/* Render one math expression into a fresh drawable list (caller owns). */
struct yetty_ydraw_drawable_list_result yetty_ymath_render(
    const char *source, size_t source_len, const struct yetty_ymath_render_config *config);

/* DCS envelope emit (YETTY_DCS_YDRAW_BIN) — same wire as ycat/yplot. */
struct yetty_ycore_size_result yetty_ymath_dcs_emit(const struct yetty_ydraw_drawable_list *list,
                                                    FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMATH_YMATH_H */
