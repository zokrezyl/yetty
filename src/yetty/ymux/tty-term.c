/*
 * ymux/tty-term.c — terminfo loader + tparm expander + features/overrides
 * (see tty-term.h). The tmux tty-term.c / tty-features.c analog, pinned to
 * the d5afb67 behavior the byte-parity oracle drives.
 */

#include "tty-term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Slot table: terminfo identity of every consumed capability. Standard
 * capabilities carry their classic strfnames[] index (extracted from
 * ncurses term.h — cursor_address=Strings[10] etc.); extended capabilities
 * (index -1) resolve by name from the entry's extended section.
 *=========================================================================*/
struct tty_term_slot_id {
    const char *name; /* terminfo capability name */
    int standard_index;
};

static const struct tty_term_slot_id *slot_table(void)
{
    static const struct tty_term_slot_id table[YMUX_TTY_TERM_SLOT_COUNT] = {
        [YMUX_TTY_TERM_CUP] = {"cup", 10},
        [YMUX_TTY_TERM_HPA] = {"hpa", 8},
        [YMUX_TTY_TERM_VPA] = {"vpa", 127},
        [YMUX_TTY_TERM_CUU] = {"cuu", 114},
        [YMUX_TTY_TERM_CUD] = {"cud", 107},
        [YMUX_TTY_TERM_CUB] = {"cub", 111},
        [YMUX_TTY_TERM_CUF] = {"cuf", 112},
        [YMUX_TTY_TERM_CUU1] = {"cuu1", 19},
        [YMUX_TTY_TERM_CUD1] = {"cud1", 11},
        [YMUX_TTY_TERM_CUB1] = {"cub1", 14},
        [YMUX_TTY_TERM_CUF1] = {"cuf1", 17},
        [YMUX_TTY_TERM_HOME] = {"home", 12},
        [YMUX_TTY_TERM_CR] = {"cr", 2},
        [YMUX_TTY_TERM_CSR] = {"csr", 3},
        [YMUX_TTY_TERM_EL] = {"el", 6},
        [YMUX_TTY_TERM_ED] = {"ed", 7},
        [YMUX_TTY_TERM_ECH] = {"ech", 37},
        [YMUX_TTY_TERM_IL] = {"il", 110},
        [YMUX_TTY_TERM_DL] = {"dl", 106},
        [YMUX_TTY_TERM_IL1] = {"il1", 53},
        [YMUX_TTY_TERM_DL1] = {"dl1", 22},
        [YMUX_TTY_TERM_ICH] = {"ich", 108},
        [YMUX_TTY_TERM_DCH] = {"dch", 105},
        [YMUX_TTY_TERM_INDN] = {"indn", 109},
        [YMUX_TTY_TERM_CLEAR] = {"clear", 5},
        [YMUX_TTY_TERM_SGR0] = {"sgr0", 39},
        [YMUX_TTY_TERM_BOLD] = {"bold", 27},
        [YMUX_TTY_TERM_DIM] = {"dim", 30},
        [YMUX_TTY_TERM_SITM] = {"sitm", 311},
        [YMUX_TTY_TERM_SMUL] = {"smul", 36},
        [YMUX_TTY_TERM_BLINK] = {"blink", 26},
        [YMUX_TTY_TERM_REV] = {"rev", 34},
        [YMUX_TTY_TERM_INVIS] = {"invis", 32},
        [YMUX_TTY_TERM_SETAF] = {"setaf", 359},
        [YMUX_TTY_TERM_SETAB] = {"setab", 360},
        [YMUX_TTY_TERM_SMACS] = {"smacs", 25},
        [YMUX_TTY_TERM_RMACS] = {"rmacs", 38},
        [YMUX_TTY_TERM_SMXX] = {"smxx", -1},
        [YMUX_TTY_TERM_RMXX] = {"rmxx", -1},
        [YMUX_TTY_TERM_SMULX] = {"Smulx", -1},
        [YMUX_TTY_TERM_SETULC] = {"Setulc", -1},
        [YMUX_TTY_TERM_SMOL] = {"Smol", -1},
        [YMUX_TTY_TERM_RMOL] = {"Rmol", -1},
        [YMUX_TTY_TERM_SYNC] = {"Sync", -1},
        [YMUX_TTY_TERM_SETRGBF] = {"setrgbf", -1},
        [YMUX_TTY_TERM_SETRGBB] = {"setrgbb", -1},
    };
    return table;
}

/* colors is Numbers[13] (max_colors) in the standard numbers section. */
enum { TTY_TERM_NUM_MAX_COLORS = 13 };

/* Compiled-terminfo DB read/parse outcome — distinguishes a genuine MISS or
 * malformed entry (fall through to the compiled-in fallback) from a RESOURCE
 * failure (allocation OOM), which must PROPAGATE rather than silently downgrade
 * an attachment to a different terminal profile (cycle-25 P1). */
enum terminfo_db_status {
    TERMINFO_DB_LOADED = 0, /* a usable entry was parsed into the model */
    TERMINFO_DB_MISS,       /* no such entry / malformed — use the fallback */
    TERMINFO_DB_OOM,        /* allocation failure — propagate, do not fall back */
};

