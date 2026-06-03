/* datepicker.c — compact month calendar. Click the ◂ / ▸ header arrows
 * to change month, click a day cell to select it. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/datepicker.h>
#include <stdio.h>

#define DP_HEADER_H 28.0f
#define DP_CELL_W 28.0f
#define DP_CELL_H 24.0f
#define DP_PAD 6.0f
#define DP_NAV_W 28.0f

/* Brand palette, packed 0xAABBGGRR. */
#define DP_BG 0xFF14100Bu     /* BRAND_BG            (11,16,20)  */
#define DP_BORDER 0xFF474A36u /* BRAND_BORDER        (54,74,71)  */
#define DP_TEXT 0xFFE4E5E0u   /* BRAND_TEXT_PRIMARY  (224,229,228) */
#define DP_MUTED 0xFFA8A79Fu  /* BRAND_TEXT_SECONDARY (159,167,168) */
#define DP_ACCENT 0xFF92A86Bu /* BRAND_ACCENT        (107,168,146) */

struct [[clang::annotate("class@ygui:datepicker")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] dp_data {
    int shown_year;
    int shown_month; /* 0-based */
    int sel_year;
    int sel_month; /* 0-based */
    int sel_day;
};

static const struct yetty_yclass *dp_class(void)
{
    return yetty_ygui_datepicker_class_get().value;
}

static int dp_days_in_month(int year, int m0)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m0 < 0 || m0 > 11) {
        return 30;
    }
    if (m0 != 1) {
        return dim[m0];
    }
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
    return 28 + leap;
}

/* Zeller-style weekday: 0 = Sunday … 6 = Saturday. */
static int dp_weekday_of(int year, int m0, int day)
{
    int m = m0 + 1;
    int y = year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

static const char *dp_month_name(int m0)
{
    static const char *names[] = {"January",   "February", "March",    "April",
                                  "May",       "June",     "July",     "August",
                                  "September", "October",  "November", "December"};
    if (m0 < 0 || m0 > 11) {
        return "?";
    }
    return names[m0];
}

/* Filled box with a 1px border, in one prim. */
static struct yetty_ycore_void_result dp_frame(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                               float w, float h, uint32_t fill, uint32_t stroke,
                                               float stroke_w)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysdf_box g = {.center_x = x + w * 0.5f,
                               .center_y = y + h * 0.5f,
                               .half_width = w * 0.5f,
                               .half_height = h * 0.5f,
                               .corner_radius = 0.0f};
    return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0, 0, fill, stroke, stroke_w,
                                                 &g);
}

