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

    /* Create the writable dirs; an already-existing directory is a no-op.
     * assets_dir/bin_dir are intentionally not created. */
    const char *writable_dirs[] = {
        paths->cache_dir_buf, paths->data_dir_buf,    paths->runtime_dir_buf, paths->config_dir_buf,
        paths->state_dir_buf, paths->shaders_dir_buf, paths->fonts_dir_buf,
    };
    for (size_t index = 0; index < sizeof(writable_dirs) / sizeof(writable_dirs[0]); index++) {
        struct yetty_ycore_void_result mkdir_res = yetty_yplatform_mkdir_p(writable_dirs[index]);
        if (YETTY_IS_ERR(mkdir_res)) {
            yetty_yplatform_paths_destroy(paths);
            return YETTY_ERR(yetty_yplatform_paths_ptr, "paths_create: cannot create writable dir",
                             mkdir_res);
        }
    }

    return res;
}
