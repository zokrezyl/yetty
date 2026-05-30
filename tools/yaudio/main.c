/*
 * tools/yaudio/main.c - audio analyzer GUI.
 *
 * Opens a window via yinit_run, hands the yinit_runtime to
 * yetty_yframework_create for the standard adapter / device / queue /
 * allocator / msdf / event-loop / render-target bring-up (same code
 * path the yetty terminal uses), then attaches a yui with a yplot
 * widget showing the RMS envelope and Prev/Next buttons that pan
 * across the detected noise intervals.
 *
 * Render loop: drain input pipe → update yplot view if selection
 * changed → clear target → yui_render → present.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yaudio/wav.h>
#include <yetty/yaudio/envelope.h>
#include <yetty/yaudio/intervals.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/ycore/types.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>

#include <yetty/yplot/yplot.h>
#include <yetty/ygui/ygui.h>

/* yui.h lives in src/yetty/yui/yui.h — already on the -I src path. */
#include <yetty/yui/yui.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WAVE_BUCKETS    = 4096,           /* per-frame bucket count for envelope */
    WAVE_DEC_CAP    = 2 * WAVE_BUCKETS, /* worst case = (min,max) pair per bucket */
};

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct yaudio_app {
    const char *wav_path;
    struct yetty_yaudio_wav       *wav;
    struct yetty_yaudio_envelope  *env;
    struct yetty_yaudio_intervals *iv;
    float                         *env_dbfs;  /* env->rms in dBFS, for plotting */
    size_t                         env_dbfs_n;
    int    selected;

    /* Generic GPU/event/render bring-up — owned here, lives for the
     * lifetime of the worker. yui borrows everything through ctx.runtime. */
    struct yetty_yframework         *runtime;
    struct yetty_context           ctx;

    /* Render-target alias for the loop (= runtime->render_target). */
    struct yetty_ydraw_target     *render_target;

    /* Bound to runtime->event_loop in yaudio_worker via
     * yetty_yevent_register_default_listeners. Receives RENDER, RESIZE,
     * MOUSE_*, KEY_*, SHUTDOWN, etc. */
    struct yetty_yevent_event_listener listener;

    struct yetty_yui              *yui;
    struct yetty_ygui_object      *plot_widget;        /* energy envelope (dBFS) */
    struct yetty_ygui_object      *wave_widget;        /* raw waveform of zoomed window */
    struct yetty_ygui_object      *plots_vbox;         /* flex column holding both plots + label */
    struct yetty_ygui_object      *prev_btn;
    struct yetty_ygui_object      *next_btn;
    struct yetty_ygui_object      *status_label;

    /* Scratch buffers for the waveform widget.
     *
     *   wave_raw    — temporary scratch for one bucket of samples read
     *                 from the WAV (bounded by max-bucket-size).
     *   wave_dec    — buffer handed to yplot. Two modes:
     *                   * wide zoom (span > WAVE_BUCKETS samples):
     *                     min/max envelope, interleaved as
     *                     [min_0, max_0, min_1, max_1, ...] → 2 floats
     *                     per bucket. yplot's linear-interp shader then
     *                     draws a continuous zigzag envelope.
     *                   * tight zoom (span ≤ WAVE_BUCKETS samples):
     *                     raw samples directly (one per output point),
     *                     so individual cycles are visible.
     *   wave_dec_n  — actual sample count written; passed as buffer
     *                 count to yplot. */
    float  *wave_raw;
    size_t  wave_raw_cap;
    float  *wave_dec;
    size_t  wave_dec_n;

    /* Current view window into the file. Updated by Prev/Next clicks
     * AND mouse-wheel events (plain=scroll, ctrl=amp zoom, ctrl+shift=
     * time zoom). apply_view() pushes this into both yplot widgets. */
    double   view_t_min;
    double   view_t_max;
    float    wave_y_max;          /* waveform amplitude clamp (zooms y of wave plot) */

    /* Click-drag pan state. Captured on MOUSE_DOWN inside the plots
     * vbox; consumed by MOUSE_DRAG / MOUSE_MOVE while held. Anchoring
     * the view to drag-start values (not incremental dx) avoids drift
     * from accumulated rounding. */
    int      dragging;
    float    drag_start_mx;       /* pixel x at MOUSE_DOWN */
    double   drag_start_t_min;
    double   drag_start_t_max;
    float    drag_plot_w;         /* plots-vbox width in pixels (fixed during a drag) */

    /* View-changed flag. Set by drag / wheel / Prev / Next so the
     * (expensive) waveform re-decimation runs at most once per render
     * frame. Without this, each high-frequency mouse-move event would
     * drive a full mmap walk and the worker thread would never reach
     * the render call — freezing the UI. */
    int      view_dirty;
};

