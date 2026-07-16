/*
 * ynb-cat — render a Jupyter .ipynb notebook to a yetty terminal.
 *
 * Drives the yetty_ynotebook model: it loads the notebook, walks every cell,
 * prints markdown/code sources, and for each output ships the RICHEST MIME
 * representation as a rich figure (image/png, image/svg+xml, application/pdf,
 * text/markdown, text/html) using the same YETTY_DCS_MIME_FILE envelope the
 * host-mime demos use — so the images, vector art and documents embedded in a
 * notebook's outputs render inline, not as base64 text.
 *
 * Usage (from inside yetty):
 *   ./build-.../tools/ynb-cat/ynb-cat demo/assets/ynotebook/showcase.ipynb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/yface/yface.h>
#include <yetty/ymime/mime.h>
#include <yetty/ynotebook/mime-bundle.h>
#include <yetty/ynotebook/notebook.h>
#include <yetty/yterminal/dcs-codes.h>

/* ANSI helpers — plain terminal styling for the prompts/labels. */
#define ANSI_RESET "\033[0m"
#define ANSI_PROMPT "\033[1;36m" /* cyan bold — In[]/Out[] prompts */
#define ANSI_DIM "\033[2m"       /* dim — stream text, separators */
#define ANSI_ERR "\033[1;31m"    /* red bold — error name/value */
#define ANSI_MD "\033[1;35m"     /* magenta — markdown cell marker */

/* Priority of a MIME type as a rich representation — higher wins when a bundle
 * carries several alternatives. 0 means "print as text, do not render". */
static int mime_render_priority(const char *mime)
{
    if (strcmp(mime, "image/svg+xml") == 0) {
        return 100;
    }
    if (strcmp(mime, "application/pdf") == 0) {
        return 90;
    }
    if (strncmp(mime, "image/", 6) == 0) {
        return 80; /* png / jpeg / gif / bmp — yimage */
    }
    if (strcmp(mime, "text/html") == 0) {
        return 60;
    }
    if (strcmp(mime, "text/markdown") == 0) {
        return 40;
    }
    return 0;
}

/* A filename whose extension hints the type to the terminal's detector. */
static const char *mime_name_hint(const char *mime)
{
    if (strcmp(mime, "image/svg+xml") == 0) {
        return "output.svg";
    }
    if (strcmp(mime, "image/png") == 0) {
        return "output.png";
    }
    if (strcmp(mime, "image/jpeg") == 0) {
        return "output.jpg";
    }
    if (strcmp(mime, "image/gif") == 0) {
        return "output.gif";
    }
    if (strcmp(mime, "application/pdf") == 0) {
        return "output.pdf";
    }
    if (strcmp(mime, "text/html") == 0) {
        return "output.html";
    }
    if (strcmp(mime, "text/markdown") == 0) {
        return "output.md";
    }
    return "output.bin";
}

/* Ship raw bytes as a single-shot YETTY_DCS_MIME_FILE envelope so the host
 * yetty decodes and renders them. Mirrors the host-mime wire shape, built with
 * the ymime prologue codec + the yface emitter instead of by hand. */
static int render_bytes(const char *mime, const uint8_t *bytes, size_t len)
{
    struct yetty_ymime_prologue prologue = {
        .mime = mime,
        .mime_len = strlen(mime),
        .name = mime_name_hint(mime),
        .name_len = strlen(mime_name_hint(mime)),
        .args = NULL,
        .args_len = 0,
    };
    uint8_t prologue_buf[YETTY_YMIME_PROLOGUE_MAX];
    struct yetty_ycore_size_result prologue_r =
        yetty_ymime_prologue_encode(&prologue, prologue_buf, sizeof(prologue_buf));
    if (YETTY_IS_ERR(prologue_r)) {
        yetty_ycore_error_destroy(prologue_r.error);
        return -1;
    }
    size_t prologue_len = prologue_r.value;

    size_t body_len = prologue_len + len;
    uint8_t *body = malloc(body_len ? body_len : 1);
    if (!body) {
        return -1;
    }
    memcpy(body, prologue_buf, prologue_len);
    if (len) {
        memcpy(body + prologue_len, bytes, len);
    }

    struct yetty_yface_file_meta meta = {
        .magic = YETTY_YFACE_FILE_MAGIC,
        .version = YETTY_YFACE_FILE_VERSION,
        .compressed = 0,
        .compression_algo = 0,
        .flags = YETTY_YFACE_FILE_FLAG_FIRST | YETTY_YFACE_FILE_FLAG_LAST,
        .stream_id = 0,
        .sequence = 0,
        .total_raw_size = body_len,
        .chunk_raw_size = (uint32_t)body_len,
        .reserved = 0,
    };

    /* stdio is buffered; the envelope is a raw write(2). Flush first so the
     * prompt printed just above lands before the figure. */
    fflush(stdout);
    struct yetty_ycore_void_result emit_r = yetty_yface_emit_to_fd(
        STDOUT_FILENO, YETTY_DCS_MIME_FILE, 0, &meta, sizeof(meta), body, body_len);
    free(body);
    if (YETTY_IS_ERR(emit_r)) {
        yetty_ycore_error_destroy(emit_r.error);
        return -1;
    }
    return 0;
}

