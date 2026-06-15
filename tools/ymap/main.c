/*
 * ymap - render a slippy map into a yetty terminal.
 *
 * Downloads the slippy tiles covering a lat/lon-centered viewport,
 * composites them, and emits one YDRAW_BIN OSC envelope to stdout — the
 * map lands in the scrolling rich grid at the cursor, like a ycat image.
 * Prints the OpenStreetMap attribution line under the map (required by
 * the OSM tile usage policy).
 *
 *   ymap --lat 47.4979 --lon 19.0402 --zoom 13
 *   ymap --lat 51.5074 --lon -0.1278 -z 11 -w 100 -H 30
 */

#include <yetty/ymap/engine.h>
#include <yetty/ymap/map.h>

#include <yetty/yclass/class.h>

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
#define YMAP_STDOUT_FD_MAIN (_fileno(stdout))
#else
#include <unistd.h>
#include <sys/ioctl.h>
#define YMAP_STDOUT_FD_MAIN (STDOUT_FILENO)
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
            "Render a slippy map into a yetty terminal (scrolls with the\n"
            "text, like a ycat image). Without --lat/--lon the map centers\n"
            "on this machine's public-IP location (one HTTPS request to\n"
            "ipinfo.io, city-level accuracy); --no-geoip or a lookup failure\n"
            "falls back to a world view.\n"
            "\n"
            "  ymap                              your area, OSM raster\n"
            "  ymap -P osm-vector -i             interactive vector map\n"
            "  ymap -P s2cloudless -z 11         satellite imagery\n"
            "\n"
            "Options:\n"
            "  -a, --lat=DEG        map center latitude\n"
            "  -o, --lon=DEG        map center longitude\n"
            "  -z, --zoom=N         zoom level (default: %u; clamped to the\n"
            "                       provider's range)\n"
            "  -P, --provider=NAME  tile provider (default: osm, or\n"
            "                       osm-vector with -V)\n"
            "      --list-providers print the built-in provider registry\n"
            "  -V, --vector         shorthand for -P osm-vector\n"
            "  -i, --interactive    stay foreground: Ctrl-Shift-wheel zooms\n"
            "                       (cursor-anchored), left-drag pans,\n"
            "                       +/- zoom, q/Esc quits\n"
            "  -w, --width=N        map width in cells (default: terminal cols)\n"
            "  -H, --height=N       map height in cells (default: %u)\n"
            "  -u, --url=TEMPLATE   custom tile URL template, three %%u slots\n"
            "                       in z/x/y order (POSIX %%1$u.. reorder)\n"
            "      --no-geoip       never look up the public-IP location\n"
            "      --sleep-after=N  sleep N ms after emitting (PTY drain aid)\n"
            "  -h, --help           show this help\n"
            "\n"
            "Every provider requires its attribution displayed with the map;\n"
            "ymap prints it under the snapshot / draws it on the figure.\n",
            prog, DEFAULT_ZOOM, DEFAULT_HEIGHT_CELLS);
}

enum {
    OPT_SLEEP_AFTER = 1000,
    OPT_NO_GEOIP = 1001,
    OPT_LIST_PROVIDERS = 1002,
};

