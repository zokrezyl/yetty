/*
 * ydoc — interactive rich-text document editor.
 *
 * Builds a yetty_yrich_ydoc, hands it to the shared yrich-runner which
 * pumps stdin events into the document and emits ydraw frames to the
 * canvas via OSC 666674.
 *
 * Usage:
 *   ydoc                       # built-in demo content
 *   ydoc -f path/to/sample.ydoc.yaml
 *   ydoc -f path/to/sample.ydoc.yaml --dump
 */

#include <yrich-runner.h>

#include <yetty/ydraw-core/buffer.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>

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
		"  -f, --file PATH  load document from YAML file\n"
		"  --dump           render once and exit (no interactive loop)\n"
		"  -h, --help       show this help\n",
		prog);
}

static void seed_demo(struct yetty_yrich_ydoc *d)
{
	struct {
		const char *text;
	} paras[] = {
		{ "Welcome to ydoc — a rich text editor in your terminal." },
		{ "" },
		{ "Type, navigate with arrow keys, select with shift, and "
		  "use Ctrl+Z / Ctrl+Y for undo / redo." },
		{ "" },
		{ "Press 'q' to quit." },
	};
	for (size_t i = 0; i < sizeof(paras) / sizeof(paras[0]); i++) {
		yetty_yrich_ydoc_add_paragraph(d, paras[i].text,
					       strlen(paras[i].text));
	}
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

	struct yetty_yrich_ydoc *doc = NULL;
	if (file_path) {
		struct yetty_yrich_ydoc_ptr_result lr =
			yetty_yrich_ydoc_load_yaml_file(file_path);
		if (YETTY_IS_ERR(lr)) {
			fprintf(stderr, "ydoc: load %s: %s\n", file_path,
				lr.error.msg);
			return 1;
		}
		doc = lr.value;
	} else {
		struct yetty_yrich_ydoc_ptr_result dr =
			yetty_yrich_ydoc_create();
		if (YETTY_IS_ERR(dr)) {
			fprintf(stderr, "ydoc: %s\n", dr.error.msg);
			return 1;
		}
		doc = dr.value;
		seed_demo(doc);
	}

	struct yetty_ydraw_core_buffer_config bcfg = {0};
	bcfg.scene_max_x = yetty_yrich_document_content_width(&doc->base);
	bcfg.scene_max_y = yetty_yrich_document_content_height(&doc->base);
	struct yetty_ydraw_core_buffer_result br =
		yetty_ydraw_core_buffer_config_buffer_create(&bcfg);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "ydoc: %s\n", br.error.msg);
		yetty_yrich_document_destroy(&doc->base);
		return 1;
	}
	struct yetty_ydraw_core_buffer *buf = br.value;
	yetty_yrich_document_set_buffer(&doc->base, buf);

	struct yrich_runner runner;
	yrich_runner_init(&runner, &doc->base, buf);
	runner.dump_once = dump;

	if (!dump) {
		yrich_runner_raw_mode_enable();
		yrich_runner_subscribe(true);
	}

	struct yetty_ycore_void_result rr = yrich_runner_loop(&runner);
	int rc = YETTY_IS_OK(rr) ? 0 : 1;
	if (YETTY_IS_ERR(rr))
		fprintf(stderr, "ydoc: %s\n", rr.error.msg);

	if (!dump)
		yrich_runner_subscribe(false);

	yrich_runner_fini(&runner);
	yetty_ydraw_core_buffer_destroy(buf);
	yetty_yrich_document_destroy(&doc->base);
	return rc;
}
