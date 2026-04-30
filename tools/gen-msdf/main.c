/*
 * yetty-msdf-gen - CLI tool for MSDF CDB generation
 *
 * Usage: yetty-msdf-gen [options] <font.ttf> <output-dir>
 */

#include <yetty/ymsdf-gen/ymsdf-gen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options] <font.ttf> <output-dir>\n", prog);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --all           Generate all glyphs in font\n");
	fprintf(stderr, "  --nerd-fonts    Include Nerd Font symbols\n");
	fprintf(stderr, "  --cjk           Include CJK characters\n");
	fprintf(stderr, "  --size N        Font size in pixels (default: 32)\n");
	fprintf(stderr, "  --range N       MSDF pixel range (default: 4)\n");
	fprintf(stderr, "  -j N            Thread count (default: auto)\n");
}

int main(int argc, char *argv[])
{
	struct yetty_ymsdf_gen_config config = {0};
	config.font_size = 32.0f;
	config.pixel_range = 4.0f;

	const char *ttf_path = NULL;
	const char *output_dir = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--all") == 0) {
			config.all_glyphs = 1;
		} else if (strcmp(argv[i], "--nerd-fonts") == 0) {
			config.include_nerd_fonts = 1;
		} else if (strcmp(argv[i], "--cjk") == 0) {
			config.include_cjk = 1;
		} else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			config.font_size = (float)atof(argv[++i]);
		} else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
			config.pixel_range = (float)atof(argv[++i]);
		} else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
			config.thread_count = atoi(argv[++i]);
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return 1;
		} else if (!ttf_path) {
			ttf_path = argv[i];
		} else if (!output_dir) {
			output_dir = argv[i];
		}
	}

	if (!ttf_path || !output_dir) {
		usage(argv[0]);
		return 1;
	}

	config.ttf_path = ttf_path;
	config.output_dir = output_dir;

	fprintf(stderr, "MSDF Generator\n");
	fprintf(stderr, "  Font: %s\n", ttf_path);
	fprintf(stderr, "  Output: %s\n", output_dir);
	fprintf(stderr, "  Size: %.0fpx\n", config.font_size);
	fprintf(stderr, "  Pixel range: %.0f\n", config.pixel_range);

	struct yetty_ycore_void_result res = yetty_ymsdf_gen_config_cpu_generate(&config);
	if (YETTY_IS_ERR(res)) {
		fprintf(stderr, "Error: %s\n", res.error.msg);
		return 1;
	}

	fprintf(stderr, "Done.\n");
	return 0;
}
