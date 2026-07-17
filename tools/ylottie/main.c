/*
 * yetty-ylottie — render a Lottie (Bodymovin) animation to ydraw DCS.
 *
 * Unlike ycat (which renders a single still frame), this tool PLAYS the whole
 * animation: it walks every frame from the composition in-point to its
 * out-point, emitting a DCS clear + ydraw-bin envelope per frame, paced at
 * the composition frame rate. Run it inside a yetty terminal to watch it move:
 *
 *   yetty-ylottie anim.json                 # play once, real time
 *   yetty-ylottie --loop anim.json          # loop forever (Ctrl-C to stop)
 *   yetty-ylottie --fps 24 anim.json        # override playback rate
 *   yetty-ylottie --frame 30 anim.json      # one still frame
 *   yetty-ylottie --time 1.5 anim.json      # one still frame at t = 1.5 s
 *   yetty-ylottie -w 60 -H 30 anim.json     # size in terminal cells
 *
 * The rendering itself is pure SDF/MSDF via yetty_ylottie_render — no thorvg.
 */

#include <yetty/yplatform/getopt.h>
#include <yetty/yplatform/time.h> /* yetty_yplatform_ytime_sleep_ms */
#include <yetty/ycore/util.h>     /* yetty_ycore_read_file */
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yterminal/dcs-codes.h> /* YETTY_DCS_YDRAW_* */
#include <yetty/ylottie/ylottie.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set by SIGINT so a --loop playback exits cleanly and clears the canvas. */
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int signum)
{
    (void)signum;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] <file.json>\n"
            "Play a Lottie animation as ydraw DCS (run inside a yetty terminal).\n"
            "\n"
            "Options:\n"
            "  -w, --width N    width in terminal cells (default: 80)\n"
            "  -H, --height N   max height in terminal cells (default: 24)\n"
            "      --frame N    render a single frame N (no animation)\n"
            "      --time T     render a single frame at T seconds\n"
            "      --fps F      playback rate override (default: composition fr)\n"
            "      --loop       loop forever (Ctrl-C to stop)\n"
            "      --bg COLOR   background fill (#RGB / #RRGGBB / #RRGGBBAA)\n"
            "      --no-clear   do not emit a clear before each frame\n"
            "  -v, --verbose    print composition info to stderr\n"
            "      --help       show this message\n",
            prog);
}

/* Emit a DCS clear so the previous frame's primitives are wiped. */
static void emit_clear(void)
{
    printf("\033P%uy;\033\\", YETTY_DCS_YDRAW_CLEAR);
}

/* Render one frame of the (already-parsed) animation and emit its ydraw-bin
 * OSC. Returns 0 on success, -1 on failure. */
