/*
 * yplot-stream — pipe a number stream into a live scrolling yplot figure.
 *
 *   python3 solver.py | yplot-stream --len 400 --yrange=-1..1 --title 'residual'
 *
 * Emits one INIT envelope carrying a ring-mode yplot (a zero-filled buffer
 * of --len samples), then, for every line of stdin that parses as a float,
 * a small CMD_UPDATE envelope writing that sample at the ring head plus a
 * head-advance op — the figure scrolls left forever at the producer's own
 * pace, with constant memory and per-sample (not per-window) uploads.
 * Non-numeric stdin lines are forwarded to stdout untouched, so a solver's
 * log keeps flowing in the same scrollback as its live plot.
 *
 * The update envelopes target the yplot prim by its record id (the plot
 * prim is the FIRST record of the INIT envelope, so id 1 — same routing
 * yvideo streaming uses).
 */

#include <yetty/yplot/yplot.h>

#include <yetty/ydraw-core/drawable-list.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YPLOT_STREAM_PRIM_ID 1u

struct stream_options {
    float width;
    float height;
    uint32_t sample_capacity;
    float y_min, y_max;
    bool y_range_set;
    const char *title;
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: <producer> | %s [options]\n"
            "\n"
            "Render stdin numbers as a live scrolling plot (ring buffer).\n"
            "Lines that don't parse as a number pass through to stdout.\n"
            "\n"
            "Options:\n"
            "  -w, --width=N        figure width in pixels (default 900)\n"
            "  -H, --height=N       figure height in pixels (default 260)\n"
            "  -l, --len=N          ring capacity in samples (default 300)\n"
            "      --yrange=lo..hi  value range (default -1..1)\n"
            "      --title=TEXT     figure title\n"
            "  -h, --help           show this help\n",
            prog);
}

static int parse_range(const char *text, float *out_lo, float *out_hi)
{
    const char *dots = text ? strstr(text, "..") : NULL;
    if (!dots) {
        return -1;
    }
    /* Copy the prefix so strtof can't walk into the ".." separator (for
     * "0..4" it would otherwise consume "0." and misalign the check). */
    char prefix[64];
    size_t prefix_len = (size_t)(dots - text);
    if (prefix_len == 0 || prefix_len >= sizeof(prefix)) {
        return -1;
    }
    memcpy(prefix, text, prefix_len);
    prefix[prefix_len] = '\0';
    char *end = NULL;
    float low = strtof(prefix, &end);
    if (!end || *end != '\0') {
        return -1;
    }
    float high = strtof(dots + 2, &end);
    if (!end || *end != '\0') {
        return -1;
    }
    *out_lo = low;
    *out_hi = high;
    return 0;
}

