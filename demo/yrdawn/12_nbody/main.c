/*
 * 12_nbody — a real gravitational N-body simulation as a live yrdawn
 * figure: 480 bodies in a rotating disc, softened Newtonian gravity,
 * leapfrog (kick-drift-kick) integration, frames streamed through the
 * bridge at ~30 fps for ~20 seconds. Spiral structure forms on screen
 * within a few seconds.
 *
 * The physics runs client-side; the bridge carries finished frames (the
 * same sustained-throughput path 04_animation exercises). Moving the
 * integrator itself onto the remote GPU via wgpuComputePipeline is the
 * research half of issue #600.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <yetty/yrdawn/client.h>

#include "common.h"

#define FRAME_W 560u
#define FRAME_H 400u
#define BODY_COUNT 480
#define FRAME_COUNT 600 /* ~20 s at 30 fps */
#define FRAME_INTERVAL_MS 33
#define TIME_STEP 0.004f
#define SOFTENING 0.02f

struct nbody_state {
    float position_x[BODY_COUNT];
    float position_y[BODY_COUNT];
    float velocity_x[BODY_COUNT];
    float velocity_y[BODY_COUNT];
    float accel_x[BODY_COUNT];
    float accel_y[BODY_COUNT];
};

/* Deterministic pseudo-random in [0,1) — keeps runs reproducible without
 * touching the platform RNG. */
static float hash_unit(uint32_t seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (float)(seed & 0xFFFFFFu) / 16777216.0f;
}

/* Rotating exponential disc around a heavy central body. */
static void nbody_init(struct nbody_state *state)
{
    state->position_x[0] = 0.0f;
    state->position_y[0] = 0.0f;
    state->velocity_x[0] = 0.0f;
    state->velocity_y[0] = 0.0f;
    for (int body = 1; body < BODY_COUNT; body++) {
        float radius = 0.12f + 0.75f * hash_unit((uint32_t)body * 2654435761u);
        float angle = 2.0f * (float)M_PI * hash_unit((uint32_t)body * 40503u + 7u);
        state->position_x[body] = radius * cosf(angle);
        state->position_y[body] = radius * sinf(angle);
        /* Near-circular orbit speed around the central mass. */
        float speed = sqrtf(0.9f / radius) * (0.92f + 0.16f * hash_unit((uint32_t)body * 97u));
        state->velocity_x[body] = -sinf(angle) * speed;
        state->velocity_y[body] = cosf(angle) * speed;
    }
}

static void nbody_accelerations(struct nbody_state *state)
{
    /* Central mass dominates; body-body forces make the structure. The
     * O(N²) pass at 480 bodies is ~115k interactions — trivially real-time
     * on the CPU. Mass 0 (center) is 600× a disc body. */
    for (int body = 0; body < BODY_COUNT; body++) {
        state->accel_x[body] = 0.0f;
        state->accel_y[body] = 0.0f;
    }
    for (int first = 0; first < BODY_COUNT; first++) {
        float first_mass = first == 0 ? 0.9f : 0.0015f;
        for (int second = first + 1; second < BODY_COUNT; second++) {
            float second_mass = second == 0 ? 0.9f : 0.0015f;
            float delta_x = state->position_x[second] - state->position_x[first];
            float delta_y = state->position_y[second] - state->position_y[first];
            float distance_sq = delta_x * delta_x + delta_y * delta_y + SOFTENING * SOFTENING;
            float inverse_distance = 1.0f / sqrtf(distance_sq);
            float inverse_cubed = inverse_distance / distance_sq;
            state->accel_x[first] += second_mass * delta_x * inverse_cubed;
            state->accel_y[first] += second_mass * delta_y * inverse_cubed;
            state->accel_x[second] -= first_mass * delta_x * inverse_cubed;
            state->accel_y[second] -= first_mass * delta_y * inverse_cubed;
        }
    }
}

static void nbody_step(struct nbody_state *state)
{
    /* kick-drift-kick */
    for (int body = 0; body < BODY_COUNT; body++) {
        state->velocity_x[body] += state->accel_x[body] * TIME_STEP * 0.5f;
        state->velocity_y[body] += state->accel_y[body] * TIME_STEP * 0.5f;
        state->position_x[body] += state->velocity_x[body] * TIME_STEP;
        state->position_y[body] += state->velocity_y[body] * TIME_STEP;
    }
    nbody_accelerations(state);
    for (int body = 0; body < BODY_COUNT; body++) {
        state->velocity_x[body] += state->accel_x[body] * TIME_STEP * 0.5f;
        state->velocity_y[body] += state->accel_y[body] * TIME_STEP * 0.5f;
    }
}