/* Set (or, with value == NULL, clear) capability `slot`. strdup FIRST, then
 * swap: an allocation failure leaves the PREVIOUS capability intact and
 * surfaces an error, so an OOM can never silently turn a real terminfo cap (or
 * an override) into an absent one and still report success. The error
 * propagates up through load / apply_features rather than being swallowed. */
static struct yetty_ycore_void_result term_set_string(struct yetty_ymux_tty_term *term,
                                                      enum yetty_ymux_tty_term_slot slot,
                                                      const char *value)
{
    if (!value) {
        free(term->strings[slot]);
        term->strings[slot] = NULL;
        return YETTY_OK_VOID();
    }
    char *copy = strdup(value);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void,
                         "term_set_string: strdup OOM (capability left unchanged)");
    }
    free(term->strings[slot]);
    term->strings[slot] = copy;
    return YETTY_OK_VOID();
}

void yetty_ymux_tty_term_free(struct yetty_ymux_tty_term *term)
{
    if (!term) {
        return;
    }
    for (int slot = 0; slot < YMUX_TTY_TERM_SLOT_COUNT; ++slot) {
        free(term->strings[slot]);
        term->strings[slot] = NULL;
    }
}

/*===========================================================================
 * Compiled terminfo(5) reader.
 *=========================================================================*/
static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* A compiled terminfo string is only a valid C string if a NUL byte exists
 * at or after `offset` and strictly before `table_len` — otherwise strcmp /
 * strdup on `table + offset` reads out of bounds on malformed data. Returns
 * 1 when the offset is in range AND NUL-terminated inside the table. */
static int terminfo_str_ok(const char *table, size_t table_len, long offset)
{
    if (offset < 0 || (size_t)offset >= table_len) {
        return 0;
    }
    return memchr(table + offset, '\0', table_len - (size_t)offset) != NULL;
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)(bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                      ((uint32_t)bytes[3] << 24));
}

/* Parse one compiled entry (legacy 0432 int16 / extended-number 01036 int32
 * format, plus the ncurses extended-capability tail) into `term`. Returns 1
 * on success. */