/* Emit one envelope containing a CMD_UPDATE with the given payload. */
static struct yetty_ycore_void_result emit_update_envelope(const uint32_t *payload,
                                                           size_t payload_words)
{
    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = 1.0f,
        .scene_max_y = 1.0f,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "yplot-stream: update list create");

    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_update(
        list_res.value, YPLOT_STREAM_PRIM_ID, payload, payload_words * sizeof(uint32_t));
    if (YETTY_IS_ERR(add_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ycore_void, "yplot-stream: add_cmd_update", add_res);
    }

    struct yetty_ycore_size_result emit_res = yetty_yplot_dcs_bin_emit(list_res.value, stdout);
    yetty_ydraw_drawable_list_destroy(list_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "yplot-stream: emit update");
    fflush(stdout);
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct stream_options options = {
        .width = 900.0f,
        .height = 260.0f,
        .sample_capacity = 300,
        .y_min = -1.0f,
        .y_max = 1.0f,
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        if ((value = strchr(arg, '=')) != NULL) {
            value++;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strncmp(arg, "--width=", 8) == 0) {
            options.width = strtof(value, NULL);
        } else if (strcmp(arg, "-w") == 0 && i + 1 < argc) {
            options.width = strtof(argv[++i], NULL);
        } else if (strncmp(arg, "--height=", 9) == 0) {
            options.height = strtof(value, NULL);
        } else if (strcmp(arg, "-H") == 0 && i + 1 < argc) {
            options.height = strtof(argv[++i], NULL);
        } else if (strncmp(arg, "--len=", 6) == 0) {
            options.sample_capacity = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "-l") == 0 && i + 1 < argc) {
            options.sample_capacity = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strncmp(arg, "--yrange=", 9) == 0) {
            if (parse_range(value, &options.y_min, &options.y_max) < 0) {
                fprintf(stderr, "yplot-stream: invalid yrange %s\n", value);
                return 2;
            }
            options.y_range_set = true;
        } else if (strncmp(arg, "--title=", 8) == 0) {
            options.title = value;
        } else if (strcmp(arg, "--title") == 0 && i + 1 < argc) {
            options.title = argv[++i];
        } else {
            fprintf(stderr, "yplot-stream: unknown option %s\n", arg);
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (options.sample_capacity < 2 || options.sample_capacity > 65536) {
        fprintf(stderr, "yplot-stream: --len must be in 2..65536\n");
        return 2;
    }

    /* INIT: a ring-mode plot around one zero-filled buffer. The plot prim
     * is the first record in the envelope → record id 1, the id every
     * following CMD_UPDATE targets. */
    float *zero_samples = calloc(options.sample_capacity, sizeof(float));
    if (!zero_samples) {
        fprintf(stderr, "yplot-stream: sample alloc failed\n");
        return 1;
    }
    struct yetty_yplot_buffer_input stream_buffer = {
        .samples = zero_samples,
        .count = options.sample_capacity,
        .ring = true,
    };
    struct yetty_yplot_render_config plot_config = {
        .bounds_w = options.width,
        .bounds_h = options.height,
        .x_min = 0.0f,
        .x_max = (float)(options.sample_capacity - 1),
        .y_min = options.y_min,
        .y_max = options.y_max,
        .title = options.title,
    };
    struct yetty_ydraw_drawable_list_result init_res =
        yetty_yplot_render_with_buffers(NULL, 0, &stream_buffer, 1, &plot_config);
    free(zero_samples);
    if (YETTY_IS_ERR(init_res)) {
        fprintf(stderr, "yplot-stream: init render failed: %s\n", init_res.error.msg);
        yetty_ycore_error_destroy(init_res.error);
        return 1;
    }
    struct yetty_ycore_size_result init_emit = yetty_yplot_dcs_bin_emit(init_res.value, stdout);
    yetty_ydraw_drawable_list_destroy(init_res.value);
    if (YETTY_IS_ERR(init_emit)) {
        fprintf(stderr, "yplot-stream: init emit failed: %s\n", init_emit.error.msg);
        yetty_ycore_error_destroy(init_emit.error);
        return 1;
    }
    fputc('\n', stdout);
    fflush(stdout);

    /* Stream: every numeric stdin line lands at the ring head; everything
     * else passes through so producer logs stay visible. */
    uint32_t head = 0;
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char *end = NULL;
        float sample = strtof(line, &end);
        bool numeric = end && end != line;
        while (numeric && *end) {
            if (*end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
                numeric = false;
            }
            end++;
        }
        if (!numeric) {
            fputs(line, stdout);
            fflush(stdout);
            continue;
        }

        /* Sample write at the current head... */
        uint32_t sample_payload[4];
        sample_payload[0] = 0;    /* buffer_index */
        sample_payload[1] = head; /* sample_offset */
        sample_payload[2] = 1;    /* count */
        memcpy(&sample_payload[3], &sample, sizeof(float));
        struct yetty_ycore_void_result sample_res = emit_update_envelope(sample_payload, 4);
        if (YETTY_IS_ERR(sample_res)) {
            fprintf(stderr, "yplot-stream: sample update failed: %s\n", sample_res.error.msg);
            yetty_ycore_error_destroy(sample_res.error);
            return 1;
        }

        /* ...then advance the head past it (the freshly written sample is
         * now the newest → right edge). */
        head = (head + 1u) % options.sample_capacity;
        uint32_t head_payload[3] = {0, 0xFFFFFFFFu, head};
        struct yetty_ycore_void_result head_res = emit_update_envelope(head_payload, 3);
        if (YETTY_IS_ERR(head_res)) {
            fprintf(stderr, "yplot-stream: head update failed: %s\n", head_res.error.msg);
            yetty_ycore_error_destroy(head_res.error);
            return 1;
        }
    }
    return 0;
}
