/*
 * yinstall/install.c — the platform-independent installer core.
 *
 * Walks a declarative table of components (see src/yetty/yinstall/README.md), unpacks
 * each embedded blob to its per-OS destination, and prints a clear log of
 * what landed where. Reads the embedded bytes through the platform backend
 * (yetty_yplatform_install_foreach_asset) and resolves destinations through
 * yetty_yplatform_get_{bin,data,config}_dir — no platform #ifdef here.
 */

#include <yetty/yinstall/install.h>

#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/install.h>
#include <yetty/yplatform/paths.h>
#include <yetty/ytrace/ytrace.h>

#include <brotli/decode.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* MSVC <sys/stat.h> lacks the POSIX S_ISREG macro. */
#if defined(_MSC_VER) && !defined(S_ISREG)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

/*
 * The component table. A static const *local* (returned by pointer) — no
 * file-scope state, same program-lifetime storage. Order is install +
 * log order: executables first so the terminal is usable the moment the
 * run finishes, heavy VM payload last.
 */
static const struct yetty_yinstall_component *yinstall_components(size_t *count)
{
    static const struct yetty_yinstall_component table[] = {
        {"Executables", "bin/", YETTY_YINSTALL_DEST_BIN, "", 1,
         "yetty terminal, companion CLIs and demos"},
        {"FFI library", "lib/", YETTY_YINSTALL_DEST_LIB, "", 0,
         "libyetty_ffi for the language bindings"},
        {"Shaders & fonts", "data/", YETTY_YINSTALL_DEST_DATA, "", 0,
         "WGSL shaders and the bundled fonts"},
        {"Greeter assets", "greeter/", YETTY_YINSTALL_DEST_DATA, "", 0,
         "ygreeter logos, intro video and samples"},
        {"Demos", "demos/", YETTY_YINSTALL_DEST_DATA, "demos", 0,
         "demo assets, scripts and sources"},
        {"Default config", "yconfig/", YETTY_YINSTALL_DEST_CONFIG, "", 0,
         "config.yaml, defaults and temu cfgs"},
        {"RISC-V VM runtime", "yemu/", YETTY_YINSTALL_DEST_DATA, "yemu", 0,
         "kernel, firmware and root filesystem"},
        {"QEMU emulator", "qemu/", YETTY_YINSTALL_DEST_DATA, "qemu", 1, "qemu-system-riscv64"},
    };
    *count = sizeof(table) / sizeof(table[0]);
    return table;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Streaming brotli decode. The one-shot API returns ERROR mid-stream on
 * the version we link against once the output buffer fills, so grow the
 * buffer between chunks with the streaming decoder. On success the value
 * is a malloc'd buffer the caller frees; *out_size holds its length.
 */
static struct yetty_ycore_uint8_ptr_result decompress_brotli(const uint8_t *data, size_t size,
                                                             size_t *out_size)
{
    BrotliDecoderState *state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!state) {
        return YETTY_ERR(yetty_ycore_uint8_ptr, "brotli: cannot create decoder (OOM)");
    }

    size_t capacity = size * 10;
    if (capacity < 65536) {
        capacity = 65536;
    }
    uint8_t *output = malloc(capacity);
    if (!output) {
        BrotliDecoderDestroyInstance(state);
        return YETTY_ERR(yetty_ycore_uint8_ptr, "brotli: OOM allocating output buffer");
    }

    size_t input_remaining = size;
    const uint8_t *next_input = data;
    size_t used = 0;
    for (;;) {
        size_t output_available = capacity - used;
        uint8_t *next_output = output + used;
        BrotliDecoderResult result = BrotliDecoderDecompressStream(
            state, &input_remaining, &next_input, &output_available, &next_output, NULL);
        used = capacity - output_available;
        if (result == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        }
        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            capacity *= 2;
            uint8_t *grown = realloc(output, capacity);
            if (!grown) {
                free(output);
                BrotliDecoderDestroyInstance(state);
                return YETTY_ERR(yetty_ycore_uint8_ptr, "brotli: OOM growing output buffer");
            }
            output = grown;
            continue;
        }
        /* NEEDS_MORE_INPUT on a complete blob, or a hard decode error:
         * the embedded stream is corrupt. */
        yerror("brotli: decode failed: %s",
               BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state)));
        free(output);
        BrotliDecoderDestroyInstance(state);
        return YETTY_ERR(yetty_ycore_uint8_ptr, "brotli: corrupt embedded stream");
    }

    BrotliDecoderDestroyInstance(state);
    *out_size = used;
    return YETTY_OK(yetty_ycore_uint8_ptr, output);
}