static enum terminfo_db_status terminfo_parse(struct yetty_ymux_tty_term *term, const uint8_t *data,
                                              size_t data_len)
{
    enum { MAGIC_INT16 = 00432, MAGIC_INT32 = 01036 };
    if (data_len < 12) {
        return TERMINFO_DB_MISS;
    }
    uint16_t magic = read_le16(data);
    if (magic != MAGIC_INT16 && magic != MAGIC_INT32) {
        return TERMINFO_DB_MISS;
    }
    size_t number_width = magic == MAGIC_INT32 ? 4 : 2;
    size_t names_len = read_le16(data + 2);
    size_t bools_count = read_le16(data + 4);
    size_t nums_count = read_le16(data + 6);
    size_t strs_count = read_le16(data + 8);
    size_t table_len = read_le16(data + 10);

    size_t offset = 12;
    if (offset + names_len > data_len) {
        return TERMINFO_DB_MISS;
    }
    offset += names_len;
    if (offset + bools_count > data_len) {
        return TERMINFO_DB_MISS;
    }
    offset += bools_count;
    if ((names_len + bools_count) & 1) {
        offset += 1; /* even alignment before the numbers section */
    }
    if (offset + nums_count * number_width > data_len) {
        return TERMINFO_DB_MISS;
    }
    const uint8_t *numbers = data + offset;
    offset += nums_count * number_width;
    if (offset + strs_count * 2 > data_len) {
        return TERMINFO_DB_MISS;
    }
    const uint8_t *string_offsets = data + offset;
    offset += strs_count * 2;
    if (offset + table_len > data_len) {
        return TERMINFO_DB_MISS;
    }
    const char *string_table = (const char *)(data + offset);
    offset += table_len;

    /* colours */
    if (TTY_TERM_NUM_MAX_COLORS < (int)nums_count) {
        const uint8_t *cell = numbers + (size_t)TTY_TERM_NUM_MAX_COLORS * number_width;
        long value = number_width == 4 ? (int32_t)read_le32(cell) : (int16_t)read_le16(cell);
        if (value > 0) {
            term->colors = (int)value;
        }
    }

    /* boolean capabilities (tmux reads the compiled boolean section — one byte
     * each at the standard index). BCE, AM and XENL drive renderer strategy. */
    {
        const uint8_t *bools = data + 12 + names_len;
        static const struct {
            enum yetty_ymux_tty_term_bool which;
            int index;
        } bool_map[] = {
            {YMUX_TTY_TERM_BOOL_BCE, 27},
            {YMUX_TTY_TERM_BOOL_AM, 1},
            {YMUX_TTY_TERM_BOOL_XENL, 13},
        };
        for (size_t entry = 0; entry < sizeof(bool_map) / sizeof(bool_map[0]); ++entry) {
            if ((size_t)bool_map[entry].index < bools_count) {
                term->bools[bool_map[entry].which] = bools[bool_map[entry].index] ? 1 : 0;
            }
        }
        term->bools_loaded = 1;
    }

    /* standard string capabilities */
    const struct tty_term_slot_id *slots = slot_table();
    for (int slot = 0; slot < YMUX_TTY_TERM_SLOT_COUNT; ++slot) {
        int index = slots[slot].standard_index;
        if (index < 0 || (size_t)index >= strs_count) {
            continue;
        }
        int16_t string_offset = (int16_t)read_le16(string_offsets + (size_t)index * 2);
        if (!terminfo_str_ok(string_table, table_len, string_offset)) {
            continue; /* absent (-1) or not NUL-terminated in the table */
        }
        struct yetty_ycore_void_result set_res = term_set_string(
            term, (enum yetty_ymux_tty_term_slot)slot, string_table + string_offset);
        if (YETTY_IS_ERR(set_res)) {
            yetty_ycore_error_destroy(set_res.error); /* OOM: abandon DB parse, use fallback */
            return TERMINFO_DB_OOM;
        }
    }

    /* extended-capability tail (ncurses): [even align] then 5 int16 counts:
     * ext_bools, ext_nums, ext_strs, ext_name_count(=bools+nums+strs),
     * ext_table_len — followed by bools, [align], numbers, string offsets,
     * name offsets, then the table (capability strings first, names after). */
    if (offset & 1) {
        offset += 1;
    }
    if (offset + 10 > data_len) {
        return TERMINFO_DB_LOADED; /* no extended section — fine */
    }
    size_t ext_bools = read_le16(data + offset);
    size_t ext_nums = read_le16(data + offset + 2);
    size_t ext_strs = read_le16(data + offset + 4);
    size_t ext_names = read_le16(data + offset + 6);
    size_t ext_table_len = read_le16(data + offset + 8);
    offset += 10;
    if (ext_names != ext_bools + ext_nums + ext_strs) {
        return TERMINFO_DB_LOADED; /* not the layout we understand — standard part already read */
    }
    offset += ext_bools;
    if (offset & 1) {
        offset += 1;
    }
    offset += ext_nums * number_width;
    if (offset + (ext_strs + ext_names) * 2 > data_len) {
        return TERMINFO_DB_LOADED;
    }
    const uint8_t *ext_string_offsets = data + offset;
    const uint8_t *ext_name_offsets = data + offset + ext_strs * 2;
    offset += (ext_strs + ext_names) * 2;
    if (offset + ext_table_len > data_len) {
        return TERMINFO_DB_LOADED;
    }
    const char *ext_table = (const char *)(data + offset);

    /* The name-offset table indexes into the table AFTER the capability
     * strings: find where names start (== end of the last string). */
    size_t names_base = 0;
    for (size_t string_index = 0; string_index < ext_strs; ++string_index) {
        int16_t string_offset = (int16_t)read_le16(ext_string_offsets + string_index * 2);
        if (string_offset < 0) {
            continue;
        }
        size_t end = (size_t)string_offset;
        while (end < ext_table_len && ext_table[end]) {
            ++end;
        }
        if (end + 1 > names_base) {
            names_base = end + 1;
        }
    }

    for (size_t name_index = 0; name_index < ext_names; ++name_index) {
        int16_t name_offset = (int16_t)read_le16(ext_name_offsets + name_index * 2);
        if (name_offset < 0 || names_base + (size_t)name_offset >= ext_table_len) {
            continue;
        }
        /* The name must be NUL-terminated inside the table before strcmp. */
        if (!terminfo_str_ok(ext_table, ext_table_len, (long)(names_base + (size_t)name_offset))) {
            continue;
        }
        const char *cap_name = ext_table + names_base + (size_t)name_offset;
        /* Only names in the string range carry a string value. */
        if (name_index < ext_bools + ext_nums) {
            continue;
        }
        size_t string_index = name_index - ext_bools - ext_nums;
        if (string_index >= ext_strs) {
            continue;
        }
        int16_t string_offset = (int16_t)read_le16(ext_string_offsets + string_index * 2);
        if (!terminfo_str_ok(ext_table, ext_table_len, string_offset)) {
            continue; /* absent or not NUL-terminated in the table */
        }
        const struct tty_term_slot_id *slots_by_name = slot_table();
        for (int slot = 0; slot < YMUX_TTY_TERM_SLOT_COUNT; ++slot) {
            if (slots_by_name[slot].standard_index < 0 &&
                strcmp(slots_by_name[slot].name, cap_name) == 0) {
                struct yetty_ycore_void_result set_res = term_set_string(
                    term, (enum yetty_ymux_tty_term_slot)slot, ext_table + string_offset);
                if (YETTY_IS_ERR(set_res)) {
                    yetty_ycore_error_destroy(set_res.error); /* OOM: fall back */
                    return TERMINFO_DB_OOM;
                }
            }
        }
    }
    return TERMINFO_DB_LOADED;
}

/* Try to read <dir>/<c>/<name> (linux layout) or <dir>/<%02x>/<name>
 * (hashed layout). Returns malloc'd file contents or NULL. On NULL, sets
 * `*out_resource_error` to 1 when the cause was an ALLOCATION failure (as
 * opposed to a plain file miss), so the caller distinguishes OOM from MISS. */