#define MOD_SHIFT 0x0001u
#define MOD_CTRL  0x0002u

/* ----------------------------------------------------------------------- */
/* Loading                                                                  */
/* ----------------------------------------------------------------------- */

static struct yetty_ycore_void_result
yaudio_load(struct yaudio_app *app)
{
    struct yetty_yaudio_wav_ptr_result wr = yetty_yaudio_wav_open(app->wav_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yaudio: open WAV failed");
    app->wav = wr.value;

    struct yetty_yaudio_envelope_ptr_result er =
        yetty_yaudio_envelope_create(app->wav, 0, 0, 0);
    if (YETTY_IS_ERR(er)) {
        yetty_yaudio_wav_close(app->wav);
        app->wav = NULL;
        return YETTY_ERR(yetty_ycore_void, "yaudio: envelope failed", er);
    }
    app->env = er.value;

    struct yetty_yaudio_intervals_ptr_result ir =
        yetty_yaudio_intervals_find(app->wav, 0, NULL);
    if (YETTY_IS_ERR(ir)) {
        yetty_yaudio_envelope_destroy(app->env);
        app->env = NULL;
        yetty_yaudio_wav_close(app->wav);
        app->wav = NULL;
        return YETTY_ERR(yetty_ycore_void, "yaudio: intervals failed", ir);
    }
    app->iv = ir.value;

    /* Convert the linear-RMS envelope to dBFS for display. yplot's
     * y-axis maps linearly, so a linear-RMS envelope (range ~1e-4
     * to ~0.5) collapses into the bottom row of pixels. dBFS spreads
     * the dynamic range across the plot. Floor at -90 dBFS so the
     * log doesn't explode on perfect-silence frames. */
    app->env_dbfs_n = app->env->n;
    app->env_dbfs   = malloc(app->env_dbfs_n * sizeof(float));
    if (!app->env_dbfs) {
        return YETTY_ERR(yetty_ycore_void, "yaudio: env_dbfs alloc failed");
    }
    for (size_t i = 0; i < app->env_dbfs_n; i++) {
        float v = app->env->rms[i];
        if (v < 1e-5f) v = 1e-5f;
        app->env_dbfs[i] = (float)(20.0 * log10((double)v));
    }

    yinfo("yaudio: loaded %s — %.1f s, %zu intervals",
          app->wav_path,
          (double)app->wav->frames / (double)app->wav->sample_rate, app->iv->n);
    return YETTY_OK_VOID();
}

/* ----------------------------------------------------------------------- */
/* Widget layout + interval navigation                                      */
/* ----------------------------------------------------------------------- */

/* Build a waveform view for [t_min..t_max] into app->wave_dec. Two
 * branches keep the curve "looking like a waveform" at every zoom:
 *
 *   • Tight zoom (span ≤ WAVE_BUCKETS samples): read the raw samples
 *     directly. yplot's linear-interp draws the actual cycle shape.
 *   • Wide zoom (span > WAVE_BUCKETS): min/max envelope. Each bucket
 *     produces two output points (min, max), so the rendered line
 *     traces the actual sample range — no aliasing where adjacent
 *     buckets pick alternating-sign peaks. */
static int load_waveform_window(struct yaudio_app *app, double t_min, double t_max)
{
    if (!app->wav || t_max <= t_min) return 0;
    if (t_min < 0.0) t_min = 0.0;
    double dur_total = (double)app->wav->frames / (double)app->wav->sample_rate;
    if (t_max > dur_total) t_max = dur_total;

    double sr    = (double)app->wav->sample_rate;
    size_t f_min = (size_t)(t_min * sr);
    size_t f_max = (size_t)(t_max * sr);
    if (f_max <= f_min) return 0;
    size_t span = f_max - f_min;

    /* ---- Tight zoom: raw samples straight into wave_dec. ---- */
    if (span <= (size_t)WAVE_BUCKETS) {
        struct yetty_ycore_size_result rr = yetty_yaudio_wav_read_channel_f32(
            app->wav, 0, f_min, app->wave_dec, span);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            app->wave_dec_n = 0;
            return 0;
        }
        app->wave_dec_n = rr.value;
        return app->wave_dec_n >= 2;
    }

    /* ---- Wide zoom: min/max envelope per bucket. ---- */
    size_t buckets    = WAVE_BUCKETS;
    size_t max_bucket = (span + buckets - 1) / buckets + 1;
    if (max_bucket < 16) max_bucket = 16;
    if (max_bucket > app->wave_raw_cap) {
        float *nb = realloc(app->wave_raw, max_bucket * sizeof(float));
        if (!nb) {
            yerror("yaudio: wave_raw realloc(%zu) failed", max_bucket);
            return 0;
        }
        app->wave_raw     = nb;
        app->wave_raw_cap = max_bucket;
    }

    for (size_t i = 0; i < buckets; i++) {
        size_t bs = f_min + (span * i)       / buckets;
        size_t be = f_min + (span * (i + 1)) / buckets;
        if (be <= bs) be = bs + 1;
        if (be > f_max) be = f_max;
        size_t n = (be > bs) ? (be - bs) : 0;
        float mn = 0.0f, mx = 0.0f;
        if (n > 0) {
            struct yetty_ycore_size_result rr = yetty_yaudio_wav_read_channel_f32(
                app->wav, 0, bs, app->wave_raw, n);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            } else if (rr.value > 0) {
                size_t got = rr.value;
                mn = mx = app->wave_raw[0];
                for (size_t k = 1; k < got; k++) {
                    float v = app->wave_raw[k];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
            }
        }
        app->wave_dec[2 * i]     = mn;
        app->wave_dec[2 * i + 1] = mx;
    }
    app->wave_dec_n = 2 * buckets;
    return 1;
}

