/*
 * stress-bomb — ydraw ingest stress through the v2 client interface.
 *
 * Bombs the enclosing yetty with the full drawable zoo: every SDF
 * primitive kind (all 28, anonymous AND addressed), text runs, and
 * complex figures (two live yplot instances plus a shadertoy), then
 * keeps hammering:
 *
 *   - GROUP(1) replacement every frame: the whole shape storm re-packed
 *     and re-ingested in place (offsets move, colors cycle) — the
 *     addressed-reopen path at volume, no new rows, no cursor movement.
 *   - CMD_UPDATE streaming into the two plots every frame — fresh sample
 *     windows without ever re-sending the creation records.
 *   - A churn group deleted and re-created periodically — the
 *     create/delete lifecycle under load.
 *
 * Run inside a yetty session:
 *   ./build-desktop-ytrace-release/yetty \
 *     -e './build-desktop-ytrace-release/demo/ydraw/demo-ydraw-stress-bomb'
 *
 * Options:
 *   --shapes=N       SDF primitives per frame        (default 400)
 *   --texts=N        text runs per frame             (default 24)
 *   --frames=N       animation frames, 0 = forever   (default 600)
 *   --period-ms=M    sleep between frames            (default 33)
 *   --reserve-px=H   reserved canvas height in px    (default 560)
 *   --churn-every=N  churn-group flip period, 0 off  (default 20)
 *   --no-complex     skip plots + shadertoy
 *   --no-sleep       emit as fast as possible
 */

#include <yetty/api/ycomplex2/shadertoy.h>
#include <yetty/api/ydrawlist2/drawable.h>
#include <yetty/api/ydrawlist2/list.h>
#include <yetty/api/ydrawlist2/shape.h>
#include <yetty/api/yplot/plot.h>
#include <yetty/api/ysdf2/shapes.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Result-check shorthand for the long setter chains below (a result/error
 * macro — the context string is the failing call itself). */
#define STRESS_TRY(call)                                                                           \
    do {                                                                                           \
        struct yetty_ycore_void_result stress_try_res = (call);                                    \
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stress_try_res, #call);                              \
    } while (0)

enum {
    STRESS_GROUP_STORM = 1,
    STRESS_GROUP_CHURN = 2,
    STRESS_PLOT_WAVE_ID = 100,
    STRESS_PLOT_MATH_ID = 101,
    STRESS_SHADER_ID = 102,
    STRESS_SHAPE_KIND_COUNT = 28,
    STRESS_WAVE_SAMPLES = 96,
    STRESS_ADDRESSED_EVERY = 7, /* every Nth storm shape gets an id */
};

struct stress_options {
    int shapes;
    int texts;
    int frames;
    int period_ms;
    int reserve_px;
    int churn_every;
    int no_complex;
    int no_sleep;
    int doubling;     /* ramp mode: double shapes per tier until it hurts */
    int double_every; /* frames per tier */
    int max_shapes;   /* ramp ceiling */
};

struct stress_stats {
    uint64_t envelopes;
    uint64_t records;
};

static void stress_sleep_ms(int milliseconds)
{
    struct timespec interval = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    nanosleep(&interval, NULL);
}

static double stress_now_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Brand ladder — fills/strokes cycle through it (alpha varies per shape). */
static uint32_t stress_palette_color(uint32_t index, uint32_t alpha)
{
    static const uint32_t palette[8] = {
        0x6BA892u, /* accent */
        0x74C5A5u, /* accent bright */
        0x5A8979u, /* accent deep */
        0x364A47u, /* border */
        0x556162u, /* text muted */
        0x9FA7A8u, /* text secondary */
        0xE0E5E4u, /* text primary */
        0x1E262Cu, /* bg row */
    };
    return (alpha << 24) | palette[index % 8u];
}

static struct yetty_yclass_object_ptr_result stress_shape_new(uint32_t kind)
{
    switch (kind % STRESS_SHAPE_KIND_COUNT) {
    case 0:
        return yetty_ysdf2_circle_create(NULL);
    case 1:
        return yetty_ysdf2_box_create(NULL);
    case 2:
        return yetty_ysdf2_segment_create(NULL);
    case 3:
        return yetty_ysdf2_triangle_create(NULL);
    case 4:
        return yetty_ysdf2_ellipse_create(NULL);
    case 5:
        return yetty_ysdf2_arc_create(NULL);
    case 6:
        return yetty_ysdf2_rounded_box_create(NULL);
    case 7:
        return yetty_ysdf2_rhombus_create(NULL);
    case 8:
        return yetty_ysdf2_pentagon_create(NULL);
    case 9:
        return yetty_ysdf2_hexagon_create(NULL);
    case 10:
        return yetty_ysdf2_star_create(NULL);
    case 11:
        return yetty_ysdf2_pie_create(NULL);
    case 12:
        return yetty_ysdf2_ring_create(NULL);
    case 13:
        return yetty_ysdf2_heart_create(NULL);
    case 14:
        return yetty_ysdf2_cross_create(NULL);
    case 15:
        return yetty_ysdf2_rounded_x_create(NULL);
    case 16:
        return yetty_ysdf2_capsule_create(NULL);
    case 17:
        return yetty_ysdf2_moon_create(NULL);
    case 18:
        return yetty_ysdf2_egg_create(NULL);
    case 19:
        return yetty_ysdf2_octogon_create(NULL);
    case 20:
        return yetty_ysdf2_hexagram_create(NULL);
    case 21:
        return yetty_ysdf2_pentagram_create(NULL);
    case 22:
        return yetty_ysdf2_linear_gradient_box_create(NULL);
    case 23:
        return yetty_ysdf2_radial_gradient_box_create(NULL);
    case 24:
        return yetty_ysdf2_sphere_3d_create(NULL);
    case 25:
        return yetty_ysdf2_box_3d_create(NULL);
    case 26:
        return yetty_ysdf2_torus_3d_create(NULL);
    default:
        return yetty_ysdf2_cylinder_3d_create(NULL);
    }
}

