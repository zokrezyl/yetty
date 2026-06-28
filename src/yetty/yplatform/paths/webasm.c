/*
 * WebAssembly platform paths — Emscripten MEMFS layout.
 *
 * Implements the platform-paths contract from
 * include/yetty/yplatform/paths.h. On webasm, paths are virtual MEMFS
 * paths inside the wasm runtime; the platform layer creates these dirs
 * lazily on first write. yetty_yplatform_extract_assets() decompresses
 * incbin'd brotli blobs into /data/{shaders,fonts,msdf-fonts,yemu}/
 * at startup, then runtime code reads them via plain fopen.
 *
 * assets_dir is "/data" because there is no exec-relative location in
 * MEMFS — incbin's the canonical source for everything.
 */

#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/paths.h>

struct yetty_yplatform_paths_ptr_result yetty_yplatform_paths_get_platform_paths(void)
{
    struct yetty_yplatform_paths *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_paths_ptr, "OOM allocating yetty_yplatform_paths");
    }

    strncpy(p->cache_dir_buf, "/cache", sizeof(p->cache_dir_buf) - 1);
    strncpy(p->data_dir_buf, "/data", sizeof(p->data_dir_buf) - 1);
    strncpy(p->config_dir_buf, "/config", sizeof(p->config_dir_buf) - 1);
    strncpy(p->state_dir_buf, "/state", sizeof(p->state_dir_buf) - 1);
    strncpy(p->runtime_dir_buf, "/tmp", sizeof(p->runtime_dir_buf) - 1);
    strncpy(p->assets_dir_buf, "/data", sizeof(p->assets_dir_buf) - 1);

    return YETTY_OK(yetty_yplatform_paths_ptr, p);
}

struct yetty_ycore_void_result yetty_yplatform_paths_destroy(struct yetty_yplatform_paths *paths)
{
    free(paths);
    return YETTY_OK_VOID();
}
