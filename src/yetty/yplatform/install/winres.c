/*
 * yplatform/install/winres.c — Windows backend for
 * yetty_yplatform_install_foreach_asset.
 *
 * On MSVC the installer's payload cannot ride `.incbin` (no inline asm),
 * so build-tools/yetty/incbin.cmake streams each blob into the PE image as
 * a Win32 RT_RCDATA resource and generates, per embedded prefix, a
 * `register_<prefix>_assets_c(callback)` entry point whose body does the
 * FindResourceA / LoadResource / LockResource dance and reports each blob
 * with its logical asset name and compressed flag. This file fans those
 * generated entry points into one enumeration — the resource access lives
 * in the generated register routines; here we only forward.
 *
 * Compiled into the yinstall *tool* target (where the manifests are
 * generated), mirroring the Linux/macOS counterpart incbin.c.
 */

#include <windows.h>

#include <yetty/yplatform/install.h>

#ifdef HAS_BIN_MANIFEST
#include "yinstall_bin_manifest.h"
#endif
#ifdef HAS_DATA_MANIFEST
#include "yinstall_data_manifest.h"
#endif
#ifdef HAS_GREETER_MANIFEST
#include "yinstall_greeter_manifest.h"
#endif
#ifdef HAS_DEMOS_MANIFEST
#include "yinstall_demos_manifest.h"
#endif
#ifdef HAS_YCONFIG_MANIFEST
#include "yinstall_yconfig_manifest.h"
#endif
#ifdef HAS_YEMU_MANIFEST
#include "yinstall_yemu_manifest.h"
#endif
#ifdef HAS_QEMU_MANIFEST
#include "yinstall_qemu_manifest.h"
#endif

/*
 * The generated register_<prefix>_assets_c entry points take a fixed
 * four-argument callback with no userdata slot. Stash the caller's
 * userdata-carrying callback in a bridge the trampoline reads. The
 * register_* calls are synchronous and non-reentrant, so the bridge is
 * set immediately before a fan-out and cleared immediately after.
 */
struct install_bridge {
    yetty_yplatform_install_asset_fn callback;
    void *userdata;
};

static struct install_bridge *current_bridge = NULL;

static void bridge_trampoline(const char *name, const uint8_t *data, size_t size, int compressed)
{
    if (current_bridge && current_bridge->callback) {
        current_bridge->callback(name, data, size, compressed, current_bridge->userdata);
    }
}

struct yetty_ycore_void_result
yetty_yplatform_install_foreach_asset(yetty_yplatform_install_asset_fn callback, void *userdata)
{
    if (!callback) {
        return YETTY_OK_VOID();
    }

    struct install_bridge bridge = {.callback = callback, .userdata = userdata};
    current_bridge = &bridge;

#ifdef HAS_BIN_MANIFEST
    register_bin_assets_c(bridge_trampoline);
#endif
#ifdef HAS_DATA_MANIFEST
    register_data_assets_c(bridge_trampoline);
#endif
#ifdef HAS_GREETER_MANIFEST
    register_greeter_assets_c(bridge_trampoline);
#endif
#ifdef HAS_DEMOS_MANIFEST
    register_demos_assets_c(bridge_trampoline);
#endif
#ifdef HAS_YCONFIG_MANIFEST
    register_yconfig_assets_c(bridge_trampoline);
#endif
#ifdef HAS_YEMU_MANIFEST
    register_yemu_assets_c(bridge_trampoline);
#endif
#ifdef HAS_QEMU_MANIFEST
    register_qemu_assets_c(bridge_trampoline);
#endif

    current_bridge = NULL;
    return YETTY_OK_VOID();
}