/* Render a byte count as a short human string into `buf`. */
static void format_size(uint64_t bytes, char *buf, size_t buf_size)
{
    if (bytes >= (uint64_t)1024 * 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%llu B", (unsigned long long)bytes);
    }
}

static const char *resolve_root(enum yetty_yinstall_destination destination,
                                const struct yetty_yplatform_paths *paths)
{
    switch (destination) {
    case YETTY_YINSTALL_DEST_BIN:
        return paths->bin_dir_buf;
    case YETTY_YINSTALL_DEST_LIB:
        return paths->lib_dir_buf;
    case YETTY_YINSTALL_DEST_DATA:
        return paths->data_dir_buf;
    case YETTY_YINSTALL_DEST_CONFIG:
        return paths->config_dir_buf;
    }
    return "";
}

/* Compare two chars as path characters. When `ci` (Windows), fold case and
 * treat '/' and '\\' as the same separator so mixed-style entries match. */
static int path_char_eq(char a, char b, int ci)
{
    if (!ci) {
        return a == b;
    }
    if (a == '/') {
        a = '\\';
    }
    if (b == '/') {
        b = '\\';
    }
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

/* Length of `s` (capped at `len`) ignoring one trailing path separator, so
 * "C:\dir\" and "C:\dir" compare equal. */
static size_t trimmed_len(const char *s, size_t len)
{
    if (len > 0 && (s[len - 1] == '/' || s[len - 1] == '\\')) {
        return len - 1;
    }
    return len;
}

/* Non-zero if `dir` is one of the entries of $PATH. Advisory only. */
static int dir_on_path(const char *dir)
{
    const char *path = getenv("PATH");
    if (!path || !*path || !dir || !*dir) {
        return 0;
    }
    /* PATH uses ';' on Windows (entries contain "C:\..."), ':' elsewhere.
     * Detect from the string rather than a platform #ifdef. The same
     * detection tells us to compare case-insensitively and separator-
     * agnostically, which is how Windows resolves PATH. */
    int windows = strchr(path, ';') != NULL;
    char separator = windows ? ';' : ':';
    size_t dir_len = trimmed_len(dir, strlen(dir));
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, separator);
        size_t segment_len = end ? (size_t)(end - cursor) : strlen(cursor);
        size_t seg_trimmed = trimmed_len(cursor, segment_len);
        if (seg_trimmed == dir_len) {
            size_t index = 0;
            for (; index < dir_len; index++) {
                if (!path_char_eq(cursor[index], dir[index], windows)) {
                    break;
                }
            }
            if (index == dir_len) {
                return 1;
            }
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Per-component extraction                                           */
/* ------------------------------------------------------------------ */

struct component_install_ctx {
    const struct yetty_yinstall_component *component;
    char dest_dir[PATH_MAX]; /* resolved root + subdir */
    int force;
    int verbose;

    size_t files_written;
    size_t files_skipped;
    uint64_t bytes_on_disk; /* decompressed size of everything that's now present */
    uint64_t bytes_packed;  /* embedded size (compressed where applicable) */
    int any_compressed;

    int failed;
    struct yetty_ycore_void_result result;
};

/* Write `data`/`size` to `path`, creating parents. Sets the exec bit when
 * `executable`. On failure no partial file is left behind — the skip check
 * in install_asset takes existence as proof of a good install, so a
 * leftover partial file would freeze the corruption in place across runs. */
static struct yetty_ycore_void_result write_file(const char *path, const uint8_t *data, size_t size,
                                                 int executable)
{
    char parent[PATH_MAX];
    if (yetty_yplatform_path_dirname(path, parent, sizeof(parent)) != 0) {
        yerror("yinstall: cannot derive parent dir of %s", path);
        return YETTY_ERR(yetty_ycore_void, "cannot derive parent directory of install path");
    }
    struct yetty_ycore_void_result mkdir_res = yetty_yplatform_mkdir_p(parent);
    if (YETTY_IS_ERR(mkdir_res)) {
        yerror("yinstall: cannot create directory %s", parent);
        return YETTY_ERR(yetty_ycore_void, "cannot create install directory", mkdir_res);
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        yerror("yinstall: cannot create %s: %s", path, strerror(errno));
        return YETTY_ERR(yetty_ycore_void, "cannot create installed file");
    }
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        yetty_yplatform_unlink(path);
        yerror("yinstall: short write on %s", path);
        return YETTY_ERR(yetty_ycore_void, "short write installing file");
    }
    if (fclose(file) != 0) {
        /* The libc buffer flush happens here — ENOSPC/EIO surface on
         * fclose, not fwrite. The file on disk is incomplete. */
        yetty_yplatform_unlink(path);
        yerror("yinstall: flush failed on %s: %s", path, strerror(errno));
        return YETTY_ERR(yetty_ycore_void, "flush failed installing file (disk full?)");
    }

    if (executable) {
        /* No-op on Windows; gives +x to the apps and qemu binary on POSIX. */
        if (yetty_yplatform_chmod(path, 0755) != 0) {
            yerror("yinstall: cannot set exec bit on %s: %s", path, strerror(errno));
            return YETTY_ERR(yetty_ycore_void, "cannot make installed file executable");
        }
    }
    return YETTY_OK_VOID();
}

/* yetty_yplatform_install_asset_fn — invoked once per embedded blob.
 * Registered as a function pointer with a void signature dictated by the
 * install iterator, so Result-returning calls inside are absorbed into
 * ctx->result / ctx->failed and surfaced by the caller after the walk. */
YETTY_EXTERNAL_CALLBACK
static void install_asset(const char *name, const uint8_t *data, size_t size, int compressed,
                          void *userdata)
{
    struct component_install_ctx *ctx = userdata;
    if (ctx->failed) {
        return;
    }

    size_t prefix_len = strlen(ctx->component->prefix);
    if (strncmp(name, ctx->component->prefix, prefix_len) != 0) {
        return; /* belongs to a different component */
    }
    const char *relative = name + prefix_len;
    if (!*relative) {
        return; /* the bare prefix, no file */
    }

    char path[PATH_MAX];
    int path_len = snprintf(path, sizeof(path), "%s/%s", ctx->dest_dir, relative);
    if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
        yerror("yinstall: install path too long for %s", name);
        ctx->failed = 1;
        ctx->result = YETTY_ERR(yetty_ycore_void, "install path too long");
        return;
    }

    /* Skip if already present. Baked-in bytes are fixed for a build, so an
     * existing file is correct. For uncompressed entries we also confirm
     * the size; for compressed ones the decompressed size isn't known
     * without decompressing, so existence is the strongest cheap predicate.
     * `force` rewrites unconditionally. */
    if (!ctx->force) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (compressed || (uint64_t)st.st_size == size) {
                ctx->files_skipped++;
                ctx->bytes_on_disk += (uint64_t)st.st_size;
                ctx->bytes_packed += size;
                ctx->any_compressed |= compressed;
                if (ctx->verbose) {
                    printf("      = %s (present)\n", path);
                }
                return;
            }
        }
    }

    const uint8_t *write_data = data;
    size_t write_size = size;
    uint8_t *decompressed = NULL;
    if (compressed) {
        struct yetty_ycore_uint8_ptr_result decomp_res = decompress_brotli(data, size, &write_size);
        if (YETTY_IS_ERR(decomp_res)) {
            yerror("yinstall: brotli decode failed for %s", name);
            ctx->failed = 1;
            ctx->result =
                YETTY_ERR(yetty_ycore_void, "failed to decompress embedded asset", decomp_res);
            return;
        }
        decompressed = decomp_res.value;
        write_data = decompressed;
    }

    struct yetty_ycore_void_result write_result =
        write_file(path, write_data, write_size, ctx->component->executable);
    free(decompressed);
    if (YETTY_IS_ERR(write_result)) {
        ctx->failed = 1;
        ctx->result = write_result;
        return;
    }

    ctx->files_written++;
    ctx->bytes_on_disk += write_size;
    ctx->bytes_packed += size;
    ctx->any_compressed |= compressed;
    if (ctx->verbose) {
        printf("      + %s\n", path);
    }
}