static uint8_t *terminfo_read_file(const char *directory, const char *term_name, size_t *out_len,
                                   int *out_resource_error)
{
    char path[512];
    const char *attempts[2];
    char letter_dir[8];
    char hex_dir[8];
    snprintf(letter_dir, sizeof(letter_dir), "%c", term_name[0]);
    snprintf(hex_dir, sizeof(hex_dir), "%02x", (unsigned char)term_name[0]);
    attempts[0] = letter_dir;
    attempts[1] = hex_dir;
    for (int attempt = 0; attempt < 2; ++attempt) {
        snprintf(path, sizeof(path), "%s/%s/%s", directory, attempts[attempt], term_name);
        FILE *file = fopen(path, "rb");
        if (!file) {
            continue;
        }
        fseek(file, 0, SEEK_END);
        long file_len = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (file_len <= 0 || file_len > 1 << 20) {
            fclose(file);
            continue;
        }
        uint8_t *data = malloc((size_t)file_len);
        if (!data) {
            fclose(file);
            if (out_resource_error) {
                *out_resource_error = 1; /* OOM, not a miss */
            }
            return NULL;
        }
        size_t got = fread(data, 1, (size_t)file_len, file);
        fclose(file);
        if (got != (size_t)file_len) {
            free(data);
            continue;
        }
        *out_len = got;
        return data;
    }
    return NULL;
}

static enum terminfo_db_status terminfo_load_from_db(struct yetty_ymux_tty_term *term,
                                                     const char *term_name,
                                                     const char *terminfo_path)
{
    const char *directories[6];
    size_t directory_count = 0;
    char home_terminfo[512];
    if (terminfo_path && terminfo_path[0]) {
        directories[directory_count++] = terminfo_path;
    } else {
        const char *env_terminfo = getenv("TERMINFO");
        if (env_terminfo && env_terminfo[0]) {
            directories[directory_count++] = env_terminfo;
        }
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(home_terminfo, sizeof(home_terminfo), "%s/.terminfo", home);
            directories[directory_count++] = home_terminfo;
        }
        directories[directory_count++] = "/etc/terminfo";
        directories[directory_count++] = "/lib/terminfo";
        directories[directory_count++] = "/usr/share/terminfo";
    }
    for (size_t index = 0; index < directory_count; ++index) {
        size_t data_len = 0;
        int resource_error = 0;
        uint8_t *data =
            terminfo_read_file(directories[index], term_name, &data_len, &resource_error);
        if (!data) {
            if (resource_error) {
                return TERMINFO_DB_OOM; /* allocation failure — propagate, do not fall back */
            }
            continue; /* plain miss — try the next directory */
        }
        enum terminfo_db_status parsed = terminfo_parse(term, data, data_len);
        free(data);
        if (parsed == TERMINFO_DB_LOADED) {
            return TERMINFO_DB_LOADED;
        }
        /* A partial parse (malformed entry, or a mid-parse OOM) may have
         * populated some string slots. Clear them so the next directory attempt
         * — or the caller's compiled-in fallback — starts from a clean model
         * and never inherits a stale/partial slot (cycle-24 P1). */
        yetty_ymux_tty_term_free(term);
        if (parsed == TERMINFO_DB_OOM) {
            return TERMINFO_DB_OOM; /* resource failure — propagate */
        }
        /* TERMINFO_DB_MISS (malformed entry): try the next directory. */
    }
    return TERMINFO_DB_MISS;
}

/*===========================================================================
 * Compiled-in fallback entries (database-less systems / unknown names).
 * The xterm-256color strings are the exact system-terminfo values the
 * byte-parity oracle is pinned to.
 *=========================================================================*/
struct tty_term_fallback_cap {
    enum yetty_ymux_tty_term_slot slot;
    const char *value;
};

static struct yetty_ycore_void_result term_apply_fallback_xterm_256color(
    struct yetty_ymux_tty_term *term)
{
    static const struct tty_term_fallback_cap caps[] = {
        {YMUX_TTY_TERM_CUP, "\033[%i%p1%d;%p2%dH"},
        {YMUX_TTY_TERM_HPA, "\033[%i%p1%dG"},
        {YMUX_TTY_TERM_VPA, "\033[%i%p1%dd"},
        {YMUX_TTY_TERM_CUU, "\033[%p1%dA"},
        {YMUX_TTY_TERM_CUD, "\033[%p1%dB"},
        {YMUX_TTY_TERM_CUB, "\033[%p1%dD"},
        {YMUX_TTY_TERM_CUF, "\033[%p1%dC"},
        {YMUX_TTY_TERM_CUU1, "\033[A"},
        {YMUX_TTY_TERM_CUD1, "\n"},
        {YMUX_TTY_TERM_CUB1, "\b"},
        {YMUX_TTY_TERM_CUF1, "\033[C"},
        {YMUX_TTY_TERM_HOME, "\033[H"},
        {YMUX_TTY_TERM_CR, "\r"},
        {YMUX_TTY_TERM_CSR, "\033[%i%p1%d;%p2%dr"},
        {YMUX_TTY_TERM_EL, "\033[K"},
        {YMUX_TTY_TERM_ED, "\033[J"},
        {YMUX_TTY_TERM_ECH, "\033[%p1%dX"},
        {YMUX_TTY_TERM_IL, "\033[%p1%dL"},
        {YMUX_TTY_TERM_DL, "\033[%p1%dM"},
        {YMUX_TTY_TERM_IL1, "\033[L"},
        {YMUX_TTY_TERM_DL1, "\033[M"},
        {YMUX_TTY_TERM_ICH, "\033[%p1%d@"},
        {YMUX_TTY_TERM_DCH, "\033[%p1%dP"},
        {YMUX_TTY_TERM_INDN, "\033[%p1%dS"},
        {YMUX_TTY_TERM_CLEAR, "\033[H\033[2J"},
        {YMUX_TTY_TERM_SGR0, "\033(B\033[m"},
        {YMUX_TTY_TERM_BOLD, "\033[1m"},
        {YMUX_TTY_TERM_DIM, "\033[2m"},
        {YMUX_TTY_TERM_SITM, "\033[3m"},
        {YMUX_TTY_TERM_SMUL, "\033[4m"},
        {YMUX_TTY_TERM_BLINK, "\033[5m"},
        {YMUX_TTY_TERM_REV, "\033[7m"},
        {YMUX_TTY_TERM_INVIS, "\033[8m"},
        {YMUX_TTY_TERM_SMXX, "\033[9m"},
        {YMUX_TTY_TERM_RMXX, "\033[29m"},
        {YMUX_TTY_TERM_SETAF, "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m"},
        {YMUX_TTY_TERM_SETAB, "\033[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m"},
        {YMUX_TTY_TERM_SMACS, "\033(0"},
        {YMUX_TTY_TERM_RMACS, "\033(B"},
    };
    for (size_t index = 0; index < sizeof(caps) / sizeof(caps[0]); ++index) {
        struct yetty_ycore_void_result set_res =
            term_set_string(term, caps[index].slot, caps[index].value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "fallback xterm-256color cap");
    }
    term->colors = 256;
    return YETTY_OK_VOID();
}

