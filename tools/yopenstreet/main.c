/*
 * yopenstreet - render an OpenStreetMap snapshot into a yetty terminal.
 *
 * Downloads the slippy tiles covering a lat/lon-centered viewport,
 * composites them, and emits one YDRAW_BIN OSC envelope to stdout — the
 * map lands in the scrolling rich grid at the cursor, like a ycat image.
 * Prints the OpenStreetMap attribution line under the map (required by
 * the OSM tile usage policy).
 *
 *   yopenstreet --lat 47.4979 --lon 19.0402 --zoom 13
 *   yopenstreet --lat 51.5074 --lon -0.1278 -z 11 -w 100 -H 30
 */

#include <yetty/yopenstreet/openstreet.h>

#include "interactive.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/yplatform/getopt.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

/* Same cell-size guesses ycat uses when the terminal can't be asked. */
#define CELL_WIDTH_PX 8u
#define CELL_HEIGHT_PX 16u
#define DEFAULT_HEIGHT_CELLS 25u
#define DEFAULT_ZOOM 13u
/* World-view fallback when no location is given and geoip is off/failed. */
#define FALLBACK_LATITUDE 25.0
#define FALLBACK_LONGITUDE 10.0
#define FALLBACK_ZOOM 2u

static int terminal_columns(void)
{
#ifdef __unix__
    struct winsize window_size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == 0 && window_size.ws_col > 0) {
        return window_size.ws_col;
    }
#endif
    return 80;
}

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [--lat <degrees> --lon <degrees>] [options]\n"
            "\n"
            "Render an OpenStreetMap snapshot into a yetty terminal (scrolls\n"
            "with the text, like a ycat image). Without --lat/--lon the map\n"
            "centers on this machine's public-IP location (one HTTPS request\n"
            "to ipinfo.io, city-level accuracy); --no-geoip or a lookup\n"
            "failure falls back to a world view.\n"
            "\n"
            "Options:\n"
            "  -a, --lat=DEG        map center latitude\n"
            "  -o, --lon=DEG        map center longitude\n"
            "  -z, --zoom=N         slippy zoom level 0..19 (default: %u)\n"
            "  -V, --vector         vector tiles -> native SDF/MSDF drawables\n"
            "                       (crisp at any zoom; max zoom 14)\n"
            "  -i, --interactive    stay foreground: Ctrl-Shift-wheel zooms,\n"
            "                       left-drag pans, +/- zoom, q/Esc quits\n"
            "  -w, --width=N        map width in cells (default: terminal cols)\n"
            "  -H, --height=N       map height in cells (default: %u)\n"
            "  -u, --url=TEMPLATE   tile URL template, three %%u slots in z/x/y\n"
            "                       order (default: openstreetmap.org)\n"
            "  -C, --cache-dir=DIR  tile cache dir (default: yetty cache dir)\n"
            "      --no-geoip       never look up the public-IP location\n"
            "      --sleep-after=N  sleep N ms after emitting (PTY drain aid)\n"
            "  -h, --help           show this help\n"
            "\n"
            "Map data © OpenStreetMap contributors (https://www.openstreetmap.org/copyright)\n",
            prog, DEFAULT_ZOOM, DEFAULT_HEIGHT_CELLS);
}

enum {
    OPT_SLEEP_AFTER = 1000,
    OPT_NO_GEOIP = 1001,
};

