/*
 * yplot-data.c — load plot samples from data files: NumPy .npy arrays and
 * delimited text columns (CSV/TSV/whitespace). Pure CPU code, part of
 * yetty_yplot_core; no new dependencies (the .npy header is a restricted
 * Python-literal dict parsed with plain string scanning).
 *
 * Spec syntax accepted by yetty_yplot_load_samples():
 *   "measurements.npy"          1-D array (f4/f8/i4/i8, C order)
 *   "run.csv"                   default column (second column when the file
 *                               has several, else the first)
 *   "run.csv:temperature"       column by header name
 *   "run.csv:2"                 column by 0-based index
 *
 * Text files: delimiter is sniffed per file (comma, tab, else whitespace);
 * a first row whose selected fields are non-numeric is treated as the
 * header; '#'-prefixed and blank lines are skipped.
 */

#include <yetty/yplot/yplot.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read the whole file into a heap buffer (NUL-terminated for text use). */
static struct yetty_ycore_void_result read_entire_file(const char *path, uint8_t **out_bytes,
                                                       size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "cannot open data file");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "cannot seek data file");
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "cannot size data file");
    }
    rewind(file);
    uint8_t *bytes = malloc((size_t)size + 1);
    if (!bytes) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "data file alloc failed");
    }
    size_t got = fread(bytes, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "short read on data file");
    }
    bytes[size] = '\0';
    *out_bytes = bytes;
    *out_size = (size_t)size;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * .npy
 *===========================================================================*/

/* Find `key` in the .npy header dict and return the pointer right after it,
 * or NULL. Keys look like 'descr': — quotes included in the needle. */
static const char *npy_find_key(const char *header, const char *key)
{
    const char *hit = strstr(header, key);
    return hit ? hit + strlen(key) : NULL;
}

static struct yetty_yplot_loaded_samples_result npy_load(const uint8_t *bytes, size_t size)
{
    if (size < 10 || memcmp(bytes, "\x93NUMPY", 6) != 0) {
        return YETTY_ERR(yetty_yplot_loaded_samples, "not a .npy file");
    }
    uint8_t version_major = bytes[6];
    size_t header_len;
    size_t header_off;
    if (version_major == 1) {
        header_len = (size_t)bytes[8] | ((size_t)bytes[9] << 8);
        header_off = 10;
    } else if (version_major == 2 || version_major == 3) {
        if (size < 12) {
            return YETTY_ERR(yetty_yplot_loaded_samples, "truncated .npy header");
        }
        header_len = (size_t)bytes[8] | ((size_t)bytes[9] << 8) | ((size_t)bytes[10] << 16) |
                     ((size_t)bytes[11] << 24);
        header_off = 12;
    } else {
        return YETTY_ERR(yetty_yplot_loaded_samples, "unsupported .npy version");
    }
    if (header_off + header_len > size) {
        return YETTY_ERR(yetty_yplot_loaded_samples, "truncated .npy header");
    }

    /* NUL-terminate a copy of the header for string scanning. */
    char *header = malloc(header_len + 1);
    if (!header) {
        return YETTY_ERR(yetty_yplot_loaded_samples, ".npy header alloc failed");
    }
    memcpy(header, bytes + header_off, header_len);
    header[header_len] = '\0';

    const char *descr = npy_find_key(header, "'descr':");
    const char *fortran = npy_find_key(header, "'fortran_order':");
    const char *shape = npy_find_key(header, "'shape':");
    if (!descr || !fortran || !shape) {
        free(header);
        return YETTY_ERR(yetty_yplot_loaded_samples, "malformed .npy header dict");
    }

    /* dtype: '<f4' / '<f8' / '<i4' / '<i8' (little-endian only). */
    while (*descr == ' ' || *descr == '\'') {
        descr++;
    }
    char dtype_kind = 0;
    int dtype_size = 0;
    if (*descr == '<' || *descr == '|') {
        dtype_kind = descr[1];
        dtype_size = descr[2] - '0';
    }
    bool dtype_ok = (dtype_kind == 'f' && (dtype_size == 4 || dtype_size == 8)) ||
                    (dtype_kind == 'i' && (dtype_size == 4 || dtype_size == 8));
    if (!dtype_ok) {
        free(header);
        return YETTY_ERR(yetty_yplot_loaded_samples,
                         ".npy dtype not supported (need little-endian f4/f8/i4/i8)");
    }