static struct yetty_ycore_void_result stress_shape_geometry(struct yetty_yclass_object *shape,
                                                            uint32_t kind, float center_x,
                                                            float center_y, float size, float aux)
{
    float direction_x = cosf(aux);
    float direction_y = sinf(aux);
    switch (kind % STRESS_SHAPE_KIND_COUNT) {
    case 0:
        STRESS_TRY(yetty_ysdf2_circle_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_circle_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_circle_radius_set(shape, size));
        break;
    case 1:
        STRESS_TRY(yetty_ysdf2_box_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_box_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_box_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_box_half_height_set(shape, size * 0.7f));
        STRESS_TRY(yetty_ysdf2_box_corner_radius_set(shape, size * 0.2f));
        break;
    case 2:
        STRESS_TRY(yetty_ysdf2_segment_start_x_set(shape, center_x - direction_x * size));
        STRESS_TRY(yetty_ysdf2_segment_start_y_set(shape, center_y - direction_y * size));
        STRESS_TRY(yetty_ysdf2_segment_end_x_set(shape, center_x + direction_x * size));
        STRESS_TRY(yetty_ysdf2_segment_end_y_set(shape, center_y + direction_y * size));
        break;
    case 3:
        STRESS_TRY(yetty_ysdf2_triangle_vertex_a_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_triangle_vertex_a_y_set(shape, center_y - size));
        STRESS_TRY(yetty_ysdf2_triangle_vertex_b_x_set(shape, center_x - size * 0.87f));
        STRESS_TRY(yetty_ysdf2_triangle_vertex_b_y_set(shape, center_y + size * 0.5f));
        STRESS_TRY(yetty_ysdf2_triangle_vertex_c_x_set(shape, center_x + size * 0.87f));
        STRESS_TRY(yetty_ysdf2_triangle_vertex_c_y_set(shape, center_y + size * 0.5f));
        break;
    case 4:
        STRESS_TRY(yetty_ysdf2_ellipse_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_ellipse_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_ellipse_radius_x_set(shape, size));
        STRESS_TRY(yetty_ysdf2_ellipse_radius_y_set(shape, size * 0.6f));
        break;
    case 5:
        STRESS_TRY(yetty_ysdf2_arc_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_arc_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_arc_aperture_x_set(shape, direction_x));
        STRESS_TRY(yetty_ysdf2_arc_aperture_y_set(shape, direction_y));
        STRESS_TRY(yetty_ysdf2_arc_radius_set(shape, size));
        STRESS_TRY(yetty_ysdf2_arc_thickness_set(shape, 1.5f + size * 0.15f));
        break;
    case 6:
        STRESS_TRY(yetty_ysdf2_rounded_box_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_rounded_box_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_rounded_box_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_rounded_box_half_height_set(shape, size * 0.75f));
        STRESS_TRY(yetty_ysdf2_rounded_box_radius_top_right_set(shape, size * 0.4f));
        STRESS_TRY(yetty_ysdf2_rounded_box_radius_bottom_right_set(shape, size * 0.1f));
        STRESS_TRY(yetty_ysdf2_rounded_box_radius_top_left_set(shape, size * 0.1f));
        STRESS_TRY(yetty_ysdf2_rounded_box_radius_bottom_left_set(shape, size * 0.4f));
        break;
    case 7:
        STRESS_TRY(yetty_ysdf2_rhombus_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_rhombus_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_rhombus_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_rhombus_half_height_set(shape, size * 0.65f));
        break;
    case 8:
        STRESS_TRY(yetty_ysdf2_pentagon_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_pentagon_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_pentagon_radius_set(shape, size));
        break;
    case 9:
        STRESS_TRY(yetty_ysdf2_hexagon_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_hexagon_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_hexagon_radius_set(shape, size));
        break;
    case 10:
        STRESS_TRY(yetty_ysdf2_star_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_star_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_star_radius_set(shape, size));
        STRESS_TRY(yetty_ysdf2_star_num_points_set(shape, 5.0f + (float)(kind % 4u)));
        STRESS_TRY(yetty_ysdf2_star_inner_ratio_set(shape, 0.45f));
        break;
    case 11:
        STRESS_TRY(yetty_ysdf2_pie_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_pie_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_pie_aperture_x_set(shape, direction_x));
        STRESS_TRY(yetty_ysdf2_pie_aperture_y_set(shape, direction_y));
        STRESS_TRY(yetty_ysdf2_pie_radius_set(shape, size));
        break;
    case 12:
        STRESS_TRY(yetty_ysdf2_ring_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_ring_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_ring_normal_x_set(shape, direction_x));
        STRESS_TRY(yetty_ysdf2_ring_normal_y_set(shape, direction_y));
        STRESS_TRY(yetty_ysdf2_ring_radius_set(shape, size));
        STRESS_TRY(yetty_ysdf2_ring_thickness_set(shape, 1.5f + size * 0.12f));
        break;
    case 13:
        STRESS_TRY(yetty_ysdf2_heart_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_heart_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_heart_scale_set(shape, size));
        break;
    case 14:
        STRESS_TRY(yetty_ysdf2_cross_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_cross_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_cross_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_cross_half_height_set(shape, size * 0.35f));
        STRESS_TRY(yetty_ysdf2_cross_corner_radius_set(shape, size * 0.1f));
        break;
    case 15:
        STRESS_TRY(yetty_ysdf2_rounded_x_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_rounded_x_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_rounded_x_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_rounded_x_radius_set(shape, size * 0.18f));
        break;
    case 16:
        STRESS_TRY(yetty_ysdf2_capsule_start_x_set(shape, center_x - direction_x * size));
        STRESS_TRY(yetty_ysdf2_capsule_start_y_set(shape, center_y - direction_y * size));
        STRESS_TRY(yetty_ysdf2_capsule_end_x_set(shape, center_x + direction_x * size));
        STRESS_TRY(yetty_ysdf2_capsule_end_y_set(shape, center_y + direction_y * size));
        STRESS_TRY(yetty_ysdf2_capsule_radius_set(shape, size * 0.3f));
        break;
    case 17:
        STRESS_TRY(yetty_ysdf2_moon_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_moon_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_moon_offset_set(shape, size * 0.4f));
        STRESS_TRY(yetty_ysdf2_moon_radius_outer_set(shape, size));
        STRESS_TRY(yetty_ysdf2_moon_radius_inner_set(shape, size * 0.75f));
        break;
    case 18:
        STRESS_TRY(yetty_ysdf2_egg_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_egg_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_egg_radius_outer_set(shape, size));
        STRESS_TRY(yetty_ysdf2_egg_radius_inner_set(shape, size * 0.5f));
        break;
    case 19:
        STRESS_TRY(yetty_ysdf2_octogon_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_octogon_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_octogon_radius_set(shape, size));
        break;
    case 20:
        STRESS_TRY(yetty_ysdf2_hexagram_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_hexagram_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_hexagram_radius_set(shape, size));
        break;
    case 21:
        STRESS_TRY(yetty_ysdf2_pentagram_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_pentagram_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_pentagram_radius_set(shape, size));
        break;
    case 22:
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_half_height_set(shape, size * 0.7f));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_corner_radius_set(shape, size * 0.2f));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_grad_x0_set(shape, center_x - size));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_grad_y0_set(shape, center_y - size));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_grad_x1_set(shape, center_x + size));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_grad_y1_set(shape, center_y + size));
        STRESS_TRY(
            yetty_ysdf2_linear_gradient_box_color0_set(shape, stress_palette_color(kind, 0xFFu)));
        STRESS_TRY(yetty_ysdf2_linear_gradient_box_color1_set(
            shape, stress_palette_color(kind + 2u, 0xFFu)));
        break;
    case 23:
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_center_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_center_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_half_width_set(shape, size));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_half_height_set(shape, size * 0.7f));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_corner_radius_set(shape, size * 0.2f));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_grad_cx_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_grad_cy_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_grad_radius_set(shape, size));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_color_inner_set(
            shape, stress_palette_color(kind + 1u, 0xFFu)));
        STRESS_TRY(yetty_ysdf2_radial_gradient_box_color_outer_set(
            shape, stress_palette_color(kind + 3u, 0x30u)));
        break;
    case 24:
        STRESS_TRY(yetty_ysdf2_sphere_3d_position_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_sphere_3d_position_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_sphere_3d_position_z_set(shape, 0.0f));
        STRESS_TRY(yetty_ysdf2_sphere_3d_radius_set(shape, size));
        break;
    case 25:
        STRESS_TRY(yetty_ysdf2_box_3d_position_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_box_3d_position_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_box_3d_position_z_set(shape, 0.0f));
        STRESS_TRY(yetty_ysdf2_box_3d_half_size_x_set(shape, size * 0.8f));
        STRESS_TRY(yetty_ysdf2_box_3d_half_size_y_set(shape, size * 0.6f));
        STRESS_TRY(yetty_ysdf2_box_3d_half_size_z_set(shape, size * 0.6f));
        break;
    case 26:
        STRESS_TRY(yetty_ysdf2_torus_3d_position_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_torus_3d_position_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_torus_3d_position_z_set(shape, 0.0f));
        STRESS_TRY(yetty_ysdf2_torus_3d_major_radius_set(shape, size));
        STRESS_TRY(yetty_ysdf2_torus_3d_minor_radius_set(shape, size * 0.3f));
        break;
    default:
        STRESS_TRY(yetty_ysdf2_cylinder_3d_position_x_set(shape, center_x));
        STRESS_TRY(yetty_ysdf2_cylinder_3d_position_y_set(shape, center_y));
        STRESS_TRY(yetty_ysdf2_cylinder_3d_position_z_set(shape, 0.0f));
        STRESS_TRY(yetty_ysdf2_cylinder_3d_radius_set(shape, size * 0.6f));
        STRESS_TRY(yetty_ysdf2_cylinder_3d_half_height_set(shape, size));
        break;
    }
    return YETTY_OK_VOID();
}

