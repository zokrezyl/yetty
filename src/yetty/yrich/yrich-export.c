/*
 * yrich-export.c — plain-text and Markdown import/export for ydoc.
 *
 * A compatibility layer over the native model (roadmap Phase 5), written
 * entirely against the public ydoc API. Export walks the paragraph tree and
 * emits Markdown; import parses the same subset back into paragraphs + style
 * runs. See yrich-export.h.
 */

#include <yetty/yrich/yrich-export.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Shared helpers
 *=========================================================================*/

/* Read a whole file into a malloc'd, NUL-terminated buffer. Caller frees. */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    if (out_len) {
        *out_len = read;
    }
    return buffer;
}

/*===========================================================================
 * Inline Markdown: model runs <-> ** _ ~~ markers
 *=========================================================================*/

/* Per-character format flags of a paragraph, resolved from base + runs. */
static uint32_t char_format_at(uint32_t base_format, const int32_t *run_start,
                               const int32_t *run_end, const uint32_t *run_format, size_t run_count,
                               size_t index)
{
    for (size_t i = 0; i < run_count; i++) {
        if ((size_t)run_start[i] <= index && index < (size_t)run_end[i]) {
            return run_format[i];
        }
    }
    return base_format;
}

/* Emit `text` with **bold** / _italic_ / ~~strike~~ markers driven by the
 * per-character format transitions. */
static void emit_inline_markdown(FILE *file, const char *text, size_t text_len,
                                 uint32_t base_format, const int32_t *run_start,
                                 const int32_t *run_end, const uint32_t *run_format,
                                 size_t run_count)
{
    int open_bold = 0;
    int open_italic = 0;
    int open_strike = 0;
    for (size_t i = 0; i < text_len; i++) {
        uint32_t format = char_format_at(base_format, run_start, run_end, run_format, run_count, i);
        int want_bold = (format & YETTY_YRICH_FMT_BOLD) != 0;
        int want_italic = (format & YETTY_YRICH_FMT_ITALIC) != 0;
        int want_strike = (format & YETTY_YRICH_FMT_STRIKE) != 0;
        if (open_strike && !want_strike) {
            fputs("~~", file);
            open_strike = 0;
        }
        if (open_italic && !want_italic) {
            fputc('_', file);
            open_italic = 0;
        }
        if (open_bold && !want_bold) {
            fputs("**", file);
            open_bold = 0;
        }
        if (!open_bold && want_bold) {
            fputs("**", file);
            open_bold = 1;
        }
        if (!open_italic && want_italic) {
            fputc('_', file);
            open_italic = 1;
        }
        if (!open_strike && want_strike) {
            fputs("~~", file);
            open_strike = 1;
        }
        fputc(text[i], file);
    }
    if (open_strike) {
        fputs("~~", file);
    }
    if (open_italic) {
        fputc('_', file);
    }
    if (open_bold) {
        fputs("**", file);
    }
}

/* A parsed inline run: byte span in the stripped plaintext + its format. */
struct inline_run {
    int32_t start;
    int32_t end;
    uint32_t format;
};

/* Parse `src` stripping the bold/italic/strike markers into `out_text`,
 * recording the format spans into `runs` (up to `runs_cap`). Returns the
 * plaintext length; sets *out_run_count. */
static size_t parse_inline_markdown(const char *src, size_t src_len, char *out_text,
                                    struct inline_run *runs, size_t runs_cap, size_t *out_run_count)
{
    uint32_t current = 0;
    size_t out_len = 0;
    size_t run_start = 0;
    size_t run_count = 0;
    size_t i = 0;
    while (i < src_len) {
        uint32_t toggle = 0;
        size_t advance = 0;
        if (i + 1 < src_len && src[i] == '*' && src[i + 1] == '*') {
            toggle = YETTY_YRICH_FMT_BOLD;
            advance = 2;
        } else if (i + 1 < src_len && src[i] == '~' && src[i + 1] == '~') {
            toggle = YETTY_YRICH_FMT_STRIKE;
            advance = 2;
        } else if (src[i] == '_') {
            toggle = YETTY_YRICH_FMT_ITALIC;
            advance = 1;
        }
        if (toggle != 0) {
            if (out_len > run_start && current != 0 && run_count < runs_cap) {
                runs[run_count].start = (int32_t)run_start;
                runs[run_count].end = (int32_t)out_len;
                runs[run_count].format = current;
                run_count++;
            }
            run_start = out_len;
            current ^= toggle;
            i += advance;
            continue;
        }
        out_text[out_len++] = src[i++];
    }
    if (out_len > run_start && current != 0 && run_count < runs_cap) {
        runs[run_count].start = (int32_t)run_start;
        runs[run_count].end = (int32_t)out_len;
        runs[run_count].format = current;
        run_count++;
    }
    out_text[out_len] = '\0';
    if (out_run_count) {
        *out_run_count = run_count;
    }
    return out_len;
}

