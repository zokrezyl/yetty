/*
 * yetty-ythorvg - Render SVG/Lottie via ThorVG into a ypaint buffer,
 * emit the result as a ypaint OSC sequence on stdout.
 *
 * Usage: yetty-ythorvg [options] <input-file>
 *
 * Consumers embed the output in a terminal that speaks ypaint OSC, e.g.:
 *   yetty-ythorvg logo.svg
 *   yetty-ythorvg --lottie anim.json --frame 12
 */

#include <yetty/yplatform/getopt.h>
#include <yetty/ycore/util.h>
#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yterm/osc-codes.h>    /* YETTY_OSC_YPAINT_* */
#include <yetty/ythorvg/ythorvg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
	fprintf(stderr,
	        "Usage: %s [options] <input-file>\n"
	        "Options:\n"
	        "  --svg            Force SVG input (default: auto-detect)\n"
	        "  --lottie         Force Lottie input\n"
	        "  --frame N        Render frame N of a Lottie animation (default: 0)\n"
	        "  -w, --width N    Target viewport width in pixels (default: 800)\n"
	        "  -h, --height N   Target viewport height in pixels (default: 600)\n"
	        "      --clear      Emit an OSC clear sequence before the content\n"
	        "  -v, --verbose    Print stats to stderr\n"
	        "      --help       Show this message\n",
	        prog);
}

int main(int argc, char **argv) {
	static const struct yetty_yplatform_option long_opts[] = {
	    {"svg",     no_argument,       NULL, 's'},
	    {"lottie",  no_argument,       NULL, 'l'},
	    {"frame",   required_argument, NULL, 'f'},
	    {"width",   required_argument, NULL, 'w'},
	    {"height",  required_argument, NULL, 'h'},
	    {"clear",   no_argument,       NULL, 'c'},
	    {"verbose", no_argument,       NULL, 'v'},
	    {"help",    no_argument,       NULL, 'H'},
	    {NULL, 0, NULL, 0},
	};

	const char *mimetype = NULL;
	float       frame    = 0.0f;
	int         have_frame = 0;
	uint32_t    width    = 800;
	uint32_t    height   = 600;
	int         do_clear = 0;
	int         verbose  = 0;

	int opt;
	while ((opt = yetty_yplatform_getopt_long(argc, argv, "w:h:f:vH", long_opts, NULL)) != -1) {
		switch (opt) {
		case 's': mimetype   = "svg";    break;
		case 'l': mimetype   = "lottie"; break;
		case 'f': frame      = (float)atof(yetty_yplatform_optarg); have_frame = 1; break;
		case 'w': width      = (uint32_t)atoi(yetty_yplatform_optarg); break;
		case 'h': height     = (uint32_t)atoi(yetty_yplatform_optarg); break;
		case 'c': do_clear   = 1; break;
		case 'v': verbose    = 1; break;
		case 'H': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 1;
		}
	}

	if (yetty_yplatform_optind >= argc) {
		fprintf(stderr, "%s: missing input file\n", argv[0]);
		usage(argv[0]);
		return 1;
	}
	const char *input_path = argv[yetty_yplatform_optind];

	/* Read the source file. */
	struct yetty_ycore_buffer_result file_res = yetty_ycore_read_file(input_path);
	if (YETTY_IS_ERR(file_res)) {
		fprintf(stderr, "%s: failed to read %s: %s\n",
		        argv[0], input_path, file_res.error.msg);
		return 1;
	}

	/* Create the ypaint buffer + thorvg renderer. */
	struct yetty_ypaint_core_buffer_result buf_res =
	    yetty_ypaint_core_buffer_config_buffer_create(NULL);
	if (YETTY_IS_ERR(buf_res)) {
		fprintf(stderr, "%s: buffer_create: %s\n", argv[0], buf_res.error.msg);
		free(file_res.value.data);
		return 1;
	}
	struct yetty_ypaint_core_buffer *buf = buf_res.value;

	struct yetty_ythorvg_renderer_ptr_result r_res =
	    yetty_ythorvg_renderer_create(buf);
	if (YETTY_IS_ERR(r_res)) {
		fprintf(stderr, "%s: renderer_create: %s\n",
		        argv[0], r_res.error.msg);
		yetty_ypaint_core_buffer_destroy(buf);
		free(file_res.value.data);
		return 1;
	}
	struct yetty_ythorvg_renderer *renderer = r_res.value;
	yetty_ythorvg_renderer_set_target(renderer, width, height);

	/* Render (loads SVG/Lottie and emits primitives into buf). */
	float content_w = 0.0f, content_h = 0.0f;
	struct yetty_ycore_void_result rr =
	    yetty_ythorvg_renderer_render(renderer, file_res.value.data, file_res.value.size,
	                        mimetype, &content_w, &content_h);
	free(file_res.value.data);
	if (YETTY_IS_ERR(rr)) {
		fprintf(stderr, "%s: render: %s\n", argv[0], rr.error.msg);
		yetty_ythorvg_renderer_destroy(renderer);
		yetty_ypaint_core_buffer_destroy(buf);
		return 1;
	}

	/* For Lottie, advance to the requested frame (no-op for static SVG). */
	if (have_frame && yetty_ythorvg_renderer_total_frames(renderer) > 0.0f) {
		struct yetty_ycore_void_result fr =
		    yetty_ythorvg_renderer_render_frame(renderer, frame);
		if (YETTY_IS_ERR(fr)) {
			fprintf(stderr, "%s: render_frame: %s\n", argv[0], fr.error.msg);
			yetty_ythorvg_renderer_destroy(renderer);
			yetty_ypaint_core_buffer_destroy(buf);
			return 1;
		}
	}

	if (verbose) {
		fprintf(stderr, "ythorvg: %s content=%.0fx%.0f target=%ux%u",
		        input_path, content_w, content_h, width, height);
		float tf = yetty_ythorvg_renderer_total_frames(renderer);
		if (tf > 0.0f)
			fprintf(stderr, " frames=%.0f dur=%.3fs frame=%.2f",
			        tf, yetty_ythorvg_renderer_duration(renderer), frame);
		fprintf(stderr, "\n");
	}

	/* Emit OSC. */
	if (do_clear) {
		printf("\033]%u;;\033\\", YETTY_OSC_YPAINT_CLEAR);
	}

	const uint8_t *raw = NULL;
	size_t raw_size = yetty_ypaint_core_buffer_serialize(buf, &raw);
	if (raw_size == 0 || !raw) {
		fprintf(stderr, "%s: serialize failed\n", argv[0]);
		yetty_ythorvg_renderer_destroy(renderer);
		yetty_ypaint_core_buffer_destroy(buf);
		return 1;
	}

	/* OSC: ESC ] 600001 ; <b64(bin meta)> ; <base64(LZ4F(framed))> ESC \ */
	struct yetty_yface_bin_meta meta = {
		.magic            = YETTY_YFACE_BIN_MAGIC,
		.version          = YETTY_YFACE_BIN_VERSION,
		.compressed       = YETTY_YFACE_COMP_LZ4F,
		.compression_algo = 0,
		.raw_size         = raw_size,
		.reserved         = {0, 0},
	};
	struct yetty_ycore_void_result emit_r = yetty_yface_emit_to_fd(
	    fileno(stdout), YETTY_OSC_YPAINT_BIN,
	    /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
	if (YETTY_IS_ERR(emit_r))
		fprintf(stderr, "%s: yface_emit: %s\n", argv[0], emit_r.error.msg);
	fflush(stdout);

	yetty_ythorvg_renderer_destroy(renderer);
	yetty_ypaint_core_buffer_destroy(buf);
	return 0;
}
