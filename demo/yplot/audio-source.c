/*
 * audio-source — synthetic audio emitter for yplot buffer-mode demos.
 *
 * Emulates an audio capture source. Two-phase output:
 *
 *   1) ONE creation envelope (YDRAW_SCENE_BIN). Opens a GROUP with a stable
 *      external id and adds a yplot prim inside it:
 *        - yfsvm-compiled math reference  ref = sin(2π · F · x)
 *        - one data buffer (zeroed, size = WINDOW samples)
 *      The receiving yetty scene-canvas creates an addressable entity for
 *      that id; under the hood the yplot factory creates a figure_instance,
 *      uploads bytecode + zero samples to a per-instance storage buffer.
 *
 *   2) STREAM of update envelopes (YDRAW_SCENE_BIN). Each carries a single
 *      CMD_UPDATE(id, payload) record. Payload is yplot-specific:
 *        u32 buffer_index      // 0 (only one data buffer in this demo)
 *        u32 sample_offset     // 0 — overwrite the whole window
 *        u32 count             // WINDOW
 *        f32 samples[count]    // fresh chunk of synthetic audio
 *      The scene-canvas resolves the id to the figure_instance and calls
 *      yplot's update_instance op, which queues a single wgpuQueueWriteBuffer
 *      into the right slot of the storage buffer. No reupload of bytecode,
 *      no rebind, no new instance.
 *
 * Run inside a yetty session, e.g.:
 *   ./build-desktop-ytrace-release/yetty \
 *     -e './build-desktop-ytrace-release/demo/yplot/demo-yplot-audio-source'
 *
 * Options:
 *   --frames=N         number of update envelopes (default 0 = stream forever)
 *   --period-ms=M      sleep between updates in ms      (default 33 ≈ 30 Hz)
 *   --freq=F           central frequency in Hz          (default 4)
 *   --sample-rate=R    samples per second               (default 1000)
 *   --window=N         samples per update               (default 256)
 *   --plot-id=I        addressable id of the yplot      (default 1)
 *   --no-sleep         emit as fast as possible
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yplot/yplot.h>
#include <yetty/yterminal/dcs-codes.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <unistd.h>
static void sleep_ms(int ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}
#endif

struct opts {
    int frames;
    int period_ms;
    int sample_rate;
    int window_samples;
    int plot_id;
    double freq;
    int no_sleep;
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--frames=N] [--period-ms=M] [--freq=F]\n"
            "           [--sample-rate=R] [--window=N] [--plot-id=I] [--no-sleep]\n",
            prog);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_double(const char *s, double *out)
{
    char *end = NULL;
    double v = strtod(s, &end);
    if (!end || *end != '\0') return -1;
    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, struct opts *o)
{
    o->frames = 0;          /* 0 → stream forever (Ctrl-C / pty close to stop) */
    o->period_ms = 33;
    o->sample_rate = 1000;
    o->window_samples = 256;
    o->plot_id = 1;
    o->freq = 4.0;
    o->no_sleep = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--frames=", 9) == 0) {
            if (parse_int(a + 9, &o->frames) < 0) return -1;
        } else if (strncmp(a, "--period-ms=", 12) == 0) {
            if (parse_int(a + 12, &o->period_ms) < 0) return -1;
        } else if (strncmp(a, "--freq=", 7) == 0) {
            if (parse_double(a + 7, &o->freq) < 0) return -1;
        } else if (strncmp(a, "--sample-rate=", 14) == 0) {
            if (parse_int(a + 14, &o->sample_rate) < 0) return -1;
        } else if (strncmp(a, "--window=", 9) == 0) {
            if (parse_int(a + 9, &o->window_samples) < 0) return -1;
        } else if (strncmp(a, "--plot-id=", 10) == 0) {
            if (parse_int(a + 10, &o->plot_id) < 0) return -1;
        } else if (strcmp(a, "--no-sleep") == 0) {
            o->no_sleep = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 1;
        } else {
            return -1;
        }
    }
    if (o->frames < 0 || o->sample_rate <= 0 || o->window_samples <= 0 || o->plot_id <= 0) {
        return -1;
    }
    return 0;
}