/*===========================================================================
 * Export
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yrich_ydoc_export_text_file(
    struct yetty_yclass_object *doc_obj, const char *path)
{
    if (!doc_obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_text: NULL arg");
    }
    struct yetty_ycore_size_result count_res = yetty_yrich_ydoc_paragraph_count(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "ydoc_export_text: count");
    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_text: fopen failed");
    }
    for (size_t i = 0; i < count_res.value; i++) {
        struct yetty_yclass_object_ptr_result para_res =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        struct yetty_ycore_const_char_ptr_result text_res =
            yetty_yrich_paragraph_text(para_res.value);
        struct yetty_ycore_size_result len_res = yetty_yrich_paragraph_text_len(para_res.value);
        if (YETTY_IS_OK(text_res) && YETTY_IS_OK(len_res) && text_res.value) {
            fwrite(text_res.value, 1, len_res.value, file);
        } else {
            if (YETTY_IS_ERR(text_res)) {
                yetty_ycore_error_destroy(text_res.error);
            }
            if (YETTY_IS_ERR(len_res)) {
                yetty_ycore_error_destroy(len_res.error);
            }
        }
        fputc('\n', file);
    }
    if (fclose(file) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_text: close failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_ydoc_export_markdown_file(
    struct yetty_yclass_object *doc_obj, const char *path)
{
    if (!doc_obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_markdown: NULL arg");
    }
    struct yetty_ycore_size_result count_res = yetty_yrich_ydoc_paragraph_count(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "ydoc_export_markdown: count");
    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_markdown: fopen failed");
    }
    for (size_t i = 0; i < count_res.value; i++) {
        struct yetty_yclass_object_ptr_result para_res =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        struct yetty_yclass_object *paragraph = para_res.value;

        struct yetty_ycore_uint32_result block_res = yetty_yrich_paragraph_block_kind(paragraph);
        if (YETTY_IS_OK(block_res) && block_res.value == 1) {
            fputs("---\n", file);
            continue;
        }
        if (YETTY_IS_ERR(block_res)) {
            yetty_ycore_error_destroy(block_res.error);
        }

        struct yetty_ycore_const_char_ptr_result text_res = yetty_yrich_paragraph_text(paragraph);
        struct yetty_ycore_size_result len_res = yetty_yrich_paragraph_text_len(paragraph);
        struct yetty_ycore_uint32_result heading_res =
            yetty_yrich_paragraph_heading_level(paragraph);
        struct yetty_ycore_uint32_result list_res = yetty_yrich_paragraph_list_kind(paragraph);
        struct yetty_ycore_uint32_result level_res = yetty_yrich_paragraph_list_level(paragraph);
        struct yetty_ycore_uint32_result checked_res =
            yetty_yrich_paragraph_list_checked(paragraph);
        struct yetty_ycore_uint32_result base_format_res = yetty_yrich_paragraph_format(paragraph);
        if (YETTY_IS_ERR(text_res) || YETTY_IS_ERR(len_res)) {
            if (YETTY_IS_ERR(text_res)) {
                yetty_ycore_error_destroy(text_res.error);
            }
            if (YETTY_IS_ERR(len_res)) {
                yetty_ycore_error_destroy(len_res.error);
            }
            continue;
        }
        uint32_t heading = YETTY_IS_OK(heading_res) ? heading_res.value : 0;
        uint32_t list_kind = YETTY_IS_OK(list_res) ? list_res.value : 0;
        uint32_t level = YETTY_IS_OK(level_res) ? level_res.value : 0;
        uint32_t checked = YETTY_IS_OK(checked_res) ? checked_res.value : 0;
        uint32_t base_format = YETTY_IS_OK(base_format_res) ? base_format_res.value : 0;

        /* Block prefix. */
        if (heading > 0) {
            uint32_t hashes = heading > 6 ? 6 : heading;
            for (uint32_t h = 0; h < hashes; h++) {
                fputc('#', file);
            }
            fputc(' ', file);
        } else if (list_kind != 0) {
            for (uint32_t indent = 0; indent < level; indent++) {
                fputs("  ", file);
            }
            if (list_kind == 3) {
                fputs(checked ? "- [x] " : "- [ ] ", file);
            } else if (list_kind == 2) {
                fputs("1. ", file);
            } else {
                fputs("- ", file);
            }
        }

        /* Inline body with runs. */
        size_t run_count = 0;
        struct yetty_ycore_size_result run_count_res = yetty_yrich_paragraph_run_count(paragraph);
        if (YETTY_IS_OK(run_count_res)) {
            run_count = run_count_res.value;
        }
        int32_t *starts = run_count ? malloc(run_count * sizeof(*starts)) : NULL;
        int32_t *ends = run_count ? malloc(run_count * sizeof(*ends)) : NULL;
        uint32_t *formats = run_count ? malloc(run_count * sizeof(*formats)) : NULL;
        if (run_count && (!starts || !ends || !formats)) {
            free(starts);
            free(ends);
            free(formats);
            fclose(file);
            return YETTY_ERR(yetty_ycore_void, "ydoc_export_markdown: run alloc");
        }
        for (size_t r = 0; r < run_count; r++) {
            uint32_t color = 0;
            uint32_t bg = 0;
            float font_size = 0.0f;
            struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_run_get(
                paragraph, r, &starts[r], &ends[r], &formats[r], &color, &bg, &font_size);
            if (YETTY_IS_ERR(run_res)) {
                yetty_ycore_error_destroy(run_res.error);
                starts[r] = 0;
                ends[r] = 0;
                formats[r] = 0;
            }
        }
        emit_inline_markdown(file, text_res.value, len_res.value, base_format, starts, ends,
                             formats, run_count);
        free(starts);
        free(ends);
        free(formats);
        fputc('\n', file);
    }
    if (fclose(file) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_markdown: close failed");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Import
 *=========================================================================*/

static void destroy_doc(struct yetty_yclass_object *doc)
{
    struct yetty_ycore_void_result res = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_text_file(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_text: NULL path");
    }
    size_t len = 0;
    char *buffer = read_file(path, &len);
    if (!buffer) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_text: read failed");
    }
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        free(buffer);
        return doc_res;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || buffer[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0 && buffer[line_start + line_len - 1] == '\r') {
                line_len--;
            }
            /* Skip a trailing empty line produced by a final newline. */
            if (!(i == len && line_len == 0 && line_start == len)) {
                struct yetty_yclass_object_ptr_result para_res =
                    yetty_yrich_ydoc_add_paragraph(doc, buffer + line_start, line_len);
                if (YETTY_IS_ERR(para_res)) {
                    yetty_ycore_error_destroy(para_res.error);
                }
            }
            line_start = i + 1;
        }
    }
    free(buffer);
    return YETTY_OK(yetty_yclass_object_ptr, doc);
}

