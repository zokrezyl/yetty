/*
 * ymcat - ship a RAW file to the terminal for terminal-side rendering.
 *
 * The counterpart of ycat with the rendering moved to the other end of the
 * wire: ycat renders client-side and ships drawable lists; ymcat ships the
 * file bytes untouched in a YETTY_DCS_MIME_FILE envelope and the terminal
 * detects the type (ymime) and runs the renderer itself (svg, markdown,
 * pdf, image). Wire format:
 *
 *   ESC P 600005 y <b64(yetty_yface_file_meta)> ; <b64(LZ4F(prologue + file))> ESC \
 *
 * The prologue (see <yetty/ymime/mime.h>) carries a MIME hint, a filename
 * hint, and optional render flags. v1 emits the single-shot form
 * (FIRST|LAST); the meta already carries the stream/sequence fields for
 * the future chunked form.
 *
 * Runs pure C; deliberately tiny — no renderer libraries, so it stays a
 * near-zero-dependency client for remote/ssh use.
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ymime/mime.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Options
 *===========================================================================*/

struct ymcat_opts {
    const char *mime_hint;   /* --mime=STRING: skip local detection */
    const char *render_args; /* --args=STRING: renderer flags, e.g. --font-size=18 */
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] [file|-]...\n"
            "\n"
            "  Wraps each input's RAW bytes in a DCS mime envelope; the yetty\n"
            "  terminal detects the file type and renders it terminal-side\n"
            "  (svg, markdown, pdf, image, music, circuit, mesh). No client-side\n"
            "  rendering — ycat is the client-side renderer, this tool is its\n"
            "  thin sibling.\n"
            "\n"
            "Options:\n"
            "  -M, --mime=STRING    declared MIME hint (default: local detection)\n"
            "  -A, --args=STRING    render flags forwarded to the terminal-side\n"
            "                       renderer (e.g. \"--font-size=18\")\n"
            "  -h, --help           show this help\n"
            "\n"
            "  Use '-' or no args to read from stdin.\n",
            prog);
}

/*=============================================================================
 * I/O helpers
 *===========================================================================*/

static int read_all_stream(FILE *stream, struct yetty_ycore_buffer *out)
{
    uint8_t chunk[65536];
    for (;;) {
        size_t got = fread(chunk, 1, sizeof(chunk), stream);
        if (got > 0) {
            struct yetty_ycore_void_result write_res = yetty_ycore_buffer_write(out, chunk, got);
            if (YETTY_IS_ERR(write_res)) {
                yetty_ycore_error_destroy(write_res.error);
                return -1;
            }
        }
        if (got < sizeof(chunk)) {
            return ferror(stream) ? -1 : 0;
        }
    }
}

/*=============================================================================
 * Envelope emit
 *===========================================================================*/