static void update_status_label(struct yaudio_app *app)
{
    char buf[192];
    if (app->iv->n == 0) {
        snprintf(buf, sizeof(buf), "no intervals detected");
    } else {
        const struct yetty_yaudio_interval *it = &app->iv->items[app->selected];
        snprintf(buf, sizeof(buf),
                 "interval %d / %zu   start %.3f s   end %.3f s   "
                 "dur %.3f s   peak %.1f dBFS",
                 app->selected + 1, app->iv->n,
                 it->start_sec, it->end_sec,
                 it->end_sec - it->start_sec,
                 (double)it->peak_dbfs);
    }
    if (app->status_label) {
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(app->status_label, buf));
    }
    if (app->yui) {
        yetty_yui_set_status_right(app->yui, buf);
    }
    yinfo("status: %s", buf);
}

static void clamp_view(struct yaudio_app *app)
{
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    double span = app->view_t_max - app->view_t_min;
    if (span < 0.001) span = 0.001;       /* never go below 1 ms */
    if (span > dur)   span = dur;
    if (app->view_t_min < 0.0) {
        app->view_t_min = 0.0;
        app->view_t_max = span;
    }
    if (app->view_t_max > dur) {
        app->view_t_max = dur;
        app->view_t_min = dur - span;
        if (app->view_t_min < 0.0) app->view_t_min = 0.0;
    }
    if (app->wave_y_max < 0.001f) app->wave_y_max = 0.001f;
    if (app->wave_y_max > 1.0f)   app->wave_y_max = 1.0f;
}

/* Cheap path — mark the view dirty and update the status label. Heavy
 * waveform decimation is deferred to flush_view_if_dirty(), which the
 * render loop calls once per frame. Drag/wheel handlers use this so a
 * burst of MOUSE_MOVE events doesn't choke the worker. */
static void request_view_update(struct yaudio_app *app)
{
    if (!app->wav) return;
    clamp_view(app);
    update_status_label(app);
    app->view_dirty = 1;
}