/* One storm shape: deterministic golden-angle spiral placement, phase
 * rotation per frame, palette cycling, every Nth addressed (HAS_ID). */
static struct yetty_ycore_void_result stress_shape_setup(struct yetty_yclass_object *shape,
                                                         uint32_t shape_index, float phase,
                                                         float storm_width, float storm_height)
{
    float angle = (float)shape_index * 2.399963f + phase;
    float spiral = fmodf((float)shape_index * 37.0f, storm_height * 0.5f);
    float size = 5.0f + fmodf((float)shape_index * 13.0f, 34.0f);
    float center_x = storm_width * 0.5f + cosf(angle) * spiral * 1.4f;
    float center_y = storm_height * 0.5f + sinf(angle) * spiral * 0.85f;
    /* The replacement contract is strict: a GROUP re-emit must fit the
     * span the group had at creation. Clamp every extent into the fixed
     * storm canvas so no frame's bounds ever drift past the frame box. */
    float margin = size + 8.0f;
    if (center_x < margin) {
        center_x = margin;
    }
    if (center_x > storm_width - margin) {
        center_x = storm_width - margin;
    }
    if (center_y < margin) {
        center_y = margin;
    }
    if (center_y > storm_height - margin) {
        center_y = storm_height - margin;
    }
    uint32_t alpha = 0x50u + (shape_index * 29u) % 0xA0u;

    if (shape_index % STRESS_ADDRESSED_EVERY == 0u) {
        STRESS_TRY(yetty_ydrawlist2_shape_id_set(shape, 1000u + shape_index));
    }
    STRESS_TRY(yetty_ydrawlist2_shape_layer_set(shape, (int32_t)(shape_index % 6u)));
    STRESS_TRY(yetty_ydrawlist2_shape_fill_set(shape, stress_palette_color(shape_index, alpha)));
    STRESS_TRY(
        yetty_ydrawlist2_shape_stroke_set(shape, stress_palette_color(shape_index + 3u, 0xFFu)));
    STRESS_TRY(yetty_ydrawlist2_shape_stroke_width_set(shape, (float)(shape_index % 3u)));
    return stress_shape_geometry(shape, shape_index, center_x, center_y, size, angle * 0.5f);
}

