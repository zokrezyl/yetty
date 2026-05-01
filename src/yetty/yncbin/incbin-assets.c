/* incbin-assets.c - Embedded asset management */

#include <brotli/decode.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yplatform/fs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* MSVC <sys/stat.h> doesn't define POSIX S_ISDIR / S_ISREG. */
#if defined(_MSC_VER) && !defined(S_ISDIR)
# define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#if defined(_MSC_VER) && !defined(S_ISREG)
# define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

#ifndef YETTY_BUILD_VERSION
#define YETTY_BUILD_VERSION "dev"
#endif

#define MAX_ASSETS 1024
#define MAX_PATH_LEN 512

/* Asset entry */
struct asset_entry {
    const char *name;
    const uint8_t *data;
    size_t size;
    int compressed;
};

/* Assets container */
struct yetty_incbin_assets {
    struct asset_entry entries[MAX_ASSETS];
    size_t count;
};

/* Forward declarations for manifest registration functions */
#ifdef HAS_DATA_MANIFEST
#include "yetty_data_manifest.h"
#endif

#ifdef HAS_YCONFIG_MANIFEST
#include "yetty_yconfig_manifest.h"
#endif

#ifdef HAS_YEMU_MANIFEST
#include "yetty_yemu_manifest.h"
#endif

#ifdef HAS_QEMU_MANIFEST
#include "yetty_qemu_manifest.h"
#endif

/* Helper to add asset entry */
static void add_asset(struct yetty_incbin_assets *assets, const char *name, const uint8_t *data,
                      size_t size, int compressed)
{
    if (assets->count >= MAX_ASSETS) {
        return;
    }

    assets->entries[assets->count].name = name;
    assets->entries[assets->count].data = data;
    assets->entries[assets->count].size = size;
    assets->entries[assets->count].compressed = compressed;
    assets->count++;
}

/* C callback for manifest registration */
static struct yetty_incbin_assets *g_current_assets = NULL;

static void register_asset_callback(const char *name, const uint8_t *data, size_t size,
                                    int compressed)
{
    if (g_current_assets) {
        add_asset(g_current_assets, name, data, size, compressed);
    }
}

/* Create assets container */
struct yetty_incbin_assets *yetty_incbin_assets_create(void)
{
    struct yetty_incbin_assets *assets;

    assets = calloc(1, sizeof(struct yetty_incbin_assets));
    if (!assets) {
        return NULL;
    }

    /* Register assets from manifests */
    g_current_assets = assets;

#ifdef HAS_DATA_MANIFEST
    register_data_assets_c(register_asset_callback);
    ydebug("Registered data assets from manifest");
#endif

#ifdef HAS_YCONFIG_MANIFEST
    register_yconfig_assets_c(register_asset_callback);
    ydebug("Registered config assets from manifest");
#endif

#ifdef HAS_YEMU_MANIFEST
    register_yemu_assets_c(register_asset_callback);
    ydebug("Registered yemu (shared RISC-V runtime) assets from manifest");
#endif

#ifdef HAS_QEMU_MANIFEST
    register_qemu_assets_c(register_asset_callback);
    ydebug("Registered qemu assets from manifest");
#endif

    g_current_assets = NULL;

    ydebug("IncbinAssets: %zu embedded assets available", assets->count);
    return assets;
}

/* Destroy assets container */
void yetty_incbin_assets_destroy(struct yetty_incbin_assets *assets)
{
    free(assets);
}

/* Get marker path */
static void get_marker_path(const char *cache_dir, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/.yetty-assets/version", cache_dir);
}

/* Create directory recursively. We can't tell from errno alone whether an
 * intermediate step "succeeded" — on iOS sandboxes mkdir on a pre-existing
 * system dir like /var returns EACCES/EROFS (not EEXIST). So for every
 * step we check whether the path already exists as a dir and treat that
 * as success. Only the final leaf must be a real success. */
static int mkdir_one(const char *path)
{
    int rc = yetty_yplatform_mkdir(path);
    int saved_errno = errno;
    if (rc == 0) {
        ydebug("mkdir_one: created '%s'", path);
        return 0;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ydebug("mkdir_one: '%s' already a dir (mkdir errno=%d)", path, saved_errno);
            return 0;
        }
        ydebug("mkdir_one: '%s' exists but is not a dir (mode=0%o)", path, st.st_mode);
    } else {
        ydebug("mkdir_one: mkdir failed errno=%d (%s); stat failed errno=%d (%s); path='%s'",
               saved_errno, strerror(saved_errno), errno, strerror(errno), path);
    }
    return -1;
}

static int mkdir_p(const char *path)
{
    char tmp[MAX_PATH_LEN];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            (void)mkdir_one(tmp); /* ancestors: best-effort */
            *p = '/';
        }
    }

    return mkdir_one(tmp); /* leaf: must end up as a dir */
}