/* Apply one parsed Markdown line to a new paragraph in `doc`. */
static struct yetty_ycore_void_result import_markdown_line(struct yetty_yclass_object *doc,
                                                           const char *line, size_t line_len)
{
    /* Horizontal rule. */
    if (line_len >= 3 && line[0] == '-' && line[1] == '-' && line[2] == '-') {
        size_t only_dashes = 1;
        for (size_t i = 0; i < line_len; i++) {
            if (line[i] != '-') {
                only_dashes = 0;
                break;
            }
        }
        if (only_dashes) {
            struct yetty_yclass_object_ptr_result para_res =
                yetty_yrich_ydoc_add_paragraph(doc, "", 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, para_res, "import md: rule paragraph");
            return yetty_yrich_paragraph_set_block_kind(para_res.value, 1);
        }
    }

    /* Leading spaces → nesting level (two spaces per level). */
    size_t pos = 0;
    uint32_t level = 0;
    while (pos + 1 < line_len && line[pos] == ' ' && line[pos + 1] == ' ') {
        level++;
        pos += 2;
    }

    uint32_t heading = 0;
    uint32_t list_kind = 0;
    uint32_t checked = 0;
    if (line[pos] == '#') {
        uint32_t hashes = 0;
        while (pos < line_len && line[pos] == '#') {
            hashes++;
            pos++;
        }
        if (pos < line_len && line[pos] == ' ') {
            pos++;
            heading = hashes > 6 ? 6 : hashes;
        }
    } else if (pos + 5 < line_len && line[pos] == '-' && line[pos + 1] == ' ' &&
               line[pos + 2] == '[' && line[pos + 4] == ']') {
        checked = (line[pos + 3] == 'x' || line[pos + 3] == 'X') ? 1 : 0;
        list_kind = 3;
        pos += 5;
        if (pos < line_len && line[pos] == ' ') {
            pos++;
        }
    } else if (pos + 1 < line_len && (line[pos] == '-' || line[pos] == '*') &&
               line[pos + 1] == ' ') {
        list_kind = 1;
        pos += 2;
    } else {
        size_t digits = pos;
        while (digits < line_len && line[digits] >= '0' && line[digits] <= '9') {
            digits++;
        }
        if (digits > pos && digits + 1 < line_len && line[digits] == '.' &&
            line[digits + 1] == ' ') {
            list_kind = 2;
            pos = digits + 2;
        }
    }

    /* Inline parse the remainder into plaintext + runs. */
    size_t body_len = line_len - pos;
    char *plain = malloc(body_len + 1);
    struct inline_run *runs = malloc((body_len + 1) * sizeof(*runs));
    if (!plain || !runs) {
        free(plain);
        free(runs);
        return YETTY_ERR(yetty_ycore_void, "import md: line alloc");
    }
    size_t run_count = 0;
    size_t plain_len =
        parse_inline_markdown(line + pos, body_len, plain, runs, body_len + 1, &run_count);

    struct yetty_yclass_object_ptr_result para_res =
        yetty_yrich_ydoc_add_paragraph(doc, plain, plain_len);
    if (YETTY_IS_ERR(para_res)) {
        free(plain);
        free(runs);
        return YETTY_ERR(yetty_ycore_void, "import md: add paragraph", para_res);
    }
    struct yetty_yclass_object *paragraph = para_res.value;
    free(plain);

    if (heading > 0) {
        struct yetty_ycore_void_result res =
            yetty_yrich_paragraph_set_heading_level(paragraph, heading);
        if (YETTY_IS_ERR(res)) {
            yetty_ycore_error_destroy(res.error);
        }
    }
    if (list_kind > 0) {
        struct yetty_ycore_void_result kind_res =
            yetty_yrich_paragraph_set_list_kind(paragraph, list_kind);
        if (YETTY_IS_ERR(kind_res)) {
            yetty_ycore_error_destroy(kind_res.error);
        }
        if (level > 0) {
            struct yetty_ycore_void_result level_res =
                yetty_yrich_paragraph_set_list_level(paragraph, level);
            if (YETTY_IS_ERR(level_res)) {
                yetty_ycore_error_destroy(level_res.error);
            }
        }
        if (checked) {
            struct yetty_ycore_void_result checked_res =
                yetty_yrich_paragraph_set_list_checked(paragraph, 1);
            if (YETTY_IS_ERR(checked_res)) {
                yetty_ycore_error_destroy(checked_res.error);
            }
        }
    }

    /* Style runs — colour/size inherit the paragraph base so text stays
     * visible. */
    if (run_count > 0) {
        uint32_t base_color = YETTY_YRICH_COLOR_BLACK;
        struct yetty_ycore_uint32_result color_res = yetty_yrich_paragraph_color(paragraph);
        if (YETTY_IS_OK(color_res)) {
            base_color = color_res.value;
        }
        float base_font = 14.0f;
        struct yetty_ycore_float_result font_res = yetty_yrich_paragraph_font_size(paragraph);
        if (YETTY_IS_OK(font_res)) {
            base_font = font_res.value;
        }
        for (size_t r = 0; r < run_count; r++) {
            struct yetty_ycore_void_result run_res =
                yetty_yrich_paragraph_add_run(paragraph, runs[r].start, runs[r].end, runs[r].format,
                                              base_color, YETTY_YRICH_COLOR_TRANSPARENT, base_font);
            if (YETTY_IS_ERR(run_res)) {
                yetty_ycore_error_destroy(run_res.error);
            }
        }
    }
    free(runs);
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_markdown_file(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_markdown: NULL path");
    }
    size_t len = 0;
    char *buffer = read_file(path, &len);
    if (!buffer) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_markdown: read failed");
    }
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        free(buffer);
        return doc_res;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || buffer[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0 && buffer[line_start + line_len - 1] == '\r') {
                line_len--;
            }
            if (!(i == len && line_len == 0 && line_start == len)) {
                struct yetty_ycore_void_result line_res =
                    import_markdown_line(doc, buffer + line_start, line_len);
                if (YETTY_IS_ERR(line_res)) {
                    yetty_ycore_error_destroy(line_res.error);
                    free(buffer);
                    destroy_doc(doc);
                    return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_markdown: line");
                }
            }
            line_start = i + 1;
        }
    }
    free(buffer);
    return YETTY_OK(yetty_yclass_object_ptr, doc);
}

/*===========================================================================
 * HTML
 *=========================================================================*/

/* Write `text` to `file` with HTML entity escaping. */
static void html_escape(FILE *file, const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        switch (text[i]) {
        case '&':
            fputs("&amp;", file);
            break;
        case '<':
            fputs("&lt;", file);
            break;
        case '>':
            fputs("&gt;", file);
            break;
        default:
            fputc(text[i], file);
            break;
        }
    }
}

/* Emit `text` with <b>/<i>/<s> tags driven by the per-character format
 * transitions, escaping the text content. */
