/* WebAssembly platform paths
 *
 * On WebASM, paths are virtual filesystem paths in Emscripten's VFS.
 */

const char *yetty_yplatform_get_cache_dir(void)
{
    return "/cache";
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    return "/tmp";
}