/* Install one component and print its log line. A component this installer
 * variant does not carry (no embedded asset under its prefix) is skipped
 * silently and does not count towards `components_present`. */
static struct yetty_ycore_void_result install_component(
    const struct yetty_yinstall_component *comp, const struct yetty_yinstall_options *options,
    const struct yetty_yplatform_paths *paths, uint64_t *total_bytes, size_t *total_files,
    size_t *components_present)
{
    struct component_install_ctx ctx = {0};
    ctx.component = comp;
    ctx.force = options->force;
    ctx.verbose = options->verbose;
    ctx.result = YETTY_OK_VOID();

    const char *root = resolve_root(comp->destination, paths);
    if (!root || !*root) {
        return YETTY_ERR(yetty_ycore_void, "could not resolve a destination directory");
    }
    int dest_len;
    if (comp->subdir && *comp->subdir) {
        dest_len = snprintf(ctx.dest_dir, sizeof(ctx.dest_dir), "%s/%s", root, comp->subdir);
    } else {
        dest_len = snprintf(ctx.dest_dir, sizeof(ctx.dest_dir), "%s", root);
    }
    if (dest_len < 0 || (size_t)dest_len >= sizeof(ctx.dest_dir)) {
        return YETTY_ERR(yetty_ycore_void, "component destination path too long");
    }
    /* The destination is created lazily by write_file (per parent dir), so a
     * component this variant does not carry leaves no empty directory. */

    struct yetty_ycore_void_result walk =
        yetty_yplatform_install_foreach_asset(install_asset, &ctx);
    if (YETTY_IS_ERR(walk)) {
        if (ctx.failed) {
            /* Both the walk and a per-asset write failed. The asset error
             * is the specific one; keep it and drop the walk's chain so
             * neither is leaked. */
            yetty_ycore_error_destroy(walk.error);
            return ctx.result;
        }
        return YETTY_ERR(yetty_ycore_void, "asset enumeration failed", walk);
    }
    if (ctx.failed) {
        return ctx.result;
    }

    size_t total_present = ctx.files_written + ctx.files_skipped;
    if (total_present == 0) {
        /* Not carried by this installer variant (yinstall-min ships no
         * tools beyond yetty, no demos, no VM). Nothing landed, so nothing
         * to log — except on --verbose, where the omission is worth a line. */
        if (options->verbose) {
            printf("  %-20s     not included in this installer\n", comp->name);
        }
        return YETTY_OK_VOID();
    }
    *components_present += 1;

    /* Build the detail column. */
    char detail[160];
    if (ctx.files_written == 0) {
        snprintf(detail, sizeof(detail), "up to date");
    } else {
        char size_str[32];
        format_size(ctx.bytes_on_disk, size_str, sizeof(size_str));
        if (ctx.any_compressed && ctx.bytes_packed < ctx.bytes_on_disk) {
            char packed_str[32];
            format_size(ctx.bytes_packed, packed_str, sizeof(packed_str));
            snprintf(detail, sizeof(detail), "%s  (%zu files, unpacked from %s)", size_str,
                     total_present, packed_str);
        } else {
            snprintf(detail, sizeof(detail), "%s  (%zu files)", size_str, total_present);
        }
    }

    /* Show the directory with a trailing slash for dir components, the
     * single path for the one-file qemu/yemu binaries reads fine too. */
    char shown[PATH_MAX];
    snprintf(shown, sizeof(shown), "%s/", ctx.dest_dir);
    printf("  %-20s ->  %-44s %s\n", comp->name, shown, detail);

    *total_bytes += ctx.bytes_on_disk;
    *total_files += ctx.files_written;
    return YETTY_OK_VOID();
}