/* Render one execute_result / display_data output: pick the richest repr, ship
 * it if renderable, else fall back to its text/plain. */
static void render_bundle(struct yetty_yclass_object *bundle)
{
    struct yetty_ycore_size_result count_r = yetty_ynotebook_mime_bundle_count(bundle);
    if (YETTY_IS_ERR(count_r)) {
        yetty_ycore_error_destroy(count_r.error);
        return;
    }
    size_t count = count_r.value;

    size_t best = count; /* sentinel: none renderable */
    int best_priority = 0;
    size_t text_plain = count;
    size_t text_fallback = count; /* any text-ish repr, for the no-render case */
    for (size_t i = 0; i < count; i++) {
        struct yetty_ycore_const_char_ptr_result mime_r =
            yetty_ynotebook_mime_bundle_mime_at(bundle, i);
        if (YETTY_IS_ERR(mime_r)) {
            yetty_ycore_error_destroy(mime_r.error);
            continue;
        }
        const char *mime = mime_r.value;
        if (strcmp(mime, "text/plain") == 0) {
            text_plain = i;
        }
        if (text_fallback == count &&
            (strncmp(mime, "text/", 5) == 0 || strcmp(mime, "application/json") == 0)) {
            text_fallback = i;
        }
        int priority = mime_render_priority(mime);
        if (priority > best_priority) {
            best_priority = priority;
            best = i;
        }
    }

    if (best < count) {
        const char *mime = yetty_ynotebook_mime_bundle_mime_at(bundle, best).value;
        const uint8_t *bytes = NULL;
        size_t len = 0;
        struct yetty_ycore_void_result bytes_r =
            yetty_ynotebook_mime_bundle_bytes_at(bundle, best, &bytes, &len);
        if (YETTY_IS_OK(bytes_r)) {
            printf("    " ANSI_DIM "[%s, %zu bytes]" ANSI_RESET "\n", mime, len);
            if (render_bytes(mime, bytes, len) != 0) {
                printf("    " ANSI_ERR "(failed to render %s)" ANSI_RESET "\n", mime);
            }
        } else {
            yetty_ycore_error_destroy(bytes_r.error);
        }
        return;
    }

    /* No renderable repr — print text/plain, else any text-ish repr (JSON,
     * LaTeX). */
    size_t print_index = text_plain < count ? text_plain : text_fallback;
    if (print_index < count) {
        const char *mime = yetty_ynotebook_mime_bundle_mime_at(bundle, print_index).value;
        if (strcmp(mime, "application/json") == 0) {
            /* JSON reprs are retained as a document, not bytes — serialize. */
            struct yetty_ycore_char_ptr_result json_r =
                yetty_ynotebook_mime_bundle_json_at(bundle, print_index);
            if (YETTY_IS_OK(json_r)) {
                printf("    " ANSI_DIM "[%s]" ANSI_RESET "\n%s\n", mime, json_r.value);
                free(json_r.value);
            } else {
                yetty_ycore_error_destroy(json_r.error);
            }
        } else {
            const uint8_t *bytes = NULL;
            size_t len = 0;
            struct yetty_ycore_void_result bytes_r =
                yetty_ynotebook_mime_bundle_bytes_at(bundle, print_index, &bytes, &len);
            if (YETTY_IS_OK(bytes_r)) {
                printf("    " ANSI_DIM "[%s]" ANSI_RESET " %.*s\n", mime, (int)len,
                       (const char *)bytes);
            } else {
                yetty_ycore_error_destroy(bytes_r.error);
            }
        }
    }
}

static void render_output(struct yetty_yclass_object *output)
{
    const char *type = yetty_ynotebook_output_type(output).value;

    if (strcmp(type, "stream") == 0) {
        struct yetty_ycore_char_ptr_result text_r = yetty_ynotebook_output_text(output);
        if (YETTY_IS_OK(text_r)) {
            printf(ANSI_DIM "%s" ANSI_RESET, text_r.value);
            free(text_r.value);
        }
        return;
    }
    if (strcmp(type, "error") == 0) {
        const char *name = yetty_ynotebook_output_error_name(output).value;
        const char *value = yetty_ynotebook_output_error_value(output).value;
        printf(ANSI_ERR "%s: %s" ANSI_RESET "\n", name, value);
        struct yetty_ycore_char_ptr_result text_r = yetty_ynotebook_output_text(output);
        if (YETTY_IS_OK(text_r)) {
            printf(ANSI_DIM "%s" ANSI_RESET "\n", text_r.value);
            free(text_r.value);
        }
        return;
    }
    /* execute_result / display_data → rich MIME bundle. The bundle (like the
     * cell/output views) is owned by the notebook's retained document tree —
     * borrowed here, freed by notebook_destroy; do not destroy it. */
    struct yetty_yclass_object_ptr_result bundle_r = yetty_ynotebook_output_bundle(output);
    if (YETTY_IS_OK(bundle_r)) {
        render_bundle(bundle_r.value);
    } else {
        yetty_ycore_error_destroy(bundle_r.error);
    }
}