static struct yetty_ycore_void_result stress_add_shape(struct yetty_yclass_object *list,
                                                       uint32_t shape_index, float phase,
                                                       float storm_width, float storm_height)
{
    struct yetty_yclass_object_ptr_result shape_res = stress_shape_new(shape_index);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "stress shape create");
    struct yetty_yclass_object *shape = shape_res.value;
    struct yetty_ycore_void_result step_res =
        stress_shape_setup(shape, shape_index, phase, storm_width, storm_height);
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(shape);
        return YETTY_ERR(yetty_ycore_void, "stress shape setup", step_res);
    }
    step_res = yetty_ydrawlist2_add(list, shape);
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(shape);
        return YETTY_ERR(yetty_ycore_void, "stress shape pack", step_res);
    }
    return yetty_yclass_object_free(shape);
}

static struct yetty_ycore_void_result stress_add_text(struct yetty_yclass_object *list,
                                                      const char *body, float x, float y,
                                                      float font_size, uint32_t color)
{
    struct yetty_yclass_object_ptr_result text_res = yetty_ydrawlist2_text_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "stress text create");
    struct yetty_yclass_object *text = text_res.value;
    struct yetty_ycore_void_result step_res = yetty_ydrawlist2_set_body(text, body);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_text_x_set(text, x);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_text_y_set(text, y);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_text_font_size_set(text, font_size);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_text_color_set(text, color);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_add(list, text);
    }
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(text);
        return YETTY_ERR(yetty_ycore_void, "stress text", step_res);
    }
    return yetty_yclass_object_free(text);
}

/* The storm canvas outline. Present in EVERY frame as the group's first
 * record, it pins the group's bounds to a constant extent — the in-place
 * replacement contract (fit the creation-time row span) then holds for
 * every frame regardless of where the spiral throws the shapes. */
