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
int yetty_incbin_assets_needs_extraction(struct yetty_incbin_assets *assets, const char *dir);
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
    needs_extract = yetty_incbin_assets_needs_extraction(assets, data_dir);
    if (needs_extract) {
        if (!yetty_incbin_assets_extract_data_to(assets, data_dir)) {
            yetty_incbin_assets_destroy(assets);
            return YETTY_ERR(yetty_ycore_void, "failed to extract data assets");
        }
    }

    /* Check if config extraction needed */
    if (config_dir && config_dir[0]) {
        needs_extract = yetty_incbin_assets_needs_extraction(assets, config_dir);
        if (needs_extract) {
            if (!yetty_incbin_assets_extract_config_to(assets, config_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract config assets");
            }
        }
    }

    /* Extract shared RISC-V runtime (kernel/opensbi/rootfs) to <data_dir>/yemu.
     * Re-extract whenever any of the expected pieces is missing so an
     * existing install picks up newly-bundled artifacts (e.g. a bumped
     * unified rootfs) without forcing the user to wipe the dir. */
    if (yetty_incbin_assets_has_yemu(assets)) {
        const char *expected[] = {
            "yemu/kernel-riscv64.bin",
            "yemu/opensbi-fw_jump.elf",
            "yemu/alpine-rootfs.img",
            "yemu/yetty-rootfs-riscv.img",
        };
        int need_extract = 0;
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", data_dir, expected[i]);
            if (!yetty_yplatform_file_exists(path)) {
                need_extract = 1;
                break;
            }
        }
        if (need_extract && !yetty_incbin_assets_extract_yemu_to(assets, data_dir)) {
            yetty_incbin_assets_destroy(assets);
            return YETTY_ERR(yetty_ycore_void, "failed to extract yemu assets");
        }
    }

    /* Extract QEMU binary to <data_dir>/qemu if embedded and not yet extracted */
    if (yetty_incbin_assets_has_qemu(assets)) {
        char qemu_bin[512];
#ifdef _WIN32
        snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64.exe", data_dir);
#else
        snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
#endif
        if (!yetty_yplatform_file_exists(qemu_bin)) {
            if (!yetty_incbin_assets_extract_qemu_to(assets, data_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract qemu assets");
            }
        }
    }

    yetty_incbin_assets_destroy(assets);
    return YETTY_OK_VOID();
}