static void render_cell(struct yetty_yclass_object *cell, size_t index)
{
    const char *type = yetty_ynotebook_cell_type(cell).value;
    struct yetty_ycore_char_ptr_result source_r = yetty_ynotebook_cell_source(cell);
    const char *source = YETTY_IS_OK(source_r) ? source_r.value : "";

    if (strcmp(type, "markdown") == 0) {
        printf("\n" ANSI_MD "# markdown cell %zu" ANSI_RESET "\n", index);
        /* Render the markdown source itself as a rich figure. */
        if (source[0]) {
            render_bytes("text/markdown", (const uint8_t *)source, strlen(source));
        }
    } else if (strcmp(type, "code") == 0) {
        struct yetty_ycore_int_result exec_r = yetty_ynotebook_cell_execution_count(cell);
        int exec = YETTY_IS_OK(exec_r) ? exec_r.value : -1;
        if (exec >= 0) {
            printf("\n" ANSI_PROMPT "In [%d]:" ANSI_RESET " %s\n", exec, source);
        } else {
            printf("\n" ANSI_PROMPT "In [ ]:" ANSI_RESET " %s\n", source);
        }

        struct yetty_ycore_size_result out_count_r = yetty_ynotebook_cell_output_count(cell);
        size_t out_count = YETTY_IS_OK(out_count_r) ? out_count_r.value : 0;
        for (size_t i = 0; i < out_count; i++) {
            struct yetty_yclass_object_ptr_result output_r =
                yetty_ynotebook_cell_output_at(cell, i);
            if (YETTY_IS_OK(output_r)) {
                render_output(output_r.value); /* borrowed — owned by the cell */
            } else {
                yetty_ycore_error_destroy(output_r.error);
            }
        }
    } else {
        printf("\n" ANSI_DIM "(%s cell %zu)" ANSI_RESET "\n%s\n", type, index, source);
    }

    if (YETTY_IS_OK(source_r)) {
        free(source_r.value);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <notebook.ipynb>\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];

    struct yetty_ycore_void_result reg_r = yetty_ynotebook_register();
    if (YETTY_IS_ERR(reg_r)) {
        fprintf(stderr, "ynb-cat: register failed: %s\n", reg_r.error.msg);
        yetty_ycore_error_destroy(reg_r.error);
        return 1;
    }

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result nb_r = yetty_ynotebook_notebook_create(&ctx);
    if (YETTY_IS_ERR(nb_r)) {
        fprintf(stderr, "ynb-cat: notebook create failed\n");
        yetty_ycore_error_destroy(nb_r.error);
        return 1;
    }
    struct yetty_yclass_object *notebook = nb_r.value;

    struct yetty_ycore_void_result load_r = yetty_ynotebook_notebook_load_file(notebook, path);
    if (YETTY_IS_ERR(load_r)) {
        fprintf(stderr, "ynb-cat: cannot load %s: %s\n", path, load_r.error.msg);
        yetty_ycore_error_destroy(load_r.error);
        yetty_ynotebook_notebook_destroy(notebook);
        return 1;
    }

    struct yetty_ycore_int_result fmt_r = yetty_ynotebook_notebook_nbformat(notebook);
    struct yetty_ycore_size_result count_r = yetty_ynotebook_notebook_cell_count(notebook);
    size_t cell_count = YETTY_IS_OK(count_r) ? count_r.value : 0;
    printf(ANSI_PROMPT "notebook" ANSI_RESET " %s " ANSI_DIM "(nbformat %d, %zu cells)" ANSI_RESET
                       "\n",
           path, YETTY_IS_OK(fmt_r) ? fmt_r.value : 0, cell_count);

    for (size_t i = 0; i < cell_count; i++) {
        struct yetty_yclass_object_ptr_result cell_r =
            yetty_ynotebook_notebook_cell_at(notebook, i);
        if (YETTY_IS_OK(cell_r)) {
            render_cell(cell_r.value, i); /* borrowed — owned by the notebook */
        } else {
            yetty_ycore_error_destroy(cell_r.error);
        }
    }

    printf("\n" ANSI_DIM "=== end of notebook ===" ANSI_RESET "\n");
    yetty_ynotebook_notebook_destroy(notebook);
    return 0;
}