static int render_and_emit(const struct yetty_ylottie_animation *anim, float frame,
                           uint32_t bg_abgr, int do_clear)
{
    struct yetty_ylottie_render_result rr =
        yetty_ylottie_animation_render_frame(anim, frame, bg_abgr);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yetty-ylottie: render failed: %s\n", rr.error.msg);
        return -1;
    }
    struct yetty_ydraw_drawable_list *buf = rr.value.buffer;

    if (do_clear) {
        emit_clear();
    }

    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(buf, &raw);
    if (raw_size == 0 || !raw) {
        fprintf(stderr, "yetty-ylottie: serialize failed\n");
        yetty_ydraw_drawable_list_destroy(buf);
        return -1;
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result er =
        yetty_yface_emit_to_fd(fileno(stdout), YETTY_DCS_YDRAW_BIN,
                               /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
    yetty_ydraw_drawable_list_destroy(buf);
    if (YETTY_IS_ERR(er)) {
        fprintf(stderr, "yetty-ylottie: yface_emit: %s\n", er.error.msg);
        return -1;
    }
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    enum { OPT_FRAME = 1000, OPT_TIME, OPT_FPS, OPT_LOOP, OPT_BG, OPT_NO_CLEAR, OPT_HELP };
    static const struct yetty_yplatform_option long_opts[] = {
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"frame", required_argument, NULL, OPT_FRAME},
        {"time", required_argument, NULL, OPT_TIME},
        {"fps", required_argument, NULL, OPT_FPS},
        {"loop", no_argument, NULL, OPT_LOOP},
        {"bg", required_argument, NULL, OPT_BG},
        {"no-clear", no_argument, NULL, OPT_NO_CLEAR},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, OPT_HELP},
        {NULL, 0, NULL, 0},
    };

    uint32_t width_cells = 80;
    uint32_t height_cells = 24;
    float single_frame = 0.0f;
    int have_single_frame = 0;
    float single_time = 0.0f;
    int have_single_time = 0;
    float fps_override = 0.0f;
    int do_loop = 0;
    int do_clear = 1;
    int verbose = 0;
    const char *bg = NULL;

    int opt;
    while ((opt = yetty_yplatform_getopt_long(argc, argv, "w:H:v", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'w':
            width_cells = (uint32_t)atoi(yetty_yplatform_optarg);
            break;
        case 'H':
            height_cells = (uint32_t)atoi(yetty_yplatform_optarg);
            break;
        case OPT_FRAME:
            single_frame = (float)atof(yetty_yplatform_optarg);
            have_single_frame = 1;
            break;
        case OPT_TIME:
            single_time = (float)atof(yetty_yplatform_optarg);
            have_single_time = 1;
            break;
        case OPT_FPS:
            fps_override = (float)atof(yetty_yplatform_optarg);
            break;
        case OPT_LOOP:
            do_loop = 1;
            break;
        case OPT_BG:
            bg = yetty_yplatform_optarg;
            break;
        case OPT_NO_CLEAR:
            do_clear = 0;
            break;
        case 'v':
            verbose = 1;
            break;
        case OPT_HELP:
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (yetty_yplatform_optind >= argc) {
        fprintf(stderr, "%s: missing input file\n", argv[0]);
        usage(argv[0]);
        return 1;
    }
    const char *path = argv[yetty_yplatform_optind];

    struct yetty_ycore_buffer_result file_res = yetty_ycore_read_file(path);
    if (YETTY_IS_ERR(file_res)) {
        fprintf(stderr, "%s: failed to read %s: %s\n", argv[0], path, file_res.error.msg);
        return 1;
    }
    const char *content = (const char *)file_res.value.data;
    size_t len = file_res.value.size;

    struct yetty_ylottie_render_config cfg = {.cell_width = 8,
                                              .cell_height = 16,
                                              .width_cells = width_cells,
                                              .height_cells = height_cells};

    /* Parse the document ONCE; render every frame from this handle. */
    struct yetty_ylottie_animation_ptr_result ar =
        yetty_ylottie_animation_create(content, len, &cfg);
    if (YETTY_IS_ERR(ar)) {
        fprintf(stderr, "%s: %s\n", argv[0], ar.error.msg);
        free(file_res.value.data);
        return 1;
    }
    struct yetty_ylottie_animation *anim = ar.value;
    struct yetty_ylottie_info info = yetty_ylottie_animation_info(anim);

    /* Resolve the background colour once. */
    uint32_t bg_abgr = 0;
    if (bg && !yetty_ylottie_parse_color(bg, strlen(bg), &bg_abgr)) {
        fprintf(stderr, "%s: ignoring bad --bg colour '%s'\n", argv[0], bg);
        bg_abgr = 0;
    }

    if (verbose) {
        fprintf(stderr, "yetty-ylottie: %s  comp=%.0fx%.0f  fr=%.2f  ip=%.2f  op=%.2f  layers=%d\n",
                path, info.width, info.height, info.frame_rate, info.in_point, info.out_point,
                info.layer_count);
    }

    /* Single-frame mode (a still). */
    if (have_single_frame || have_single_time) {
        float frame = have_single_time
                          ? single_time * (info.frame_rate > 0.0f ? info.frame_rate : 30.0f)
                          : single_frame;
        int rc = render_and_emit(anim, frame, bg_abgr, do_clear);
        yetty_ylottie_animation_destroy(anim);
        free(file_res.value.data);
        return rc == 0 ? 0 : 1;
    }

    /* Animation mode. */
    float fps =
        (fps_override > 0.0f) ? fps_override : (info.frame_rate > 0.0f ? info.frame_rate : 30.0f);
    unsigned delay_ms = (unsigned)(1000.0f / fps + 0.5f);
    if (delay_ms == 0) {
        delay_ms = 1;
    }
    float ip = info.in_point;
    float op = info.out_point;
    int frame_count = (op > ip) ? (int)(op - ip) : 1; /* render [ip, op) by integer frames */

    signal(SIGINT, on_sigint);
    int rc = 0;
    do {
        for (int i = 0; i < frame_count && !g_stop; i++) {
            if (render_and_emit(anim, ip + (float)i, bg_abgr, do_clear) != 0) {
                rc = 1;
                g_stop = 1;
                break;
            }
            yetty_yplatform_ytime_sleep_ms(delay_ms);
        }
    } while (do_loop && !g_stop);

    /* Leave a clean canvas on exit. */
    if (do_clear) {
        emit_clear();
        fflush(stdout);
    }
    yetty_ylottie_animation_destroy(anim);
    free(file_res.value.data);
    return rc;
}