/* Minimal vt100-class profile for unknown names: addressable cursor +
 * line clears only — no ECH, no IL/DL, no colours. */
static struct yetty_ycore_void_result term_apply_fallback_minimal(struct yetty_ymux_tty_term *term)
{
    static const struct tty_term_fallback_cap caps[] = {
        {YMUX_TTY_TERM_CUP, "\033[%i%p1%d;%p2%dH"},
        {YMUX_TTY_TERM_CUU1, "\033[A"},
        {YMUX_TTY_TERM_CUD1, "\n"},
        {YMUX_TTY_TERM_CUB1, "\b"},
        {YMUX_TTY_TERM_CUF1, "\033[C"},
        {YMUX_TTY_TERM_HOME, "\033[H"},
        {YMUX_TTY_TERM_CR, "\r"},
        {YMUX_TTY_TERM_CSR, "\033[%i%p1%d;%p2%dr"},
        {YMUX_TTY_TERM_EL, "\033[K"},
        {YMUX_TTY_TERM_ED, "\033[J"},
        {YMUX_TTY_TERM_CLEAR, "\033[H\033[2J"},
        {YMUX_TTY_TERM_SGR0, "\033[m"},
        {YMUX_TTY_TERM_BOLD, "\033[1m"},
        {YMUX_TTY_TERM_SMUL, "\033[4m"},
        {YMUX_TTY_TERM_REV, "\033[7m"},
        {YMUX_TTY_TERM_SMACS, "\033(0"},
        {YMUX_TTY_TERM_RMACS, "\033(B"},
    };
    for (size_t index = 0; index < sizeof(caps) / sizeof(caps[0]); ++index) {
        struct yetty_ycore_void_result set_res =
            term_set_string(term, caps[index].slot, caps[index].value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "fallback minimal cap");
    }
    term->colors = 8;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymux_tty_term_load(struct yetty_ymux_tty_term *out,
                                                        const char *term_name,
                                                        const char *terminfo_path)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "tty_term_load: NULL out");
    }
    memset(out, 0, sizeof(*out));
    if (!term_name || !term_name[0]) {
        term_name = "unknown";
    }
    snprintf(out->name, sizeof(out->name), "%s", term_name);
    /* The TERM name is ATTACH-CONTROLLED and becomes a path component in the
     * database lookup (<dir>/<c>/<name>). Reject anything that could escape
     * the database directory or is over-long — a terminfo(5) alias is
     * [A-Za-z0-9._-]+ with no separators. On rejection, skip the database and
     * fall through to the compiled-in profile (never open an arbitrary path). */
    int name_is_path_safe = 1;
    if (strlen(term_name) >= sizeof(out->name)) {
        name_is_path_safe = 0;
    }
    for (const char *scan = term_name; *scan; ++scan) {
        char ch = *scan;
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                 ch == '.' || ch == '_' || ch == '-' || ch == '+';
        if (!ok) {
            name_is_path_safe = 0;
            break;
        }
    }
    /* A leading '.' (".", "..") or an empty component would still be traversal-
     * adjacent; the alnum-first rule blocks "/" and ".." interior, but guard a
     * leading dot explicitly. */
    if (term_name[0] == '.') {
        name_is_path_safe = 0;
    }
    if (name_is_path_safe) {
        enum terminfo_db_status db = terminfo_load_from_db(out, term_name, terminfo_path);
        if (db == TERMINFO_DB_LOADED) {
            out->loaded_from_db = 1;
            return YETTY_OK_VOID();
        }
        if (db == TERMINFO_DB_OOM) {
            /* A RESOURCE failure must PROPAGATE — never silently downgrade a
             * real terminfo entry to a different compiled-in profile (cycle-25
             * P1). Only a genuine MISS falls through to the fallback below. */
            return YETTY_ERR(yetty_ycore_void,
                             "tty_term_load: terminfo DB read/parse allocation failure");
        }
    }
    /* Database miss: the compiled-in fallback keeps resolution deterministic.
     * xterm-256color-family names get the pinned profile; anything else the
     * MINIMAL vt100-class profile (never silently xterm — review #20). */
    if (strncmp(term_name, "xterm-256color", 14) == 0 || strncmp(term_name, "xterm", 5) == 0 ||
        strncmp(term_name, "tmux", 4) == 0 || strncmp(term_name, "screen-256color", 15) == 0) {
        struct yetty_ycore_void_result fallback_res = term_apply_fallback_xterm_256color(out);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fallback_res, "apply xterm-256color fallback");
        if (strncmp(term_name, "xterm", 5) == 0 && strncmp(term_name, "xterm-256color", 14) != 0) {
            out->colors = 8;
        }
    } else {
        struct yetty_ycore_void_result fallback_res = term_apply_fallback_minimal(out);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fallback_res, "apply minimal fallback");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * tparm-style expansion.
 *=========================================================================*/