static void apply_view(struct yaudio_app *app)
{
    if (!app->wav) return;
    clamp_view(app);
    update_status_label(app);
    app->view_dirty = 0;

    double t_min = app->view_t_min;
    double t_max = app->view_t_max;

    /* Envelope plot — slice of env_dbfs spanning [t_min..t_max]. */
    if (app->plot_widget) {
        double hop_sec = (double)app->env->hop_samples / (double)app->env->sample_rate;
        size_t i_min   = (size_t)(t_min / hop_sec);
        size_t i_max   = (size_t)(t_max / hop_sec);
        if (i_max > app->env_dbfs_n) i_max = app->env_dbfs_n;
        if (i_min >= i_max) i_min = i_max > 0 ? i_max - 1 : 0;

        struct yetty_ygui_yplot_config pc = {0};
        pc.x_min = (float)t_min;
        pc.x_max = (float)t_max;
        pc.y_min = -90.0f;
        pc.y_max = 0.0f;
        struct yetty_yplot_buffer_input bi = {
            .samples = app->env_dbfs + i_min,
            .count   = i_max - i_min,
            .color   = 0,
        };
        yetty_ycore_error_destroy_safe(
            yetty_ygui_yplot_set_buffers(app->plot_widget, NULL, 0, &bi, 1, &pc));
    }

    /* Waveform plot — freshly streamed from the mmap and decimated. */
    if (app->wave_widget && app->wave_dec) {
        if (load_waveform_window(app, t_min, t_max)) {
            struct yetty_ygui_yplot_config wpc = {0};
            wpc.x_min = (float)t_min;
            wpc.x_max = (float)t_max;
            wpc.y_min = -(float)app->wave_y_max;
            wpc.y_max =  (float)app->wave_y_max;
            struct yetty_yplot_buffer_input wbi = {
                .samples = app->wave_dec,
                .count   = app->wave_dec_n,
                .color   = 0xFFE0E5E4u,
            };
            yetty_ycore_error_destroy_safe(
                yetty_ygui_yplot_set_buffers(app->wave_widget, NULL, 0, &wbi, 1, &wpc));
        }
    }
}

static void recenter_plot_on_selected(struct yaudio_app *app)
{
    if (!app->plot_widget || app->iv->n == 0) return;

    /* Preserve the user's current zoom: keep the span, just translate
     * the view so the selected interval's start_sec lands at the left
     * edge. clamp_view (called from request_view_update) handles file
     * edges. */
    const struct yetty_yaudio_interval *it = &app->iv->items[app->selected];
    double span = app->view_t_max - app->view_t_min;
    if (span <= 0.0) {
        /* No prior zoom yet (e.g. degenerate startup state) — fall back
         * to an 8×interval window so the user sees context, not noise. */
        double iv_d = it->end_sec - it->start_sec;
        span = iv_d * 8.0;
        if (span < 30.0) span = 30.0;
    }
    app->view_t_min = it->start_sec;
    app->view_t_max = it->start_sec + span;
    request_view_update(app);
}

/* Mouse-wheel routing:
 *   plain wheel        — scroll in time (pan)
 *   Ctrl + wheel       — zoom amplitude (waveform y range)
 *   Ctrl+Shift + wheel — zoom in time (around current center) */
static void on_wheel(struct yaudio_app *app, float dy, int mods)
{
    if (dy == 0.0f) return;
    double span = app->view_t_max - app->view_t_min;
    if (span <= 0.0) return;

    int ctrl  = (mods & MOD_CTRL)  != 0;
    int shift = (mods & MOD_SHIFT) != 0;

    if (ctrl && shift) {
        /* Time zoom: dy > 0 → zoom in. */
        double factor = exp(-(double)dy * 0.2);
        double mid    = 0.5 * (app->view_t_min + app->view_t_max);
        double new_sp = span * factor;
        app->view_t_min = mid - 0.5 * new_sp;
        app->view_t_max = mid + 0.5 * new_sp;
    } else if (ctrl) {
        /* Amplitude zoom on the waveform. dy > 0 → zoom in (smaller y range). */
        double factor = exp(-(double)dy * 0.2);
        app->wave_y_max = (float)((double)app->wave_y_max * factor);
    } else {
        /* Pan: dy > 0 → scroll BACK in time. */
        double shift_t = -(double)dy * span * 0.1;
        app->view_t_min += shift_t;
        app->view_t_max += shift_t;
    }
    request_view_update(app);
}