/* Check if extraction is needed */
int yetty_incbin_assets_needs_extraction(struct yetty_incbin_assets *assets, const char *cache_dir)
{
    char marker_path[MAX_PATH_LEN];
    char version[64];
    FILE *f;

    (void)assets;

    get_marker_path(cache_dir, marker_path, sizeof(marker_path));
    ydebug("needsExtraction: marker path = %s", marker_path);

    f = fopen(marker_path, "r");
    if (!f) {
        ydebug("needsExtraction: returning 1 (marker not found)");
        return 1;
    }

    if (!fgets(version, sizeof(version), f)) {
        fclose(f);
        return 1;
    }
    fclose(f);

    /* Strip newline */
    version[strcspn(version, "\n")] = 0;

    ydebug("needsExtraction: marker version = '%s', build version = '%s'", version,
           YETTY_BUILD_VERSION);

    if (strcmp(version, YETTY_BUILD_VERSION) != 0) {
        ydebug("needsExtraction: returning 1 (version mismatch)");
        return 1;
    }

    ydebug("needsExtraction: returning 0");
    return 0;
}

/* Decompress brotli data */
static uint8_t *decompress_brotli(const uint8_t *data, size_t size, size_t *out_size)
{
    size_t decoded_size;
    uint8_t *output;
    size_t capacity;
    BrotliDecoderResult result;

    /* The one-shot BrotliDecoderDecompress returns ERROR (not
   * NEEDS_MORE_OUTPUT) once it has flushed enough output to fill the buffer
   * partway through a stream that needs more — at least with the brotli
   * version we link against. Use the streaming API instead so we can grow
   * the buffer between chunks. */
    BrotliDecoderState *st = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!st) {
        ydebug("decompress_brotli: BrotliDecoderCreateInstance failed");
        return NULL;
    }

    capacity = size * 10;
    if (capacity < 65536) {
        capacity = 65536;
    }
    output = malloc(capacity);
    if (!output) {
        ydebug("decompress_brotli: malloc(%zu) failed for input %zu", capacity, size);
        BrotliDecoderDestroyInstance(st);
        return NULL;
    }

    size_t in_remaining = size;
    const uint8_t *next_in = data;
    size_t out_used = 0;

    for (;;) {
        size_t out_avail = capacity - out_used;
        uint8_t *next_out = output + out_used;
        result =
            BrotliDecoderDecompressStream(st, &in_remaining, &next_in, &out_avail, &next_out, NULL);
        out_used = capacity - out_avail;
        if (result == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        }
        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            capacity *= 2;
            uint8_t *new_output = realloc(output, capacity);
            if (!new_output) {
                ydebug("decompress_brotli: realloc(%zu) failed", capacity);
                free(output);
                BrotliDecoderDestroyInstance(st);
                return NULL;
            }
            output = new_output;
            continue;
        }
        /* ERROR or NEEDS_MORE_INPUT (one-shot input — shouldn't happen) */
        ydebug("decompress_brotli: stream result=%d in=%zu out_used=%zu", (int)result, size,
               out_used);
        free(output);
        BrotliDecoderDestroyInstance(st);
        return NULL;
    }

    BrotliDecoderDestroyInstance(st);
    decoded_size = out_used;

    *out_size = decoded_size;
    return output;
}

/* Extract assets with given prefix to directory, stripping the prefix */
static int extract_with_prefix(struct yetty_incbin_assets *assets, const char *prefix,
                               const char *dest_dir)
{
    char path[MAX_PATH_LEN];
    size_t i;
    size_t prefix_len = strlen(prefix);
    FILE *f;

    ydebug("extract_with_prefix: prefix='%s' dest='%s'", prefix, dest_dir);

    for (i = 0; i < assets->count; i++) {
        const struct asset_entry *entry = &assets->entries[i];
        const uint8_t *write_data;
        size_t write_size;
        uint8_t *decompressed = NULL;
        char *last_slash;
        const char *rel_name;

        /* Skip assets that don't match prefix */
        if (strncmp(entry->name, prefix, prefix_len) != 0) {
            continue;
        }

        /* Strip prefix from name */
        rel_name = entry->name + prefix_len;

        snprintf(path, sizeof(path), "%s/%s", dest_dir, rel_name);
        ydebug("extractTo: extracting '%s' -> '%s'", entry->name, path);

        /* Create parent directories */
        last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = 0;
            if (mkdir_p(path) != 0) {
                ydebug("Failed to create directory: %s", path);
                return 0;
            }
            *last_slash = '/';
        }

        /* Decompress if needed */
        if (entry->compressed) {
            decompressed = decompress_brotli(entry->data, entry->size, &write_size);
            if (!decompressed) {
                ydebug("Failed to decompress: %s", entry->name);
                return 0;
            }
            write_data = decompressed;
            ydebug("Decompressed: %s (%zu -> %zu bytes)", entry->name, entry->size, write_size);
        } else {
            write_data = entry->data;
            write_size = entry->size;
        }

        /* Write file */
        f = fopen(path, "wb");
        if (!f) {
            free(decompressed);
            ydebug("Failed to open file: %s", path);
            return 0;
        }

        if (fwrite(write_data, 1, write_size, f) != write_size) {
            fclose(f);
            free(decompressed);
            ydebug("Failed to write file: %s", path);
            return 0;
        }

        fclose(f);
        free(decompressed);

        if (!entry->compressed) {
            ydebug("Extracted: %s (%zu bytes)", entry->name, write_size);
        }
    }

    return 1;
}