static struct yetty_ycore_size_result emit_file(const uint8_t *bytes, size_t len,
                                                const char *mime_hint, const char *name_hint,
                                                const char *render_args, FILE *out)
{
    if (!bytes || len == 0 || !out) {
        return YETTY_ERR(yetty_ycore_size, "ymcat: no bytes or no out");
    }

    /* Fill the MIME hint by local detection when the caller has none —
     * detection terminal-side would work too, but the hint is what
     * resolves the sniff-blind cases (markdown vs plain text). */
    if (!mime_hint || !*mime_hint) {
        enum yetty_ymime_type detected = yetty_ymime_detect(NULL, name_hint, bytes, len);
        mime_hint = yetty_ymime_type_canonical_mime(detected); /* may stay NULL */
    }

    /* Reduce the name hint to its basename — the receiver only needs the
     * extension, and a full local path leaks the sender's filesystem. */
    if (name_hint) {
        const char *slash = strrchr(name_hint, '/');
        if (slash) {
            name_hint = slash + 1;
        }
    }

    struct yetty_ymime_prologue prologue = {
        .mime = mime_hint,
        .mime_len = mime_hint ? strlen(mime_hint) : 0,
        .name = name_hint,
        .name_len = name_hint ? strlen(name_hint) : 0,
        .args = render_args,
        .args_len = render_args ? strlen(render_args) : 0,
    };
    /* u8 length fields on the wire — refuse rather than truncate. */
    if (prologue.mime_len > 255u || prologue.name_len > 255u) {
        return YETTY_ERR(yetty_ycore_size, "ymcat: hint too long");
    }
    uint8_t prologue_bytes[YETTY_YMIME_PROLOGUE_MAX];
    struct yetty_ycore_size_result prologue_res =
        yetty_ymime_prologue_encode(&prologue, prologue_bytes, sizeof(prologue_bytes));
    YETTY_RETURN_IF_ERR(yetty_ycore_size, prologue_res, "ymcat: prologue encode");
    size_t prologue_len = prologue_res.value;

    uint64_t total_raw = (uint64_t)prologue_len + (uint64_t)len;
    struct yetty_yface_file_meta meta = {
        .magic = YETTY_YFACE_FILE_MAGIC,
        .version = YETTY_YFACE_FILE_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .flags = YETTY_YFACE_FILE_FLAG_FIRST | YETTY_YFACE_FILE_FLAG_LAST,
        .stream_id = 0,
        .sequence = 0,
        .total_raw_size = total_raw,
        .chunk_raw_size = total_raw <= 0xFFFFFFFFull ? (uint32_t)total_raw : 0u,
        .reserved = 0,
    };

    /* Streaming write through a transient yface: prologue + file bytes go
     * through LZ4F + base64 without gluing them into one buffer first. */
    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_size, yface_res, "ymcat: yface create");
    struct yetty_yface *yface = yface_res.value;

    struct yetty_ycore_void_result step_res =
        yetty_yface_start_write(yface, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_MIME_FILE,
                                /*compressed=*/1, &meta, sizeof(meta));
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_yface_write(yface, prologue_bytes, prologue_len);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_yface_write(yface, bytes, len);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_yface_finish_write(yface);
    }
    if (YETTY_IS_ERR(step_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yface_destroy(yface);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_size, "ymcat: envelope write", step_res);
    }

    struct yetty_ycore_buffer *envelope = yetty_yface_out_buf(yface);
    size_t written = 0;
    if (envelope->size > 0) {
        written = fwrite(envelope->data, 1, envelope->size, out);
    }
    size_t expected = envelope->size;
    struct yetty_ycore_void_result destroy_res = yetty_yface_destroy(yface);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    if (written != expected) {
        return YETTY_ERR(yetty_ycore_size, "ymcat: short write");
    }
    return YETTY_OK(yetty_ycore_size, written);
}

/*=============================================================================
 * Per-file processing
 *===========================================================================*/

static int process_one(const char *arg, const struct ymcat_opts *opts)
{
    const bool is_stdin = (strcmp(arg, "-") == 0);
    const char *name_hint = is_stdin ? NULL : arg;

    struct yetty_ycore_buffer bytes = {0};
    int read_rc;
    if (is_stdin) {
        read_rc = read_all_stream(stdin, &bytes);
    } else {
        FILE *file = fopen(arg, "rb");
        if (!file) {
            fprintf(stderr, "ymcat: %s: failed to open\n", arg);
            return -1;
        }
        read_rc = read_all_stream(file, &bytes);
        fclose(file);
    }
    if (read_rc < 0) {
        fprintf(stderr, "ymcat: %s: failed to read\n", arg);
        yetty_ycore_buffer_destroy(&bytes);
        return -1;
    }

    struct yetty_ycore_size_result emit_res =
        emit_file(bytes.data, bytes.size, opts->mime_hint, name_hint, opts->render_args, stdout);
    yetty_ycore_buffer_destroy(&bytes);
    if (YETTY_IS_ERR(emit_res)) {
        fprintf(stderr, "ymcat: %s: %s\n", arg, emit_res.error.msg);
        yetty_ycore_error_destroy(emit_res.error);
        return -1;
    }
    return emit_res.value > 0 ? 0 : -1;
}

/*=============================================================================
 * main
 *===========================================================================*/

int main(int argc, char **argv)
{
    struct ymcat_opts opts = {
        .mime_hint = NULL,
        .render_args = NULL,
    };

    static const struct yetty_yplatform_option long_opts[] = {
        {"mime", required_argument, NULL, 'M'},
        {"args", required_argument, NULL, 'A'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int option;
    while ((option = yetty_yplatform_getopt_long(argc, argv, "M:A:h", long_opts, NULL)) != -1) {
        switch (option) {
        case 'M':
            opts.mime_hint = yetty_yplatform_optarg;
            break;
        case 'A':
            opts.render_args = yetty_yplatform_optarg;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    int rc = 0;
    if (yetty_yplatform_optind >= argc) {
        if (process_one("-", &opts) < 0) {
            rc = 1;
        }
    } else {
        for (int i = yetty_yplatform_optind; i < argc; i++) {
            if (process_one(argv[i], &opts) < 0) {
                rc = 1;
            }
        }
    }

    fflush(stdout);
    return rc;
}
