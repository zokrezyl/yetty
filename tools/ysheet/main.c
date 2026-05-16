/*
 * ysheet — interactive spreadsheet editor.
 *
 * Renders a yetty_yrich_spreadsheet to a ydraw buffer and emits frames as
 * OSC 666674. Mouse / key events come back over stdin and drive the
 * document via the shared runner.
 *
 * Usage:
 *   ysheet                              # built-in demo grid
 *   ysheet -f path/to/sample.ysheet.yaml
 *   ysheet -f path/to/sample.ysheet.yaml --dump
 */

#include <yrich-runner.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>
#include <yetty/yrich/yspreadsheet.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -f, --file PATH  load spreadsheet from YAML file\n"
		"  --dump           render once and exit (no interactive loop)\n"
		"  -h, --help       show this help\n",
		prog);
}

static void set_cell(struct yetty_yrich_spreadsheet *s, int row, int col,
		     const char *value)
{
	struct yetty_yrich_cell_addr addr = { row, col };
	struct yetty_ycore_void_result r =
		yetty_yrich_spreadsheet_set_cell_value(s, addr, value, strlen(value));
	if (YETTY_IS_ERR(r)) {
		yetty_ycore_error_destroy(r.error);
	}
}

static void seed_demo(struct yetty_yrich_spreadsheet *s)
{
	yetty_yrich_spreadsheet_set_grid_size(s, 50, 20);
	set_cell(s, 0, 0, "yrich spreadsheet");
	set_cell(s, 1, 0, "A");
	set_cell(s, 1, 1, "B");
	set_cell(s, 1, 2, "C");
	set_cell(s, 2, 0, "100");
	set_cell(s, 2, 1, "200");
	set_cell(s, 2, 2, "300");
}

int main(int argc, char **argv)
{
	bool dump = false;
	const char *file_path = NULL;

	static const struct yetty_yplatform_option long_opts[] = {
		{ "file", required_argument, NULL, 'f' },
		{ "dump", no_argument,       NULL, 'D' },
		{ "help", no_argument,       NULL, 'h' },
		{ NULL,   0,                 NULL, 0   },
	};

	int c;
	while ((c = yetty_yplatform_getopt_long(argc, argv, "f:h", long_opts, NULL)) != -1) {
		switch (c) {
		case 'f': file_path = yetty_yplatform_optarg; break;
		case 'D': dump = true; break;
		case 'h': usage(stdout, argv[0]); return 0;
		default:  usage(stderr, argv[0]); return 2;
		}
	}

	struct yetty_yrich_spreadsheet *sheet = NULL;
	if (file_path) {
		struct yetty_yrich_spreadsheet_ptr_result lr =
			yetty_yrich_spreadsheet_load_yaml_file(file_path);
		if (YETTY_IS_ERR(lr)) {
			fprintf(stderr, "ysheet: load %s: %s\n", file_path,
				lr.error.msg);
			return 1;
		}
		sheet = lr.value;
	} else {
		struct yetty_yrich_spreadsheet_ptr_result sr =
			yetty_yrich_spreadsheet_create();
		if (YETTY_IS_ERR(sr)) {
			fprintf(stderr, "ysheet: %s\n", sr.error.msg);
			return 1;
		}
		sheet = sr.value;
		seed_demo(sheet);
	}

	struct yetty_ydraw_core_draw_list_config bcfg = {0};
	bcfg.scene_max_x = yetty_yrich_document_content_width(&sheet->base);
	bcfg.scene_max_y = yetty_yrich_document_content_height(&sheet->base);
	struct yetty_ydraw_core_draw_list_result br =
		yetty_ydraw_core_draw_list_config_buffer_create(&bcfg);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "ysheet: %s\n", br.error.msg);
		yetty_yrich_document_destroy(&sheet->base);
		return 1;
	}
	struct yetty_ydraw_core_draw_list *buf = br.value;
	yetty_yrich_document_set_buffer(&sheet->base, buf);

	struct yrich_runner runner;
	yrich_runner_init(&runner, &sheet->base, buf);
	runner.dump_once = dump;

	if (!dump) {
		yrich_runner_raw_mode_enable();
		yrich_runner_subscribe(true);
	}

	struct yetty_ycore_void_result rr = yrich_runner_loop(&runner);
	int rc = YETTY_IS_OK(rr) ? 0 : 1;
	if (YETTY_IS_ERR(rr))
		fprintf(stderr, "ysheet: %s\n", rr.error.msg);

	if (!dump)
		yrich_runner_subscribe(false);

	yrich_runner_fini(&runner);
	yetty_ydraw_core_draw_list_destroy(buf);
	yetty_yrich_document_destroy(&sheet->base);
	return rc;
}