static struct yetty_ycore_void_result stress_add_frame_box(struct yetty_yclass_object *list,
                                                           float storm_width, float storm_height)
{
    struct yetty_yclass_object_ptr_result box_res = yetty_ysdf2_box_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_res, "stress frame box create");
    struct yetty_yclass_object *box = box_res.value;
    struct yetty_ycore_void_result step_res = yetty_ysdf2_box_center_x_set(box, storm_width * 0.5f);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_center_y_set(box, storm_height * 0.5f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_half_width_set(box, storm_width * 0.5f - 1.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_half_height_set(box, storm_height * 0.5f - 1.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_corner_radius_set(box, 8.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_fill_set(box, 0x00000000u); /* outline only */
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_stroke_set(box, 0xFF364A47u);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_stroke_width_set(box, 2.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_add(list, box);
    }
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(box);
        return YETTY_ERR(yetty_ycore_void, "stress frame box", step_res);
    }
    return yetty_yclass_object_free(box);
}

/* GROUP(1): the shape storm plus orbiting text runs. Emitted once at
 * creation and then REPLACED in place every frame. */
static struct yetty_ycore_void_result stress_build_storm(struct yetty_yclass_object *list,
                                                         const struct stress_options *options,
                                                         uint32_t frame_index,
                                                         struct stress_stats *stats)
{
    float phase = (float)frame_index * 0.06f;
    float storm_width = options->no_complex ? 1180.0f : 740.0f;
    float storm_height = (float)options->reserve_px - 40.0f;

    STRESS_TRY(yetty_ydrawlist2_begin_group(list, STRESS_GROUP_STORM));
    STRESS_TRY(stress_add_frame_box(list, storm_width, storm_height));
    stats->records++;
    for (int shape_index = 0; shape_index < options->shapes; shape_index++) {
        STRESS_TRY(stress_add_shape(list, (uint32_t)shape_index, phase, storm_width, storm_height));
        stats->records++;
    }
    for (int text_index = 0; text_index < options->texts; text_index++) {
        char body[64];
        snprintf(body, sizeof(body), "zoo %02d/%02d frame %u", text_index, options->texts,
                 frame_index);
        float angle =
            phase * 0.7f + (float)text_index * (float)(2.0 * M_PI) / (float)options->texts;
        float text_x = storm_width * 0.5f + cosf(angle) * storm_width * 0.36f;
        float text_y = storm_height * 0.5f + sinf(angle) * storm_height * 0.38f;
        STRESS_TRY(stress_add_text(list, body, text_x, text_y, 12.0f,
                                   stress_palette_color((uint32_t)text_index + 4u, 0xFFu)));
        stats->records++;
    }
    STRESS_TRY(yetty_ydrawlist2_end_group(list));
    return YETTY_OK_VOID();
}

/* GROUP(2): a small badge that gets deleted / re-created periodically. */
static struct yetty_ycore_void_result stress_build_churn(struct yetty_yclass_object *list,
                                                         uint32_t incarnation,
                                                         struct stress_stats *stats)
{
    STRESS_TRY(yetty_ydrawlist2_begin_group(list, STRESS_GROUP_CHURN));
    struct yetty_yclass_object_ptr_result box_res = yetty_ysdf2_box_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_res, "stress churn box create");
    struct yetty_yclass_object *box = box_res.value;
    struct yetty_ycore_void_result step_res = yetty_ysdf2_box_center_x_set(box, 90.0f);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_center_y_set(box, 24.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_half_width_set(box, 84.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_half_height_set(box, 18.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ysdf2_box_corner_radius_set(box, 8.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_fill_set(box, 0xFF1E262Cu);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_stroke_set(box, 0xFF6BA892u);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_shape_stroke_width_set(box, 2.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_add(list, box);
    }
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(box);
        return YETTY_ERR(yetty_ycore_void, "stress churn box", step_res);
    }
    STRESS_TRY(yetty_yclass_object_free(box));
    char body[48];
    snprintf(body, sizeof(body), "churn #%u", incarnation);
    STRESS_TRY(stress_add_text(list, body, 34.0f, 17.0f, 14.0f, 0xFF74C5A5u));
    STRESS_TRY(yetty_ydrawlist2_end_group(list));
    stats->records += 2u;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stress_add_plot(struct yetty_yclass_object *list,
                                                      uint32_t plot_id, float x, float y,
                                                      const char *title, int with_function,
                                                      struct stress_stats *stats)
{
    struct yetty_yclass_object_ptr_result plot_res = yetty_api_yplot_plot_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_res, "stress plot create");
    struct yetty_yclass_object *plot = plot_res.value;

    struct yetty_ycore_void_result step_res = yetty_api_yplot_plot_x_set(plot, x);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_plot_y_set(plot, y);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_plot_width_set(plot, 420.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_plot_height_set(plot, 160.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_plot_id_set(plot, plot_id);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_set_title(plot, title);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_set_y_range(plot, -1.4f, 1.4f);
    }
    if (YETTY_IS_OK(step_res)) {
        /* The streamed buffer: zeroed window, filled by CMD_UPDATE. */
        struct yetty_yclass_object_ptr_result buffer_res = yetty_api_yplot_buffer_create(NULL);
        if (YETTY_IS_ERR(buffer_res)) {
            step_res = YETTY_ERR(yetty_ycore_void, "stress plot buffer create", buffer_res);
        } else {
            struct yetty_yclass_object *buffer = buffer_res.value;
            /* Size-only declaration — the receiver zero-fills the slot at
             * the declared length. Inline values would CAP the slot at the
             * DSL's 64-value inline maximum (silently truncated) and every
             * larger streamed chunk would be rejected as an overflow. */
            step_res = yetty_api_yplot_set_name(buffer, "live");
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_buffer_size_set(buffer, STRESS_WAVE_SAMPLES);
            }
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_set_color(buffer, "#6BA892");
            }
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_add_buffer(plot, buffer);
            }
            struct yetty_ycore_void_result free_res = yetty_yclass_object_free(buffer);
            if (YETTY_IS_OK(step_res) && YETTY_IS_ERR(free_res)) {
                step_res = YETTY_ERR(yetty_ycore_void, "stress plot buffer free", free_res);
            } else if (YETTY_IS_ERR(free_res)) {
                yetty_ycore_error_destroy(free_res.error);
            }
        }
    }
    if (YETTY_IS_OK(step_res) && with_function) {
        struct yetty_yclass_object_ptr_result function_res = yetty_api_yplot_function_create(NULL);
        if (YETTY_IS_ERR(function_res)) {
            step_res = YETTY_ERR(yetty_ycore_void, "stress plot function create", function_res);
        } else {
            struct yetty_yclass_object *function = function_res.value;
            step_res = yetty_api_yplot_set_name(function, "carrier");
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_set_body(function, "sin(3*x) * 0.8");
            }
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_set_color(function, "#556162");
            }
            if (YETTY_IS_OK(step_res)) {
                step_res = yetty_api_yplot_add_function(plot, function);
            }
            struct yetty_ycore_void_result free_res = yetty_yclass_object_free(function);
            if (YETTY_IS_OK(step_res) && YETTY_IS_ERR(free_res)) {
                step_res = YETTY_ERR(yetty_ycore_void, "stress plot function free", free_res);
            } else if (YETTY_IS_ERR(free_res)) {
                yetty_ycore_error_destroy(free_res.error);
            }
        }
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_add(list, plot);
    }
    struct yetty_ycore_void_result destroy_res = yetty_api_yplot_destroy(plot);
    if (YETTY_IS_ERR(step_res)) {
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "stress plot", step_res);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_res, "stress plot destroy");
    stats->records++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stress_add_shadertoy(struct yetty_yclass_object *list,
                                                           float x, float y,
                                                           struct stress_stats *stats)
{
    struct yetty_yclass_object_ptr_result shader_res = yetty_ycomplex2_shadertoy_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shader_res, "stress shadertoy create");
    struct yetty_yclass_object *shader = shader_res.value;
    struct yetty_ycore_void_result step_res = yetty_ycomplex2_shadertoy_x_set(shader, x);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ycomplex2_shadertoy_y_set(shader, y);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ycomplex2_shadertoy_width_set(shader, 420.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ycomplex2_shadertoy_height_set(shader, 150.0f);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ycomplex2_shadertoy_id_set(shader, STRESS_SHADER_ID);
    }
    if (YETTY_IS_OK(step_res)) {
        /* A minimal mainImage-contract WGSL: iTime-driven brand-palette
         * plasma — the receiving factory compiles a pipeline around it,
         * so the figure animates itself with zero client traffic. */
        step_res = yetty_ycomplex2_set_source(
            shader, "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
                    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
                    "    let uv = fragCoord / iResolution.xy;\n"
                    "    let wave = sin(uv.x * 12.0 + iTime * 1.7) * cos(uv.y * 9.0 - iTime);\n"
                    "    let pulse = 0.5 + 0.5 * sin(iTime + uv.x * 6.2831);\n"
                    "    let accent = vec3<f32>(0.420, 0.659, 0.573);\n"
                    "    let deep = vec3<f32>(0.043, 0.063, 0.078);\n"
                    "    let mixed = mix(deep, accent, clamp(wave * pulse + 0.5, 0.0, 1.0));\n"
                    "    return vec4<f32>(mixed, 1.0);\n"
                    "}\n");
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_ydrawlist2_add(list, shader);
    }
    if (YETTY_IS_ERR(step_res)) {
        (void)yetty_yclass_object_free(shader);
        return YETTY_ERR(yetty_ycore_void, "stress shadertoy", step_res);
    }
    stats->records++;
    return yetty_yclass_object_free(shader);
}

