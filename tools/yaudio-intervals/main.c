/*
 * yaudio-intervals - find "active" intervals (above noise floor) in
 *                    WAV files. Batch-friendly: takes multiple files
 *                    on the command line, prints one TSV row per
 *                    interval. Designed for many-GB sensor recordings.
 *
 * Output format (TSV, no header by default; use --header to add one):
 *   file<TAB>channel<TAB>start_s<TAB>end_s<TAB>dur_s<TAB>peak_dbfs<TAB>rms_dbfs
 */

#include <yetty/yaudio/wav.h>
#include <yetty/yaudio/intervals.h>
#include <yetty/ycore/result.h>

#include <getopt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct opts {
    int   channel;            /* -1 → all channels */
    float noise_percentile;
    float open_db;
    float close_db;
    double min_active_sec;
    double min_gap_sec;
    int   frame_samples;
    int   hop_samples;
    int   print_header;
    int   print_summary;      /* one extra stderr line per (file,channel) */
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] file.wav [file2.wav ...]\n"
        "\n"
        "Find intervals of energy above the noise floor in PCM WAV files.\n"
        "Outputs TSV: file<TAB>channel<TAB>start_s<TAB>end_s<TAB>dur_s<TAB>peak_dbfs<TAB>rms_dbfs\n"
        "\n"
        "Options:\n"
        "  -c, --channel=N         analyse channel N only (default: all)\n"
        "  -p, --percentile=F      noise-floor percentile, 0..1 (default 0.15)\n"
        "      --open-db=F         open threshold dB above floor   (default 10)\n"
        "      --close-db=F        close threshold dB above floor  (default 6)\n"
        "      --min-active=S      drop intervals shorter than S s (default 0.05)\n"
        "      --min-gap=S         merge across gaps shorter than S s (default 0.15)\n"
        "      --frame=N           analysis frame in samples       (default 1024)\n"
        "      --hop=N             hop between frames in samples   (default = frame)\n"
        "  -H, --header            emit a TSV header on the first line\n"
        "  -s, --summary           emit a one-line summary per (file,channel) to stderr\n"
        "  -h, --help              show this help\n",
        prog);
}

enum {
    OPT_OPEN_DB = 1000,
    OPT_CLOSE_DB,
    OPT_MIN_ACTIVE,
    OPT_MIN_GAP,
    OPT_FRAME,
    OPT_HOP,
};

static int process_channel(const char *path,
                           const struct yetty_yaudio_wav *w,
                           uint32_t channel,
                           const struct opts *opts,
                           FILE *tsv)
{
    struct yetty_yaudio_intervals_config cfg;
    yetty_yaudio_intervals_config_defaults(&cfg);
    cfg.noise_percentile     = opts->noise_percentile;
    cfg.open_db_above_floor  = opts->open_db;
    cfg.close_db_above_floor = opts->close_db;
    cfg.min_active_sec       = opts->min_active_sec;
    cfg.min_gap_sec          = opts->min_gap_sec;
    if (opts->frame_samples > 0) cfg.frame_samples = (uint32_t)opts->frame_samples;
    if (opts->hop_samples > 0)   cfg.hop_samples   = (uint32_t)opts->hop_samples;

    struct yetty_yaudio_intervals_ptr_result r =
        yetty_yaudio_intervals_find(w, channel, &cfg, NULL, NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "%s: ch%u: %s\n", path, channel, r.error.msg);
        for (const struct yetty_ycore_error *e = r.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(r.error);
        return 1;
    }
    struct yetty_yaudio_intervals *iv = r.value;

    if (opts->print_summary) {
        fprintf(stderr,
                "%s ch%u: floor=%.1f dBFS open=%.1f close=%.1f "
                "frames=%zu intervals=%zu\n",
                path, channel, iv->noise_floor_dbfs,
                iv->open_threshold_dbfs, iv->close_threshold_dbfs,
                iv->total_frames, iv->n);
    }

    for (size_t i = 0; i < iv->n; i++) {
        const struct yetty_yaudio_interval *it = &iv->items[i];
        fprintf(tsv, "%s\t%u\t%.6f\t%.6f\t%.6f\t%.2f\t%.2f\n",
                path, channel,
                it->start_sec, it->end_sec,
                it->end_sec - it->start_sec,
                (double)it->peak_dbfs, (double)it->rms_dbfs);
    }
    yetty_yaudio_intervals_destroy(iv);
    return 0;
}

