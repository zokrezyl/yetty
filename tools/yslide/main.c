/*
 * yslide — interactive presentation editor.
 *
 * Builds a yetty_yrich_slides with a few demo slides, hands it to the
 * shared runner. PageUp / PageDown switch slides; Esc exits presentation.
 *
 * Usage:
 *   yslide                              # built-in demo deck
 *   yslide -f path/to/sample.yslide.yaml
 *   yslide -f path/to/sample.yslide.yaml --dump
 */

#include <yrich-runner.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>
#include <yetty/yrich/yslides.h>

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
		"  -f, --file PATH  load presentation from YAML file\n"
		"  --dump           render once and exit (no interactive loop)\n"
		"  -h, --help       show this help\n",
		prog);
}

static void seed_demo(struct yetty_yrich_slides *s)
{
	/* Slide 1 — title. */
	yetty_yrich_slides_add_textbox(s, 200, 150, 560, 80,
				       "Welcome to yslide",
				       strlen("Welcome to yslide"));
	yetty_yrich_slides_add_textbox(s, 200, 250, 560, 40,
				       "Presentations on the yetty canvas.",
				       strlen("Presentations on the yetty canvas."));

	/* Slide 2 — shapes. */
	yetty_yrich_slides_add_slide(s);
	yetty_yrich_slides_set_current(s, 1);
	yetty_yrich_slides_add_textbox(s, 200, 50, 560, 50,
				       "Shape examples",
				       strlen("Shape examples"));
	yetty_yrich_slides_add_rectangle(s, 100, 150, 200, 150);
	yetty_yrich_slides_add_ellipse(s, 400, 150, 200, 150);
	yetty_yrich_slides_add_line(s, 150, 400, 550, 400);

	/* Slide 3 — features. */
	yetty_yrich_slides_add_slide(s);
	yetty_yrich_slides_set_current(s, 2);
	yetty_yrich_slides_add_textbox(s, 200, 50, 560, 50,
				       "Features", strlen("Features"));
	const char *bullets[] = {
		"- Multiple shapes",
		"- Text editing",
		"- Keyboard navigation",
	};
	float y = 150.0f;
	for (size_t i = 0; i < sizeof(bullets) / sizeof(bullets[0]); i++) {
		yetty_yrich_slides_add_textbox(s, 100, y, 400, 40,
					       bullets[i],
					       strlen(bullets[i]));
		y += 50.0f;
	}

	yetty_yrich_slides_set_current(s, 0);
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

	struct yetty_yrich_slides *deck = NULL;
	if (file_path) {
		struct yetty_yrich_slides_ptr_result lr =
			yetty_yrich_slides_load_yaml_file(file_path);
		if (YETTY_IS_ERR(lr)) {
			fprintf(stderr, "yslide: load %s: %s\n", file_path,
				lr.error.msg);
			return 1;
		}
		deck = lr.value;
	} else {
		struct yetty_yrich_slides_ptr_result sr =
			yetty_yrich_slides_create();
		if (YETTY_IS_ERR(sr)) {
			fprintf(stderr, "yslide: %s\n", sr.error.msg);
			return 1;
		}
		deck = sr.value;
		seed_demo(deck);
	}

	struct yetty_ydraw_core_draw_list_config bcfg = {
		.scene_max_x = deck->slide_width,
		.scene_max_y = deck->slide_height,
	};
	struct yetty_ydraw_core_draw_list_result br =
		yetty_ydraw_core_draw_list_config_buffer_create(&bcfg);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "yslide: %s\n", br.error.msg);
		yetty_yrich_document_destroy(&deck->base);
		return 1;
	}
	struct yetty_ydraw_core_draw_list *buf = br.value;
	yetty_yrich_document_set_buffer(&deck->base, buf);

	struct yrich_runner runner;
	yrich_runner_init(&runner, &deck->base, buf);
	runner.dump_once = dump;

	if (!dump) {
		yrich_runner_raw_mode_enable();
		yrich_runner_subscribe(true);
	}

	struct yetty_ycore_void_result rr = yrich_runner_loop(&runner);
	int rc = YETTY_IS_OK(rr) ? 0 : 1;
	if (YETTY_IS_ERR(rr))
		fprintf(stderr, "yslide: %s\n", rr.error.msg);

	if (!dump)
		yrich_runner_subscribe(false);

	yrich_runner_fini(&runner);
	yetty_ydraw_core_draw_list_destroy(buf);
	yetty_yrich_document_destroy(&deck->base);
	return rc;
}