static void emit_inline_html(FILE *file, const char *text, size_t text_len, uint32_t base_format,
                             const int32_t *run_start, const int32_t *run_end,
                             const uint32_t *run_format, size_t run_count)
{
    int open_bold = 0;
    int open_italic = 0;
    int open_strike = 0;
    for (size_t i = 0; i < text_len; i++) {
        uint32_t format = char_format_at(base_format, run_start, run_end, run_format, run_count, i);
        int want_bold = (format & YETTY_YRICH_FMT_BOLD) != 0;
        int want_italic = (format & YETTY_YRICH_FMT_ITALIC) != 0;
        int want_strike = (format & YETTY_YRICH_FMT_STRIKE) != 0;
        if (open_strike && !want_strike) {
            fputs("</s>", file);
            open_strike = 0;
        }
        if (open_italic && !want_italic) {
            fputs("</i>", file);
            open_italic = 0;
        }
        if (open_bold && !want_bold) {
            fputs("</b>", file);
            open_bold = 0;
        }
        if (!open_bold && want_bold) {
            fputs("<b>", file);
            open_bold = 1;
        }
        if (!open_italic && want_italic) {
            fputs("<i>", file);
            open_italic = 1;
        }
        if (!open_strike && want_strike) {
            fputs("<s>", file);
            open_strike = 1;
        }
        html_escape(file, text + i, 1);
    }
    if (open_strike) {
        fputs("</s>", file);
    }
    if (open_italic) {
        fputs("</i>", file);
    }
    if (open_bold) {
        fputs("</b>", file);
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_export_html_file(
    struct yetty_yclass_object *doc_obj, const char *path)
{
    if (!doc_obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_html: NULL arg");
    }
    struct yetty_ycore_size_result count_res = yetty_yrich_ydoc_paragraph_count(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "ydoc_export_html: count");
    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_html: fopen failed");
    }
    fputs("<!DOCTYPE html>\n<html>\n<body>\n", file);
    for (size_t i = 0; i < count_res.value; i++) {
        struct yetty_yclass_object_ptr_result para_res =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        struct yetty_yclass_object *paragraph = para_res.value;
        struct yetty_ycore_uint32_result block_res = yetty_yrich_paragraph_block_kind(paragraph);
        if (YETTY_IS_OK(block_res) && block_res.value == 1) {
            fputs("<hr>\n", file);
            continue;
        }
        if (YETTY_IS_ERR(block_res)) {
            yetty_ycore_error_destroy(block_res.error);
        }
        struct yetty_ycore_const_char_ptr_result text_res = yetty_yrich_paragraph_text(paragraph);
        struct yetty_ycore_size_result len_res = yetty_yrich_paragraph_text_len(paragraph);
        struct yetty_ycore_uint32_result heading_res =
            yetty_yrich_paragraph_heading_level(paragraph);
        struct yetty_ycore_uint32_result base_format_res = yetty_yrich_paragraph_format(paragraph);
        if (YETTY_IS_ERR(text_res) || YETTY_IS_ERR(len_res)) {
            if (YETTY_IS_ERR(text_res)) {
                yetty_ycore_error_destroy(text_res.error);
            }
            if (YETTY_IS_ERR(len_res)) {
                yetty_ycore_error_destroy(len_res.error);
            }
            continue;
        }
        uint32_t heading = YETTY_IS_OK(heading_res) ? heading_res.value : 0;
        uint32_t base_format = YETTY_IS_OK(base_format_res) ? base_format_res.value : 0;
        char open_tag[8];
        char close_tag[8];
        if (heading >= 1 && heading <= 6) {
            snprintf(open_tag, sizeof(open_tag), "<h%u>", heading);
            snprintf(close_tag, sizeof(close_tag), "</h%u>", heading);
        } else {
            snprintf(open_tag, sizeof(open_tag), "<p>");
            snprintf(close_tag, sizeof(close_tag), "</p>");
        }
        fputs(open_tag, file);

        size_t run_count = 0;
        struct yetty_ycore_size_result run_count_res = yetty_yrich_paragraph_run_count(paragraph);
        if (YETTY_IS_OK(run_count_res)) {
            run_count = run_count_res.value;
        }
        int32_t *starts = run_count ? malloc(run_count * sizeof(*starts)) : NULL;
        int32_t *ends = run_count ? malloc(run_count * sizeof(*ends)) : NULL;
        uint32_t *formats = run_count ? malloc(run_count * sizeof(*formats)) : NULL;
        if (run_count && (!starts || !ends || !formats)) {
            free(starts);
            free(ends);
            free(formats);
            fclose(file);
            return YETTY_ERR(yetty_ycore_void, "ydoc_export_html: run alloc");
        }
        for (size_t r = 0; r < run_count; r++) {
            uint32_t color = 0;
            uint32_t bg = 0;
            float font_size = 0.0f;
            struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_run_get(
                paragraph, r, &starts[r], &ends[r], &formats[r], &color, &bg, &font_size);
            if (YETTY_IS_ERR(run_res)) {
                yetty_ycore_error_destroy(run_res.error);
                starts[r] = 0;
                ends[r] = 0;
                formats[r] = 0;
            }
        }
        emit_inline_html(file, text_res.value, len_res.value, base_format, starts, ends, formats,
                         run_count);
        free(starts);
        free(ends);
        free(formats);
        fputs(close_tag, file);
        fputc('\n', file);
    }
    fputs("</body>\n</html>\n", file);
    if (fclose(file) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_html: close failed");
    }
    return YETTY_OK_VOID();
}

/* Parse `src` decoding HTML entities into `out` and turning <b>/<i>/<s> (and
 * strong/em/strike/del) into runs. Returns plaintext length; sets
 * *out_run_count. */
static size_t parse_inline_html(const char *src, size_t src_len, char *out, struct inline_run *runs,
                                size_t runs_cap, size_t *out_run_count)
{
    uint32_t current = 0;
    size_t out_len = 0;
    size_t run_start = 0;
    size_t run_count = 0;
    size_t i = 0;
    while (i < src_len) {
        if (src[i] == '<') {
            size_t tag = i + 1;
            int closing = 0;
            if (tag < src_len && src[tag] == '/') {
                closing = 1;
                tag++;
            }
            size_t name_start = tag;
            while (tag < src_len && src[tag] != '>' && src[tag] != ' ') {
                tag++;
            }
            size_t name_len = tag - name_start;
            size_t close = tag;
            while (close < src_len && src[close] != '>') {
                close++;
            }
            uint32_t toggle = 0;
            if ((name_len == 1 && (src[name_start] == 'b' || src[name_start] == 'B')) ||
                (name_len == 6 && strncmp(src + name_start, "strong", 6) == 0)) {
                toggle = YETTY_YRICH_FMT_BOLD;
            } else if ((name_len == 1 && (src[name_start] == 'i' || src[name_start] == 'I')) ||
                       (name_len == 2 && strncmp(src + name_start, "em", 2) == 0)) {
                toggle = YETTY_YRICH_FMT_ITALIC;
            } else if ((name_len == 1 && (src[name_start] == 's' || src[name_start] == 'S')) ||
                       (name_len == 6 && strncmp(src + name_start, "strike", 6) == 0) ||
                       (name_len == 3 && strncmp(src + name_start, "del", 3) == 0)) {
                toggle = YETTY_YRICH_FMT_STRIKE;
            }
            if (toggle != 0) {
                if (out_len > run_start && current != 0 && run_count < runs_cap) {
                    runs[run_count].start = (int32_t)run_start;
                    runs[run_count].end = (int32_t)out_len;
                    runs[run_count].format = current;
                    run_count++;
                }
                run_start = out_len;
                if (closing) {
                    current &= ~toggle;
                } else {
                    current |= toggle;
                }
            }
            i = close < src_len ? close + 1 : src_len;
            continue;
        }
        if (src[i] == '&') {
            if (src_len - i >= 4 && strncmp(src + i, "&lt;", 4) == 0) {
                out[out_len++] = '<';
                i += 4;
                continue;
            }
            if (src_len - i >= 4 && strncmp(src + i, "&gt;", 4) == 0) {
                out[out_len++] = '>';
                i += 4;
                continue;
            }
            if (src_len - i >= 5 && strncmp(src + i, "&amp;", 5) == 0) {
                out[out_len++] = '&';
                i += 5;
                continue;
            }
            if (src_len - i >= 6 && strncmp(src + i, "&quot;", 6) == 0) {
                out[out_len++] = '"';
                i += 6;
                continue;
            }
        }
        out[out_len++] = src[i++];
    }
    if (out_len > run_start && current != 0 && run_count < runs_cap) {
        runs[run_count].start = (int32_t)run_start;
        runs[run_count].end = (int32_t)out_len;
        runs[run_count].format = current;
        run_count++;
    }
    out[out_len] = '\0';
    if (out_run_count) {
        *out_run_count = run_count;
    }
    return out_len;
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_html_file(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_html: NULL path");
    }
    size_t len = 0;
    char *buffer = read_file(path, &len);
    if (!buffer) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_html: read failed");
    }
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        free(buffer);
        return doc_res;
    }
    struct yetty_yclass_object *doc = doc_res.value;

    size_t i = 0;
    while (i < len) {
        if (buffer[i] != '<') {
            i++;
            continue;
        }
        size_t name_start = i + 1;
        size_t tag = name_start;
        while (tag < len && buffer[tag] != '>' && buffer[tag] != ' ') {
            tag++;
        }
        size_t name_len = tag - name_start;
        size_t gt = tag;
        while (gt < len && buffer[gt] != '>') {
            gt++;
        }

        uint32_t heading = 0;
        int is_para = 0;
        int is_hr = 0;
        if (name_len == 2 && buffer[name_start] == 'h' && buffer[name_start + 1] >= '1' &&
            buffer[name_start + 1] <= '6') {
            heading = (uint32_t)(buffer[name_start + 1] - '0');
        } else if (name_len == 1 && (buffer[name_start] == 'p' || buffer[name_start] == 'P')) {
            is_para = 1;
        } else if (name_len == 2 && (buffer[name_start] == 'h' || buffer[name_start] == 'H') &&
                   (buffer[name_start + 1] == 'r' || buffer[name_start + 1] == 'R')) {
            is_hr = 1;
        }

        if (is_hr) {
            struct yetty_yclass_object_ptr_result rule = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
            if (!YETTY_IS_ERR(rule)) {
                struct yetty_ycore_void_result block_res =
                    yetty_yrich_paragraph_set_block_kind(rule.value, 1);
                if (YETTY_IS_ERR(block_res)) {
                    yetty_ycore_error_destroy(block_res.error);
                }
            } else {
                yetty_ycore_error_destroy(rule.error);
            }
            i = gt < len ? gt + 1 : len;
            continue;
        }

        if (heading > 0 || is_para) {
            size_t inner_start = gt < len ? gt + 1 : len;
            /* Find the matching block close tag (</p> or </hN>), skipping any
             * inline </b>, </i>, </s> that appear inside the block. */
            size_t scan = inner_start;
            size_t inner_end = len;
            while (scan < len) {
                if (buffer[scan] == '<' && scan + 2 < len && buffer[scan + 1] == '/' &&
                    (size_t)(len - (scan + 2)) >= name_len &&
                    strncmp(buffer + scan + 2, buffer + name_start, name_len) == 0 &&
                    (scan + 2 + name_len < len &&
                     (buffer[scan + 2 + name_len] == '>' || buffer[scan + 2 + name_len] == ' '))) {
                    inner_end = scan;
                    break;
                }
                scan++;
            }
            size_t inner_len = inner_end - inner_start;
            char *plain = malloc(inner_len + 1);
            struct inline_run *runs = malloc((inner_len + 1) * sizeof(*runs));
            if (!plain || !runs) {
                free(plain);
                free(runs);
                free(buffer);
                destroy_doc(doc);
                return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_html: line alloc");
            }
            size_t run_count = 0;
            size_t plain_len = parse_inline_html(buffer + inner_start, inner_len, plain, runs,
                                                 inner_len + 1, &run_count);
            struct yetty_yclass_object_ptr_result para_res =
                yetty_yrich_ydoc_add_paragraph(doc, plain, plain_len);
            free(plain);
            if (YETTY_IS_ERR(para_res)) {
                free(runs);
                free(buffer);
                destroy_doc(doc);
                return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_html: add paragraph",
                                 para_res);
            }
            struct yetty_yclass_object *paragraph = para_res.value;
            if (heading > 0) {
                struct yetty_ycore_void_result h =
                    yetty_yrich_paragraph_set_heading_level(paragraph, heading);
                if (YETTY_IS_ERR(h)) {
                    yetty_ycore_error_destroy(h.error);
                }
            }
            if (run_count > 0) {
                uint32_t base_color = YETTY_YRICH_COLOR_BLACK;
                struct yetty_ycore_uint32_result color_res = yetty_yrich_paragraph_color(paragraph);
                if (YETTY_IS_OK(color_res)) {
                    base_color = color_res.value;
                }
                float base_font = 14.0f;
                struct yetty_ycore_float_result font_res =
                    yetty_yrich_paragraph_font_size(paragraph);
                if (YETTY_IS_OK(font_res)) {
                    base_font = font_res.value;
                }
                for (size_t r = 0; r < run_count; r++) {
                    struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_add_run(
                        paragraph, runs[r].start, runs[r].end, runs[r].format, base_color,
                        YETTY_YRICH_COLOR_TRANSPARENT, base_font);
                    if (YETTY_IS_ERR(run_res)) {
                        yetty_ycore_error_destroy(run_res.error);
                    }
                }
            }
            free(runs);
            size_t resume = inner_end;
            while (resume < len && buffer[resume] != '>') {
                resume++;
            }
            i = resume < len ? resume + 1 : len;
            continue;
        }

        i = gt < len ? gt + 1 : len;
    }
    free(buffer);
    return YETTY_OK(yetty_yclass_object_ptr, doc);
}

