/*
 * ymesh — emit a ymesh complex-prim OSC envelope for a glTF 2.0 (.glb) file.
 *
 * Two operating modes (mirroring ynetsurf):
 *
 *   ONE-SHOT (default when stdout isn't a TTY, or with --once):
 *     parse the .glb, emit one OSC envelope, exit. Same shape as yplot,
 *     yimage, ynetsurf --once.
 *
 *   INTERACTIVE (default when stdin + stdout are both TTYs, or with -i):
 *     parse the .glb once, then sit in a select() loop:
 *       - subscribe to terminal-wide mouse + keyboard via
 *         YMGUI_OSC_CS_TERM_INPUT_SUB
 *       - left-drag      → orbit camera (azimuth/elevation)
 *       - right-drag     → pan
 *       - middle-drag    → pan
 *       - mouse wheel    → zoom (dolly)
 *       - W              → toggle wireframe ↔ solid
 *       - F              → frame all (reset camera)
 *       - R              → reset rotation only
 *       - Q / ESC / ^C   → quit
 *     On any state change re-emit OSC 600000 (ydraw clear) + OSC 600001
 *     (bin) so the receiving ydraw-layer replaces instead of stacks.
 */

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <yetty/ymesh/ymesh.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterm/osc-codes.h>

/*=============================================================================
 * Output side — one-shot emit + interactive clear+bin re-render.
 *===========================================================================*/

static int emit_envelope(int osc_code, int compressed,
                         const void *args, size_t args_len,
                         const void *body, size_t body_len)
{
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        osc_code, compressed, args, args_len, body, body_len, &env);
    int rc = 0;
    if (YETTY_IS_OK(r) && env.size > 0)
        fwrite(env.data, 1, env.size, stdout);
    else if (YETTY_IS_ERR(r))
        rc = 1;
    yetty_ycore_buffer_destroy(&env);
    return rc;
}

static int emit_bin_osc(const uint8_t *bytes, size_t blen)
{
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = blen,
        .reserved = {0, 0},
    };
    return emit_envelope(YETTY_OSC_YDRAW_BIN, /*compressed=*/1,
                         &meta, sizeof(meta), bytes, blen);
}

/*=============================================================================
 * TTY raw mode — saved + restored around the interactive loop.
 *===========================================================================*/

static struct termios saved_tty;
static int tty_active = 0;

static void tty_restore(void)
{
    if (tty_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
        tty_active = 0;
    }
}

static int tty_raw(void)
{
    if (!isatty(STDIN_FILENO)) return 0;
    if (tcgetattr(STDIN_FILENO, &saved_tty) < 0) return -1;
    struct termios raw = saved_tty;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) return -1;
    tty_active = 1;
    atexit(tty_restore);
    return 0;
}

static volatile sig_atomic_t signal_quit = 0;
static void on_signal(int sig) { (void)sig; signal_quit = 1; }

/*=============================================================================
 * Interactive event state.
 *===========================================================================*/

struct ev_state {
    /* Cached file bytes — re-fed through yetty_ymesh_render() each frame.
     * Avoids re-reading from disk; cgltf parse is fast enough that we don't
     * cache the parsed mesh either. */
    uint8_t *glb_bytes;
    size_t   glb_len;

    /* Display bounds in pane pixels. */
    float bounds_w, bounds_h;

    /* Camera state. */
    float azimuth;
    float elevation;
    float dist_factor;
    float pan_x, pan_y;
    uint32_t mode;

    /* Drag tracking (pane-pixel coords last seen). */
    int   buttons_held;   /* bitmask: 1<<button */
    float last_x, last_y;
    int   drag_active;

    /* Loop control. */
    int dirty;
    int want_quit;
};

static void state_reset_camera(struct ev_state *st)
{
    st->azimuth = 0.7f;
    st->elevation = 0.5f;
    st->dist_factor = 3.0f;
    st->pan_x = 0.0f;
    st->pan_y = 0.0f;
}

/*=============================================================================
 * Render: build a fresh ydraw buffer from the cached .glb + current camera,
 * emit OSC clear + bin so the receiving layer replaces instead of stacks.
 *===========================================================================*/

