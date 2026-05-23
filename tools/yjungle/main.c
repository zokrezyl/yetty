/*
 * yjungle — incremental-update test scene for scene-canvas.
 *
 * The yetty_yjungle library produces an incremental ydraw command
 * stream: GROUP / DELETE / CMD_ZERO records targeting named entities
 * inside the receiver's scene-canvas. This tool drives time, owns the
 * envelope ydraw_list, watches for pane resize / quit keys via yface,
 * and ships envelopes to stdout on YETTY_OSC_YCOMPOSITOR_BIN.
 *
 * Modelled on tools/yzoo/main.c. Key differences:
 *   - Targets the SCENE_BIN OSC code so the receiver routes to the
 *     scene-canvas variant (not the scrolling-canvas).
 *   - Sends an envelope ONLY when yjungle_tick produced bytes (i.e.
 *     when an event fired this tick), so the wire stays quiet between
 *     events. yzoo, by contrast, ships every frame.
 */

#include <yetty/yjungle/yjungle.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterm/client-input.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yterm/osc-codes.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/yplatform/compat.h> /* clock_gettime shim on MSVC */
#include <yetty/yplatform/tty.h>

/*=============================================================================
 * Output side — emit OSC envelopes via yface_emit.
 *===========================================================================*/

static struct yetty_ycore_void_result
emit_envelope(int osc_code, int compressed,
              const void *args, size_t args_len,
              const void *body, size_t body_len)
{
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        osc_code, compressed, args, args_len, body, body_len, &env);
    if (YETTY_IS_OK(r) && env.size > 0) {
        fwrite(env.data, 1, env.size, stdout);
    }
    yetty_ycore_buffer_destroy(&env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_envelope: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Ship the contents of `buf` as a YCOMPOSITOR_BIN envelope. Returns
 * OK and emits nothing if the serialised buffer carries no commands
 * (i.e. only the 24-byte framed-envelope header — no actual prims). */
static struct yetty_ycore_void_result
emit_scene_bin(struct yetty_ydraw_draw_list *buf)
{
    if (yetty_ydraw_draw_list_size(buf) == 0u) {
        /* Quiet tick — yjungle had no event this frame. */
        return YETTY_OK_VOID();
    }

    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_draw_list_serialize(buf, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "emit_scene_bin: serialize empty");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result r =
        emit_envelope(YETTY_OSC_YCOMPOSITOR_BIN, /*compressed=*/1,
                      &meta, sizeof(meta), raw, raw_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_scene_bin");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_clear(void)
{
    return emit_envelope(YETTY_OSC_YDRAW_CLEAR, 0, NULL, 0, NULL, 0);
}

static struct yetty_ycore_void_result term_input_subscribe(uint32_t flags)
{
    struct yetty_client_input_sub msg = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
        ._pad0 = 0,
    };
    return emit_envelope(YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0,
                         NULL, 0, &msg, sizeof(msg));
}

/*=============================================================================
 * Alternate screen buffer (raw mode is handled by the platform tty API).
 *===========================================================================*/

static int alt_screen_active = 0;

static void alt_screen_leave(void)
{
    if (alt_screen_active) {
        static const char seq[] = "\033[?1049l";
        fwrite(seq, 1, sizeof(seq) - 1, stdout);
        fflush(stdout);
        alt_screen_active = 0;
    }
}

static void alt_screen_enter(void)
{
    if (!yetty_yplatform_tty_stdout_is_tty()) {
        return;
    }
    static const char seq[] = "\033[?1049h";
    fwrite(seq, 1, sizeof(seq) - 1, stdout);
    fflush(stdout);
    alt_screen_active = 1;
    atexit(alt_screen_leave);
}

static volatile sig_atomic_t signal_quit = 0;

YETTY_EXTERNAL_CALLBACK
static void on_signal(int sig) { (void)sig; signal_quit = 1; }

/*=============================================================================
 * Time helper — monotonic milliseconds since first call.
 *===========================================================================*/

static uint64_t monotonic_ms(void)
{
    static int initialized = 0;
    static struct timespec start;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!initialized) {
        start = ts;
        initialized = 1;
    }
    int64_t sec = (int64_t)(ts.tv_sec - start.tv_sec);
    int64_t nsec = (int64_t)(ts.tv_nsec - start.tv_nsec);
    return (uint64_t)(sec * 1000 + nsec / 1000000);
}

/*=============================================================================
 * App state — collected so yface callbacks can mutate it via void *user.
 *===========================================================================*/

struct yjungle_app {
    struct yetty_yjungle         *jungle;
    struct yetty_ydraw_draw_list *buf;

    float pane_w;
    float pane_h;
    bool  have_pane_size;

    bool want_quit;
};

static struct yetty_ycore_void_result
apply_pane_size(struct yjungle_app *app, float w, float h)
{
    if (w < 1.0f || h < 1.0f) {
        return YETTY_OK_VOID();
    }
    if (app->have_pane_size && w == app->pane_w && h == app->pane_h) {
        return YETTY_OK_VOID();
    }
    app->pane_w = w;
    app->pane_h = h;
    app->have_pane_size = true;
    struct yetty_ycore_void_result r = yetty_yjungle_set_scene_size(app->jungle, w, h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "apply_pane_size: set_scene_size");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Key handling — codepoint dispatch, shared by raw bytes + wire CHAR events.
 *===========================================================================*/

static void on_key_codepoint(struct yjungle_app *app, uint32_t cp)
{
    switch (cp) {
    case 'q': case 'Q':
    case 0x03:    /* Ctrl-C */
    case 0x1b:    /* ESC */
        app->want_quit = 1;
        break;
    default:
        break;
    }
}

/*=============================================================================
 * yface callbacks.
 *===========================================================================*/

YETTY_EXTERNAL_CALLBACK
static void on_osc(void *user, int osc_code,
                   const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yjungle_app *app = user;

    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE || osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE) {
        if (payload_len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *r =
            (const struct yetty_client_input_resize *)payload;
        struct yetty_ycore_void_result ar = apply_pane_size(app, r->width, r->height);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
        }
        return;
    }

    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY || osc_code == YETTY_OSC_SC_CLIENT_INPUT_KEY) {
        if (payload_len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *k =
            (const struct yetty_client_input_key *)payload;
        if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint) {
            on_key_codepoint(app, k->codepoint);
        }
        return;
    }
}