/*===========================================================================
 * RTF
 *=========================================================================*/

/* Write `text` to `file`, escaping the RTF metacharacters \ { }. */
static void rtf_escape(FILE *file, const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char code = text[i];
        if (code == '\\' || code == '{' || code == '}') {
            fputc('\\', file);
        }
        fputc(code, file);
    }
}

/* Emit `text` with \b / \i / \strike control words driven by the per-character
 * format transitions, escaping the text content. */
static void emit_inline_rtf(FILE *file, const char *text, size_t text_len, uint32_t base_format,
                            const int32_t *run_start, const int32_t *run_end,
                            const uint32_t *run_format, size_t run_count)
{
    int open_bold = 0;
    int open_italic = 0;
    int open_strike = 0;
    for (size_t i = 0; i < text_len; i++) {
        uint32_t format = char_format_at(base_format, run_start, run_end, run_format, run_count, i);
        int want_bold = (format & YETTY_YRICH_FMT_BOLD) != 0;
        int want_italic = (format & YETTY_YRICH_FMT_ITALIC) != 0;
        int want_strike = (format & YETTY_YRICH_FMT_STRIKE) != 0;
        if (open_strike && !want_strike) {
            fputs("\\strike0 ", file);
            open_strike = 0;
        }
        if (open_italic && !want_italic) {
            fputs("\\i0 ", file);
            open_italic = 0;
        }
        if (open_bold && !want_bold) {
            fputs("\\b0 ", file);
            open_bold = 0;
        }
        if (!open_bold && want_bold) {
            fputs("\\b ", file);
            open_bold = 1;
        }
        if (!open_italic && want_italic) {
            fputs("\\i ", file);
            open_italic = 1;
        }
        if (!open_strike && want_strike) {
            fputs("\\strike ", file);
            open_strike = 1;
        }
        rtf_escape(file, text + i, 1);
    }
    if (open_strike) {
        fputs("\\strike0 ", file);
    }
    if (open_italic) {
        fputs("\\i0 ", file);
    }
    if (open_bold) {
        fputs("\\b0 ", file);
    }
}