static int redraw_and_push(struct ev_state *st)
{
    struct yetty_ymesh_render_config cfg = {
        .bounds_w   = st->bounds_w,
        .bounds_h   = st->bounds_h,
        .azimuth    = st->azimuth,
        .elevation  = st->elevation,
        .dist_factor = st->dist_factor,
        .pan_x      = st->pan_x,
        .pan_y      = st->pan_y,
        .mode       = st->mode,
    };
    struct yetty_ydraw_draw_list_result br =
        yetty_ymesh_render(st->glb_bytes, st->glb_len, &cfg);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "ymesh: render failed: %s\n", br.error.msg);
        return 1;
    }

    const uint8_t *bytes = NULL;
    size_t blen = yetty_ydraw_draw_list_serialize(br.value, &bytes);
    (void)emit_envelope(YETTY_OSC_YDRAW_CLEAR, 0, NULL, 0, NULL, 0);
    (void)emit_bin_osc(bytes, blen);
    fflush(stdout);

    yetty_ydraw_draw_list_destroy(br.value);
    return 0;
}

/*=============================================================================
 * Mouse handling.
 *===========================================================================*/

/* Sensitivity tuning — picked to feel comfortable for typical pane sizes
 * (≈600px). Scales linearly with drag delta in pixels. */
#define YMESH_ORBIT_SENS  0.01f   /* radians per pixel */
#define YMESH_PAN_SENS    1.0f    /* world-pixels per pane-pixel */
#define YMESH_ZOOM_STEP   0.10f   /* dist_factor multiplier per wheel notch */

static void handle_mouse_button(struct ev_state *st,
                                const struct yetty_ymgui_wire_input_mouse *m)
{
    if (m->pressed) {
        st->buttons_held |= (1u << m->button);
        st->last_x = m->x;
        st->last_y = m->y;
        st->drag_active = 1;
    } else {
        st->buttons_held &= ~(1u << m->button);
        if (st->buttons_held == 0)
            st->drag_active = 0;
    }
}

static void handle_mouse_move(struct ev_state *st,
                              const struct yetty_ymgui_wire_input_mouse *m)
{
    /* Use the wire's buttons_held when in a POS event — its bitmask is the
     * authority on what's still pressed (handles missed-button edges). */
    uint32_t held = m->buttons_held ? m->buttons_held : (uint32_t)st->buttons_held;
    if (!held || !st->drag_active) {
        st->last_x = m->x;
        st->last_y = m->y;
        return;
    }
    float dx = m->x - st->last_x;
    float dy = m->y - st->last_y;
    st->last_x = m->x;
    st->last_y = m->y;
    if (dx == 0.0f && dy == 0.0f) return;

    /* Button 0 = left → orbit. Buttons 1 (right) or 2 (middle) → pan. */
    if (held & 0x1u) {
        /* Orbit: x-drag rotates around Y (azimuth), y-drag tilts (elevation).
         * Negate azimuth so the mesh follows the cursor direction. */
        st->azimuth   -= dx * YMESH_ORBIT_SENS;
        st->elevation += dy * YMESH_ORBIT_SENS;
    } else if (held & 0x6u) {
        st->pan_x += dx * YMESH_PAN_SENS;
        st->pan_y -= dy * YMESH_PAN_SENS;  /* screen-y is down, world-y is up */
    }
    st->dirty = 1;
}

static void handle_mouse_wheel(struct ev_state *st,
                               const struct yetty_ymgui_wire_input_mouse *m)
{
    /* Each wheel notch ~ 1.0; positive = up = zoom in (closer). */
    float factor = 1.0f - m->wheel_dy * YMESH_ZOOM_STEP;
    if (factor < 0.1f) factor = 0.1f;  /* clamp to avoid crossing the origin */
    st->dist_factor *= factor;
    if (st->dist_factor < 0.2f)  st->dist_factor = 0.2f;
    if (st->dist_factor > 50.0f) st->dist_factor = 50.0f;
    st->dirty = 1;
}

/*=============================================================================
 * Keyboard handling — both via OSC (YMGUI_OSC_SC_TERM_KEY) and raw TTY
 * bytes. The two sources may both fire for the same keystroke; both paths
 * just set state.dirty, so duplicates are harmless.
 *===========================================================================*/

static void handle_key_codepoint(struct ev_state *st, uint32_t cp)
{
    switch (cp) {
    case 'q': case 'Q':
    case 0x03: case 0x1b:    /* Ctrl-C / ESC */
        st->want_quit = 1;
        break;
    case 'w': case 'W':
        st->mode = (st->mode == YETTY_YMESH_MODE_WIREFRAME)
                 ? YETTY_YMESH_MODE_SOLID : YETTY_YMESH_MODE_WIREFRAME;
        st->dirty = 1;
        break;
    case 'f': case 'F':
        state_reset_camera(st);
        st->mode = YETTY_YMESH_MODE_SOLID;
        st->dirty = 1;
        break;
    case 'r': case 'R':
        st->azimuth = 0.7f;
        st->elevation = 0.5f;
        st->pan_x = st->pan_y = 0.0f;
        st->dirty = 1;
        break;
    /* Arrow keys via raw TTY arrive as ESC [ A/B/C/D — handled in on_raw. */
    default: break;
    }
}

