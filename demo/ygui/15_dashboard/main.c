/*
 * Demo 15: Dashboard — multi-panel system dashboard.
 * Ported from yetty-poc/demo/assets/ygui-c/python/10_dashboard.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_engine* g_engine = NULL;
static struct yetty_ygui_old_widget* g_status = NULL;

static struct yetty_ygui_old_widget* g_cpu_bar  = NULL;
static struct yetty_ygui_old_widget* g_cpu_val  = NULL;
static struct yetty_ygui_old_widget* g_mem_bar  = NULL;
static struct yetty_ygui_old_widget* g_mem_val  = NULL;
static struct yetty_ygui_old_widget* g_net_in   = NULL;
static struct yetty_ygui_old_widget* g_net_out  = NULL;

static struct yetty_ygui_old_widget* g_dark_cb     = NULL;
static struct yetty_ygui_old_widget* g_notif_cb    = NULL;
static struct yetty_ygui_old_widget* g_refresh_sl  = NULL;
static struct yetty_ygui_old_widget* g_refresh_val = NULL;

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static float frand(float lo, float hi) {
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static void set_status(const char* text) {
    yetty_ygui_old_widget_label_set_text(g_status, text);
}

static void on_refresh(struct yetty_ygui_old_widget* w, void* u) {
    (void)w; (void)u;
    float cpu = frand(20, 80);
    float mem = frand(40, 85);
    char buf[32];

    yetty_ygui_old_widget_progress_set_value(g_cpu_bar, cpu / 100.0f);
    snprintf(buf, sizeof(buf), "%d%%", (int)cpu);
    yetty_ygui_old_widget_label_set_text(g_cpu_val, buf);

    yetty_ygui_old_widget_progress_set_value(g_mem_bar, mem / 100.0f);
    snprintf(buf, sizeof(buf), "%d%%", (int)mem);
    yetty_ygui_old_widget_label_set_text(g_mem_val, buf);

    snprintf(buf, sizeof(buf), "In: %d KB/s", (int)frand(0, 500));
    yetty_ygui_old_widget_label_set_text(g_net_in, buf);
    snprintf(buf, sizeof(buf), "Out: %d KB/s", (int)frand(0, 200));
    yetty_ygui_old_widget_label_set_text(g_net_out, buf);

    set_status("Refreshed");
}

static void on_cache(struct yetty_ygui_old_widget* w, void* u)    { (void)w; (void)u; set_status("Cache cleared"); }
static void on_restart(struct yetty_ygui_old_widget* w, void* u)  { (void)w; (void)u; set_status("Restarting..."); }
static void on_backup(struct yetty_ygui_old_widget* w, void* u)   { (void)w; (void)u; set_status("Backup started"); }
static void on_logs(struct yetty_ygui_old_widget* w, void* u)     { (void)w; (void)u; set_status("Opening logs"); }
static void on_settings(struct yetty_ygui_old_widget* w, void* u) { (void)w; (void)u; set_status("Settings open"); }

static void on_refresh_rate(struct yetty_ygui_old_widget* w, float value, void* u) {
    (void)w; (void)u;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fs", value);
    yetty_ygui_old_widget_label_set_text(g_refresh_val, buf);
}

static void on_apply(struct yetty_ygui_old_widget* w, void* u) {
    (void)w; (void)u;
    char buf[96];
    snprintf(buf, sizeof(buf), "Applied: dark=%d, notif=%d, rate=%.1fs",
             yetty_ygui_old_widget_checkbox_get_checked(g_dark_cb),
             yetty_ygui_old_widget_checkbox_get_checked(g_notif_cb),
             yetty_ygui_old_widget_slider_get_value(g_refresh_sl));
    set_status(buf);
}

static void on_quit(struct yetty_ygui_old_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_old_engine_stop(g_engine);
}

static void on_key(struct yetty_ygui_old_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_old_engine_stop(e);
}

int main(void) {
    srand((unsigned)time(NULL));

    if (yetty_ygui_old_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_old_engine_create((struct yetty_ygui_old_engine_args){.name = "dashboard"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_old_shutdown(); return 1; }

    uint32_t panel_bg = rgba(45, 45, 50, 255);

    yetty_ygui_old_engine_label(g_engine, "header", 30, 15, "System Dashboard");
    g_status = yetty_ygui_old_engine_label(g_engine, "status", 650, 15, "Online");

    /* === System Stats === */
    struct yetty_ygui_old_widget* stats = yetty_ygui_old_engine_panel(g_engine, "stats_panel", 20, 50, 370, 180);
    yetty_ygui_old_widget_set_bg_color(stats, panel_bg);
    yetty_ygui_old_engine_label(g_engine, "stats_title", 35, 65, "System Resources");

    yetty_ygui_old_engine_label(g_engine, "cpu_label", 35, 100, "CPU");
    g_cpu_bar = yetty_ygui_old_engine_progress(g_engine, "cpu_bar", 90, 98, 220, 18, 0.45f);
    g_cpu_val = yetty_ygui_old_engine_label(g_engine, "cpu_value", 320, 100, "45%");

    yetty_ygui_old_engine_label(g_engine, "mem_label", 35, 130, "Memory");
    g_mem_bar = yetty_ygui_old_engine_progress(g_engine, "mem_bar", 90, 128, 220, 18, 0.62f);
    g_mem_val = yetty_ygui_old_engine_label(g_engine, "mem_value", 320, 130, "62%");

    yetty_ygui_old_engine_label(g_engine, "disk_label", 35, 160, "Disk");
    yetty_ygui_old_engine_progress(g_engine, "disk_bar", 90, 158, 220, 18, 0.78f);
    yetty_ygui_old_engine_label(g_engine, "disk_value", 320, 160, "78%");

    yetty_ygui_old_engine_label(g_engine, "net_label", 35, 195, "Network");
    g_net_in  = yetty_ygui_old_engine_label(g_engine, "net_in",  90, 195, "In: 0 KB/s");
    g_net_out = yetty_ygui_old_engine_label(g_engine, "net_out", 200, 195, "Out: 0 KB/s");

    /* === Quick Actions === */
    struct yetty_ygui_old_widget* actions = yetty_ygui_old_engine_panel(g_engine, "actions_panel", 410, 50, 370, 180);
    yetty_ygui_old_widget_set_bg_color(actions, panel_bg);
    yetty_ygui_old_engine_label(g_engine, "actions_title", 425, 65, "Quick Actions");

    struct yetty_ygui_old_widget* refresh = yetty_ygui_old_engine_button(g_engine, "refresh",     425, 95, 100, 35, "Refresh");
    struct yetty_ygui_old_widget* cache   = yetty_ygui_old_engine_button(g_engine, "clear_cache", 540, 95, 100, 35, "Clear Cache");
    struct yetty_ygui_old_widget* restart = yetty_ygui_old_engine_button(g_engine, "restart",     655, 95, 100, 35, "Restart");
    struct yetty_ygui_old_widget* backup  = yetty_ygui_old_engine_button(g_engine, "backup",      425, 145, 100, 35, "Backup");
    struct yetty_ygui_old_widget* logs    = yetty_ygui_old_engine_button(g_engine, "logs",        540, 145, 100, 35, "View Logs");
    struct yetty_ygui_old_widget* set     = yetty_ygui_old_engine_button(g_engine, "settings",    655, 145, 100, 35, "Settings");
    yetty_ygui_old_widget_button_on_click(refresh, on_refresh,  NULL);
    yetty_ygui_old_widget_button_on_click(cache,   on_cache,    NULL);
    yetty_ygui_old_widget_button_on_click(restart, on_restart,  NULL);
    yetty_ygui_old_widget_button_on_click(backup,  on_backup,   NULL);
    yetty_ygui_old_widget_button_on_click(logs,    on_logs,     NULL);
    yetty_ygui_old_widget_button_on_click(set,     on_settings, NULL);

    yetty_ygui_old_engine_checkbox(g_engine, "alerts", 425, 195, 180, 25, "Enable Alerts", 1);

    /* === Settings === */
    struct yetty_ygui_old_widget* settings = yetty_ygui_old_engine_panel(g_engine, "settings_panel", 20, 250, 370, 180);
    yetty_ygui_old_widget_set_bg_color(settings, panel_bg);
    yetty_ygui_old_engine_label(g_engine, "settings_title", 35, 265, "Settings");

    g_dark_cb  = yetty_ygui_old_engine_checkbox(g_engine, "dark_mode",     35, 300, 160, 25, "Dark Mode", 1);
    g_notif_cb = yetty_ygui_old_engine_checkbox(g_engine, "notifications", 35, 335, 160, 25, "Notifications", 1);

    yetty_ygui_old_engine_label(g_engine, "refresh_label", 35, 380, "Refresh:");
    g_refresh_sl  = yetty_ygui_old_engine_slider(g_engine, "refresh_rate", 110, 378, 180, 22, 0.5f, 5.0f, 1.0f);
    g_refresh_val = yetty_ygui_old_engine_label(g_engine, "refresh_val", 300, 380, "1.0s");
    yetty_ygui_old_widget_slider_on_change(g_refresh_sl, on_refresh_rate, NULL);

    /* === Info === */
    struct yetty_ygui_old_widget* info = yetty_ygui_old_engine_panel(g_engine, "info_panel", 410, 250, 370, 180);
    yetty_ygui_old_widget_set_bg_color(info, panel_bg);
    yetty_ygui_old_engine_label(g_engine, "info_title", 425, 265, "System Info");
    yetty_ygui_old_engine_label(g_engine, "host_label",   425, 300, "Host: workstation");
    yetty_ygui_old_engine_label(g_engine, "os_label",     425, 330, "OS: Linux 6.8");
    yetty_ygui_old_engine_label(g_engine, "uptime_label", 425, 360, "Uptime: 3d 12h 45m");

    struct yetty_ygui_old_widget* apply = yetty_ygui_old_engine_button(g_engine, "apply", 550, 395, 100, 35, "Apply");
    struct yetty_ygui_old_widget* quit  = yetty_ygui_old_engine_button(g_engine, "quit",  665, 395, 100, 35, "Quit");
    yetty_ygui_old_widget_button_on_click(apply, on_apply, NULL);
    yetty_ygui_old_widget_button_on_click(quit,  on_quit,  NULL);

    yetty_ygui_old_engine_label(g_engine, "footer", 30, 510, "Dashboard v1.0");

    yetty_ygui_old_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_old_engine_run(g_engine);

    yetty_ygui_old_engine_destroy(g_engine);
    yetty_ygui_old_shutdown();
    return 0;
}