static struct yetty_ycore_void_result on_prev_click(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *w, void *userdata)
{
    (void)ctx;
    (void)w;
    struct yaudio_app *app = userdata;
    if (app->iv->n == 0) return YETTY_OK_VOID();
    app->selected = app->selected == 0 ? (int)app->iv->n - 1 : app->selected - 1;
    recenter_plot_on_selected(app);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_next_click(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *w, void *userdata)
{
    (void)ctx;
    (void)w;
    struct yaudio_app *app = userdata;
    if (app->iv->n == 0) return YETTY_OK_VOID();
    app->selected = (app->selected + 1) % (int)app->iv->n;
    recenter_plot_on_selected(app);
    return YETTY_OK_VOID();
}

static void build_widgets(struct yaudio_app *app, struct yetty_yinit_runtime *rt)
{
    struct yetty_ygui_runtime *engine = yetty_yui_engine(app->yui);
    if (!engine) {
        yerror("yaudio: yui engine is NULL — yui allocation failed");
        return;
    }
    struct yetty_ygui_object *root = yetty_ygui_framework_root(engine);
    if (!root) {
        yerror("yaudio: yui engine has no root");
        return;
    }

    float W = (float)rt->surface_width;
    float H = (float)rt->surface_height;
    float top         = 36.0f;     /* room for yui's tabbar */
    float sb_h        = yetty_yui_statusbar_height(app->yui);
    float btn_strip_h = 48.0f;
    float plots_h     = H - top - btn_strip_h - sb_h - 16.0f;
    float plots_w     = W - 32.0f;

    /* Initial plot config — full duration. */
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    app->view_t_min  = 0.0;
    app->view_t_max  = dur;
    app->wave_y_max  = 1.0f;

    /* Flex-column container hosts the two plots + the status label that
     * sits between them. ygui handles the gap automatically — the gap
     * value is what leaves room for axis labels / inline text between
     * the plot widgets. Padding is zero; gap eats all the inter-row
     * space. */
    /* Flex-column container hosting the two plots + the status label.
     * Absolutely positioned over the workspace area (below yui's
     * titlebar, above its statusbar + the button strip). */
    struct yetty_ygui_object *col = NULL;
    {
        struct yetty_ygui_object_ptr_result cr =
            yetty_ygui_add(yetty_ygui_vbox_class_get().value, root);
        if (YETTY_IS_OK(cr)) {
            col = cr.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
                col, "display:flex; flex-direction:column; padding:0 0 0 0; gap:28;"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(col, 16.0f, top));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(col, plots_w, plots_h));
        } else {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    app->plots_vbox = col;

    struct yetty_ygui_yplot_config pc = {0};
    pc.x_min = (float)app->view_t_min;
    pc.x_max = (float)app->view_t_max;
    pc.y_min = -90.0f;
    pc.y_max = 0.0f;
    struct yetty_yplot_buffer_input bi = {
        .samples = app->env_dbfs,
        .count   = app->env_dbfs_n,
        .color   = 0,
    };
    if (col) {
        struct yetty_ygui_object_ptr_result pr =
            yetty_ygui_add(yetty_ygui_yplot_class_get().value, col);
        if (YETTY_IS_OK(pr)) {
            app->plot_widget = pr.value;
            yetty_ycore_error_destroy_safe(
                yetty_ygui_yplot_set_buffers(app->plot_widget, NULL, 0, &bi, 1, &pc));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_apply_css(app->plot_widget, "flex:1 1 0; align-self:stretch;"));
        } else {
            yetty_ycore_error_destroy(pr.error);
            yerror("yaudio: yplot widget create failed");
        }
    }