YETTY_EXTERNAL_CALLBACK
static void on_raw(void *user, const char *bytes, size_t n)
{
    struct yjungle_app *app = user;
    for (size_t i = 0; i < n; i++) {
        on_key_codepoint(app, (unsigned char)bytes[i]);
    }
}

/*=============================================================================
 * Driving loop.
 *===========================================================================*/

static struct yetty_ycore_void_result tick(struct yjungle_app *app)
{
    uint64_t now = monotonic_ms();
    struct yetty_ycore_void_result tr = yetty_yjungle_tick(app->jungle, app->buf, now);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "tick: yjungle_tick");
    struct yetty_ycore_void_result er = emit_scene_bin(app->buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "tick: emit_scene_bin");
    fflush(stdout);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * CLI parsing.
 *===========================================================================*/

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Random connected SDF chain — emits incremental scene-canvas OSC envelopes to stdout.\n"
        "Tests CMD_GROUP / CMD_DELETE / CMD_ZERO handling against scene-canvas.\n"
        "\n"
        "Chain shape:\n"
        "  --initial-chain N        initial chain length          (default 8)\n"
        "  --max-chain N            cap on chain length           (default 30)\n"
        "  --extend-prob F          chance an event is extend     (0..1, default 0.30)\n"
        "  --step-min F             min random-walk step (px)     (default 40)\n"
        "  --step-max F             max random-walk step (px)     (default 140)\n"
        "  --off-canvas-margin F    permitted overshoot (px)      (default 60)\n"
        "\n"
        "Group nesting:\n"
        "  --max-depth N            max group nesting depth       (default 3, capped at 6)\n"
        "  --group-prob F           group probability at depth 0  (0..1, default 0.5,\n"
        "                           halves per deeper level)\n"
        "  --group-children-min N   sub-segments per group        (default 2)\n"
        "  --group-children-max N   sub-segments per group        (default 3)\n"
        "\n"
        "Event cadence:\n"
        "  --interval-min MS        min ms between events         (default 500)\n"
        "  --interval-max MS        max ms between events         (default 2500)\n"
        "\n"
        "Frontend:\n"
        "  -w, --width PX           scene width fallback          (default 800)\n"
        "  -H, --height PX          scene height fallback         (default 600)\n"
        "      --seed N             RNG seed (default = clock)\n"
        "  -h, --help               show this help\n"
        "\n"
        "Interactive controls:\n"
        "  q / Q / ESC / Ctrl-C     quit\n",
        prog);
}

