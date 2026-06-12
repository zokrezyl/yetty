/*
 * handler-music.c — LilyPond score → ydraw buffer via ymusic.
 *
 * Engraves a LilyPond-subset source into one drawable list: staff lines,
 * stems, beams and barlines as SDF segments; clefs, noteheads, rests and
 * accidentals as Emmentaler MSDF glyphs. Systems wrap to the configured
 * card width, so the score flows inline in the scrollback like any other
 * ycat card.
 *
 * The Emmentaler font is referenced by name in the rendered drawable list;
 * the receiver resolves it from the install (a pre-generated MSDF atlas
 * shipped beside the default font). ymusic never opens or ships the font, so
 * this handler does not locate it on disk.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ymusic/music.h>

/* Error-path teardown is best-effort — absorb a failed destroy so the
 * original error is the one that surfaces. */
static void music_destroy_best_effort(struct yetty_yclass_object *music)
{
    struct yetty_ycore_void_result destroy_res = yetty_ymusic_destroy(NULL, music);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_music(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;

    struct yetty_ycore_void_result register_res = yetty_ymusic_register();
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, register_res, "ymusic register failed");

    struct yetty_yclass_object_ptr_result object_res = yetty_ymusic_music_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_res, "ymusic create failed");
    struct yetty_yclass_object *music = object_res.value;

    /* Systems wrap to the card width; staff spacing tracks the cell height so
     * the score reads at the terminal's text scale (clamped so tiny cells
     * don't produce an unreadable staff). */
    float cell_width = (config && config->cell_width) ? (float)config->cell_width : 8.0f;
    float cell_height = (config && config->cell_height) ? (float)config->cell_height : 16.0f;
    uint32_t width_cells = (config && config->width_cells) ? config->width_cells : 80u;
    float width_px = (float)width_cells * cell_width;
    float staff_space = cell_height * 0.85f;
    if (staff_space < 8.0f) {
        staff_space = 8.0f;
    }

    struct yetty_ycore_void_result configure_res =
        yetty_ymusic_configure(NULL, music, width_px, staff_space, YETTY_YMUSIC_FLAG_NONE);
    if (YETTY_IS_ERR(configure_res)) {
        music_destroy_best_effort(music);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymusic configure failed", configure_res);
    }

    struct yetty_ycore_void_result parse_res =
        yetty_ymusic_parse(NULL, music, (const char *)bytes, len);
    if (YETTY_IS_ERR(parse_res)) {
        music_destroy_best_effort(music);
        return YETTY_ERR(yetty_ydraw_drawable_list, "lilypond parse failed", parse_res);
    }

    struct yetty_ydraw_drawable_list_result render_res = yetty_ymusic_render(NULL, music);
    /* The rendered list owns its own bytes (primitives + a font-name ref, not
     * the font itself) — the score
     * model can go regardless of the render outcome. */
    music_destroy_best_effort(music);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, render_res, "lilypond render failed");
    return render_res;
}