    /* Status label — between the two plots, inside the same vbox so it
     * gets the same gap treatment automatically. */
    if (col) {
        struct yetty_ygui_object_ptr_result lr =
            yetty_ygui_add(yetty_ygui_label_class_get().value, col);
        if (YETTY_IS_OK(lr)) {
            app->status_label = lr.value;
            yetty_ycore_error_destroy_safe(
                yetty_ygui_label_set_text(app->status_label, "(loading…)"));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_label_set_font_size(app->status_label, 18.0f));
        } else {
            yetty_ycore_error_destroy(lr.error);
        }
    }

    /* Raw-waveform plot directly under the envelope (with the vbox gap).
     * Initial data: a min/max envelope of the WHOLE file. */
    app->wave_dec = calloc(WAVE_DEC_CAP, sizeof(float));
    if (app->wave_dec) {
        load_waveform_window(app, 0.0, dur);
    }
    struct yetty_ygui_yplot_config wpc = {0};
    wpc.x_min = 0.0f;
    wpc.x_max = (float)dur;
    wpc.y_min = -1.0f;
    wpc.y_max =  1.0f;
    struct yetty_yplot_buffer_input wbi = {
        .samples = app->wave_dec,
        .count   = app->wave_dec_n,
        .color   = 0xFFE0E5E4u,
    };
    if (col) {
        struct yetty_ygui_object_ptr_result wr =
            yetty_ygui_add(yetty_ygui_yplot_class_get().value, col);
        if (YETTY_IS_OK(wr)) {
            app->wave_widget = wr.value;
            yetty_ycore_error_destroy_safe(
                yetty_ygui_yplot_set_buffers(app->wave_widget, NULL, 0, &wbi, 1, &wpc));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_apply_css(app->wave_widget, "flex:1 1 0; align-self:stretch;"));
        } else {
            yetty_ycore_error_destroy(wr.error);
            yerror("yaudio: yplot wave widget create failed");
        }
    }

    /* Buttons live above yui's statusbar; positioned absolutely. */
    float btn_w = 120.0f;
    float btn_h = 36.0f;
    float btn_y = H - sb_h - btn_strip_h + 6.0f;
    {
        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_button_class_get().value, root);
        if (YETTY_IS_OK(br)) {
            app->prev_btn = br.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(app->prev_btn, "◀ Prev"));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(app->prev_btn, 16.0f, btn_y));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_size(app->prev_btn, btn_w, btn_h));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(app->prev_btn, on_prev_click, app));
        } else {
            yetty_ycore_error_destroy(br.error);
        }
    }
    {
        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_button_class_get().value, root);
        if (YETTY_IS_OK(br)) {
            app->next_btn = br.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(app->next_btn, "Next ▶"));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(app->next_btn, 16.0f + btn_w + 12.0f, btn_y));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_size(app->next_btn, btn_w, btn_h));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(app->next_btn, on_next_click, app));
        } else {
            yetty_ycore_error_destroy(br.error);
        }
    }

    char status[160];
    snprintf(status, sizeof(status), "%s  •  %.1f s  •  %zu intervals",
             app->wav_path, dur, app->iv->n);
    yetty_yui_set_status_left(app->yui, status);

    update_status_label(app);
}

/* ----------------------------------------------------------------------- */
/* Input handling                                                           */
/* ----------------------------------------------------------------------- */

/* Registered on runtime->event_loop via yetty_yevent_register_default_listeners
 * — the libuv event loop reads events off the platform_input_pipe and
 * dispatches them here. RENDER does the per-frame work; SHUTDOWN /
 * WINDOW_CLOSE / Escape stop the loop so yaudio_worker returns. */
