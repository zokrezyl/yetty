/*
 * Demo 12: Simple Calculator — grid of buttons over a display label.
 * Ported from yetty-poc/demo/assets/ygui-c/python/07_calculator.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ygui.h"

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_display = NULL;

static char  g_current[64]  = "0";
static char  g_previous[64] = "";
static int   g_has_previous = 0;
static char  g_operator     = 0;   /* 0 = none, otherwise '+' '-' 'x' '/' */
static int   g_new_number   = 1;

static void update_display(void) {
    yetty_ygui_widget_label_set_text(g_display, g_current);
}

static void format_result(double v) {
    if (v == (double)(long long)v) {
        snprintf(g_current, sizeof(g_current), "%lld", (long long)v);
    } else {
        snprintf(g_current, sizeof(g_current), "%.6g", v);
    }
}

static void calculate(void) {
    if (!g_has_previous || g_operator == 0) return;
    double prev = strtod(g_previous, NULL);
    double curr = strtod(g_current,  NULL);
    double r = curr;
    switch (g_operator) {
        case '+': r = prev + curr; break;
        case '-': r = prev - curr; break;
        case 'x': r = prev * curr; break;
        case '/': r = curr != 0 ? prev / curr : 0; break;
        default: break;
    }
    format_result(r);
    g_has_previous = 0;
    g_operator = 0;
}

static void on_button(struct yetty_ygui_widget* w, void* u) {
    (void)u;
    const char* key = yetty_ygui_widget_button_get_label(w);
    if (!key || !key[0]) return;

    char c = key[0];

    if (c >= '0' && c <= '9') {
        if (g_new_number) {
            g_current[0] = c; g_current[1] = '\0';
            g_new_number = 0;
        } else if (strcmp(g_current, "0") == 0) {
            g_current[0] = c; g_current[1] = '\0';
        } else {
            size_t n = strlen(g_current);
            if (n + 1 < sizeof(g_current)) {
                g_current[n] = c;
                g_current[n + 1] = '\0';
            }
        }
    } else if (c == '.') {
        if (!strchr(g_current, '.')) {
            size_t n = strlen(g_current);
            if (n + 1 < sizeof(g_current)) {
                g_current[n] = '.';
                g_current[n + 1] = '\0';
                g_new_number = 0;
            }
        }
    } else if (c == 'C') {
        strcpy(g_current, "0");
        g_has_previous = 0;
        g_operator = 0;
        g_new_number = 1;
    } else if (strcmp(key, "+/-") == 0) {
        if (g_current[0] == '-') {
            memmove(g_current, g_current + 1, strlen(g_current));
        } else if (strcmp(g_current, "0") != 0) {
            size_t n = strlen(g_current);
            if (n + 2 < sizeof(g_current)) {
                memmove(g_current + 1, g_current, n + 1);
                g_current[0] = '-';
            }
        }
    } else if (c == '%') {
        format_result(strtod(g_current, NULL) / 100.0);
    } else if (c == '+' || c == '-' || c == 'x' || c == '/') {
        if (g_has_previous) calculate();
        snprintf(g_previous, sizeof(g_previous), "%s", g_current);
        g_has_previous = 1;
        g_operator = c;
        g_new_number = 1;
    } else if (c == '=') {
        calculate();
        g_new_number = 1;
    }

    update_display();
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create_with_pixel_hint("calculator", 2, 2, 380.0f, 480.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    g_display = yetty_ygui_engine_label(g_engine, "display", 20, 20, "0");

    static const char* layout[5][4] = {
        {"C",   "+/-", "%",  "/"},
        {"7",   "8",   "9",  "x"},
        {"4",   "5",   "6",  "-"},
        {"1",   "2",   "3",  "+"},
        {"0",   "0",   ".",  "="},
    };

    const float btn_w = 80, btn_h = 50, margin = 10, start_y = 70;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            /* Skip duplicate '0' slot in last row */
            if (row == 4 && col == 1) continue;

            const char* lbl = layout[row][col];
            float x = 20 + col * (btn_w + margin);
            float y = start_y + row * (btn_h + margin);
            float w = btn_w;
            if (row == 4 && col == 0) w = btn_w * 2 + margin;  /* wide zero */

            char id[32];
            snprintf(id, sizeof(id), "btn_%d_%d", row, col);
            struct yetty_ygui_widget* btn = yetty_ygui_engine_button(g_engine, id, x, y, w, btn_h, lbl);
            yetty_ygui_widget_button_on_click(btn, on_button, NULL);
        }
    }

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