/* Visual half-point font size for a heading level (RTF \fs is in half-points);
 * body text is 28 (14pt). */
static int rtf_heading_half_points(uint32_t heading)
{
    switch (heading) {
    case 1:
        return 72;
    case 2:
        return 64;
    case 3:
        return 56;
    case 4:
        return 48;
    case 5:
        return 40;
    case 6:
        return 36;
    default:
        return 28;
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_export_rtf_file(struct yetty_yclass_object *doc_obj,
                                                                const char *path)
{
    if (!doc_obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_rtf: NULL arg");
    }
    struct yetty_ycore_size_result count_res = yetty_yrich_ydoc_paragraph_count(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "ydoc_export_rtf: count");
    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_rtf: fopen failed");
    }
    fputs("{\\rtf1\\ansi\\deff0\n", file);
    fputs("{\\fonttbl{\\f0 Helvetica;}}\n", file);
    fputs("{\\stylesheet{\\s0 Normal;}{\\s1 heading 1;}{\\s2 heading 2;}"
          "{\\s3 heading 3;}{\\s4 heading 4;}{\\s5 heading 5;}{\\s6 heading 6;}}\n",
          file);
    for (size_t i = 0; i < count_res.value; i++) {
        struct yetty_yclass_object_ptr_result para_res =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        struct yetty_yclass_object *paragraph = para_res.value;
        struct yetty_ycore_uint32_result block_res = yetty_yrich_paragraph_block_kind(paragraph);
        if (YETTY_IS_OK(block_res) && block_res.value == 1) {
            fputs("\\pard\\brdrb\\brdrs\\brdrw10 \\par\n", file);
            continue;
        }
        if (YETTY_IS_ERR(block_res)) {
            yetty_ycore_error_destroy(block_res.error);
        }
        struct yetty_ycore_const_char_ptr_result text_res = yetty_yrich_paragraph_text(paragraph);
        struct yetty_ycore_size_result len_res = yetty_yrich_paragraph_text_len(paragraph);
        struct yetty_ycore_uint32_result heading_res =
            yetty_yrich_paragraph_heading_level(paragraph);
        struct yetty_ycore_uint32_result base_format_res = yetty_yrich_paragraph_format(paragraph);
        if (YETTY_IS_ERR(text_res) || YETTY_IS_ERR(len_res)) {
            if (YETTY_IS_ERR(text_res)) {
                yetty_ycore_error_destroy(text_res.error);
            }
            if (YETTY_IS_ERR(len_res)) {
                yetty_ycore_error_destroy(len_res.error);
            }
            continue;
        }
        uint32_t heading = YETTY_IS_OK(heading_res) ? heading_res.value : 0;
        uint32_t base_format = YETTY_IS_OK(base_format_res) ? base_format_res.value : 0;
        uint32_t style_level = (heading >= 1 && heading <= 6) ? heading : 0;
        fprintf(file, "\\pard\\s%u\\fs%d ", style_level, rtf_heading_half_points(heading));

        size_t run_count = 0;
        struct yetty_ycore_size_result run_count_res = yetty_yrich_paragraph_run_count(paragraph);
        if (YETTY_IS_OK(run_count_res)) {
            run_count = run_count_res.value;
        }
        int32_t *starts = run_count ? malloc(run_count * sizeof(*starts)) : NULL;
        int32_t *ends = run_count ? malloc(run_count * sizeof(*ends)) : NULL;
        uint32_t *formats = run_count ? malloc(run_count * sizeof(*formats)) : NULL;
        if (run_count && (!starts || !ends || !formats)) {
            free(starts);
            free(ends);
            free(formats);
            fclose(file);
            return YETTY_ERR(yetty_ycore_void, "ydoc_export_rtf: run alloc");
        }
        for (size_t r = 0; r < run_count; r++) {
            uint32_t color = 0;
            uint32_t bg = 0;
            float font_size = 0.0f;
            struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_run_get(
                paragraph, r, &starts[r], &ends[r], &formats[r], &color, &bg, &font_size);
            if (YETTY_IS_ERR(run_res)) {
                yetty_ycore_error_destroy(run_res.error);
                starts[r] = 0;
                ends[r] = 0;
                formats[r] = 0;
            }
        }
        emit_inline_rtf(file, text_res.value, len_res.value, base_format, starts, ends, formats,
                        run_count);
        free(starts);
        free(ends);
        free(formats);
        fputs("\\par\n", file);
    }
    fputs("}\n", file);
    if (fclose(file) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_export_rtf: close failed");
    }
    return YETTY_OK_VOID();
}

/* One entry in the RTF import format stack (pushed on '{', popped on '}'). */
struct rtf_group_state {
    uint32_t format;
};

/* Flush the current paragraph's accumulated text + runs into a new ydoc
 * paragraph and reset the per-paragraph accumulators. */
static struct yetty_ycore_void_result rtf_flush_paragraph(struct yetty_yclass_object *doc,
                                                          char *out, size_t out_len,
                                                          struct inline_run *runs, size_t run_count,
                                                          uint32_t heading, int is_hr)
{
    out[out_len] = '\0';
    struct yetty_yclass_object_ptr_result para_res =
        yetty_yrich_ydoc_add_paragraph(doc, out, out_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, para_res, "ydoc_import_rtf: add paragraph");
    struct yetty_yclass_object *paragraph = para_res.value;
    if (is_hr) {
        struct yetty_ycore_void_result block_res =
            yetty_yrich_paragraph_set_block_kind(paragraph, 1);
        if (YETTY_IS_ERR(block_res)) {
            yetty_ycore_error_destroy(block_res.error);
        }
        return YETTY_OK_VOID();
    }
    if (heading >= 1 && heading <= 6) {
        struct yetty_ycore_void_result h =
            yetty_yrich_paragraph_set_heading_level(paragraph, heading);
        if (YETTY_IS_ERR(h)) {
            yetty_ycore_error_destroy(h.error);
        }
    }
    if (run_count > 0) {
        uint32_t base_color = YETTY_YRICH_COLOR_BLACK;
        struct yetty_ycore_uint32_result color_res = yetty_yrich_paragraph_color(paragraph);
        if (YETTY_IS_OK(color_res)) {
            base_color = color_res.value;
        }
        float base_font = 14.0f;
        struct yetty_ycore_float_result font_res = yetty_yrich_paragraph_font_size(paragraph);
        if (YETTY_IS_OK(font_res)) {
            base_font = font_res.value;
        }
        for (size_t r = 0; r < run_count; r++) {
            struct yetty_ycore_void_result run_res =
                yetty_yrich_paragraph_add_run(paragraph, runs[r].start, runs[r].end, runs[r].format,
                                              base_color, YETTY_YRICH_COLOR_TRANSPARENT, base_font);
            if (YETTY_IS_ERR(run_res)) {
                yetty_ycore_error_destroy(run_res.error);
            }
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_rtf_file(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_rtf: NULL path");
    }
    size_t len = 0;
    char *buffer = read_file(path, &len);
    if (!buffer) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_rtf: read failed");
    }
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        free(buffer);
        return doc_res;
    }
    struct yetty_yclass_object *doc = doc_res.value;

    char *out = malloc(len + 1);
    struct inline_run *runs = malloc((len + 1) * sizeof(*runs));
    enum { RTF_STACK_MAX = 64 };
    struct rtf_group_state stack[RTF_STACK_MAX];
    if (!out || !runs) {
        free(out);
        free(runs);
        free(buffer);
        destroy_doc(doc);
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_rtf: alloc");
    }

    size_t out_len = 0;
    size_t run_count = 0;
    size_t run_start = 0;
    uint32_t current = 0;
    uint32_t heading = 0;
    int is_hr = 0;
    int any_text = 0; /* whether the current paragraph accumulated anything */
    size_t depth = 0;

    size_t i = 0;
    while (i < len) {
        char code = buffer[i];
        if (code == '{') {
            /* Destination groups (font/style/colour tables, \* groups, pictures,
             * headers/footers/info) carry no body text — skip them whole. */
            size_t look = i + 1;
            while (look < len && (buffer[look] == ' ' || buffer[look] == '\n' ||
                                  buffer[look] == '\r' || buffer[look] == '\t')) {
                look++;
            }
            int skip_group = 0;
            if (look < len && buffer[look] == '\\') {
                size_t kw = look + 1;
                if (kw < len && buffer[kw] == '*') {
                    skip_group = 1;
                } else {
                    size_t kw_end = kw;
                    while (kw_end < len && ((buffer[kw_end] >= 'a' && buffer[kw_end] <= 'z') ||
                                            (buffer[kw_end] >= 'A' && buffer[kw_end] <= 'Z'))) {
                        kw_end++;
                    }
                    size_t kw_len = kw_end - kw;
                    const char *dests[] = {"fonttbl", "colortbl", "stylesheet", "info", "pict",
                                           "header",  "footer",   "footnote",   "field"};
                    for (size_t d = 0; d < sizeof(dests) / sizeof(dests[0]); d++) {
                        if (kw_len == strlen(dests[d]) &&
                            strncmp(buffer + kw, dests[d], kw_len) == 0) {
                            skip_group = 1;
                            break;
                        }
                    }
                }
            }
            if (skip_group) {
                size_t brace_depth = 0;
                size_t scan = i;
                while (scan < len) {
                    if (buffer[scan] == '\\' && scan + 1 < len) {
                        scan += 2;
                        continue;
                    }
                    if (buffer[scan] == '{') {
                        brace_depth++;
                    } else if (buffer[scan] == '}') {
                        brace_depth--;
                        if (brace_depth == 0) {
                            scan++;
                            break;
                        }
                    }
                    scan++;
                }
                i = scan;
                continue;
            }
            if (depth < RTF_STACK_MAX) {
                stack[depth].format = current;
            }
            depth++;
            i++;
            continue;
        }
        if (code == '}') {
            if (out_len > run_start && current != 0 && run_count < len + 1) {
                runs[run_count].start = (int32_t)run_start;
                runs[run_count].end = (int32_t)out_len;
                runs[run_count].format = current;
                run_count++;
            }
            run_start = out_len;
            if (depth > 0) {
                depth--;
                if (depth < RTF_STACK_MAX) {
                    current = stack[depth].format;
                }
            }
            i++;
            continue;
        }
        if (code == '\\') {
            size_t kw = i + 1;
            if (kw >= len) {
                break;
            }
            char first = buffer[kw];
            /* Escaped literal or special. */
            if (first == '\\' || first == '{' || first == '}') {
                out[out_len++] = first;
                any_text = 1;
                i = kw + 1;
                continue;
            }
            if (first == '\'') {
                /* \'xx hex byte. */
                int value = 0;
                size_t hexpos = kw + 1;
                for (int digit = 0; digit < 2 && hexpos < len; digit++, hexpos++) {
                    char hex = buffer[hexpos];
                    int nibble;
                    if (hex >= '0' && hex <= '9') {
                        nibble = hex - '0';
                    } else if (hex >= 'a' && hex <= 'f') {
                        nibble = hex - 'a' + 10;
                    } else if (hex >= 'A' && hex <= 'F') {
                        nibble = hex - 'A' + 10;
                    } else {
                        break;
                    }
                    value = value * 16 + nibble;
                }
                out[out_len++] = (char)value;
                any_text = 1;
                i = hexpos;
                continue;
            }
            if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
                /* Control symbol like \~ \- \_; ignore. */
                i = kw + 1;
                continue;
            }
            /* Control word: letters then optional signed numeric parameter. */
            size_t kw_end = kw;
            while (kw_end < len && ((buffer[kw_end] >= 'a' && buffer[kw_end] <= 'z') ||
                                    (buffer[kw_end] >= 'A' && buffer[kw_end] <= 'Z'))) {
                kw_end++;
            }
            size_t kw_len = kw_end - kw;
            int has_param = 0;
            int negative = 0;
            long param = 0;
            size_t num = kw_end;
            if (num < len && buffer[num] == '-') {
                negative = 1;
                num++;
            }
            while (num < len && buffer[num] >= '0' && buffer[num] <= '9') {
                has_param = 1;
                param = param * 10 + (buffer[num] - '0');
                num++;
            }
            if (negative) {
                param = -param;
            }
            size_t after = num;
            if (after < len && buffer[after] == ' ') {
                after++; /* a single trailing space delimits the control word */
            }

            /* Toggle helper: flush the open run before any format change. */
#define RTF_SET_FORMAT(expr)                                                                       \
    do {                                                                                           \
        if (out_len > run_start && current != 0 && run_count < len + 1) {                          \
            runs[run_count].start = (int32_t)run_start;                                            \
            runs[run_count].end = (int32_t)out_len;                                                \
            runs[run_count].format = current;                                                      \
            run_count++;                                                                           \
        }                                                                                          \
        run_start = out_len;                                                                       \
        (expr);                                                                                    \
    } while (0)

            if (kw_len == 1 && (buffer[kw] == 'b' || buffer[kw] == 'B')) {
                int on = !(has_param && param == 0);
                RTF_SET_FORMAT(current = on ? (current | YETTY_YRICH_FMT_BOLD)
                                            : (current & ~YETTY_YRICH_FMT_BOLD));
            } else if (kw_len == 1 && (buffer[kw] == 'i' || buffer[kw] == 'I')) {
                int on = !(has_param && param == 0);
                RTF_SET_FORMAT(current = on ? (current | YETTY_YRICH_FMT_ITALIC)
                                            : (current & ~YETTY_YRICH_FMT_ITALIC));
            } else if (kw_len == 6 && strncmp(buffer + kw, "strike", 6) == 0) {
                int on = !(has_param && param == 0);
                RTF_SET_FORMAT(current = on ? (current | YETTY_YRICH_FMT_STRIKE)
                                            : (current & ~YETTY_YRICH_FMT_STRIKE));
            } else if (kw_len == 1 && (buffer[kw] == 's' || buffer[kw] == 'S') && has_param) {
                if (param >= 1 && param <= 6) {
                    heading = (uint32_t)param;
                }
            } else if (kw_len == 5 && strncmp(buffer + kw, "brdrb", 5) == 0) {
                is_hr = 1;
            } else if (kw_len == 4 && strncmp(buffer + kw, "pard", 4) == 0) {
                heading = 0;
                is_hr = 0;
                RTF_SET_FORMAT(current = 0);
            } else if (kw_len == 3 && strncmp(buffer + kw, "par", 3) == 0) {
                if (out_len > run_start && current != 0 && run_count < len + 1) {
                    runs[run_count].start = (int32_t)run_start;
                    runs[run_count].end = (int32_t)out_len;
                    runs[run_count].format = current;
                    run_count++;
                }
                struct yetty_ycore_void_result flush_res =
                    rtf_flush_paragraph(doc, out, out_len, runs, run_count, heading, is_hr);
                if (YETTY_IS_ERR(flush_res)) {
                    yetty_ycore_error_destroy(flush_res.error);
                    free(out);
                    free(runs);
                    free(buffer);
                    destroy_doc(doc);
                    return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_import_rtf: paragraph");
                }
                out_len = 0;
                run_count = 0;
                run_start = 0;
                heading = 0;
                is_hr = 0;
                any_text = 0;
            }
#undef RTF_SET_FORMAT
            i = after;
            continue;
        }
        if (code == '\n' || code == '\r') {
            i++;
            continue; /* raw newlines in RTF are not text */
        }
        out[out_len++] = code;
        any_text = 1;
        i++;
    }

    /* Trailing paragraph without a closing \par (rare, but be safe). */
    if (any_text || out_len > 0) {
        if (out_len > run_start && current != 0 && run_count < len + 1) {
            runs[run_count].start = (int32_t)run_start;
            runs[run_count].end = (int32_t)out_len;
            runs[run_count].format = current;
            run_count++;
        }
        struct yetty_ycore_void_result flush_res =
            rtf_flush_paragraph(doc, out, out_len, runs, run_count, heading, is_hr);
        if (YETTY_IS_ERR(flush_res)) {
            yetty_ycore_error_destroy(flush_res.error);
        }
    }

    free(out);
    free(runs);
    free(buffer);
    return YETTY_OK(yetty_yclass_object_ptr, doc);
}