struct expand_state {
    long stack[16];
    int stack_depth;
    long params[9];
    long variables[26];
};

static void expand_push(struct expand_state *state, long value)
{
    if (state->stack_depth < 16) {
        state->stack[state->stack_depth++] = value;
    }
}

static long expand_pop(struct expand_state *state)
{
    return state->stack_depth > 0 ? state->stack[--state->stack_depth] : 0;
}

size_t yetty_ymux_tty_term_expand(const char *cap, const long *params, size_t param_count,
                                  char *out, size_t out_cap)
{
    if (!cap || !out || out_cap == 0) {
        return 0;
    }
    struct expand_state state = {0};
    for (size_t index = 0; index < param_count && index < 9; ++index) {
        state.params[index] = params[index];
    }
    size_t out_len = 0;
    const char *cursor = cap;
#define EXPAND_PUT(character)                                                                      \
    do {                                                                                           \
        if (out_len + 1 >= out_cap) {                                                              \
            return 0;                                                                              \
        }                                                                                          \
        out[out_len++] = (char)(character);                                                        \
    } while (0)

    while (*cursor) {
        if (*cursor != '%') {
            EXPAND_PUT(*cursor);
            ++cursor;
            continue;
        }
        ++cursor; /* consume '%' */
        switch (*cursor) {
        case '%':
            EXPAND_PUT('%');
            ++cursor;
            break;
        case 'i':
            state.params[0] += 1;
            state.params[1] += 1;
            ++cursor;
            break;
        case 'p':
            ++cursor;
            if (*cursor >= '1' && *cursor <= '9') {
                expand_push(&state, state.params[*cursor - '1']);
                ++cursor;
            }
            break;
        case '{': {
            ++cursor;
            long value = strtol(cursor, (char **)&cursor, 10);
            if (*cursor == '}') {
                ++cursor;
            }
            expand_push(&state, value);
            break;
        }
        case '\'':
            ++cursor;
            expand_push(&state, (long)(unsigned char)*cursor);
            ++cursor;
            if (*cursor == '\'') {
                ++cursor;
            }
            break;
        case 'd': {
            char scratch[32];
            int scratch_len = snprintf(scratch, sizeof(scratch), "%ld", expand_pop(&state));
            for (int put = 0; put < scratch_len; ++put) {
                EXPAND_PUT(scratch[put]);
            }
            ++cursor;
            break;
        }
        case 'c':
            EXPAND_PUT((char)expand_pop(&state));
            ++cursor;
            break;
        case 's': {
            /* String parameters are not used by the caps we emit; pop and
             * ignore rather than derail the whole expansion. */
            (void)expand_pop(&state);
            ++cursor;
            break;
        }
        case 'P':
            ++cursor;
            if (*cursor >= 'a' && *cursor <= 'z') {
                state.variables[*cursor - 'a'] = expand_pop(&state);
                ++cursor;
            }
            break;
        case 'g':
            ++cursor;
            if (*cursor >= 'a' && *cursor <= 'z') {
                expand_push(&state, state.variables[*cursor - 'a']);
                ++cursor;
            }
            break;
        case '+': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) + right);
            ++cursor;
            break;
        }
        case '-': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) - right);
            ++cursor;
            break;
        }
        case '*': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) * right);
            ++cursor;
            break;
        }
        case '/': {
            long right = expand_pop(&state);
            expand_push(&state, right ? expand_pop(&state) / right : (expand_pop(&state), 0));
            ++cursor;
            break;
        }
        case 'm': {
            long right = expand_pop(&state);
            expand_push(&state, right ? expand_pop(&state) % right : (expand_pop(&state), 0));
            ++cursor;
            break;
        }
        case '&': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) & right);
            ++cursor;
            break;
        }
        case '|': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) | right);
            ++cursor;
            break;
        }
        case '^': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) ^ right);
            ++cursor;
            break;
        }
        case '=': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) == right);
            ++cursor;
            break;
        }
        case '>': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) > right);
            ++cursor;
            break;
        }
        case '<': {
            long right = expand_pop(&state);
            expand_push(&state, expand_pop(&state) < right);
            ++cursor;
            break;
        }
        case '!':
            expand_push(&state, !expand_pop(&state));
            ++cursor;
            break;
        case '~':
            expand_push(&state, ~expand_pop(&state));
            ++cursor;
            break;
        case '?':
            ++cursor; /* if — nothing to do until %t */
            break;
        case 't': {
            long condition = expand_pop(&state);
            ++cursor;
            if (!condition) {
                /* Skip to the matching %e or %; at this nesting depth. */
                int depth = 0;
                while (*cursor) {
                    if (cursor[0] == '%' && cursor[1] == '?') {
                        ++depth;
                        cursor += 2;
                    } else if (cursor[0] == '%' && cursor[1] == ';') {
                        if (depth == 0) {
                            cursor += 2;
                            break;
                        }
                        --depth;
                        cursor += 2;
                    } else if (cursor[0] == '%' && cursor[1] == 'e' && depth == 0) {
                        cursor += 2;
                        break;
                    } else {
                        ++cursor;
                    }
                }
            }
            break;
        }
        case 'e': {
            /* Reached only when the THEN branch ran — skip to %; */
            ++cursor;
            int depth = 0;
            while (*cursor) {
                if (cursor[0] == '%' && cursor[1] == '?') {
                    ++depth;
                    cursor += 2;
                } else if (cursor[0] == '%' && cursor[1] == ';') {
                    if (depth == 0) {
                        cursor += 2;
                        break;
                    }
                    --depth;
                    cursor += 2;
                } else {
                    ++cursor;
                }
            }
            break;
        }
        case ';':
            ++cursor;
            break;
        default:
            /* Unknown escape: emit verbatim (matches ncurses laxity). */
            EXPAND_PUT('%');
            if (*cursor) {
                EXPAND_PUT(*cursor);
                ++cursor;
            }
            break;
        }
    }
