/*
 * ymesh — emit a ymesh complex-prim OSC envelope for a glTF 2.0 (.glb) file.
 *
 * Inside a yetty terminal the OSC is routed to the ypaint scrolling layer,
 * which renders the mesh via the ymesh pipeline. Outside yetty the bytes
 * are still written (mostly garbage on a vt100), so typical usage is
 * `yetty -e 'ymesh path/to/model.glb'` or invocation from a script running
 * in a yetty session.
 */

#include <yetty/ymesh/ymesh.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] <path-to-glb>\n"
        "\n"
        "Emit a YMesh OSC envelope (YETTY_OSC_YPAINT_BIN) consumed by the\n"
        "yetty ypaint scrolling layer.\n"
        "\n"
        "Options:\n"
        "  -w <px>   Display width in pixels  (default 400)\n"
        "  -H <px>   Display height in pixels (default 400)\n"
        "  -h, --help  Show this help\n"
        "\n"
        "Example:\n"
        "  yetty -e '%s Box.glb'\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    struct yetty_ymesh_render_config cfg = {0};
    cfg.bounds_w = 400.0f;
    cfg.bounds_h = 400.0f;

    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(a, "-w") == 0 && i + 1 < argc) {
            cfg.bounds_w = (float)atof(argv[++i]);
        } else if (strcmp(a, "-H") == 0 && i + 1 < argc) {
            cfg.bounds_h = (float)atof(argv[++i]);
        } else if (a[0] == '-') {
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

    struct yetty_ypaint_core_buffer_result br = yetty_ymesh_render_path(path, &cfg);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "ymesh: %s\n", br.error.msg);
        return 1;
    }

    struct yetty_ycore_size_result sr = yetty_ymesh_osc_bin_emit(br.value, stdout);
    yetty_ypaint_core_buffer_destroy(br.value);
    if (YETTY_IS_ERR(sr)) {
        fprintf(stderr, "ymesh: emit failed: %s\n", sr.error.msg);
        return 1;
    }
    fputc('\n', stdout);
    return 0;
}