/* ------------------------------------------------------------------ */
/* Version marker                                                     */
/* ------------------------------------------------------------------ */

static struct yetty_ycore_void_result marker_path(char *out, size_t out_size,
                                                  const struct yetty_yplatform_paths *paths)
{
    int written = snprintf(out, out_size, "%s/.yinstall/version", paths->data_dir_buf);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "install-marker path too long");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result write_marker(const char *version,
                                                   const struct yetty_yplatform_paths *paths)
{
    char path[PATH_MAX];
    char dir[PATH_MAX];
    struct yetty_ycore_void_result path_res = marker_path(path, sizeof(path), paths);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "cannot build install-marker path");
    if (yetty_yplatform_path_dirname(path, dir, sizeof(dir)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "cannot derive install-marker directory");
    }
    struct yetty_ycore_void_result mkdir_res = yetty_yplatform_mkdir_p(dir);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mkdir_res, "cannot create install-marker directory");

    FILE *file = fopen(path, "w");
    if (!file) {
        yerror("yinstall: cannot create marker %s: %s", path, strerror(errno));
        return YETTY_ERR(yetty_ycore_void, "cannot create install marker");
    }
    /* A partial marker could pass the next run's version compare, so any
     * failed write removes the file — absence forces a clean refresh. */
    if (fprintf(file, "%s", version) < 0) {
        fclose(file);
        yetty_yplatform_unlink(path);
        return YETTY_ERR(yetty_ycore_void, "cannot write install marker");
    }
    if (fclose(file) != 0) {
        yetty_yplatform_unlink(path);
        return YETTY_ERR(yetty_ycore_void, "flush failed writing install marker");
    }
    return YETTY_OK_VOID();
}

