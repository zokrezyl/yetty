/* WebAssembly platform paths
 *
 * On WebASM, paths are virtual filesystem paths in Emscripten's MEMFS.
 * yetty_yplatform_extract_assets() decompresses incbin'd brotli blobs
 * into /data/{shaders,fonts,msdf-fonts,yemu}/ at startup.
 */

const char *yetty_yplatform_get_data_dir(void)
{
    return "/data";
}

const char *yetty_yplatform_get_config_dir(void)
{
    return "/config";
}

const char *yetty_yplatform_get_cache_dir(void)
{
    return "/cache";
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    return "/tmp";
}