static int list_providers(void)
{
    printf("%-16s %-7s %-5s %s\n", "NAME", "KIND", "MAXZ", "ATTRIBUTION");
    uint32_t count = yetty_ymap_provider_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = NULL;
        const char *attribution = NULL;
        uint32_t max_zoom = 0;
        int is_vector = 0;
        struct yetty_ycore_void_result info_res =
            yetty_ymap_provider_info(i, &name, &attribution, &max_zoom, &is_vector);
        if (YETTY_IS_ERR(info_res)) {
            yetty_ycore_error_destroy(info_res.error);
            continue;
        }
        printf("%-16s %-7s %-5u %s\n", name, is_vector ? "vector" : "raster", max_zoom,
               attribution);
    }
    return 0;
}

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
    int sleep_after_ms = 0;
    bool vector_mode = false;
    bool interactive_mode = false;
    bool no_geoip = false;
    bool have_zoom = false;
    const char *provider_name = NULL;

    static const struct yetty_yplatform_option long_opts[] = {
        {"lat", required_argument, NULL, 'a'},
        {"lon", required_argument, NULL, 'o'},
        {"zoom", required_argument, NULL, 'z'},
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"url", required_argument, NULL, 'u'},
        {"vector", no_argument, NULL, 'V'},
        {"interactive", no_argument, NULL, 'i'},
        {"no-geoip", no_argument, NULL, OPT_NO_GEOIP},
        {"provider", required_argument, NULL, 'P'},
        {"list-providers", no_argument, NULL, OPT_LIST_PROVIDERS},
        {"sleep-after", required_argument, NULL, OPT_SLEEP_AFTER},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = yetty_yplatform_getopt_long(argc, argv, "a:o:z:w:H:u:P:Vih", long_opts, NULL)) !=
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
        case 'P':
            provider_name = yetty_yplatform_optarg;
            break;
        case OPT_LIST_PROVIDERS:
            return list_providers();
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (have_latitude != have_longitude) {
        fprintf(stderr, "ymap: --lat and --lon must be given together\n\n");
        usage(stderr, argv[0]);
        return 2;
    }
    /* The geoip default runs AFTER provider validation below — an invalid
     * invocation must not cost a network round-trip. */
    if (width_cells <= 0) {
        width_cells = terminal_columns();
    }
    if (height_cells <= 0) {
        height_cells = DEFAULT_HEIGHT_CELLS;
    }

    if (!yetty_running_under_yetty()) {
        fprintf(stderr, "ymap: warning: not inside a yetty terminal — "
                        "the emitted map will be invisible here\n");
    }

    /* Build the ymap:map model that both modes drive. */
    {
        struct yetty_ycore_void_result register_res = yetty_ymap_register();
        if (YETTY_IS_ERR(register_res)) {
            fprintf(stderr, "ymap: ymap register failed: %s\n", register_res.error.msg);
            yetty_ycore_error_destroy(register_res.error);
            return 1;
        }
    }
    struct yetty_yclass_object_ptr_result map_res_obj = yetty_ymap_map_create(NULL);
    if (YETTY_IS_ERR(map_res_obj)) {
        fprintf(stderr, "ymap: map create failed: %s\n", map_res_obj.error.msg);
        yetty_ycore_error_destroy(map_res_obj.error);
        return 1;
    }
    struct yetty_yclass_object *map_object = map_res_obj.value;

    int exit_code = 1;
    if (url_template) {
        struct yetty_ycore_void_result custom_res = yetty_ymap_set_custom_provider(map_object, url_template, vector_mode ? 1 : 0, NULL, 0,
            "(C) custom tile provider — check its usage terms");
        if (YETTY_IS_ERR(custom_res)) {
            fprintf(stderr, "ymap: --url rejected: %s\n", custom_res.error.msg);
            yetty_ycore_error_destroy(custom_res.error);
            goto out;
        }
    } else {
        const char *chosen = provider_name ? provider_name : (vector_mode ? "osm-vector" : "osm");
        struct yetty_ycore_void_result provider_res =
            yetty_ymap_set_provider(map_object, chosen);
        if (YETTY_IS_ERR(provider_res)) {
            fprintf(stderr, "ymap: unknown provider '%s' (see --list-providers)\n",
                    chosen);
            yetty_ycore_error_destroy(provider_res.error);
            goto out;
        }
    }

    if (!have_latitude) {
        bool located = false;
        if (!no_geoip) {
            struct yetty_ycore_void_result geo_res =
                yetty_ymap_geolocate_public_ip(&latitude, &longitude);
            if (YETTY_IS_OK(geo_res)) {
                located = true;
                fprintf(stderr,
                        "ymap: centered on the public-IP location %.4f,%.4f "
                        "(city-level guess; pass --lat/--lon to override, "
                        "--no-geoip to never ask)\n",
                        latitude, longitude);
            } else {
                fprintf(stderr, "ymap: public-IP lookup failed (%s) — world view\n",
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

    uint32_t width_px = (uint32_t)width_cells * CELL_WIDTH_PX;
    uint32_t height_px = (uint32_t)height_cells * CELL_HEIGHT_PX;
    {
        struct yetty_ycore_void_result configure_res = yetty_ymap_configure(map_object, latitude, longitude, (uint32_t)zoom, width_px, height_px);
        if (YETTY_IS_ERR(configure_res)) {
            fprintf(stderr, "ymap: configure failed: %s\n", configure_res.error.msg);
            yetty_ycore_error_destroy(configure_res.error);
            goto out;
        }
    }

    if (interactive_mode) {
        exit_code = ymap_interactive_run(map_object, width_px, height_px);
        goto out;
    }

    struct yetty_ydraw_drawable_list_result render_res = yetty_ymap_render(map_object);
    if (YETTY_IS_ERR(render_res)) {
        fprintf(stderr, "ymap: render failed: %s\n", render_res.error.msg);
        yetty_ycore_error_destroy(render_res.error);
        goto out;
    }

    fflush(stdout);
    struct yetty_ycore_void_result emit_res =
        yetty_ymap_emit_osc(render_res.value, YMAP_STDOUT_FD_MAIN);
    yetty_ydraw_drawable_list_destroy(render_res.value);
    if (YETTY_IS_ERR(emit_res)) {
        fprintf(stderr, "ymap: emit failed: %s\n", emit_res.error.msg);
        yetty_ycore_error_destroy(emit_res.error);
        goto out;
    }

    /* Required by every provider's terms — keep visible under the map. */
    {
        struct yetty_ycore_const_char_ptr_result attribution_res =
            yetty_ymap_attribution(map_object);
        printf("%s\n", YETTY_IS_OK(attribution_res) ? attribution_res.value
                                                     : "(C) tile provider");
        if (YETTY_IS_ERR(attribution_res)) {
            yetty_ycore_error_destroy(attribution_res.error);
        }
    }
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
    exit_code = 0;

out:;
    struct yetty_ycore_void_result destroy_res = yetty_ymap_destroy(map_object);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    return exit_code;
}