/* One CMD_UPDATE into a live plot: the yplot streaming payload
 * [u32 buffer_index][u32 sample_offset][u32 count][f32 samples...]. */
static struct yetty_ycore_void_result stress_plot_update(struct yetty_yclass_object *list,
                                                         uint32_t plot_id, float phase,
                                                         int wave_kind, struct stress_stats *stats)
{
    uint8_t payload[3u * sizeof(uint32_t) + STRESS_WAVE_SAMPLES * sizeof(float)];
    uint32_t header[3] = {0u, 0u, STRESS_WAVE_SAMPLES};
    float samples[STRESS_WAVE_SAMPLES];
    for (int sample_index = 0; sample_index < STRESS_WAVE_SAMPLES; sample_index++) {
        float position = (float)sample_index / (float)STRESS_WAVE_SAMPLES;
        if (wave_kind == 0) {
            samples[sample_index] = sinf(position * 6.2831853f * 2.0f + phase) *
                                    (0.6f + 0.4f * sinf(phase * 0.4f + position * 9.0f));
        } else {
            samples[sample_index] = sinf(position * 6.2831853f * 3.0f + phase * 1.7f) *
                                    cosf(position * 6.2831853f + phase * 0.6f);
        }
    }
    memcpy(payload, header, sizeof(header));
    memcpy(payload + sizeof(header), samples, sizeof(samples));
    struct yetty_ycore_buffer payload_buffer = {
        .data = payload,
        .capacity = sizeof(payload),
        .size = sizeof(payload),
    };
    STRESS_TRY(yetty_ydrawlist2_update(list, plot_id, payload_buffer));
    stats->records++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stress_emit_and_destroy(struct yetty_yclass_object *list,
                                                              struct stress_stats *stats)
{
    struct yetty_ycore_void_result emit_res = yetty_ydrawlist2_dcs_emit(list);
    struct yetty_ycore_void_result destroy_res = yetty_ydrawlist2_destroy(list);
    if (YETTY_IS_ERR(emit_res)) {
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "stress emit", emit_res);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_res, "stress list destroy");
    stats->envelopes++;
    return YETTY_OK_VOID();
}

static struct yetty_yclass_object_ptr_result stress_list_new(void)
{
    return yetty_ydrawlist2_drawable_list_create(NULL);
}

/* Run `build` steps against a fresh list, then emit + destroy. The list is
 * always destroyed, success or failure. */
static struct yetty_ycore_void_result stress_close_list(struct yetty_yclass_object *list,
                                                        struct yetty_ycore_void_result build_res,
                                                        struct stress_stats *stats)
{
    if (YETTY_IS_ERR(build_res)) {
        struct yetty_ycore_void_result cleanup_res = yetty_ydrawlist2_destroy(list);
        if (YETTY_IS_ERR(cleanup_res)) {
            yetty_ycore_error_destroy(cleanup_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "stress build", build_res);
    }
    return stress_emit_and_destroy(list, stats);
}

/* Envelope 1: the reservation, the storm, the churn badge, the complexes. */
static struct yetty_ycore_void_result stress_emit_creation(const struct stress_options *options,
                                                           struct stress_stats *stats)
{
    struct yetty_yclass_object_ptr_result list_res = stress_list_new();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "stress creation list");
    struct yetty_yclass_object *list = list_res.value;