int main(int argc, char **argv)
{
    struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
    uint32_t seed = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout, argv[0]);
            return 0;
        } else if (!strcmp(a, "--initial-chain") && i + 1 < argc) {
            cfg.initial_chain_length = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--max-chain") && i + 1 < argc) {
            cfg.max_chain_length = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--extend-prob") && i + 1 < argc) {
            cfg.extend_probability = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--step-min") && i + 1 < argc) {
            cfg.step_min = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--step-max") && i + 1 < argc) {
            cfg.step_max = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--off-canvas-margin") && i + 1 < argc) {
            cfg.off_canvas_margin = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--max-depth") && i + 1 < argc) {
            cfg.max_depth = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--group-prob") && i + 1 < argc) {
            cfg.group_prob_depth0 = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--group-children-min") && i + 1 < argc) {
            cfg.group_children_min = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--group-children-max") && i + 1 < argc) {
            cfg.group_children_max = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--interval-min") && i + 1 < argc) {
            cfg.event_interval_ms_min = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--interval-max") && i + 1 < argc) {
            cfg.event_interval_ms_max = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if ((!strcmp(a, "-w") || !strcmp(a, "--width")) && i + 1 < argc) {
            cfg.scene_width = (float)atof(argv[++i]);
        } else if ((!strcmp(a, "-H") || !strcmp(a, "--height")) && i + 1 < argc) {
            cfg.scene_height = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--seed") && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "yjungle: unknown option %s\n", a);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, seed);
    if (YETTY_IS_ERR(jr)) {
        fprintf(stderr, "yjungle: %s\n", jr.error.msg);
        yetty_ycore_error_destroy(jr.error);
        return 1;
    }

    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = cfg.scene_width,
        .scene_max_y = cfg.scene_height,
    };
    struct yetty_ydraw_draw_list_result br =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "yjungle: %s\n", br.error.msg);
        yetty_ycore_error_destroy(br.error);
        yetty_yjungle_destroy(jr.value);
        return 1;
    }

    struct yjungle_app app = {
        .jungle         = jr.value,
        .buf            = br.value,
        .pane_w         = cfg.scene_width,
        .pane_h         = cfg.scene_height,
        .have_pane_size = false,
        .want_quit      = false,
    };

    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (YETTY_IS_ERR(yr)) {
        fprintf(stderr, "yjungle: yface_create: %s\n", yr.error.msg);
        yetty_ycore_error_destroy(yr.error);
        yetty_ydraw_draw_list_destroy(app.buf);
        yetty_yjungle_destroy(app.jungle);
        return 1;
    }
    struct yetty_yface *yface = yr.value;
    yetty_yface_set_handlers(yface, on_osc, on_raw, &app);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGHUP
    signal(SIGHUP,  on_signal);
#endif

    yetty_yplatform_tty_binary_io();
    if (yetty_yplatform_tty_set_raw() < 0) {
        fprintf(stderr, "yjungle: cannot put stdin into raw mode\n");
        yetty_yface_destroy(yface);
        yetty_ydraw_draw_list_destroy(app.buf);
        yetty_yjungle_destroy(app.jungle);
        return 1;
    }
    atexit(yetty_yplatform_tty_restore);

    alt_screen_enter();

    {
        struct yetty_ycore_void_result sr =
            term_input_subscribe(YETTY_CLIENT_INPUT_SUB_KEY);
        if (YETTY_IS_ERR(sr)) {
            fprintf(stderr, "yjungle: subscribe: %s\n", sr.error.msg);
            yetty_ycore_error_destroy(sr.error);
        }
    }
    fflush(stdout);

    /* Force the first tick now so the receiver gets CMD_ZERO + initial
     * chain right away, without waiting for the first event-interval. */
    {
        struct yetty_ycore_void_result fr = tick(&app);
        if (YETTY_IS_ERR(fr)) {
            fprintf(stderr, "yjungle: initial tick: %s\n", fr.error.msg);
            yetty_ycore_error_destroy(fr.error);
        }
    }

    char ibuf[4096];
    while (!signal_quit && !app.want_quit) {
        /* ~33ms wakeup — fine granularity for the event scheduler;
         * yjungle_tick is a no-op between events so this is cheap. */
        int rdy = yetty_yplatform_tty_stdin_wait(33);
        if (rdy < 0) {
            break;
        }
        if (rdy > 0) {
            int n = yetty_yplatform_tty_stdin_read(ibuf, sizeof(ibuf));
            if (n < 0) {
                break;
            }
            if (n == 0) {
                if (!yetty_yplatform_tty_stdin_is_tty()) {
                    break;
                }
                /* tty stdin with no bytes — keep ticking. */
            } else {
                struct yetty_ycore_void_result fr =
                    yetty_yface_feed_bytes(yface, ibuf, (size_t)n);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
        }
        struct yetty_ycore_void_result dr = tick(&app);
        if (YETTY_IS_ERR(dr)) {
            fprintf(stderr, "yjungle: tick: %s\n", dr.error.msg);
            yetty_ycore_error_destroy(dr.error);
            app.want_quit = true;
        }
    }

    {
        struct yetty_ycore_void_result sr = term_input_subscribe(0);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
        struct yetty_ycore_void_result cr = emit_clear();
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    fflush(stdout);
    alt_screen_leave();

    yetty_yface_destroy(yface);
    yetty_ydraw_draw_list_destroy(app.buf);
    yetty_yjungle_destroy(app.jungle);
    return 0;
}
