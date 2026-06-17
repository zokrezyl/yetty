/*
 * Shared, platform-independent paths setup.
 *
 * yetty_yplatform_paths_create layers directory-creation policy on top of
 * the per-platform resolver (paths/<platform>.{c,m}): it derives the
 * shaders/fonts dirs from data_dir and materializes the writable directory
 * set, so callers receive a layout that is ready for plain POSIX I/O.
 */

#include <stdio.h>

#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_create(void)
{
    struct yetty_yplatform_paths_ptr_result res = yetty_yplatform_paths_get_platform_paths();
    if (!res.ok) {
        return res;
    }
    struct yetty_yplatform_paths *paths = res.value;

    snprintf(paths->shaders_dir_buf, sizeof(paths->shaders_dir_buf), "%s/shaders",
             paths->data_dir_buf);
    snprintf(paths->fonts_dir_buf, sizeof(paths->fonts_dir_buf), "%s/fonts", paths->data_dir_buf);

    /* Create the writable dirs. mkdir_p returns void; an already-existing
     * directory is a no-op. assets_dir/bin_dir are intentionally not created. */
    yetty_yplatform_mkdir_p(paths->cache_dir_buf);
    yetty_yplatform_mkdir_p(paths->data_dir_buf);
    yetty_yplatform_mkdir_p(paths->runtime_dir_buf);
    yetty_yplatform_mkdir_p(paths->config_dir_buf);
    yetty_yplatform_mkdir_p(paths->shaders_dir_buf);
    yetty_yplatform_mkdir_p(paths->fonts_dir_buf);

    return res;
}