    while (*fortran == ' ') {
        fortran++;
    }
    if (strncmp(fortran, "True", 4) == 0) {
        free(header);
        return YETTY_ERR(yetty_yplot_loaded_samples, "Fortran-order .npy not supported");
    }

    /* shape: (N,) — 1-D only (a trailing ,1 dimension is tolerated). */
    const char *open_paren = strchr(shape, '(');
    if (!open_paren) {
        free(header);
        return YETTY_ERR(yetty_yplot_loaded_samples, "malformed .npy shape");
    }
    char *cursor = (char *)open_paren + 1;
    size_t dims[4] = {0};
    int dim_count = 0;
    while (dim_count < 4) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == ')') {
            break;
        }
        char *end = NULL;
        unsigned long value = strtoul(cursor, &end, 10);
        if (end == cursor) {
            break;
        }
        dims[dim_count++] = (size_t)value;
        cursor = end;
        while (*cursor == ' ' || *cursor == ',') {
            cursor++;
        }
    }
    free(header);
    if (dim_count == 0) {
        return YETTY_ERR(yetty_yplot_loaded_samples, "empty .npy shape");
    }
    if (dim_count > 2 || (dim_count == 2 && dims[1] != 1)) {
        return YETTY_ERR(yetty_yplot_loaded_samples,
                         "only 1-D .npy arrays are supported (use numpy to slice a column)");
    }
    size_t count = dims[0];

    size_t data_off = header_off + header_len;
    size_t need = count * (size_t)dtype_size;
    if (data_off + need > size) {
        return YETTY_ERR(yetty_yplot_loaded_samples, ".npy data truncated");
    }

    float *samples = malloc(count * sizeof(float));
    if (!samples) {
        return YETTY_ERR(yetty_yplot_loaded_samples, ".npy samples alloc failed");
    }
    const uint8_t *data = bytes + data_off;
    for (size_t i = 0; i < count; i++) {
        if (dtype_kind == 'f' && dtype_size == 4) {
            float value;
            memcpy(&value, data + i * 4, 4);
            samples[i] = value;
        } else if (dtype_kind == 'f' && dtype_size == 8) {
            double value;
            memcpy(&value, data + i * 8, 8);
            samples[i] = (float)value;
        } else if (dtype_kind == 'i' && dtype_size == 4) {
            int32_t value;
            memcpy(&value, data + i * 4, 4);
            samples[i] = (float)value;
        } else {
            int64_t value;
            memcpy(&value, data + i * 8, 8);
            samples[i] = (float)value;
        }
    }
    struct yetty_yplot_loaded_samples loaded = {.samples = samples, .count = count};
    return YETTY_OK(yetty_yplot_loaded_samples, loaded);
}

/*=============================================================================
 * Delimited text columns
 *===========================================================================*/

/* Split one line in place into fields by `delimiter` (0 = any whitespace
 * run). Returns the field count; writes field start pointers. */