/* sin fundamental + 2nd & 3rd harmonics + noise. */
static void generate_frame(float *out, int n, double t0, double sample_rate, double freq)
{
    for (int i = 0; i < n; i++) {
        double t = t0 + (double)i / sample_rate;
        double v = sin(2.0 * M_PI * freq * t)
                 + 0.30 * sin(2.0 * M_PI * 2.0 * freq * t)
                 + 0.10 * sin(2.0 * M_PI * 3.0 * freq * t)
                 + 0.05 * ((double)rand() / (double)RAND_MAX - 0.5);
        out[i] = (float)v;
    }
}

/* Serialize a drawable_list and ship it as a YDRAW_SCENE_BIN OSC envelope on
 * stdout. */
static struct yetty_ycore_void_result emit_scene_bin(const struct yetty_ydraw_drawable_list *dl)
{
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)dl, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "drawable_list_serialize: empty");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result er = yetty_yface_emit(
        YETTY_DCS_YDRAW_SCENE_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &out);
    if (YETTY_IS_ERR(er)) {
        yetty_ycore_buffer_destroy(&out);
        return YETTY_ERR(yetty_ycore_void, "yface_emit failed", er);
    }
    if (out.size > 0) {
        fwrite(out.data, 1, out.size, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    yetty_ycore_buffer_destroy(&out);
    return YETTY_OK_VOID();
}

/* Compile the reference expression to yfsvm bytecode (server-side path:
 * yexpr_parse_plot → yfsvm_compile_multi → serialize). Returns the number
 * of u32 words written. */
static int compile_reference(double freq, uint32_t *bc_buf, size_t bc_cap)
{
    char expr[128];
    int n = snprintf(expr, sizeof expr, "ref=sin(2*pi*%g*x)", freq);
    if (n <= 0 || n >= (int)sizeof expr) {
        return -1;
    }
    struct yetty_yexpr_plot_parse_result pr = yetty_yexpr_parse_plot(expr, (size_t)n);
    if (YETTY_IS_ERR(pr)) {
        yetty_ycore_error_destroy(pr.error);
        return -1;
    }
    struct yetty_yfsvm_program_result prog = yetty_yfsvm_compile_multi(&pr.value.plot);
    if (YETTY_IS_ERR(prog)) {
        yetty_ycore_error_destroy(prog.error);
        return -1;
    }
    return (int)yetty_yfsvm_program_serialize(&prog.value, bc_buf, (uint32_t)bc_cap);
}

/* Build the creation envelope: GROUP(plot_id) wrapping a yplot prim that
 * carries the bytecode + one zero-initialised data buffer. */
static struct yetty_ycore_void_result emit_create_envelope(const struct opts *o, double window_s)
{
    uint32_t bc_buf[1024];
    int bc_len = compile_reference(o->freq, bc_buf, sizeof bc_buf / sizeof bc_buf[0]);
    if (bc_len <= 0) {
        return YETTY_ERR(yetty_ycore_void, "create envelope: bytecode compile failed");
    }

    float *zeros = calloc((size_t)o->window_samples, sizeof(float));
    if (!zeros) {
        return YETTY_ERR(yetty_ycore_void, "create envelope: zero-buffer alloc failed");
    }
    struct yetty_yplot_data_buffer wire_buf = {
        .samples = zeros,
        .count = (size_t)o->window_samples,
    };

    struct yetty_yplot_uniforms u = {
        .bounds_x = 0.0f,  .bounds_y = 0.0f,
        .bounds_w = 720.0f, .bounds_h = 260.0f,
        .x_min = 0.0f, .x_max = (float)window_s,
        .y_min = -1.6f, .y_max = 1.6f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
        .function_count = 1u,
    };
    /* Mint palette for the math reference, coral for the data buffer.
     * Slot 0 = expression `ref`; slot 1 = buffer 0 (function_count + 0). */
    static const uint32_t MINT  = 0xFF74C5A5u;
    static const uint32_t CORAL = 0xFFFF6B6Bu;
    u.colors[0] = MINT;
    u.colors[1] = CORAL;

    struct yetty_yplot_buffers bufs = {
        .bytecode = bc_buf,
        .bytecode_len = (size_t)bc_len,
        .data = &wire_buf,
        .data_count = 1,
    };

    size_t prim_size = yetty_yplot_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_bytes = malloc(prim_size);
    if (!prim_bytes) {
        free(zeros);
        return YETTY_ERR(yetty_ycore_void, "create envelope: prim alloc failed");
    }
    struct yetty_ycore_size_result sr =
        yetty_yplot_uniforms_serialize(&u, &bufs, prim_bytes, prim_size);
    free(zeros);
    if (YETTY_IS_ERR(sr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: serialize failed", sr);
    }

    /* Wrap in CMD_GROUP(id=plot_id) so scene-canvas creates an addressable
     * entity. Subsequent CMD_UPDATE records target this id. */
    struct yetty_ydraw_drawable_list_config cfg = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_w, .scene_max_y = u.bounds_h,
    };
    struct yetty_ydraw_drawable_list_result dlr = yetty_ydraw_drawable_list_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(dlr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: drawable_list create failed", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;

    struct yetty_ydraw_id_result mk =
        yetty_ydraw_drawable_list_begin_group(dl, (uint32_t)o->plot_id);
    if (YETTY_IS_ERR(mk)) {
        yetty_ydraw_drawable_list_destroy(dl);
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: begin_group failed", mk);
    }

    struct yetty_ydraw_id_result ar =
        yetty_ydraw_drawable_list_add_prim(dl, prim_bytes, prim_size);
    free(prim_bytes);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "create envelope: add_prim failed", ar);
    }

    struct yetty_ycore_void_result er =
        yetty_ydraw_drawable_list_end_group(dl, (uint32_t)mk.value);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "create envelope: end_group failed", er);
    }

    struct yetty_ycore_void_result emit_r = emit_scene_bin(dl);
    yetty_ydraw_drawable_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "create envelope: emit failed");
    return YETTY_OK_VOID();
}