#undef EXPAND_PUT
    out[out_len] = 0;
    return out_len;
}

size_t yetty_ymux_tty_term_emit(const struct yetty_ymux_tty_term *term,
                                enum yetty_ymux_tty_term_slot slot, const long *params,
                                size_t param_count, char *out, size_t out_cap)
{
    if (!term || !term->strings[slot]) {
        return 0;
    }
    return yetty_ymux_tty_term_expand(term->strings[slot], params, param_count, out, out_cap);
}

/*===========================================================================
 * Features + overrides (tmux tty-features.c / tty_term_apply_overrides).
 *=========================================================================*/
struct tty_term_feature_cap {
    enum yetty_ymux_tty_term_slot slot;
    const char *value;
};

/* tmux d5afb67 feature tables — each feature ADDS capability strings. Returns an
 * error only on capability-string allocation failure (term_set_string OOM),
 * which propagates up through apply_features rather than silently dropping a
 * capability. */
static struct yetty_ycore_void_result feature_apply(struct yetty_ymux_tty_term *term,
                                                    const char *feature, size_t len)
{
#define FEATURE_IS(literal) (len == sizeof(literal) - 1 && strncmp(feature, literal, len) == 0)
#define FEATURE_SET(slot, value)                                                                   \
    do {                                                                                           \
        struct yetty_ycore_void_result set_res = term_set_string(term, (slot), (value));           \
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "feature capability string");               \
    } while (0)
    if (FEATURE_IS("256")) {
        term->colors = term->colors < 256 ? 256 : term->colors;
        /* 256-colour setaf/setab (the xterm-256color forms). */
        if (!term->strings[YMUX_TTY_TERM_SETAF]) {
            FEATURE_SET(YMUX_TTY_TERM_SETAF,
                        "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m");
        }
        if (!term->strings[YMUX_TTY_TERM_SETAB]) {
            FEATURE_SET(YMUX_TTY_TERM_SETAB,
                        "\033[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m");
        }
    } else if (FEATURE_IS("RGB")) {
        term->colors = 0x1000000;
        FEATURE_SET(YMUX_TTY_TERM_SETRGBF, "\033[38;2;%p1%d;%p2%d;%p3%dm");
        FEATURE_SET(YMUX_TTY_TERM_SETRGBB, "\033[48;2;%p1%d;%p2%d;%p3%dm");
    } else if (FEATURE_IS("usstyle")) {
        FEATURE_SET(YMUX_TTY_TERM_SMULX, "\033[4:%p1%dm");
        FEATURE_SET(YMUX_TTY_TERM_SETULC,
                    "\033[58::2::%p1%{65536}%/%d::%p1%{256}%/%{255}%&%d::%p1%{255}%&%d%;m");
    } else if (FEATURE_IS("overline")) {
        FEATURE_SET(YMUX_TTY_TERM_SMOL, "\033[53m");
        FEATURE_SET(YMUX_TTY_TERM_RMOL, "\033[55m");
    } else if (FEATURE_IS("strikethrough")) {
        FEATURE_SET(YMUX_TTY_TERM_SMXX, "\033[9m");
        FEATURE_SET(YMUX_TTY_TERM_RMXX, "\033[29m");
    } else if (FEATURE_IS("sync")) {
        FEATURE_SET(YMUX_TTY_TERM_SYNC, "\033[?2026%?%p1%{1}%-%tl%eh%;");
    }
    /* Flag-only features (mouse/title/clipboard/focus/…): no capability
     * STRING the renderer consumes — they surface through the caps flags in
     * tty-render.c's feature handling, which shares this token walk. */
    return YETTY_OK_VOID();
