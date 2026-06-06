/*
 * Singleton accessors for the platform-paths convenience getters.
 *
 * Per-platform impls under src/yetty/yplatform/paths/<plat>.{c,m}
 * provide the heap-allocating yetty_yplatform_paths_get_platform_paths
 * + matching destroy. This file layers the older
 * yetty_yplatform_get_<x>_dir() getters on top — process-wide
 * singleton, lazily created on first call, never freed.
 *
 * Lifetime: the singleton lives until process exit. Leaking it is the
 * intended behaviour — paths are needed throughout the run, freeing
 * them would mean the convenience getters point at dangling memory.
 * If someone needs per-call ownership (e.g. to swap directories under
 * test), they call yetty_yplatform_paths_get_platform_paths directly.
 *
 * Threading: pthread_once gives us a barrier on POSIX; on Windows we
 * fall back to InterlockedCompareExchangePointer for first-init. After
 * init the singleton is read-only, so reads from any thread are safe.
 */

#include <stddef.h>
#include <stdlib.h>

#include <yetty/yplatform/paths.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static struct yetty_yplatform_paths *g_paths = NULL;

#ifdef _WIN32
static void init_singleton(void)
{
    if (g_paths) {
        return;
    }
    struct yetty_yplatform_paths_ptr_result r = yetty_yplatform_paths_get_platform_paths();
    if (!r.ok) {
        if (r.error.msg) {
            /* Can't easily route this through yerror — paths layer is
             * a transitive dep of the tracer. Drop to stderr. */
        }
        return;
    }
    struct yetty_yplatform_paths *prev =
        InterlockedCompareExchangePointer((PVOID volatile *)&g_paths, r.value, NULL);
    if (prev != NULL) {
        /* Another thread beat us — discard our copy. */
        yetty_yplatform_paths_destroy(r.value);
    }
}
#else
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void init_singleton_once(void)
{
    struct yetty_yplatform_paths_ptr_result r = yetty_yplatform_paths_get_platform_paths();
    if (r.ok) {
        g_paths = r.value;
    }
}
static void init_singleton(void)
{
    pthread_once(&g_once, init_singleton_once);
}
#endif

#define FIELD_GETTER(name, field)                                                                  \
    const char *yetty_yplatform_get_##name##_dir(void)                                             \
    {                                                                                              \
        init_singleton();                                                                          \
        if (!g_paths)                                                                              \
            return "";                                                                             \
        return g_paths->field##_buf;                                                               \
    }

FIELD_GETTER(cache, cache_dir)
FIELD_GETTER(data, data_dir)
FIELD_GETTER(runtime, runtime_dir)
FIELD_GETTER(config, config_dir)
FIELD_GETTER(assets, assets_dir)
FIELD_GETTER(bin, bin_dir)