    struct yetty_ycore_void_result build_res =
        yetty_ydrawlist2_reserve(list, (uint32_t)options->reserve_px);
    if (YETTY_IS_OK(build_res)) {
        build_res = stress_build_storm(list, options, 0u, stats);
    }
    if (YETTY_IS_OK(build_res)) {
        build_res = stress_build_churn(list, 0u, stats);
    }
    if (YETTY_IS_OK(build_res) && !options->no_complex) {
        build_res = stress_add_plot(list, STRESS_PLOT_WAVE_ID, 760.0f, 16.0f,
                                    "streamed wave (id 100)", 0, stats);
        if (YETTY_IS_OK(build_res)) {
            build_res = stress_add_plot(list, STRESS_PLOT_MATH_ID, 760.0f, 196.0f,
                                        "stream + function (id 101)", 1, stats);
        }
        if (YETTY_IS_OK(build_res)) {
            build_res = stress_add_shadertoy(list, 760.0f, 386.0f, stats);
        }
    }
    return stress_close_list(list, build_res, stats);
}

/* Per-frame storm envelope: GROUP(1) replacement + churn flip. */
static struct yetty_ycore_void_result stress_emit_frame(const struct stress_options *options,
                                                        uint32_t frame_index,
                                                        struct stress_stats *stats)
{
    struct yetty_yclass_object_ptr_result list_res = stress_list_new();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "stress frame list");
    struct yetty_yclass_object *list = list_res.value;

    struct yetty_ycore_void_result build_res =
        stress_build_storm(list, options, frame_index, stats);
    if (YETTY_IS_OK(build_res) && options->churn_every > 0 &&
        frame_index % (uint32_t)options->churn_every == 0u && frame_index > 0u) {
        uint32_t flip = frame_index / (uint32_t)options->churn_every;
        if (flip % 2u == 1u) {
            build_res = yetty_ydrawlist2_delete_group(list, STRESS_GROUP_CHURN);
            stats->records++;
        } else {
            build_res = stress_build_churn(list, flip / 2u, stats);
        }
    }
    return stress_close_list(list, build_res, stats);
}

/* Streaming envelope: ONE CMD_UPDATE per envelope — the documented yplot
 * streaming shape (creation envelope once, then update envelopes). */
static struct yetty_ycore_void_result stress_emit_update(uint32_t plot_id, float phase,
                                                         int wave_kind, struct stress_stats *stats)
{
    struct yetty_yclass_object_ptr_result list_res = stress_list_new();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "stress update list");
    struct yetty_yclass_object *list = list_res.value;
    struct yetty_ycore_void_result build_res =
        stress_plot_update(list, plot_id, phase, wave_kind, stats);
    return stress_close_list(list, build_res, stats);
}

static void stress_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--shapes=N] [--texts=N] [--frames=N] [--period-ms=M]\n"
            "          [--reserve-px=H] [--churn-every=N] [--no-complex] [--no-sleep]\n"
            "          [--double] [--double-every=N] [--max-shapes=N]\n",
            program);
}

static int stress_parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int stress_parse_args(int argc, char **argv, struct stress_options *options)
{
    options->shapes = 400;
    options->texts = 24;
    options->frames = 600;
    options->period_ms = 33;
    options->reserve_px = 560;
    options->churn_every = 20;
    options->no_complex = 0;
    options->no_sleep = 0;
    options->doubling = 0;
    options->double_every = 30;
    options->max_shapes = 1 << 20;

    for (int arg_index = 1; arg_index < argc; arg_index++) {
        const char *argument = argv[arg_index];
        int parsed = 0;
        if (strncmp(argument, "--shapes=", 9) == 0) {
            parsed = stress_parse_int(argument + 9, &options->shapes);
        } else if (strncmp(argument, "--texts=", 8) == 0) {
            parsed = stress_parse_int(argument + 8, &options->texts);
        } else if (strncmp(argument, "--frames=", 9) == 0) {
            parsed = stress_parse_int(argument + 9, &options->frames);
        } else if (strncmp(argument, "--period-ms=", 12) == 0) {
            parsed = stress_parse_int(argument + 12, &options->period_ms);
        } else if (strncmp(argument, "--reserve-px=", 13) == 0) {
            parsed = stress_parse_int(argument + 13, &options->reserve_px);
        } else if (strncmp(argument, "--churn-every=", 14) == 0) {
            parsed = stress_parse_int(argument + 14, &options->churn_every);
        } else if (strncmp(argument, "--double-every=", 15) == 0) {
            parsed = stress_parse_int(argument + 15, &options->double_every);
        } else if (strncmp(argument, "--max-shapes=", 13) == 0) {
            parsed = stress_parse_int(argument + 13, &options->max_shapes);
        } else if (strcmp(argument, "--double") == 0) {
            options->doubling = 1;
        } else if (strcmp(argument, "--no-complex") == 0) {
            options->no_complex = 1;
        } else if (strcmp(argument, "--no-sleep") == 0) {
            options->no_sleep = 1;
        } else {
            parsed = -1;
        }
        if (parsed != 0) {
            stress_usage(argv[0]);
            return -1;
        }
    }
    if (options->shapes < 1 || options->texts < 0 || options->frames < 0 ||
        options->period_ms < 1 || options->reserve_px < 100 || options->double_every < 1 ||
        options->max_shapes < options->shapes) {
        stress_usage(argv[0]);
        return -1;
    }
    return 0;
}