/* Write version marker to directory */
static void write_marker(const char *dir)
{
    char marker_path[MAX_PATH_LEN];
    char marker_dir[MAX_PATH_LEN];
    FILE *f;

    get_marker_path(dir, marker_path, sizeof(marker_path));
    strncpy(marker_dir, marker_path, sizeof(marker_dir) - 1);
    marker_dir[sizeof(marker_dir) - 1] = 0;
    char *slash = strrchr(marker_dir, '/');
    if (slash) {
        *slash = 0;
        mkdir_p(marker_dir);
    }

    f = fopen(marker_path, "w");
    if (f) {
        fprintf(f, "%s", YETTY_BUILD_VERSION);
        fclose(f);
    }
}

/* Extract data assets (shaders, fonts, etc.) to data directory */
int yetty_incbin_assets_extract_data_to(struct yetty_incbin_assets *assets, const char *data_dir)
{
    ydebug("extract_data_to: starting extraction to %s", data_dir);

    if (!extract_with_prefix(assets, "data/", data_dir)) {
        return 0;
    }

    write_marker(data_dir);
    ydebug("Data asset extraction complete");
    return 1;
}

/* Extract config assets to config directory.
 * Assets are registered by the build with prefix "yconfig/" (see
 * incbin_add_directory in build-tools/cmake/targets/shared.cmake); the
 * prefix must match here or extraction silently matches zero entries and
 * still writes the version marker, wedging all future runs. */
int yetty_incbin_assets_extract_config_to(struct yetty_incbin_assets *assets,
                                          const char *config_dir)
{
    ydebug("extract_config_to: starting extraction to %s", config_dir);

    if (!extract_with_prefix(assets, "yconfig/", config_dir)) {
        return 0;
    }

    write_marker(config_dir);
    ydebug("Config asset extraction complete");
    return 1;
}

/* Extract shared RISC-V runtime (kernel, opensbi, rootfs.img) to <data_dir>/yemu */
int yetty_incbin_assets_extract_yemu_to(struct yetty_incbin_assets *assets, const char *data_dir)
{
    char yemu_dir[MAX_PATH_LEN];

    snprintf(yemu_dir, sizeof(yemu_dir), "%s/yemu", data_dir);
    ydebug("extract_yemu_to: starting extraction to %s", yemu_dir);

    if (mkdir_p(yemu_dir) != 0) {
        ydebug("Failed to create yemu directory: %s", yemu_dir);
        return 0;
    }

    if (!extract_with_prefix(assets, "yemu/", yemu_dir)) {
        return 0;
    }

    ydebug("yemu asset extraction complete");
    return 1;
}

/* Check if yemu (shared RISC-V runtime) assets are available */
int yetty_incbin_assets_has_yemu(struct yetty_incbin_assets *assets)
{
    size_t i;

    for (i = 0; i < assets->count; i++) {
        if (strncmp(assets->entries[i].name, "yemu/", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Extract QEMU binary to data directory */
int yetty_incbin_assets_extract_qemu_to(struct yetty_incbin_assets *assets, const char *data_dir)
{
    char qemu_dir[MAX_PATH_LEN];
    char qemu_bin[MAX_PATH_LEN];

    snprintf(qemu_dir, sizeof(qemu_dir), "%s/qemu", data_dir);
    ydebug("extract_qemu_to: starting extraction to %s", qemu_dir);

    if (mkdir_p(qemu_dir) != 0) {
        ydebug("Failed to create qemu directory: %s", qemu_dir);
        return 0;
    }

    if (!extract_with_prefix(assets, "qemu/", qemu_dir)) {
        return 0;
    }

    /* Make QEMU binary executable */
    snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu-system-riscv64", qemu_dir);
    if (yetty_yplatform_chmod(qemu_bin, 0755) != 0) {
        ydebug("Failed to make QEMU executable: %s", strerror(errno));
    }

    ydebug("QEMU asset extraction complete");
    return 1;
}

/* Check if QEMU assets are available */
int yetty_incbin_assets_has_qemu(struct yetty_incbin_assets *assets)
{
    size_t i;

    for (i = 0; i < assets->count; i++) {
        if (strncmp(assets->entries[i].name, "qemu/", 5) == 0) {
            return 1;
        }
    }
    return 0;
}