/*=============================================================================
 * yface callbacks.
 *===========================================================================*/

static void on_osc(void *user, int osc_code,
                   const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args; (void)args_len;
    struct ev_state *st = user;

    if (osc_code == YMGUI_OSC_SC_MOUSE || osc_code == YMGUI_OSC_SC_TERM_MOUSE) {
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_mouse)) return;
        const struct yetty_ymgui_wire_input_mouse *m =
            (const struct yetty_ymgui_wire_input_mouse *)payload;
        switch (m->kind) {
        case YETTY_YMGUI_INPUT_MOUSE_BUTTON: handle_mouse_button(st, m); break;
        case YETTY_YMGUI_INPUT_MOUSE_POS:    handle_mouse_move(st, m);   break;
        case YETTY_YMGUI_INPUT_MOUSE_WHEEL:  handle_mouse_wheel(st, m);  break;
        }
        return;
    }

    if (osc_code == YMGUI_OSC_SC_KEY || osc_code == YMGUI_OSC_SC_TERM_KEY) {
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_key)) return;
        const struct yetty_ymgui_wire_input_key *k =
            (const struct yetty_ymgui_wire_input_key *)payload;
        if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint)
            handle_key_codepoint(st, k->codepoint);
        return;
    }
}

/* Raw-byte handler: ASCII shortcuts + arrow-key ESC sequences.
 * Arrow keys give a coarse keyboard alternative to mouse drag. */
static void on_raw(void *user, const char *bytes, size_t n)
{
    struct ev_state *st = user;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)bytes[i];

        /* CSI arrow: ESC [ A/B/C/D. Look-ahead 2 bytes if we have them. */
        if (c == 0x1b && i + 2 < n &&
            bytes[i + 1] == '[' &&
            (bytes[i + 2] == 'A' || bytes[i + 2] == 'B' ||
             bytes[i + 2] == 'C' || bytes[i + 2] == 'D')) {
            switch (bytes[i + 2]) {
            case 'A': st->elevation += 0.1f; break;  /* up */
            case 'B': st->elevation -= 0.1f; break;  /* down */
            case 'C': st->azimuth   -= 0.1f; break;  /* right */
            case 'D': st->azimuth   += 0.1f; break;  /* left */
            }
            st->dirty = 1;
            i += 2;
            continue;
        }

        /* Lone ESC at end-of-read: quit. (Happens on real ESC, since the
         * arrow case requires the trailing bytes to already be in `bytes`.) */
        handle_key_codepoint(st, c);
    }
}

/*=============================================================================
 * Terminal-wide input subscription — same pattern as ynetsurf.
 *===========================================================================*/

static void term_input_subscribe(uint32_t flags)
{
    struct yetty_ymgui_wire_term_input_sub msg = {
        .magic = YMGUI_WIRE_MAGIC_TERM_INPUT_SUB,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
        ._pad0 = 0,
    };
    (void)emit_envelope(YMGUI_OSC_CS_TERM_INPUT_SUB, /*compressed=*/0,
                        NULL, 0, &msg, sizeof(msg));
}

/*=============================================================================
 * Interactive loop.
 *===========================================================================*/

static int interactive_loop(struct ev_state *st)
{
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (YETTY_IS_ERR(yr)) {
        fprintf(stderr, "yface_create: %s\n", yr.error.msg);
        return 1;
    }
    struct yetty_yface *yface = yr.value;
    yetty_yface_set_handlers(yface, on_osc, on_raw, st);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP,  on_signal);

    if (tty_raw() < 0) {
        fprintf(stderr, "ymesh: cannot put stdin into raw mode\n");
        yetty_yface_destroy(yface);
        return 1;
    }

    term_input_subscribe(YETTY_YMGUI_TERM_SUB_MOUSE_CLICK |
                         YETTY_YMGUI_TERM_SUB_MOUSE_MOVE  |
                         YETTY_YMGUI_TERM_SUB_MOUSE_WHEEL |
                         YETTY_YMGUI_TERM_SUB_KEY);
    fflush(stdout);

    (void)redraw_and_push(st);

    char buf[4096];
    while (!signal_quit && !st->want_quit) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };  /* 100ms */
        int rdy = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (rdy < 0) {
            if (errno == EINTR) continue;
            break;
        }
        st->dirty = 0;
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) (void)yetty_yface_feed_bytes(yface, buf, (size_t)n);
            else if (n == 0 && !isatty(STDIN_FILENO)) break;
        }
        if (st->dirty)
            (void)redraw_and_push(st);
    }

    /* Unsubscribe so the user's next foreground program doesn't inherit
     * the term-wide forwarding state. Then clear the pane on exit so the
     * mesh disappears with the viewer (instead of leaving a stuck frame). */
    term_input_subscribe(0);
    (void)emit_envelope(YETTY_OSC_YDRAW_CLEAR, 0, NULL, 0, NULL, 0);
    fflush(stdout);

    yetty_yface_destroy(yface);
    return 0;
}