static struct yetty_ycore_void_result stress_run(const struct stress_options *options,
                                                 struct stress_stats *stats)
{
    STRESS_TRY(stress_emit_creation(options, stats));
    for (uint32_t frame_index = 1; options->frames == 0 || frame_index <= (uint32_t)options->frames;
         frame_index++) {
        STRESS_TRY(stress_emit_frame(options, frame_index, stats));
        if (!options->no_complex) {
            float phase = (float)frame_index * 0.11f;
            STRESS_TRY(stress_emit_update(STRESS_PLOT_WAVE_ID, phase, 0, stats));
            STRESS_TRY(stress_emit_update(STRESS_PLOT_MATH_ID, phase, 1, stats));
        }
        if (!options->no_sleep) {
            stress_sleep_ms(options->period_ms);
        }
    }
    return YETTY_OK_VOID();
}

/* Ramp mode: hold each tier for double_every frames, measure the emit-side
 * cost per frame (this is where PTY backpressure shows up — the write
 * blocks when the terminal stops draining), report, then DOUBLE the shape
 * count. Runs until the ceiling or a hard failure. */
static struct yetty_ycore_void_result stress_run_doubling(const struct stress_options *options,
                                                          struct stress_stats *stats)
{
    struct stress_options tier_options = *options;
    STRESS_TRY(stress_emit_creation(&tier_options, stats));
    uint32_t frame_index = 1;
    for (int tier = 0; tier_options.shapes <= options->max_shapes; tier++) {
        double tier_started = stress_now_seconds();
        double emit_seconds = 0.0;
        double emit_worst = 0.0;
        uint64_t tier_records_before = stats->records;
        for (int tier_frame = 0; tier_frame < options->double_every; tier_frame++, frame_index++) {
            double frame_started = stress_now_seconds();
            STRESS_TRY(stress_emit_frame(&tier_options, frame_index, stats));
            if (!tier_options.no_complex) {
                float phase = (float)frame_index * 0.11f;
                STRESS_TRY(stress_emit_update(STRESS_PLOT_WAVE_ID, phase, 0, stats));
                STRESS_TRY(stress_emit_update(STRESS_PLOT_MATH_ID, phase, 1, stats));
            }
            double frame_seconds = stress_now_seconds() - frame_started;
            emit_seconds += frame_seconds;
            if (frame_seconds > emit_worst) {
                emit_worst = frame_seconds;
            }
            if (!tier_options.no_sleep) {
                stress_sleep_ms(tier_options.period_ms);
            }
        }
        double tier_elapsed = stress_now_seconds() - tier_started;
        double average_emit_ms = emit_seconds * 1000.0 / (double)options->double_every;
        uint64_t tier_records = stats->records - tier_records_before;
        /* Both streams on purpose: stdout lands in the terminal under the
         * canvas (watch the ramp live), stderr goes to the log file. */
        printf("tier %2d: %7d shapes/frame — emit avg %7.2f ms, worst %7.2f ms, "
               "%.0f records/s\n",
               tier, tier_options.shapes, average_emit_ms, emit_worst * 1000.0,
               (double)tier_records / tier_elapsed);
        fflush(stdout);
        fprintf(stderr,
                "tier %2d: %7d shapes/frame — emit avg %7.2f ms, worst %7.2f ms, "
                "%.0f records/s\n",
                tier, tier_options.shapes, average_emit_ms, emit_worst * 1000.0,
                (double)tier_records / tier_elapsed);
        if (tier_options.shapes > options->max_shapes / 2) {
            break; /* doubling once more would cross the ceiling */
        }
        tier_options.shapes *= 2;
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct stress_options options;
    if (stress_parse_args(argc, argv, &options) != 0) {
        return 2;
    }

    if (options.doubling) {
        printf("ydraw stress-bomb RAMP: %d shapes doubling every %d frames "
               "(ceiling %d) @ %d ms%s\n",
               options.shapes, options.double_every, options.max_shapes, options.period_ms,
               options.no_complex ? "" : ", 2 plots + shadertoy streaming");
    } else {
        printf("ydraw stress-bomb: %d shapes (%d kinds) + %d texts per frame, "
               "%d frames @ %d ms%s\n",
               options.shapes, STRESS_SHAPE_KIND_COUNT, options.texts, options.frames,
               options.period_ms, options.no_complex ? "" : ", 2 plots + shadertoy streaming");
    }
    fflush(stdout);

    struct stress_stats stats = {0};
    double started = stress_now_seconds();
    struct yetty_ycore_void_result run_res =
        options.doubling ? stress_run_doubling(&options, &stats) : stress_run(&options, &stats);
    double elapsed = stress_now_seconds() - started;

    if (YETTY_IS_ERR(run_res)) {
        fprintf(stderr, "stress-bomb: FAILED: %s\n",
                run_res.error.msg ? run_res.error.msg : "(no message)");
        for (struct yetty_ycore_error *cause = run_res.error.cause; cause; cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg ? cause->msg : "(no message)");
        }
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }

    fprintf(stderr,
            "stress-bomb: done — %llu envelopes, %llu records, %.1f s "
            "(%.0f records/s, %.1f envelopes/s)\n",
            (unsigned long long)stats.envelopes, (unsigned long long)stats.records, elapsed,
            elapsed > 0.0 ? (double)stats.records / elapsed : 0.0,
            elapsed > 0.0 ? (double)stats.envelopes / elapsed : 0.0);
    return 0;
}
