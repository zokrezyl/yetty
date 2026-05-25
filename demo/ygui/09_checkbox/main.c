/*
 * Demo 09: Checkbox — multiple checkboxes with combined status.
 * Ported from yetty-poc/demo/assets/ygui-c/python/04_checkbox.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_engine* g_engine = NULL;
static struct yetty_ygui_old_widget* g_email = NULL;
static struct yetty_ygui_old_widget* g_sms = NULL;
static struct yetty_ygui_old_widget* g_push = NULL;
static struct yetty_ygui_old_widget* g_status = NULL;

static void update_status(void) {
    char buf[128] = "Enabled: ";
    int first = 1;
    if (yetty_ygui_old_widget_checkbox_get_checked(g_email)) {
        strcat(buf, "Email");
        first = 0;
    }
    if (yetty_ygui_old_widget_checkbox_get_checked(g_sms)) {
        if (!first) strcat(buf, ", ");
        strcat(buf, "SMS");
        first = 0;
    }
    if (yetty_ygui_old_widget_checkbox_get_checked(g_push)) {
        if (!first) strcat(buf, ", ");
        strcat(buf, "Push");
        first = 0;
    }
    if (first) {
        yetty_ygui_old_widget_label_set_text(g_status, "All notifications disabled");
    } else {
        yetty_ygui_old_widget_label_set_text(g_status, buf);
    }
}

static void on_change(struct yetty_ygui_old_widget* w, int checked, void* u) {
    (void)w; (void)checked; (void)u;
    update_status();
}

static void on_key(struct yetty_ygui_old_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_old_engine_stop(e);
}

int main(void) {
    if (yetty_ygui_old_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_old_engine_create((struct yetty_ygui_old_engine_args){.name = "settings"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_old_shutdown(); return 1; }

    yetty_ygui_old_engine_label(g_engine, "title", 30, 20, "Notification Settings");
    g_status = yetty_ygui_old_engine_label(g_engine, "status", 30, 240, "");

    g_email = yetty_ygui_old_engine_checkbox(g_engine, "email", 30, 70,  280, 35, "Email notifications", 1);
    g_sms   = yetty_ygui_old_engine_checkbox(g_engine, "sms",   30, 115, 280, 35, "SMS notifications",   0);
    g_push  = yetty_ygui_old_engine_checkbox(g_engine, "push",  30, 160, 280, 35, "Push notifications",  1);

    yetty_ygui_old_widget_checkbox_on_change(g_email, on_change, NULL);
    yetty_ygui_old_widget_checkbox_on_change(g_sms,   on_change, NULL);
    yetty_ygui_old_widget_checkbox_on_change(g_push,  on_change, NULL);

    update_status();

    yetty_ygui_old_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_old_engine_run(g_engine);

    yetty_ygui_old_engine_destroy(g_engine);
    yetty_ygui_old_shutdown();
    return 0;
}
