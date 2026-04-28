/* extract-assets.c - Extract embedded assets to data and config directories */

#include <yetty/platform/extract-assets.h>
#include <yetty/yconfig.h>
#include <yetty/yplatform/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations - implemented in platform-paths.c */
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_config_dir(void);

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

struct yetty_ycore_void_result yetty_yplatform_extract_assets(struct yetty_yconfig *config)
{
    const char *data_dir;
    const char *config_dir;
    struct yetty_incbin_assets *assets;
    int needs_extract;

    (void)config;

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
     * Presence of the kernel is our gate — both --temu and --qemu need it. */
    if (yetty_incbin_assets_has_yemu(assets)) {
        char yemu_kernel[512];
        snprintf(yemu_kernel, sizeof(yemu_kernel), "%s/yemu/kernel-riscv64.bin", data_dir);
        if (!yplatform_file_exists(yemu_kernel)) {
            if (!yetty_incbin_assets_extract_yemu_to(assets, data_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract yemu assets");
            }
        }
    }

    /* Extract QEMU binary to <data_dir>/qemu if embedded and not yet extracted */
    if (yetty_incbin_assets_has_qemu(assets)) {
        char qemu_bin[512];
        snprintf(qemu_bin, sizeof(qemu_bin), "%s/qemu/qemu-system-riscv64", data_dir);
        if (!yplatform_file_exists(qemu_bin)) {
            if (!yetty_incbin_assets_extract_qemu_to(assets, data_dir)) {
                yetty_incbin_assets_destroy(assets);
                return YETTY_ERR(yetty_ycore_void, "failed to extract qemu assets");
            }
        }
    }

    yetty_incbin_assets_destroy(assets);
    return YETTY_OK_VOID();
}
