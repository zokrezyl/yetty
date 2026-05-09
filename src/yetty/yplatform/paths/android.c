/* Android platform paths - app internal storage */

static const char *cache_dir = "/data/data/com.yetty.terminal/cache";
static const char *data_dir = "/data/data/com.yetty.terminal/files/data";
static const char *config_dir = "/data/data/com.yetty.terminal/files";

const char *yetty_yplatform_get_cache_dir(void)
{
    return cache_dir;
}

const char *yetty_yplatform_get_data_dir(void)
{
    return data_dir;
}

const char *yetty_yplatform_get_runtime_dir(void)
{
    /* Android uses cache for runtime files */
    return cache_dir;
}

const char *yetty_yplatform_get_config_dir(void)
{
    return config_dir;
}
