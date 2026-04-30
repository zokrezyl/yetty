/*
 * Demo 15: Dashboard — multi-panel system dashboard.
 * Ported from yetty-poc/demo/assets/ygui-c/python/10_dashboard.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_status = NULL;

static ygui_widget_t* g_cpu_bar  = NULL;
static ygui_widget_t* g_cpu_val  = NULL;
static ygui_widget_t* g_mem_bar  = NULL;
static ygui_widget_t* g_mem_val  = NULL;
static ygui_widget_t* g_net_in   = NULL;
static ygui_widget_t* g_net_out  = NULL;

static ygui_widget_t* g_dark_cb     = NULL;
static ygui_widget_t* g_notif_cb    = NULL;
static ygui_widget_t* g_refresh_sl  = NULL;
static ygui_widget_t* g_refresh_val = NULL;

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static float frand(float lo, float hi) {
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static void set_status(const char* text) {
    ygui_label_set_text(g_status, text);
}

static void on_refresh(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    float cpu = frand(20, 80);
    float mem = frand(40, 85);
    char buf[32];

    ygui_progress_set_value(g_cpu_bar, cpu / 100.0f);
    snprintf(buf, sizeof(buf), "%d%%", (int)cpu);
    ygui_label_set_text(g_cpu_val, buf);

    ygui_progress_set_value(g_mem_bar, mem / 100.0f);
    snprintf(buf, sizeof(buf), "%d%%", (int)mem);
    ygui_label_set_text(g_mem_val, buf);

    snprintf(buf, sizeof(buf), "In: %d KB/s", (int)frand(0, 500));
    ygui_label_set_text(g_net_in, buf);
    snprintf(buf, sizeof(buf), "Out: %d KB/s", (int)frand(0, 200));
    ygui_label_set_text(g_net_out, buf);

    set_status("Refreshed");
}

static void on_cache(ygui_widget_t* w, void* u)    { (void)w; (void)u; set_status("Cache cleared"); }
static void on_restart(ygui_widget_t* w, void* u)  { (void)w; (void)u; set_status("Restarting..."); }
static void on_backup(ygui_widget_t* w, void* u)   { (void)w; (void)u; set_status("Backup started"); }
static void on_logs(ygui_widget_t* w, void* u)     { (void)w; (void)u; set_status("Opening logs"); }
static void on_settings(ygui_widget_t* w, void* u) { (void)w; (void)u; set_status("Settings open"); }

static void on_refresh_rate(ygui_widget_t* w, float value, void* u) {
    (void)w; (void)u;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fs", value);
    ygui_label_set_text(g_refresh_val, buf);
}

static void on_apply(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    char buf[96];
    snprintf(buf, sizeof(buf), "Applied: dark=%d, notif=%d, rate=%.1fs",
             ygui_checkbox_get_checked(g_dark_cb),
             ygui_checkbox_get_checked(g_notif_cb),
             ygui_slider_get_value(g_refresh_sl));
    set_status(buf);
}

static void on_quit(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    ygui_engine_stop(g_engine);
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);
    srand((unsigned)time(NULL));

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("dashboard", 1, 1, 800.0f, 550.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    uint32_t panel_bg = rgba(45, 45, 50, 255);

    ygui_label(g_engine, "header", 30, 15, "System Dashboard");
    g_status = ygui_label(g_engine, "status", 650, 15, "Online");

    /* === System Stats === */
    ygui_widget_t* stats = ygui_panel(g_engine, "stats_panel", 20, 50, 370, 180);
    ygui_widget_set_bg_color(stats, panel_bg);
    ygui_label(g_engine, "stats_title", 35, 65, "System Resources");

    ygui_label(g_engine, "cpu_label", 35, 100, "CPU");
    g_cpu_bar = ygui_progress(g_engine, "cpu_bar", 90, 98, 220, 18, 0.45f);
    g_cpu_val = ygui_label(g_engine, "cpu_value", 320, 100, "45%");

    ygui_label(g_engine, "mem_label", 35, 130, "Memory");
    g_mem_bar = ygui_progress(g_engine, "mem_bar", 90, 128, 220, 18, 0.62f);
    g_mem_val = ygui_label(g_engine, "mem_value", 320, 130, "62%");

    ygui_label(g_engine, "disk_label", 35, 160, "Disk");
    ygui_progress(g_engine, "disk_bar", 90, 158, 220, 18, 0.78f);
    ygui_label(g_engine, "disk_value", 320, 160, "78%");

    ygui_label(g_engine, "net_label", 35, 195, "Network");
    g_net_in  = ygui_label(g_engine, "net_in",  90, 195, "In: 0 KB/s");
    g_net_out = ygui_label(g_engine, "net_out", 200, 195, "Out: 0 KB/s");

    /* === Quick Actions === */
    ygui_widget_t* actions = ygui_panel(g_engine, "actions_panel", 410, 50, 370, 180);
    ygui_widget_set_bg_color(actions, panel_bg);
    ygui_label(g_engine, "actions_title", 425, 65, "Quick Actions");

    ygui_widget_t* refresh = ygui_button(g_engine, "refresh",     425, 95, 100, 35, "Refresh");
    ygui_widget_t* cache   = ygui_button(g_engine, "clear_cache", 540, 95, 100, 35, "Clear Cache");
    ygui_widget_t* restart = ygui_button(g_engine, "restart",     655, 95, 100, 35, "Restart");
    ygui_widget_t* backup  = ygui_button(g_engine, "backup",      425, 145, 100, 35, "Backup");
    ygui_widget_t* logs    = ygui_button(g_engine, "logs",        540, 145, 100, 35, "View Logs");
    ygui_widget_t* set     = ygui_button(g_engine, "settings",    655, 145, 100, 35, "Settings");
    ygui_button_on_click(refresh, on_refresh,  NULL);
    ygui_button_on_click(cache,   on_cache,    NULL);
    ygui_button_on_click(restart, on_restart,  NULL);
    ygui_button_on_click(backup,  on_backup,   NULL);
    ygui_button_on_click(logs,    on_logs,     NULL);
    ygui_button_on_click(set,     on_settings, NULL);

    ygui_checkbox(g_engine, "alerts", 425, 195, 180, 25, "Enable Alerts", 1);

    /* === Settings === */
    ygui_widget_t* settings = ygui_panel(g_engine, "settings_panel", 20, 250, 370, 180);
    ygui_widget_set_bg_color(settings, panel_bg);
    ygui_label(g_engine, "settings_title", 35, 265, "Settings");

    g_dark_cb  = ygui_checkbox(g_engine, "dark_mode",     35, 300, 160, 25, "Dark Mode", 1);
    g_notif_cb = ygui_checkbox(g_engine, "notifications", 35, 335, 160, 25, "Notifications", 1);

    ygui_label(g_engine, "refresh_label", 35, 380, "Refresh:");
    g_refresh_sl  = ygui_slider(g_engine, "refresh_rate", 110, 378, 180, 22, 0.5f, 5.0f, 1.0f);
    g_refresh_val = ygui_label(g_engine, "refresh_val", 300, 380, "1.0s");
    ygui_slider_on_change(g_refresh_sl, on_refresh_rate, NULL);

    /* === Info === */
    ygui_widget_t* info = ygui_panel(g_engine, "info_panel", 410, 250, 370, 180);
    ygui_widget_set_bg_color(info, panel_bg);
    ygui_label(g_engine, "info_title", 425, 265, "System Info");
    ygui_label(g_engine, "host_label",   425, 300, "Host: workstation");
    ygui_label(g_engine, "os_label",     425, 330, "OS: Linux 6.8");
    ygui_label(g_engine, "uptime_label", 425, 360, "Uptime: 3d 12h 45m");

    ygui_widget_t* apply = ygui_button(g_engine, "apply", 550, 395, 100, 35, "Apply");
    ygui_widget_t* quit  = ygui_button(g_engine, "quit",  665, 395, 100, 35, "Quit");
    ygui_button_on_click(apply, on_apply, NULL);
    ygui_button_on_click(quit,  on_quit,  NULL);

    ygui_label(g_engine, "footer", 30, 510, "Dashboard v1.0");

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
