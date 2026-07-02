/*
 * yplatform/install.h — enumerate the assets embedded in the running
 * executable.
 *
 * The yinstall tool carries its whole payload (executables, shaders,
 * fonts, config, the RISC-V VM runtime) baked into its own binary. How
 * those bytes are physically embedded differs by platform:
 *
 *   - Linux / macOS  → `.incbin` symbols      (src/yetty/yplatform/install/incbin.c)
 *   - Windows        → Win32 RT_RCDATA        (src/yetty/yplatform/install/winres.c)
 *
 * Both backends present the same enumeration interface below, so the
 * yinstall module never sees a platform #ifdef — it just walks the
 * embedded blobs and lays each one down.
 */

#ifndef YETTY_YPLATFORM_INSTALL_H
#define YETTY_YPLATFORM_INSTALL_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Invoked once per embedded blob.
 *
 *   name       — full asset name including its component prefix, exactly
 *                as the build registered it, e.g. "bin/ycat",
 *                "data/shaders/text-layer.wgsl", "yemu/kernel-riscv64.bin".
 *   data, size — the embedded bytes. Read-only; owned by the binary image
 *                for the lifetime of the process — never freed by the callee.
 *   compressed — non-zero when `data` is a brotli stream the caller must
 *                inflate before writing to disk.
 *   userdata   — opaque pointer forwarded verbatim from foreach_asset.
 */
typedef void (*yetty_yplatform_install_asset_fn)(const char *name, const uint8_t *data, size_t size,
                                                 int compressed, void *userdata);

/*
 * Enumerate every blob embedded in this executable, calling `callback`
 * for each one. The two platform backends implement this identically
 * from the caller's point of view; the only difference is where the
 * bytes are read from. Returns an error only on an internal enumeration
 * fault — an executable with no embedded payload simply yields zero
 * callbacks and still returns success (a development build links no
 * manifest).
 */
struct yetty_ycore_void_result yetty_yplatform_install_foreach_asset(
    yetty_yplatform_install_asset_fn callback, void *userdata);

/*
 * Outcome of yetty_yplatform_install_add_to_user_path. Lets the caller word
 * its message correctly: "added" vs "already there, open a new terminal" vs
 * "not something we manage here" are three distinct user situations.
 */
enum yetty_yplatform_path_outcome {
    /* Platform doesn't manage a persistent PATH from the installer (POSIX):
     * PATH lives in the user's shell profile. Caller should advise manually. */
    YETTY_YPLATFORM_PATH_UNSUPPORTED = 0,
    /* `dir` was newly written to the persistent PATH. */
    YETTY_YPLATFORM_PATH_ADDED = 1,
    /* `dir` was already on the persistent PATH; nothing was written. */
    YETTY_YPLATFORM_PATH_ALREADY_PRESENT = 2,
};

/*
 * Ensure `dir` is on the user's persistent PATH. Platform-specific, so it
 * lives beside the two asset backends rather than in the installer core:
 *
 *   - Windows → append `dir` to HKCU\Environment\Path (creating the value
 *     if absent) and broadcast WM_SETTINGCHANGE so newly launched processes
 *     see it. Sets *out_outcome to ADDED when it writes the registry, or
 *     ALREADY_PRESENT when `dir` is already there (nothing written).
 *   - POSIX   → no-op: PATH is owned by the user's shell profile, so the
 *     installer only advises. Sets *out_outcome to UNSUPPORTED and returns
 *     success — preserving the pre-existing advisory-note behaviour.
 *
 * Returns an error only on a genuine platform failure (e.g. a denied
 * registry write); the caller then falls back to printing manual advice.
 * `out_outcome` may be NULL if the caller doesn't care.
 */
struct yetty_ycore_void_result yetty_yplatform_install_add_to_user_path(
    const char *dir, enum yetty_yplatform_path_outcome *out_outcome);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_INSTALL_H */
