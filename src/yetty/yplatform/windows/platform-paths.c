/* Windows platform paths - AppData directories */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cache_dir_buf[512];
static char data_dir_buf[512];
static char runtime_dir_buf[512];
static char config_dir_buf[512];

// TODO: unify the platform-paths.c for all platform into one
const char *yetty_yplatform_get_cache_dir(void)
{
    const char *localAppData = getenv("LOCALAPPDATA");
    if (localAppData) {
        snprintf(cache_dir_buf, sizeof(cache_dir_buf), "%s\\yetty\\cache", localAppData);
        return cache_dir_buf;
    }
    return "C:\\temp\\yetty";
}

const char *yetty_yplatform_get_data_dir(void)
{
    const char *localAppData = getenv("LOCALAPPDATA");
    if (localAppData) {
        snprintf(data_dir_buf, sizeof(data_dir_buf), "%s\\yetty\\data", localAppData);
        return data_dir_buf;
    }
    return "C:\\temp\\yetty";
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    const char *temp = getenv("TEMP");
    if (temp) {
        snprintf(runtime_dir_buf, sizeof(runtime_dir_buf), "%s\\yetty", temp);
        return runtime_dir_buf;
    }
    return "C:\\temp\\yetty";
}

const char *yetty_yplatform_get_config_dir(void)
{
    const char *appData = getenv("APPDATA");
    if (appData) {
        snprintf(config_dir_buf, sizeof(config_dir_buf), "%s\\yetty", appData);
        return config_dir_buf;
    }
    return "C:\\temp\\yetty";
}