#undef FEATURE_SET
#undef FEATURE_IS
}

/* Decode a terminfo-style value with backslash/caret escapes into `out`. */
static void override_decode_value(const char *value, size_t value_len, char *out, size_t out_cap)
{
    size_t out_len = 0;
    for (size_t index = 0; index < value_len && out_len + 1 < out_cap; ++index) {
        char character = value[index];
        if (character == '\\' && index + 1 < value_len) {
            ++index;
            switch (value[index]) {
            case 'E':
            case 'e':
                out[out_len++] = 0x1B;
                break;
            case 'n':
                out[out_len++] = '\n';
                break;
            case 'r':
                out[out_len++] = '\r';
                break;
            case 't':
                out[out_len++] = '\t';
                break;
            case 'a':
                out[out_len++] = '\a';
                break;
            case '\\':
                out[out_len++] = '\\';
                break;
            default:
                out[out_len++] = value[index];
                break;
            }
        } else if (character == '^' && index + 1 < value_len) {
            ++index;
            out[out_len++] = (char)(value[index] & 0x1F);
        } else {
            out[out_len++] = character;
        }
    }
    out[out_len] = 0;
}

/* Find a capability slot by terminfo name (standard or extended). */
static int slot_by_name(const char *name, size_t name_len)
{
    const struct tty_term_slot_id *slots = slot_table();
    for (int slot = 0; slot < YMUX_TTY_TERM_SLOT_COUNT; ++slot) {
        if (strlen(slots[slot].name) == name_len &&
            strncmp(slots[slot].name, name, name_len) == 0) {
            return slot;
        }
    }
    return -1;
}

int yetty_ymux_tty_term_bool(const struct yetty_ymux_tty_term *term,
                             enum yetty_ymux_tty_term_bool which)
{
    if (!term || which < 0 || which >= YMUX_TTY_TERM_BOOL_COUNT) {
        return 0;
    }
    return term->bools[which] ? 1 : 0;
}

/* Map a boolean capability NAME (bce/am/xenl) to its enum, or -1. */
static int bool_by_name(const char *name, size_t name_len)
{
    if (name_len == 3 && strncmp(name, "bce", 3) == 0) {
        return YMUX_TTY_TERM_BOOL_BCE;
    }
    if (name_len == 2 && strncmp(name, "am", 2) == 0) {
        return YMUX_TTY_TERM_BOOL_AM;
    }
    if (name_len == 4 && strncmp(name, "xenl", 4) == 0) {
        return YMUX_TTY_TERM_BOOL_XENL;
    }
    return -1;
}

struct yetty_ycore_void_result yetty_ymux_tty_term_apply_features(struct yetty_ymux_tty_term *term,
                                                                  const char *features)
{
    if (!term) {
        return YETTY_ERR(yetty_ycore_void, "tty_term_apply_features: NULL term");
    }
    if (!features) {
        return YETTY_OK_VOID();
    }
    const char *cursor = features;
    while (*cursor) {
        while (*cursor == ',' || *cursor == ' ') {
            ++cursor;
        }
        const char *token = cursor;
        while (*cursor && *cursor != ',') {
            ++cursor;
        }
        size_t token_len = (size_t)(cursor - token);
        if (token_len == 0) {
            continue;
        }
        const char *equals = memchr(token, '=', token_len);
        if (equals) {
            /* cap=value OVERRIDE. */
            size_t name_len = (size_t)(equals - token);
            int slot = slot_by_name(token, name_len);
            if (slot >= 0) {
                char decoded[256];
                override_decode_value(equals + 1, token_len - name_len - 1, decoded,
                                      sizeof(decoded));
                struct yetty_ycore_void_result set_res =
                    term_set_string(term, (enum yetty_ymux_tty_term_slot)slot, decoded);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "apply cap override");
            }
            continue;
        }
        if (token_len > 1 && token[token_len - 1] == '@') {
            /* cap@ CANCELLATION (capability removed). Feature-token
             * cancellation (e.g. `usstyle@`) is handled by the caller's
             * token walk in tty-render.c; here a name matching a capability
             * slot removes that capability. */
            int slot = slot_by_name(token, token_len - 1);
            if (slot >= 0) {
                /* Clearing to NULL cannot allocate, but keep the propagation
                 * uniform so no term_set_string result is silently dropped. */
                struct yetty_ycore_void_result clear_res =
                    term_set_string(term, (enum yetty_ymux_tty_term_slot)slot, NULL);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "cancel capability");
            } else {
                /* A cancelled BOOLEAN (bce@ / am@ / xenl@) — no string slot. */
                int boolean = bool_by_name(token, token_len - 1);
                if (boolean >= 0) {
                    term->bools[boolean] = 0;
                    term->bools_loaded = 1;
                }
            }
            continue;
        }
        /* A bare boolean name in the override string SETS it (bce / am / xenl). */
        int boolean = bool_by_name(token, token_len);
        if (boolean >= 0) {
            term->bools[boolean] = 1;
            term->bools_loaded = 1;
            continue;
        }
        struct yetty_ycore_void_result feature_res = feature_apply(term, token, token_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feature_res, "apply feature");
    }
    return YETTY_OK_VOID();
}