static struct yetty_ycore_int_result
yaudio_event_handler(struct yetty_yevent_event_listener *listener,
                     const struct yetty_yui_event *ev)
{
    struct yaudio_app *app = container_of(listener, struct yaudio_app, listener);

    /* WINDOW_REFRESH (X11 Expose / uncover): tell the damage-aware target
     * to mark every tile dirty so the next render actually re-blits, then
     * fall through to the normal RENDER path. */
    if (ev->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (app->render_target && app->render_target->ops->refresh_full) {
            app->render_target->ops->refresh_full(app->render_target);
        }
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return yaudio_event_handler(listener, &re);
    }

    if (ev->type == YETTY_YCORE_RENDER) {
        if (!app->render_target) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        /* Back-pressure: skip while x11-tile / vnc target is still
         * flushing the previous frame. Same gating yetty.c uses. */
        if (app->render_target->ops->is_busy &&
            app->render_target->ops->is_busy(app->render_target)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (app->view_dirty) {
            apply_view(app);
        }
        struct yetty_ycore_void_result cl =
            app->render_target->ops->clear(app->render_target);
        if (YETTY_IS_ERR(cl)) yetty_ycore_error_destroy(cl.error);

        struct yetty_ycore_void_result rr =
            yetty_yui_render(app->yui, app->render_target);
        if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);

        struct yetty_ycore_void_result pp =
            app->render_target->ops->present(app->render_target);
        if (YETTY_IS_ERR(pp)) yetty_ycore_error_destroy(pp.error);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (app->runtime && app->runtime->event_loop &&
            app->runtime->event_loop->ops->stop) {
            app->runtime->event_loop->ops->stop(app->runtime->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE:
        /* Mirror the new size into the runtime + texture target so
         * surface present uses the live framebuffer dims. */
        yetty_yframework_reconfigure_surface(app->runtime,
                                           (uint32_t)ev->resize.width,
                                           (uint32_t)ev->resize.height);
        if (app->render_target && app->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {
                0, 0, ev->resize.width, ev->resize.height
            };
            app->render_target->ops->resize(app->render_target, vp);
        }
        if (app->yui) {
            yetty_yui_resize(app->yui,
                             (uint32_t)ev->resize.width,
                             (uint32_t)ev->resize.height);
        }
        break;
    case YETTY_YCORE_MOUSE_SCROLL:
        on_wheel(app, ev->mouse_scroll.dy, ev->mouse_scroll.mods);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_DOWN:
        if (ev->mouse.button == 0 && app->plots_vbox && app->wav) {
            struct yetty_ycore_rectangle box = yetty_ygui_widget_rect(app->plots_vbox);
            if (ev->mouse.x >= box.min.x && ev->mouse.x <= box.max.x &&
                ev->mouse.y >= box.min.y && ev->mouse.y <= box.max.y &&
                box.max.x > box.min.x) {
                app->dragging         = 1;
                app->drag_start_mx    = ev->mouse.x;
                app->drag_start_t_min = app->view_t_min;
                app->drag_start_t_max = app->view_t_max;
                app->drag_plot_w      = box.max.x - box.min.x;
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        break;
    case YETTY_YCORE_MOUSE_DRAG:
    case YETTY_YCORE_MOUSE_MOVE:
        if (app->dragging && app->drag_plot_w > 0.0f) {
            double dx   = (double)(ev->mouse.x - app->drag_start_mx);
            double span = app->drag_start_t_max - app->drag_start_t_min;
            /* Drag right → content under cursor follows the mouse →
             * view shifts LEFT in time. Hence the minus. */
            double dt = -dx * span / (double)app->drag_plot_w;
            app->view_t_min = app->drag_start_t_min + dt;
            app->view_t_max = app->drag_start_t_max + dt;
            request_view_update(app);
            return YETTY_OK(yetty_ycore_int, 1);
        }
        break;
    case YETTY_YCORE_MOUSE_UP:
        if (app->dragging) {
            app->dragging = 0;
            return YETTY_OK(yetty_ycore_int, 1);
        }
        break;
    case YETTY_YCORE_KEY_DOWN:
        if (ev->key.key == 256 || ev->key.key == 81) {
            if (app->runtime && app->runtime->event_loop &&
                app->runtime->event_loop->ops->stop) {
                app->runtime->event_loop->ops->stop(app->runtime->event_loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ev->key.key == 262) {
            yetty_ycore_error_destroy_safe(on_next_click(NULL, NULL, app));
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ev->key.key == 263) {
            yetty_ycore_error_destroy_safe(on_prev_click(NULL, NULL, app));
            return YETTY_OK(yetty_ycore_int, 1);
        }
        break;
    default:
        break;
    }

    /* Forward everything else (mouse down/up/move, char, key up, ...)
     * to yui so its ygui engine can hit-test widgets and fire click
     * callbacks. Without this, Prev/Next buttons receive no input. */
    if (app->yui) {
        (void)yetty_yui_on_event(app->yui, ev);
    }

    /* Schedule a re-render. Any input event might have changed yui's
     * widget state (hover, focus, button press) or our view (drag,
     * wheel, prev/next); the cheapest correct policy is to always
     * follow up with a RENDER. The x11-tile target's tile-diff makes
     * unchanged frames near-free. */
    if (app->runtime && app->runtime->event_loop &&
        app->runtime->event_loop->ops->request_render) {
        app->runtime->event_loop->ops->request_render(app->runtime->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* ----------------------------------------------------------------------- */
/* Render loop                                                              */
/* ----------------------------------------------------------------------- */

static struct yetty_ycore_void_result
yaudio_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct yaudio_app *app = user;

    struct yetty_ycore_void_result lr = yaudio_load(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "yaudio: load failed");

    /* Standard GPU/event/render-target bring-up. Same call yetty's own
     * main uses — adapter, device, queue, allocator, msdf generator,
     * surface configuration, event loop, render target. The runtime
     * owns all of it; we just borrow via app->ctx.runtime->X. */
    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yrt_res, "yaudio: yframework_create failed");
    app->runtime           = yrt_res.value;
    app->ctx.runtime       = app->runtime;
    app->ctx.pty_factory   = NULL;          /* yaudio has no terminal */
    app->ctx.event_loop    = app->runtime->event_loop;
    app->render_target     = app->runtime->render_target;

    /* yui — owns the scene-canvas + ygui engine. cell_w/h are mostly
     * arbitrary for our use; pick the same defaults yui's tabbar uses. */
    struct yetty_yui_ptr_result yr =
        yetty_yui_create(&app->ctx, rt->surface_width, rt->surface_height, 8.0f, 16.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yui_create failed");
    app->yui = yr.value;

    build_widgets(app, rt);

    /* Wire up our event handler on the runtime's libuv event loop. The
     * loop drives the platform_input_pipe; events dispatch into
     * yaudio_event_handler, which renders frames on YETTY_YCORE_RENDER
     * and stops the loop on SHUTDOWN / WINDOW_CLOSE / Esc. */
    app->listener.handler = yaudio_event_handler;
    struct yetty_ycore_void_result rel = yetty_yevent_register_default_listeners(
        app->runtime->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "register_default_listeners failed");

    /* Kick the first frame. ymain/glfw.c posts an initial RESIZE before
     * calling the worker; libuv will dispatch it once the loop starts. */
    yetty_yevent_post_async(rt->platform_input_pipe,
                            &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    /* Run the libuv loop until SHUTDOWN. Replaces the old hand-rolled
     * poll() / drain pipe / render / present loop — same outcome
     * (input-driven render coalesced via libuv's idle handle) but no
     * duplicated machinery and the x11-tile target's tile-diff readback
     * callbacks now actually fire. */
    struct yetty_ycore_void_result run_res =
        app->runtime->event_loop->ops->start(app->runtime->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        ywarn("yaudio: event_loop start returned error: %s", run_res.error.msg);
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown: yui first (it holds GPU resources owned by the runtime),
     * then the runtime (render target, msdf, allocator, event loop,
     * surface, queue, device, adapter, in reverse-creation order). */
    if (app->yui)     yetty_yui_destroy(app->yui);
    if (app->runtime) yetty_yframework_destroy(app->runtime);

    yetty_yaudio_intervals_destroy(app->iv);
    yetty_yaudio_envelope_destroy(app->env);
    free(app->env_dbfs);
    free(app->wave_raw);
    free(app->wave_dec);
    yetty_yaudio_wav_close(app->wav);
    return YETTY_OK_VOID();
}

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s <file.wav>\n"
        "\n"
        "Opens an audio analyzer window for a WAV file. Shows the RMS\n"
        "envelope as a yplot; Prev/Next buttons (and ←/→) walk the\n"
        "detected noise intervals.\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *wav_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (argv[i][0] != '-' && !wav_path) wav_path = argv[i];
    }
    if (!wav_path) {
        usage(stderr, argv[0]);
        return 2;
    }

    struct yaudio_app app = {.wav_path = wav_path};
    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    return yetty_yinit_run(argc, argv, &cfg, yaudio_worker, &app);
}
