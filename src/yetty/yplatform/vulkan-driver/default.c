/*
 * yplatform/vulkan-driver/default.c - no bundled Vulkan driver.
 *
 * Linux/macOS/Android/iOS get their GPU drivers (Mesa, MoltenVK-less
 * Metal, vendor ICDs) from the OS; nothing to register. See
 * include/yetty/yplatform/vulkan-driver.h.
 */

#include <yetty/yplatform/vulkan-driver.h>

void yetty_yplatform_vulkan_register_bundled_driver(void)
{
}