/* Read the installed-version marker into `out`. Value 1 when a marker was
 * read, 0 when none exists (first install). A marker that exists but cannot
 * be opened is an error — mapping it to "first install" would silently flip
 * the refresh policy. */
static struct yetty_ycore_int_result read_marker(char *out, size_t out_size,
                                                 const struct yetty_yplatform_paths *paths)
{
    char path[PATH_MAX];
    struct yetty_ycore_void_result path_res = marker_path(path, sizeof(path), paths);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, path_res, "cannot build install-marker path");
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        yerror("yinstall: cannot open marker %s: %s", path, strerror(errno));
        return YETTY_ERR(yetty_ycore_int, "cannot open existing install marker");
    }
    int have_line = fgets(out, (int)out_size, file) != NULL;
    fclose(file);
    if (have_line) {
        out[strcspn(out, "\n")] = 0;
    }
    return YETTY_OK(yetty_ycore_int, have_line);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

/* Error-path teardown: the install error being propagated outranks a
 * paths-release failure, so absorb the latter. The success path checks
 * the destroy result properly. */
static void release_paths_absorb(struct yetty_yplatform_paths *paths)
{
    struct yetty_ycore_void_result destroy_res = yetty_yplatform_paths_destroy(paths);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

struct yetty_ycore_void_result yetty_yinstall_run(const struct yetty_yinstall_options *options)
{
    struct yetty_yinstall_options defaults = {0};
    if (!options) {
        options = &defaults;
    }
    const char *version = options->version && *options->version ? options->version : "dev";
    const char *name = options->name && *options->name ? options->name : "yinstall";

    printf("yetty installer (%s) \xc2\xb7 version %s\n\n", name, version);
    printf("Installing to this machine:\n\n");

    /* Resolve the platform dirs once for this run (standalone tool, no config
     * in scope) and thread the struct through; release it before returning. */
    struct yetty_yplatform_paths_ptr_result paths_res = yetty_yplatform_paths_get_platform_paths();
    if (YETTY_IS_ERR(paths_res)) {
        return YETTY_ERR(yetty_ycore_void, "yinstall: resolve platform paths failed", paths_res);
    }
    struct yetty_yplatform_paths *paths = paths_res.value;

    /* Refresh policy. The per-file skip in the writer only checks
     * EXISTENCE for compressed entries, so changed content under an
     * existing name would never land. The version marker is the rewrite
     * gate: a marker mismatch (upgrade) forces a full rewrite, and a
     * "-dirty" development build always does — its version string does
     * not move between rebuilds, so existence is no evidence of
     * freshness. A matching stable version keeps the cheap
     * fill-missing-files pass. */
    struct yetty_yinstall_options effective_options = *options;
    if (!effective_options.force) {
        char installed_version[64] = {0};
        struct yetty_ycore_int_result marker_res =
            read_marker(installed_version, sizeof(installed_version), paths);
        if (YETTY_IS_ERR(marker_res)) {
            release_paths_absorb(paths);
            return YETTY_ERR(yetty_ycore_void, "cannot read install marker", marker_res);
        }
        int have_marker = marker_res.value;
        size_t version_len = strlen(version);
        int dirty_build = version_len >= 6 && strcmp(version + version_len - 6, "-dirty") == 0;
        if (!have_marker || dirty_build || strcmp(installed_version, version) != 0) {
            effective_options.force = 1;
            printf("Refreshing every file (%s).\n\n", !have_marker  ? "no install marker"
                                                      : dirty_build ? "development build"
                                                                    : "installed version differs");
        }
    }

    size_t component_count = 0;
    const struct yetty_yinstall_component *components = yinstall_components(&component_count);

    uint64_t total_bytes = 0;
    size_t total_files = 0;
    size_t components_present = 0;
    for (size_t index = 0; index < component_count; index++) {
        struct yetty_ycore_void_result result =
            install_component(&components[index], &effective_options, paths, &total_bytes,
                              &total_files, &components_present);
        if (YETTY_IS_ERR(result)) {
            printf("\n  %s: install failed.\n", components[index].name);
            release_paths_absorb(paths);
            return YETTY_ERR(yetty_ycore_void, "component install failed", result);
        }
    }

    struct yetty_ycore_void_result marker_write = write_marker(version, paths);
    if (YETTY_IS_ERR(marker_write)) {
        release_paths_absorb(paths);
        return YETTY_ERR(yetty_ycore_void, "cannot record installed version", marker_write);
    }

    char total_str[32];
    format_size(total_bytes, total_str, sizeof(total_str));
    printf("\n");
    if (total_files == 0) {
        printf("Nothing to do \xc2\xb7 yetty is already installed.\n");
    } else {
        printf("Installed %zu components \xc2\xb7 %s written.\n", components_present, total_str);
    }

    const char *bin_dir = paths->bin_dir_buf;
    printf("yetty is ready -- run:  %s/yetty\n", bin_dir);
    if (!dir_on_path(bin_dir)) {
        /* bin_dir isn't on the *current process* PATH. Try to persist it on
         * the user's PATH. On Windows this writes HKCU\Environment and
         * broadcasts the change; on POSIX it is a no-op (UNSUPPORTED) and we
         * fall through to the advisory note, exactly as before. */
        enum yetty_yplatform_path_outcome outcome = YETTY_YPLATFORM_PATH_UNSUPPORTED;
        struct yetty_ycore_void_result path_res =
            yetty_yplatform_install_add_to_user_path(bin_dir, &outcome);
        if (YETTY_IS_ERR(path_res)) {
            yetty_ycore_error_destroy(path_res.error);
            printf("\nNote: %s is not on your PATH. Add it to run `yetty` directly.\n", bin_dir);
        } else if (outcome == YETTY_YPLATFORM_PATH_ADDED) {
            printf("\nAdded %s to your PATH. Open a new terminal for it to take effect.\n",
                   bin_dir);
        } else if (outcome == YETTY_YPLATFORM_PATH_ALREADY_PRESENT) {
            /* Already persisted (e.g. a re-install) but not yet visible in this
             * shell — tell the user to open a new terminal, not to add it. */
            printf("\n%s is already on your PATH. Open a new terminal for it to take effect.\n",
                   bin_dir);
        } else {
            /* UNSUPPORTED (POSIX): unchanged advisory note. */
            printf("\nNote: %s is not on your PATH. Add it to run `yetty` directly.\n", bin_dir);
        }
    }

    struct yetty_ycore_void_result destroy_res = yetty_yplatform_paths_destroy(paths);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_res, "cannot release platform paths");
    return YETTY_OK_VOID();
}
