/* extract-assets.c - Extract embedded assets to data and config directories */

#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration - implemented by incbin-assets.c */
struct yetty_incbin_assets;
struct yetty_incbin_assets *yetty_incbin_assets_create(void);
void yetty_incbin_assets_destroy(struct yetty_incbin_assets *assets);
int yetty_incbin_assets_needs_extraction(struct yetty_incbin_assets *assets, const char *dir,
                                         const char *kind);
int yetty_incbin_assets_extract_data_to(struct yetty_incbin_assets *assets, const char *data_dir);
int yetty_incbin_assets_extract_config_to(struct yetty_incbin_assets *assets,
                                          const char *config_dir);
int yetty_incbin_assets_extract_yemu_to(struct yetty_incbin_assets *assets, const char *data_dir);
int yetty_incbin_assets_has_yemu(struct yetty_incbin_assets *assets);
int yetty_incbin_assets_extract_qemu_to(struct yetty_incbin_assets *assets, const char *data_dir);
int yetty_incbin_assets_has_qemu(struct yetty_incbin_assets *assets);

struct yetty_ycore_void_result yetty_platform_extract_assets(void)
{
    const char *data_dir;
    const char *config_dir;
    struct yetty_incbin_assets *assets;
    int needs_extract;

    data_dir = yetty_yplatform_get_data_dir();
    config_dir = yetty_yplatform_get_config_dir();

    if (!data_dir || !data_dir[0]) {
        return YETTY_OK_VOID();
    }

    assets = yetty_incbin_assets_create();
    if (!assets) {
        return YETTY_OK_VOID(); /* No embedded assets - development build */
    }

    /* Check if data extraction needed */
    needs_extract = yetty_incbin_assets_needs_extraction(assets, data_dir, "data");
    if (needs_extract) {
        if (!yetty_incbin_assets_extract_data_to(assets, data_dir)) {
            yetty_incbin_assets_destroy(assets);
            return YETTY_ERR(yetty_ycore_void, "failed to extract data assets");
        }
    }

    /* Check if config extraction needed. Separate marker kind from data
     * extraction's so that on platforms where data_dir == config_dir
     * (macOS: both resolve to ~/Library/Application Support/yetty) the
     * data-marker we just wrote doesn't mask the config check and cause
     * the config payload (e.g. temu cfg files) to never land. */
    if (config_dir && config_dir[0]) {
        needs_extract = yetty_incbin_assets_needs_extraction(assets, config_dir, "config");
        if (needs_extract) {
            if (!yetty_incbin_assets_extract_config_to(assets, config_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract config assets");
            }
        }
    }

    /* Extract shared RISC-V runtime (kernel/opensbi/rootfs) to <data_dir>/yemu.
     *
     * Gate on the per-kind version marker so a yetty upgrade with refreshed
     * yemu payloads (kernel bump, new rootfs) replaces what's on disk. The
     * yemu entries are brotli-compressed, so extract_with_prefix's inner
     * "skip if file exists" check can't tell new content from old — without
     * the version gate, a stale image would silently survive forever.
     *
     * On version mismatch we force re-extract by removing the existing
     * payload files first (extract_with_prefix's compressed-path skip-if-
     * exists check fires per-file regardless of the marker). */
    if (yetty_incbin_assets_has_yemu(assets)) {
        if (yetty_incbin_assets_needs_extraction(assets, data_dir, "yemu")) {
            const char *yemu_files[] = {
                "yemu/kernel-riscv64.bin",
                "yemu/opensbi-fw_jump.elf",
                "yemu/opensbi-fw_dynamic.bin",
                "yemu/alpine-rootfs.img",
                "yemu/yetty-rootfs-riscv.img",
            };
            for (size_t i = 0; i < sizeof(yemu_files) / sizeof(yemu_files[0]); i++) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", data_dir, yemu_files[i]);
                if (yetty_yplatform_file_exists(path)) {
                    (void)yetty_yplatform_unlink(path);
                }
            }
            if (!yetty_incbin_assets_extract_yemu_to(assets, data_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract yemu assets");
            }
        }
    }

    /* Extract QEMU binary to <data_dir>/qemu.
     *
     * Same version gate as yemu: a rebuilt yetty may ship a newer qemu
     * binary and the stale on-disk copy must yield. The qemu entry itself
     * is uncompressed, so extract_with_prefix would notice the size
     * mismatch — but the prior outer file-exists gate short-circuited
     * before that, leaving older binaries (e.g. an Apr build missing
     * virtio-blk-device) parked indefinitely. */
    if (yetty_incbin_assets_has_qemu(assets)) {
        if (yetty_incbin_assets_needs_extraction(assets, data_dir, "qemu")) {
            char qemu_bin[512];
#ifdef _WIN32
            snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64.exe", data_dir);
#else
            snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
#endif
            if (yetty_yplatform_file_exists(qemu_bin)) {
                (void)yetty_yplatform_unlink(qemu_bin);
            }
            if (!yetty_incbin_assets_extract_qemu_to(assets, data_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract qemu assets");
            }
        }
    }

    yetty_incbin_assets_destroy(assets);
    return YETTY_OK_VOID();
}
