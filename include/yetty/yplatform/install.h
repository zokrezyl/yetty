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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_INSTALL_H */