static size_t split_fields(char *line, char delimiter, char **fields, size_t max_fields)
{
    size_t count = 0;
    char *cursor = line;
    if (delimiter == 0) {
        while (*cursor && count < max_fields) {
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            if (!*cursor) {
                break;
            }
            fields[count++] = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') {
                cursor++;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        return count;
    }
    while (count < max_fields) {
        fields[count++] = cursor;
        char *next = strchr(cursor, delimiter);
        if (!next) {
            break;
        }
        *next = '\0';
        cursor = next + 1;
    }
    return count;
}

static bool field_is_numeric(const char *field)
{
    if (!field || !*field) {
        return false;
    }
    char *end = NULL;
    strtod(field, &end);
    while (end && (*end == ' ' || *end == '\r')) {
        end++;
    }
    return end && *end == '\0';
}

static struct yetty_yplot_loaded_samples_result text_load(char *text, const char *column_spec)
{
    enum { MAX_FIELDS = 64 };
    char *fields[MAX_FIELDS];

    /* Delimiter sniff on the first content line — skipping '#' comment
     * lines and blanks (NOAA-style data files open with a comment block). */
    char delimiter = 0;
    for (char *scan = text; *scan;) {
        while (*scan == '\n' || *scan == '\r' || *scan == ' ' || *scan == '\t') {
            scan++;
        }
        char *line_end = strchr(scan, '\n');
        size_t line_len = line_end ? (size_t)(line_end - scan) : strlen(scan);
        if (line_len > 0 && *scan != '#') {
            if (memchr(scan, ',', line_len)) {
                delimiter = ',';
            } else if (memchr(scan, '\t', line_len)) {
                delimiter = '\t';
            }
            break;
        }
        if (!line_end) {
            break;
        }
        scan = line_end + 1;
    }

    float *samples = NULL;
    size_t count = 0;
    size_t capacity = 0;
    long column_index = -1; /* resolved below */
    bool header_checked = false;

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        size_t line_len = strlen(line);
        while (line_len > 0 && (line[line_len - 1] == '\r' || line[line_len - 1] == ' ')) {
            line[--line_len] = '\0';
        }
        if (line_len == 0 || line[0] == '#') {
            continue;
        }

        size_t field_count = split_fields(line, delimiter, fields, MAX_FIELDS);
        if (field_count == 0) {
            continue;
        }

        if (!header_checked) {
            header_checked = true;
            /* Resolve the column: explicit index, header name, or default
             * (second column when several, else the first). */
            if (column_spec && *column_spec) {
                char *end = NULL;
                long parsed_index = strtol(column_spec, &end, 10);
                if (end && *end == '\0') {
                    column_index = parsed_index;
                } else {
                    for (size_t i = 0; i < field_count; i++) {
                        if (strcmp(fields[i], column_spec) == 0) {
                            column_index = (long)i;
                            break;
                        }
                    }
                    if (column_index < 0) {
                        free(samples);
                        return YETTY_ERR(yetty_yplot_loaded_samples,
                                         "column name not found in header");
                    }
                }
            } else {
                column_index = field_count >= 2 ? 1 : 0;
            }
            /* A non-numeric selected field marks this row as the header —
             * consume it. */
            if (column_index < (long)field_count && !field_is_numeric(fields[column_index])) {
                continue;
            }
        }

        if (column_index >= (long)field_count || !field_is_numeric(fields[column_index])) {
            continue; /* ragged or non-numeric row — skip */
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 1024;
            float *grown = realloc(samples, capacity * sizeof(float));
            if (!grown) {
                free(samples);
                return YETTY_ERR(yetty_yplot_loaded_samples, "samples alloc failed");
            }
            samples = grown;
        }
        samples[count++] = (float)strtod(fields[column_index], NULL);
    }

    if (count == 0) {
        free(samples);
        return YETTY_ERR(yetty_yplot_loaded_samples, "no numeric samples in data file");
    }
    struct yetty_yplot_loaded_samples loaded = {.samples = samples, .count = count};
    return YETTY_OK(yetty_yplot_loaded_samples, loaded);
}

/*=============================================================================
 * Entry point
 *===========================================================================*/

struct yetty_yplot_loaded_samples_result yetty_yplot_load_samples(const char *spec)
{
    if (!spec || !*spec) {
        return YETTY_ERR(yetty_yplot_loaded_samples, "empty data spec");
    }

    /* Split "path:column". A lone trailing ":..." is only treated as a
     * column spec when the prefix names an existing file — so paths with
     * colons in them still work when the file exists. */
    char path[1024];
    const char *column_spec = NULL;
    const char *colon = strrchr(spec, ':');
    if (colon && colon != spec) {
        size_t prefix_len = (size_t)(colon - spec);
        if (prefix_len < sizeof(path)) {
            memcpy(path, spec, prefix_len);
            path[prefix_len] = '\0';
            FILE *probe = fopen(path, "rb");
            if (probe) {
                fclose(probe);
                column_spec = colon + 1;
            }
        }
    }
    if (!column_spec) {
        if (strlen(spec) >= sizeof(path)) {
            return YETTY_ERR(yetty_yplot_loaded_samples, "data path too long");
        }
        strcpy(path, spec);
    }

    uint8_t *bytes = NULL;
    size_t size = 0;
    struct yetty_ycore_void_result read_res = read_entire_file(path, &bytes, &size);
    YETTY_RETURN_IF_ERR(yetty_yplot_loaded_samples, read_res, "load_samples: read");

    struct yetty_yplot_loaded_samples_result out;
    if (size >= 6 && memcmp(bytes, "\x93NUMPY", 6) == 0) {
        out = npy_load(bytes, size);
    } else {
        out = text_load((char *)bytes, column_spec);
    }
    free(bytes);
    return out;
}
