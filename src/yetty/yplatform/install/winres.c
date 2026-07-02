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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Persist `dir` on the user's PATH via HKCU\Environment. Reads the current
 * value (raw, unexpanded — REG_EXPAND_SZ entries keep their %VARS%),
 * appends `dir` if absent (case-insensitive, ';'-delimited), writes it back
 * preserving the original value type, and broadcasts WM_SETTINGCHANGE so
 * Explorer and newly spawned shells re-read the environment.
 */

/* Non-zero if `dir` is already one of the ';'-separated entries in `path`
 * (case-insensitive, tolerating one trailing backslash on either side). */
static int win_path_contains(const char *path, const char *dir)
{
    size_t dir_len = strlen(dir);
    if (dir_len && (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/')) {
        dir_len--;
    }
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t seg_len = end ? (size_t)(end - cursor) : strlen(cursor);
        size_t trimmed = seg_len;
        if (trimmed && (cursor[trimmed - 1] == '\\' || cursor[trimmed - 1] == '/')) {
            trimmed--;
        }
        if (trimmed == dir_len && _strnicmp(cursor, dir, dir_len) == 0) {
            return 1;
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    return 0;
}

struct yetty_ycore_void_result yetty_yplatform_install_add_to_user_path(
    const char *dir, enum yetty_yplatform_path_outcome *out_outcome)
{
    if (out_outcome) {
        *out_outcome = YETTY_YPLATFORM_PATH_UNSUPPORTED;
    }
    if (!dir || !*dir) {
        return YETTY_ERR(yetty_ycore_void, "add_to_user_path: empty directory");
    }

    HKEY key;
    LONG rc = RegCreateKeyExA(HKEY_CURRENT_USER, "Environment", 0, NULL, 0,
                              KEY_READ | KEY_WRITE, NULL, &key, NULL);
    if (rc != ERROR_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "add_to_user_path: cannot open HKCU\\Environment");
    }

    /* Read the existing Path (may be absent). */
    DWORD type = REG_EXPAND_SZ;
    DWORD bytes = 0;
    char *current = NULL;
    rc = RegQueryValueExA(key, "Path", NULL, &type, NULL, &bytes);
    if (rc == ERROR_SUCCESS && bytes > 0) {
        current = malloc(bytes + 1);
        if (!current) {
            RegCloseKey(key);
            return YETTY_ERR(yetty_ycore_void, "add_to_user_path: OOM reading PATH");
        }
        rc = RegQueryValueExA(key, "Path", NULL, &type, (LPBYTE)current, &bytes);
        if (rc != ERROR_SUCCESS) {
            free(current);
            RegCloseKey(key);
            return YETTY_ERR(yetty_ycore_void, "add_to_user_path: cannot read PATH");
        }
        current[bytes] = '\0'; /* guard against an unterminated REG_SZ */
    } else {
        current = calloc(1, 1);
        type = REG_EXPAND_SZ;
    }

    if (win_path_contains(current, dir)) {
        free(current);
        RegCloseKey(key);
        if (out_outcome) {
            *out_outcome = YETTY_YPLATFORM_PATH_ALREADY_PRESENT;
        }
        return YETTY_OK_VOID(); /* already on the persistent PATH; nothing written */
    }

    /* Join current + ";" + dir, avoiding a doubled or leading separator. */
    size_t cur_len = strlen(current);
    int need_sep = (cur_len > 0 && current[cur_len - 1] != ';');
    size_t new_len = cur_len + (need_sep ? 1 : 0) + strlen(dir);
    char *updated = malloc(new_len + 1);
    if (!updated) {
        free(current);
        RegCloseKey(key);
        return YETTY_ERR(yetty_ycore_void, "add_to_user_path: OOM building PATH");
    }
    snprintf(updated, new_len + 1, "%s%s%s", current, need_sep ? ";" : "", dir);
    free(current);

    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        type = REG_EXPAND_SZ; /* never clobber PATH with a non-string type */
    }
    rc = RegSetValueExA(key, "Path", 0, type, (const BYTE *)updated,
                        (DWORD)(new_len + 1));
    free(updated);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "add_to_user_path: cannot write PATH");
    }

    /* Tell running processes the environment changed. Best-effort. */
    DWORD_PTR result = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM) "Environment",
                        SMTO_ABORTIFHUNG, 5000, &result);

    if (out_outcome) {
        *out_outcome = YETTY_YPLATFORM_PATH_ADDED;
    }
    return YETTY_OK_VOID();
}