int main(int argc, char **argv)
{
    double latitude = 0.0;
    double longitude = 0.0;
    bool have_latitude = false;
    bool have_longitude = false;
    long zoom = DEFAULT_ZOOM;
    int width_cells = 0;
    int height_cells = DEFAULT_HEIGHT_CELLS;
    const char *url_template = NULL;
    const char *cache_dir = NULL;
    int sleep_after_ms = 0;
    bool vector_mode = false;
    bool interactive_mode = false;
    bool no_geoip = false;
    bool have_zoom = false;

    static const struct yetty_yplatform_option long_opts[] = {
        {"lat", required_argument, NULL, 'a'},
        {"lon", required_argument, NULL, 'o'},
        {"zoom", required_argument, NULL, 'z'},
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"url", required_argument, NULL, 'u'},
        {"cache-dir", required_argument, NULL, 'C'},
        {"vector", no_argument, NULL, 'V'},
        {"interactive", no_argument, NULL, 'i'},
        {"no-geoip", no_argument, NULL, OPT_NO_GEOIP},
        {"sleep-after", required_argument, NULL, OPT_SLEEP_AFTER},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = yetty_yplatform_getopt_long(argc, argv, "a:o:z:w:H:u:C:Vih", long_opts, NULL)) !=
           -1) {
        switch (opt) {
        case 'a':
            latitude = atof(yetty_yplatform_optarg);
            have_latitude = true;
            break;
        case 'o':
            longitude = atof(yetty_yplatform_optarg);
            have_longitude = true;
            break;
        case 'z':
            zoom = atol(yetty_yplatform_optarg);
            have_zoom = true;
            break;
        case 'w':
            width_cells = atoi(yetty_yplatform_optarg);
            break;
        case 'H':
            height_cells = atoi(yetty_yplatform_optarg);
            break;
        case 'u':
            url_template = yetty_yplatform_optarg;
            break;
        case 'C':
            cache_dir = yetty_yplatform_optarg;
            break;
        case 'V':
            vector_mode = true;
            break;
        case 'i':
            interactive_mode = true;
            break;
        case OPT_SLEEP_AFTER:
            sleep_after_ms = atoi(yetty_yplatform_optarg);
            break;
        case OPT_NO_GEOIP:
            no_geoip = true;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (have_latitude != have_longitude) {
        fprintf(stderr, "yopenstreet: --lat and --lon must be given together\n\n");
        usage(stderr, argv[0]);
        return 2;
    }
    if (!have_latitude) {
        bool located = false;
        if (!no_geoip) {
            struct yetty_ycore_void_result geo_res =
                yetty_yopenstreet_geolocate_public_ip(&latitude, &longitude);
            if (YETTY_IS_OK(geo_res)) {
                located = true;
                fprintf(stderr,
                        "yopenstreet: centered on the public-IP location %.4f,%.4f "
                        "(city-level guess; pass --lat/--lon to override, "
                        "--no-geoip to never ask)\n",
                        latitude, longitude);
            } else {
                fprintf(stderr, "yopenstreet: public-IP lookup failed (%s) — world view\n",
                        geo_res.error.msg);
                yetty_ycore_error_destroy(geo_res.error);
            }
        }
        if (!located) {
            latitude = FALLBACK_LATITUDE;
            longitude = FALLBACK_LONGITUDE;
            if (!have_zoom) {
                zoom = FALLBACK_ZOOM;
            }
        }
    }
    long max_zoom = vector_mode ? (long)YETTY_YOPENSTREET_MAX_VECTOR_ZOOM
                                : (long)YETTY_YOPENSTREET_MAX_ZOOM;
    if (zoom < 0 || zoom > max_zoom) {
        fprintf(stderr, "yopenstreet: zoom must be 0..%ld%s\n", max_zoom,
                vector_mode ? " in vector mode" : "");
        return 2;
    }
    if (width_cells <= 0) {
        width_cells = terminal_columns();
    }
    if (height_cells <= 0) {
        height_cells = DEFAULT_HEIGHT_CELLS;
    }

    if (!yetty_running_under_yetty()) {
        fprintf(stderr, "yopenstreet: warning: not inside a yetty terminal — "
                        "the emitted map will be invisible here\n");
    }

    struct yetty_yopenstreet_config config = {
        .latitude = latitude,
        .longitude = longitude,
        .zoom = (uint32_t)zoom,
        .width_px = (uint32_t)width_cells * CELL_WIDTH_PX,
        .height_px = (uint32_t)height_cells * CELL_HEIGHT_PX,
        .tile_url_template = url_template,
        .cache_dir = cache_dir,
    };

    if (interactive_mode) {
        return yopenstreet_interactive_run(&config, vector_mode);
    }

    struct yetty_ydraw_drawable_list_result map_res =
        vector_mode ? yetty_yopenstreet_render_vector(&config) : yetty_yopenstreet_render(&config);
    if (YETTY_IS_ERR(map_res)) {
        fprintf(stderr, "yopenstreet: render failed: %s\n", map_res.error.msg);
        yetty_ycore_error_destroy(map_res.error);
        return 1;
    }

    struct yetty_ycore_size_result emit_res = yetty_yopenstreet_osc_bin_emit(map_res.value, stdout);
    yetty_ydraw_drawable_list_destroy(map_res.value);
    if (YETTY_IS_ERR(emit_res)) {
        fprintf(stderr, "yopenstreet: emit failed: %s\n", emit_res.error.msg);
        yetty_ycore_error_destroy(emit_res.error);
        return 1;
    }

    /* Required by the OSM tile usage policy — keep under the map. */
    printf("© OpenStreetMap contributors\n");
    fflush(stdout);

    if (sleep_after_ms > 0) {
#ifdef _WIN32
        Sleep((DWORD)sleep_after_ms);
#else
        struct timespec pause = {
            .tv_sec = sleep_after_ms / 1000,
            .tv_nsec = (long)(sleep_after_ms % 1000) * 1000000L,
        };
        nanosleep(&pause, NULL);
#endif
    }
    return 0;
}
