/*
 * yinstall/install.c — the platform-independent installer core.
 *
 * Walks a declarative table of components (see docs/yinstall.md), unpacks
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
        {"Shaders & fonts", "data/", YETTY_YINSTALL_DEST_DATA, "", 0,
         "WGSL shaders and the bundled fonts"},
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
 * buffer between chunks with the streaming decoder. Returns a malloc'd
 * buffer the caller frees, or NULL on failure.
 */
static uint8_t *decompress_brotli(const uint8_t *data, size_t size, size_t *out_size)
{
    BrotliDecoderState *state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!state) {
        return NULL;
    }

    size_t capacity = size * 10;
    if (capacity < 65536) {
        capacity = 65536;
    }
    uint8_t *output = malloc(capacity);
    if (!output) {
        BrotliDecoderDestroyInstance(state);
        return NULL;
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
                return NULL;
            }
            output = grown;
            continue;
        }
        free(output);
        BrotliDecoderDestroyInstance(state);
        return NULL;
    }

    BrotliDecoderDestroyInstance(state);
    *out_size = used;
    return output;
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

static const char *resolve_root(enum yetty_yinstall_destination destination)
{
    switch (destination) {
    case YETTY_YINSTALL_DEST_BIN:
        return yetty_yplatform_get_bin_dir();
    case YETTY_YINSTALL_DEST_DATA:
        return yetty_yplatform_get_data_dir();
    case YETTY_YINSTALL_DEST_CONFIG:
        return yetty_yplatform_get_config_dir();
    }
    return "";
}

/* Non-zero if `dir` is one of the entries of $PATH. Advisory only. */
static int dir_on_path(const char *dir)
{
    const char *path = getenv("PATH");
    if (!path || !*path || !dir || !*dir) {
        return 0;
    }
    /* PATH uses ';' on Windows (entries contain "C:\..."), ':' elsewhere.
     * Detect from the string rather than a platform #ifdef. */
    char separator = strchr(path, ';') ? ';' : ':';
    size_t dir_len = strlen(dir);
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, separator);
        size_t segment_len = end ? (size_t)(end - cursor) : strlen(cursor);
        if (segment_len == dir_len && strncmp(cursor, dir, dir_len) == 0) {
            return 1;
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
 * `executable`. Returns a Result. */
static struct yetty_ycore_void_result write_file(const char *path, const uint8_t *data, size_t size,
                                                 int executable)
{
    char parent[PATH_MAX];
    if (yetty_yplatform_path_dirname(path, parent, sizeof(parent)) == 0) {
        yetty_yplatform_mkdir_p(parent);
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        yerror("yinstall: cannot create %s: %s", path, strerror(errno));
        return YETTY_ERR(yetty_ycore_void, "cannot create installed file");
    }
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        yerror("yinstall: short write on %s", path);
        return YETTY_ERR(yetty_ycore_void, "short write installing file");
    }
    fclose(file);

    if (executable) {
        /* No-op on Windows; gives +x to the apps and qemu binary on POSIX. */
        yetty_yplatform_chmod(path, 0755);
    }
    return YETTY_OK_VOID();
}

/* yetty_yplatform_install_asset_fn — invoked once per embedded blob. */
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
    snprintf(path, sizeof(path), "%s/%s", ctx->dest_dir, relative);

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
        decompressed = decompress_brotli(data, size, &write_size);
        if (!decompressed) {
            yerror("yinstall: brotli decode failed for %s", name);
            ctx->failed = 1;
            ctx->result = YETTY_ERR(yetty_ycore_void, "failed to decompress embedded asset");
            return;
        }
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

/* Install one component and print its log line. */
static struct yetty_ycore_void_result install_component(const struct yetty_yinstall_component *comp,
                                                        const struct yetty_yinstall_options *options,
                                                        uint64_t *total_bytes, size_t *total_files)
{
    struct component_install_ctx ctx = {0};
    ctx.component = comp;
    ctx.force = options->force;
    ctx.verbose = options->verbose;
    ctx.result = YETTY_OK_VOID();

    const char *root = resolve_root(comp->destination);
    if (!root || !*root) {
        return YETTY_ERR(yetty_ycore_void, "could not resolve a destination directory");
    }
    if (comp->subdir && *comp->subdir) {
        snprintf(ctx.dest_dir, sizeof(ctx.dest_dir), "%s/%s", root, comp->subdir);
    } else {
        snprintf(ctx.dest_dir, sizeof(ctx.dest_dir), "%s", root);
    }
    yetty_yplatform_mkdir_p(ctx.dest_dir);

    struct yetty_ycore_void_result walk =
        yetty_yplatform_install_foreach_asset(install_asset, &ctx);
    if (YETTY_IS_ERR(walk)) {
        return YETTY_ERR(yetty_ycore_void, "asset enumeration failed", walk);
    }
    if (ctx.failed) {
        return ctx.result;
    }

    /* Build the detail column. */
    char detail[160];
    size_t total_present = ctx.files_written + ctx.files_skipped;
    if (ctx.files_written == 0 && total_present > 0) {
        snprintf(detail, sizeof(detail), "up to date");
    } else if (total_present == 0) {
        snprintf(detail, sizeof(detail), "nothing embedded");
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

static void marker_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/.yinstall/version", yetty_yplatform_get_data_dir());
}

static void write_marker(const char *version)
{
    char path[PATH_MAX];
    char dir[PATH_MAX];
    marker_path(path, sizeof(path));
    if (yetty_yplatform_path_dirname(path, dir, sizeof(dir)) == 0) {
        yetty_yplatform_mkdir_p(dir);
    }
    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s", version);
        fclose(file);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

struct yetty_ycore_void_result yetty_yinstall_run(const struct yetty_yinstall_options *options)
{
    struct yetty_yinstall_options defaults = {0};
    if (!options) {
        options = &defaults;
    }
    const char *version = options->version && *options->version ? options->version : "dev";

    printf("yetty installer \xc2\xb7 version %s\n\n", version);
    printf("Installing to this machine:\n\n");

    size_t component_count = 0;
    const struct yetty_yinstall_component *components = yinstall_components(&component_count);

    uint64_t total_bytes = 0;
    size_t total_files = 0;
    for (size_t index = 0; index < component_count; index++) {
        struct yetty_ycore_void_result result =
            install_component(&components[index], options, &total_bytes, &total_files);
        if (YETTY_IS_ERR(result)) {
            printf("\n  %s: install failed.\n", components[index].name);
            return YETTY_ERR(yetty_ycore_void, "component install failed", result);
        }
    }

    write_marker(version);

    char total_str[32];
    format_size(total_bytes, total_str, sizeof(total_str));
    printf("\n");
    if (total_files == 0) {
        printf("Nothing to do \xc2\xb7 yetty is already installed.\n");
    } else {
        printf("Installed %zu components \xc2\xb7 %s written.\n", component_count, total_str);
    }

    const char *bin_dir = yetty_yplatform_get_bin_dir();
    printf("yetty is ready -- run:  %s/yetty\n", bin_dir);
    if (!dir_on_path(bin_dir)) {
        printf("\nNote: %s is not on your PATH. Add it to run `yetty` directly.\n", bin_dir);
    }

    return YETTY_OK_VOID();
}