/* Build an UPDATE envelope: one CMD_UPDATE(plot_id, payload) record.
 * Payload schema (yplot-defined): u32 buffer_index, u32 sample_offset,
 * u32 count, f32 samples[count]. */
static struct yetty_ycore_void_result emit_update_envelope(int plot_id, const float *samples,
                                                           int count)
{
    size_t payload_size = 12u + (size_t)count * sizeof(float);
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "update envelope: payload alloc failed");
    }
    uint32_t *hdr = (uint32_t *)payload;
    hdr[0] = 0u;                       /* buffer_index */
    hdr[1] = 0u;                       /* sample_offset — overwrite the whole window */
    hdr[2] = (uint32_t)count;          /* count */
    memcpy(payload + 12, samples, (size_t)count * sizeof(float));

    /* Scene bounds are meaningless for an update-only envelope — pass zeros;
     * the canvas already knows the entity's footprint from the create env. */
    struct yetty_ydraw_drawable_list_config cfg = {0};
    struct yetty_ydraw_drawable_list_result dlr = yetty_ydraw_drawable_list_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(dlr)) {
        free(payload);
        return YETTY_ERR(yetty_ycore_void, "update envelope: drawable_list create", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;

    struct yetty_ycore_void_result cu = yetty_ydraw_drawable_list_add_cmd_update(
        dl, (uint32_t)plot_id, payload, payload_size);
    free(payload);
    if (YETTY_IS_ERR(cu)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "update envelope: add_cmd_update", cu);
    }

    struct yetty_ycore_void_result emit_r = emit_scene_bin(dl);
    yetty_ydraw_drawable_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "update envelope: emit");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct opts o;
    int r = parse_args(argc, argv, &o);
    if (r == 1) return 0;
    if (r < 0) {
        usage(argv[0]);
        return 2;
    }

    double window_s = (double)o.window_samples / (double)o.sample_rate;

    struct yetty_ycore_void_result cr = emit_create_envelope(&o, window_s);
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "audio-source: create envelope failed: %s\n", cr.error.msg);
        yetty_ycore_error_destroy(cr.error);
        return 1;
    }

    float *samples = malloc((size_t)o.window_samples * sizeof(float));
    if (!samples) {
        fprintf(stderr, "audio-source: out of memory\n");
        return 1;
    }
    srand(42);
    double t0 = 0.0;

    /* frames == 0 → stream until killed (pty close, SIGINT, …). */
    for (int f = 0; o.frames == 0 || f < o.frames; f++) {
        generate_frame(samples, o.window_samples, t0, (double)o.sample_rate, o.freq);
        t0 += window_s;

        struct yetty_ycore_void_result ur =
            emit_update_envelope(o.plot_id, samples, o.window_samples);
        if (YETTY_IS_ERR(ur)) {
            fprintf(stderr, "audio-source: update envelope failed: %s\n", ur.error.msg);
            yetty_ycore_error_destroy(ur.error);
            free(samples);
            return 1;
        }
        if (!o.no_sleep) sleep_ms(o.period_ms);
    }

    free(samples);
    return 0;
}
