/* macOS platform paths - ~/Library directories */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char cache_dir_buf[512];
static char data_dir_buf[512];
static char runtime_dir_buf[512];
static char config_dir_buf[512];

const char *yetty_yplatform_get_cache_dir(void)
{
    const char *home = getenv("HOME");
    if (home) {
        snprintf(cache_dir_buf, sizeof(cache_dir_buf), "%s/Library/Caches/yetty", home);
        return cache_dir_buf;
    }
    return "/tmp/yetty";
}

const char *yetty_yplatform_get_data_dir(void)
{
    const char *home = getenv("HOME");
    if (home) {
        snprintf(data_dir_buf, sizeof(data_dir_buf), "%s/Library/Application Support/yetty", home);
        return data_dir_buf;
    }
    return "/tmp/yetty";
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    /* macOS doesn't have XDG_RUNTIME_DIR, use tmp with uid */
    snprintf(runtime_dir_buf, sizeof(runtime_dir_buf), "/tmp/yetty-%d", getuid());
    return runtime_dir_buf;
}

const char *yetty_yplatform_get_config_dir(void)
{
    const char *home = getenv("HOME");
    if (home) {
        snprintf(config_dir_buf, sizeof(config_dir_buf), "%s/Library/Application Support/yetty",
                 home);
        return config_dir_buf;
    }
    return "/tmp/yetty";
}
