/*
 * Demo 09: Checkbox — multiple checkboxes with combined status.
 * Ported from yetty-poc/demo/assets/ygui-c/python/04_checkbox.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_email = NULL;
static ygui_widget_t* g_sms = NULL;
static ygui_widget_t* g_push = NULL;
static ygui_widget_t* g_status = NULL;

static void update_status(void) {
    char buf[128] = "Enabled: ";
    int first = 1;
    if (ygui_checkbox_get_checked(g_email)) {
        strcat(buf, "Email");
        first = 0;
    }
    if (ygui_checkbox_get_checked(g_sms)) {
        if (!first) strcat(buf, ", ");
        strcat(buf, "SMS");
        first = 0;
    }
    if (ygui_checkbox_get_checked(g_push)) {
        if (!first) strcat(buf, ", ");
        strcat(buf, "Push");
        first = 0;
    }
    if (first) {
        ygui_label_set_text(g_status, "All notifications disabled");
    } else {
        ygui_label_set_text(g_status, buf);
    }
}

static void on_change(ygui_widget_t* w, int checked, void* u) {
    (void)w; (void)checked; (void)u;
    update_status();
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("settings", 2, 2, 350.0f, 300.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 30, 20, "Notification Settings");
    g_status = ygui_label(g_engine, "status", 30, 240, "");

    g_email = ygui_checkbox(g_engine, "email", 30, 70,  280, 35, "Email notifications", 1);
    g_sms   = ygui_checkbox(g_engine, "sms",   30, 115, 280, 35, "SMS notifications",   0);
    g_push  = ygui_checkbox(g_engine, "push",  30, 160, 280, 35, "Push notifications",  1);

    ygui_checkbox_on_change(g_email, on_change, NULL);
    ygui_checkbox_on_change(g_sms,   on_change, NULL);
    ygui_checkbox_on_change(g_push,  on_change, NULL);

    update_status();

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