static int process_file(const char *path, const struct opts *opts, FILE *tsv)
{
    struct yetty_yaudio_wav_ptr_result wr = yetty_yaudio_wav_open(path);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "%s: open failed: %s\n", path, wr.error.msg);
        for (const struct yetty_ycore_error *e = wr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(wr.error);
        return 1;
    }
    struct yetty_yaudio_wav *w = wr.value;

    int rc = 0;
    if (opts->channel < 0) {
        for (uint16_t c = 0; c < w->channels; c++) {
            int r = process_channel(path, w, c, opts, tsv);
            if (r != 0) rc = r;
        }
    } else {
        if ((uint32_t)opts->channel >= w->channels) {
            fprintf(stderr, "%s: channel %d out of range (file has %u)\n",
                    path, opts->channel, w->channels);
            rc = 1;
        } else {
            rc = process_channel(path, w, (uint32_t)opts->channel, opts, tsv);
        }
    }
    yetty_yaudio_wav_close(w);
    return rc;
}

int main(int argc, char **argv)
{
    struct opts opts = {
        .channel = -1,
        .noise_percentile = 0.15f,
        .open_db   = 10.0f,
        .close_db  = 6.0f,
        .min_active_sec = 0.050,
        .min_gap_sec    = 0.150,
        .frame_samples  = 1024,
        .hop_samples    = 1024,
        .print_header   = 0,
        .print_summary  = 0,
    };

    static const struct option long_opts[] = {
        {"channel",    required_argument, NULL, 'c'},
        {"percentile", required_argument, NULL, 'p'},
        {"open-db",    required_argument, NULL, OPT_OPEN_DB},
        {"close-db",   required_argument, NULL, OPT_CLOSE_DB},
        {"min-active", required_argument, NULL, OPT_MIN_ACTIVE},
        {"min-gap",    required_argument, NULL, OPT_MIN_GAP},
        {"frame",      required_argument, NULL, OPT_FRAME},
        {"hop",        required_argument, NULL, OPT_HOP},
        {"header",     no_argument,       NULL, 'H'},
        {"summary",    no_argument,       NULL, 's'},
        {"help",       no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "c:p:Hsh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'c': opts.channel = atoi(optarg); break;
        case 'p': opts.noise_percentile = (float)atof(optarg); break;
        case OPT_OPEN_DB:    opts.open_db = (float)atof(optarg); break;
        case OPT_CLOSE_DB:   opts.close_db = (float)atof(optarg); break;
        case OPT_MIN_ACTIVE: opts.min_active_sec = atof(optarg); break;
        case OPT_MIN_GAP:    opts.min_gap_sec    = atof(optarg); break;
        case OPT_FRAME:      opts.frame_samples = atoi(optarg); break;
        case OPT_HOP:        opts.hop_samples   = atoi(optarg); break;
        case 'H': opts.print_header = 1; break;
        case 's': opts.print_summary = 1; break;
        case 'h': usage(stdout, argv[0]); return 0;
        default:  usage(stderr, argv[0]); return 2;
        }
    }

    if (optind >= argc) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (opts.print_header) {
        printf("file\tchannel\tstart_s\tend_s\tdur_s\tpeak_dbfs\trms_dbfs\n");
    }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        int r = process_file(argv[i], &opts, stdout);
        if (r != 0) rc = r;
        fflush(stdout);
    }
    return rc;
}