/*=============================================================================
 * One-shot emit (legacy default for non-TTY stdout).
 *===========================================================================*/

static int oneshot(const char *path, float bounds_w, float bounds_h)
{
    struct yetty_ymesh_render_config cfg = {
        .bounds_w = bounds_w, .bounds_h = bounds_h,
    };
    struct yetty_ydraw_draw_list_result br =
        yetty_ymesh_render_path(path, &cfg);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "ymesh: %s\n", br.error.msg);
        return 1;
    }
    struct yetty_ycore_size_result sr = yetty_ymesh_osc_bin_emit(br.value, stdout);
    yetty_ydraw_draw_list_destroy(br.value);
    if (YETTY_IS_ERR(sr)) {
        fprintf(stderr, "ymesh: emit failed: %s\n", sr.error.msg);
        return 1;
    }
    fputc('\n', stdout);
    return 0;
}

/*=============================================================================
 * Loader for the interactive mode — slurp the whole .glb into memory.
 *===========================================================================*/

static int load_glb(const char *path, uint8_t **out_bytes, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ymesh: cannot open %s\n", path); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return 1; }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 1; }
    fclose(f);
    *out_bytes = buf;
    *out_len = (size_t)sz;
    return 0;
}

/*=============================================================================
 * main
 *===========================================================================*/

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] <path-to-glb>\n"
        "\n"
        "Render a glTF 2.0 (.glb) mesh inside the host yetty terminal.\n"
        "\n"
        "Modes:\n"
        "  default     interactive when stdin+stdout are TTYs, one-shot otherwise\n"
        "  -i, --interactive   force interactive\n"
        "      --once           force one-shot\n"
        "\n"
        "Options:\n"
        "  -w <px>     Display width in pixels  (default 600)\n"
        "  -H <px>     Display height in pixels (default 600)\n"
        "  -h, --help  Show this help\n"
        "\n"
        "Interactive controls:\n"
        "  left-drag   orbit (azimuth, elevation)\n"
        "  right-drag  pan\n"
        "  wheel       zoom (dolly)\n"
        "  arrow keys  step orbit (keyboard fallback)\n"
        "  W           toggle wireframe ↔ solid\n"
        "  F           frame all (reset camera)\n"
        "  R           reset rotation only\n"
        "  Q / ESC     quit\n"
        "\n"
        "Example:\n"
        "  yetty -e '%s -i Box.glb'\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    float bounds_w = 600.0f;
    float bounds_h = 600.0f;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (!strcmp(a, "-i") || !strcmp(a, "--interactive")) interactive = 1;
        else if (!strcmp(a, "--once")) interactive = 0;
        else if (!strcmp(a, "-w") && i + 1 < argc) bounds_w = (float)atof(argv[++i]);
        else if (!strcmp(a, "-H") && i + 1 < argc) bounds_h = (float)atof(argv[++i]);
        else if (a[0] == '-') {
            fprintf(stderr, "ymesh: unknown option %s\n", a);
            usage(stderr, argv[0]);
            return 2;
        } else {
            path = a;
        }
    }
    if (!path) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (!interactive) {
        return oneshot(path, bounds_w, bounds_h);
    }

    /* Interactive mode. */
    struct ev_state st = {0};
    st.bounds_w = bounds_w;
    st.bounds_h = bounds_h;
    state_reset_camera(&st);
    st.mode = YETTY_YMESH_MODE_SOLID;

    if (load_glb(path, &st.glb_bytes, &st.glb_len) != 0) {
        return 1;
    }

    int rc = interactive_loop(&st);
    free(st.glb_bytes);
    return rc;
}
