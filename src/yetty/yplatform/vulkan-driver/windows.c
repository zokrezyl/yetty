/*
 * yplatform/vulkan-driver/windows.c - register bundled Mesa lavapipe.
 *
 * See include/yetty/yplatform/vulkan-driver.h for the contract and
 * build-tools/yetty/mesa-lavapipe.cmake for how the files get next to
 * the exe.
 */

#include <yetty/yplatform/vulkan-driver.h>

#include <yetty/ytrace/ytrace.h>

#include <windows.h>

#include <stdlib.h>
#include <string.h>

void yetty_yplatform_vulkan_register_bundled_driver(void)
{
    /* The user's own loader configuration always wins. */
    if (getenv("VK_ADD_DRIVER_FILES") || getenv("VK_DRIVER_FILES") ||
        getenv("VK_ICD_FILENAMES")) {
        return;
    }
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    if (n == 0 || n >= sizeof path) {
        return;
    }
    char *slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    const char manifest[] = "lvp_icd.x86_64.json";
    if ((size_t)(slash + 1 - path) + sizeof manifest > sizeof path) {
        return;
    }
    memcpy(slash + 1, manifest, sizeof manifest);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return; /* not bundled alongside this exe — nothing to register */
    }
    _putenv_s("VK_ADD_DRIVER_FILES", path);
    ydebug("yplatform: registered bundled lavapipe ICD: %s", path);
}