/* Additive splat with trails: fade the previous frame, then stamp a 3×3
 * kernel per body. Warm center, cooler disc. */
static void nbody_draw(const struct nbody_state *state, uint8_t *pixels)
{
    for (size_t i = 0; i < (size_t)FRAME_W * FRAME_H; i++) {
        uint8_t *pixel = pixels + i * 4;
        pixel[0] = (uint8_t)(pixel[0] * 9 / 10);
        pixel[1] = (uint8_t)(pixel[1] * 9 / 10);
        pixel[2] = (uint8_t)(pixel[2] * 87 / 100);
        pixel[3] = 255;
    }
    for (int body = 0; body < BODY_COUNT; body++) {
        float scale = (float)FRAME_H * 0.42f;
        int center_x = (int)((float)FRAME_W * 0.5f + state->position_x[body] * scale);
        int center_y = (int)((float)FRAME_H * 0.5f + state->position_y[body] * scale);
        int brightness = body == 0 ? 255 : 150;
        for (int offset_y = -1; offset_y <= 1; offset_y++) {
            for (int offset_x = -1; offset_x <= 1; offset_x++) {
                int x = center_x + offset_x;
                int y = center_y + offset_y;
                if (x < 0 || y < 0 || x >= (int)FRAME_W || y >= (int)FRAME_H) {
                    continue;
                }
                int falloff = (offset_x == 0 && offset_y == 0) ? brightness : brightness / 3;
                uint8_t *pixel = pixels + ((size_t)y * FRAME_W + (size_t)x) * 4;
                int red = pixel[0] + falloff;
                int green = pixel[1] + falloff * (body == 0 ? 3 : 4) / 5;
                int blue = pixel[2] + falloff * (body == 0 ? 2 : 3) / 5;
                pixel[0] = (uint8_t)(red > 255 ? 255 : red);
                pixel[1] = (uint8_t)(green > 255 ? 255 : green);
                pixel[2] = (uint8_t)(blue > 255 ? 255 : blue);
            }
        }
    }
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("12-nbody");
#define LOG(...)                                                                                   \
    do {                                                                                           \
        if (trace)                                                                                 \
            fprintf(trace, __VA_ARGS__);                                                           \
    } while (0)

    struct yetty_yrdawn_client *client = NULL;
    struct yetty_yrdawn_canvas *canvas = demo_bringup_single_canvas(
        /*figure_id=*/1, (float)FRAME_W, (float)FRAME_H, trace, &client);
    if (!canvas) {
        LOG("12_nbody: bringup failed\n");
        return 1;
    }

    struct nbody_state *state = calloc(1, sizeof(struct nbody_state));
    uint8_t *pixels = calloc((size_t)FRAME_W * FRAME_H, 4);
    if (!state || !pixels) {
        LOG("12_nbody: oom\n");
        free(state);
        free(pixels);
        return 1;
    }
    nbody_init(state);
    nbody_accelerations(state);

    int failures = 0;
    for (int frame = 0; frame < FRAME_COUNT && !demo_quit_flag; frame++) {
        /* A few physics substeps per displayed frame keeps orbits tight. */
        for (int substep = 0; substep < 3; substep++) {
            nbody_step(state);
        }
        nbody_draw(state, pixels);
        struct yetty_ycore_void_result present_res = yetty_yrdawn_canvas_present_frame(
            canvas, FRAME_W, FRAME_H, pixels, (size_t)FRAME_W * FRAME_H * 4u);
        if (present_res.ok != 1) {
            LOG("12_nbody: frame %d present failed: %s\n", frame, present_res.error.msg);
            if (++failures > 3) {
                break;
            }
        }
        (void)yetty_yrdawn_client_pump(client);
        demo_sleep_ms(FRAME_INTERVAL_MS);
    }

    free(state);
    free(pixels);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(client);
    if (trace) {
        fclose(trace);
    }
    return 0;
}
