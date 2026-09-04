/*
 * yplatform/install/incbin.c — Linux/macOS backend for
 * yetty_yplatform_install_foreach_asset.
 *
 * On GCC/Clang the installer's payload is embedded with the `.incbin`
 * assembler directive (see build-tools/yetty/incbin.cmake). For each
 * embedded prefix the build generates a `<target>_<prefix>_manifest.h`
 * exposing a `register_<prefix>_assets_c(callback)` entry point that walks
 * that prefix's blobs. This file includes whichever manifests the build
 * produced and fans them all into one enumeration.
 *
 * This translation unit is compiled into the yinstall *tool* target (not
 * yplatform_core) because that is where the manifests and their
 * HAS_<PREFIX>_MANIFEST guards are generated — the same arrangement
 * ygreeter uses for its own embedded assets. The Windows counterpart is
 * winres.c.
 */

#include <yetty/yplatform/install.h>

/* Each manifest defines a `register_<prefix>_assets_c` (a static inline
 * blob-walker on this backend) plus the HAS_<PREFIX>_MANIFEST guard.
 * Absent guards mean this installer variant did not embed that prefix
 * (yinstall-min carries no tools beyond yetty, no demos, no VM runtime) —
 * the corresponding fan-out simply compiles out. The manifest headers are
 * named after the variant target, so the build writes one umbrella header
 * per variant (tools/yinstall/CMakeLists.txt) that pulls in its set. */
#include "yinstall_manifests.h"

/*
 * The generated register_<prefix>_assets_c entry points take a fixed
 * four-argument callback with no userdata slot. To forward each blob to
 * the caller's userdata-carrying callback we stash both in a bridge that
 * the four-arg trampoline reads. The register_* calls are synchronous and
 * non-reentrant, so the bridge is set immediately before a fan-out and
 * cleared immediately after — it never outlives a single foreach call.
 * (Same shape as the asset extractors in yncbin and ygreeter, which face
 * the identical generated-callback ABI.)
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

struct yetty_ycore_void_result yetty_yplatform_install_foreach_asset(
    yetty_yplatform_install_asset_fn callback, void *userdata)
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

/*
 * POSIX no-op. On Linux/macOS the user's PATH is owned by their shell
 * profile (~/.profile, ~/.zshrc, …), not a machine setting the installer
 * can safely rewrite, so we leave it to the advisory note the installer
 * already prints. Reports UNSUPPORTED and returns success — the installer
 * then prints its manual PATH advice exactly as before. The Windows
 * counterpart in winres.c does the real HKCU\Environment work.
 */
struct yetty_ycore_void_result yetty_yplatform_install_add_to_user_path(
    const char *dir, enum yetty_yplatform_path_outcome *out_outcome)
{
    (void)dir;
    if (out_outcome) {
        *out_outcome = YETTY_YPLATFORM_PATH_UNSUPPORTED;
    }
    return YETTY_OK_VOID();
}