[[clang::annotate("override@ygui:datepicker:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, dp_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "datepicker: super");
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(obj, dp_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct dp_data *d = d_dr.value;
    d->shown_year = 2025;
    d->shown_month = 0;
    d->sel_year = -1;
    d->sel_month = -1;
    d->sel_day = -1;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:datepicker:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "datepicker paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(obj, dp_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct dp_data *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float fs = 13.0f;

    struct yetty_ycore_void_result result_131 = dp_frame(ctx, r.min.x, r.min.y, w, h, DP_BG, DP_BORDER, 1.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_131, "dp: frame");

    /* Header: ◂ Month Year ▸ */
    float hdr_ty = r.min.y + (DP_HEADER_H + fs) * 0.5f - 3.0f;
    struct yetty_ycore_void_result result_136 = yguix_text(ctx, "<", r.min.x + DP_PAD, hdr_ty, fs, DP_MUTED);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_136, "dp: prev");
    struct yetty_ycore_void_result result_138 = yguix_text(ctx, ">", r.min.x + w - DP_NAV_W + DP_PAD, hdr_ty, fs, DP_MUTED);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_138, "dp: next");
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s %d", dp_month_name(d->shown_month), d->shown_year);
    struct yetty_ycore_void_result result_143 = yguix_text(ctx, hdr, r.min.x + DP_NAV_W + DP_PAD, hdr_ty, fs, DP_TEXT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_143, "dp: title");

    /* Weekday header row. */
    static const char *wd[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    float grid_x = r.min.x + DP_PAD;
    float row_y = r.min.y + DP_HEADER_H + 4.0f;
    for (int i = 0; i < 7; i++) {
        struct yetty_ycore_void_result result_152 = yguix_text(ctx, wd[i], grid_x + (float)i * DP_CELL_W + 6.0f,
                                       row_y + fs, fs, DP_MUTED);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_152, "dp: weekday");
    }

    /* Day cells. */
    float grid_y = row_y + DP_CELL_H;
    int first_wd = dp_weekday_of(d->shown_year, d->shown_month, 1);
    int dim = dp_days_in_month(d->shown_year, d->shown_month);
    for (int day = 1; day <= dim; day++) {
        int cell = first_wd + day - 1;
        int col = cell % 7;
        int drow = cell / 7;
        float cx = grid_x + (float)col * DP_CELL_W;
        float cy = grid_y + (float)drow * DP_CELL_H;
        int is_sel = (d->sel_year == d->shown_year && d->sel_month == d->shown_month &&
                      d->sel_day == day);
        if (is_sel) {
            struct yetty_ycore_void_result result_171 = yguix_box(ctx, cx, cy, DP_CELL_W, DP_CELL_H, DP_ACCENT, 4.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_171, "dp: sel");
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", day);
        struct yetty_ycore_void_result result_177 = yguix_text(ctx, buf, cx + 7.0f, cy + fs + 2.0f, fs,
                                       is_sel ? DP_BG : DP_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_177, "dp: day");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:datepicker:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)yclass_ctx;
    (void)btn;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(obj, dp_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "on_press: data_get");
    struct dp_data *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float lx = x - r.min.x;
    float ly = y - r.min.y;
    float w = r.max.x - r.min.x;

    /* Header arrows step the shown month. */
    if (ly < DP_HEADER_H) {
        if (lx < DP_NAV_W) {
            if (--d->shown_month < 0) {
                d->shown_month = 11;
                d->shown_year--;
            }
        } else if (lx > w - DP_NAV_W) {
            if (++d->shown_month > 11) {
                d->shown_month = 0;
                d->shown_year++;
            }
        } else {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "dp: dirty", dr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Day grid hit-test. */
    float grid_x = DP_PAD;
    float grid_y = DP_HEADER_H + 4.0f + DP_CELL_H;
    if (ly < grid_y) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int col = (int)((lx - grid_x) / DP_CELL_W);
    int drow = (int)((ly - grid_y) / DP_CELL_H);
    if (col < 0 || col > 6 || drow < 0 || drow > 5) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int first_wd = dp_weekday_of(d->shown_year, d->shown_month, 1);
    int dim = dp_days_in_month(d->shown_year, d->shown_month);
    int day = drow * 7 + col - first_wd + 1;
    if (day < 1 || day > dim) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->sel_year = d->shown_year;
    d->sel_month = d->shown_month;
    d->sel_day = day;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "dp: dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_datepicker_set_date(struct yetty_ygui_object *obj,
                                                              int year, int month_0_based, int day)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "datepicker_set_date: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(obj, dp_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_datepicker_set_date: data_get");
    struct dp_data *d = d_dr.value;
    d->shown_year = year;
    d->shown_month = month_0_based;
    d->sel_year = year;
    d->sel_month = month_0_based;
    d->sel_day = day;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
void yetty_ygui_datepicker_get_date(const struct yetty_ygui_object *obj, int *year,
                                    int *month_0_based, int *day)
{
    int yy = -1, mm = -1, dd = -1;
    if (obj) {
        struct dp_data *d = yetty_ygui_data_get((struct yetty_ygui_object *)obj, dp_class());
        yy = d->sel_year;
        mm = d->sel_month;
        dd = d->sel_day;
    }
    if (year) {
        *year = yy;
    }
    if (month_0_based) {
        *month_0_based = mm;
    }
    if (day) {
        *day = dd;
    }
}

#include "datepicker.gen.c"
