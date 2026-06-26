/*
 * tools/yaudio/main.c - audio analyzer GUI (yclass class `yaudio:app`).
 *
 * Subclass of yapp:app. main() parses the WAV path, then drives the platform
 * bring-up sequence directly (it can't route through the shared ymain entry: the
 * positional path would trip yconfig's unknown-flag handling). The platform
 * hands the runtime to run, which calls yetty_yframework_create for the standard
 * adapter / device / queue / allocator / msdf / event-loop / render-target
 * bring-up (same code path the yetty terminal uses), then attaches a yui with a
 * yplot widget showing the RMS envelope and Prev/Next buttons that pan across
 * the detected noise intervals.
 *
 * Startup is two-staged so the window appears immediately even for a
 * large file:
 *   1. Bring up the runtime + yui and a centred progress bar, then
 *      present that first frame.
 *   2. Run the heavy WAV open / envelope / interval passes on a
 *      yworkpool worker thread. The worker publishes a [0,1] fraction
 *      and posts (throttled) RENDER events so the event loop keeps
 *      repainting the bar; when it finishes, the pool's done() callback
 *      builds the real plot UI back on the loop thread and hides the
 *      loading screen.
 *
 * Render loop: update progress bar while loading (or apply the yplot
 * view if the selection changed) → clear target → yui_render → present.
 */

#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yapp/app.h>
#include <yetty/yclass/class.h>
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
#include <yetty/yplatform/yworkpool.h>
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
    WAVE_BUCKETS = 4096,             /* per-frame bucket count for envelope */
    WAVE_DEC_CAP = 2 * WAVE_BUCKETS, /* worst case = (min,max) pair per bucket */
};

/* Stage of the background load — drives the progress-bar caption and the
 * mapping from each pass's local [0,1] fraction onto the global bar. */
enum yaudio_load_phase {
    YAUDIO_PHASE_OPEN = 0,
    YAUDIO_PHASE_ENVELOPE,
    YAUDIO_PHASE_INTERVALS,
};

/* Outcome the worker hands to done() (on the loop thread). */
enum yaudio_load_state {
    YAUDIO_LOAD_RUNNING = 0,
    YAUDIO_LOAD_OK,
    YAUDIO_LOAD_FAILED,
};

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct YETTY_ANNOTATE("class@yaudio:app") YETTY_ANNOTATE("parent@yapp:app") yetty_yaudio_app {
    const char *wav_path;
    struct yetty_yaudio_wav *wav;
    struct yetty_yaudio_envelope *env;
    struct yetty_yaudio_intervals *iv;
    float *env_dbfs; /* env->rms in dBFS, for plotting */
    size_t env_dbfs_n;
    int selected;

    /* Generic GPU/event/render bring-up — owned here, lives for the
     * lifetime of the worker. yui borrows everything through ctx.runtime. */
    struct yetty_yframework *runtime;
    struct yetty_context ctx;

    /* Render-target alias for the loop (= runtime->render_target). */
    struct yetty_ydraw_target *render_target;

    /* Bound to runtime->event_loop in yaudio_worker via
     * yetty_yevent_register_default_listeners. Receives RENDER, RESIZE,
     * MOUSE_*, KEY_*, SHUTDOWN, etc. */
    struct yetty_yevent_event_listener listener;

    struct yetty_yui *yui;
    struct yetty_yclass_object *plot_widget; /* energy envelope (dBFS) */
    struct yetty_yclass_object *wave_widget; /* raw waveform of zoomed window */
    struct yetty_yclass_object *plots_vbox;  /* flex column holding both plots + label */
    struct yetty_yclass_object *prev_btn;
    struct yetty_yclass_object *next_btn;
    struct yetty_yclass_object *status_label;

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
    float *wave_raw;
    size_t wave_raw_cap;
    float *wave_dec;
    size_t wave_dec_n;

    /* Current view window into the file. Updated by Prev/Next clicks
     * AND mouse-wheel events (plain=scroll, ctrl=amp zoom, ctrl+shift=
     * time zoom). apply_view() pushes this into both yplot widgets. */
    double view_t_min;
    double view_t_max;
    float wave_y_max; /* waveform amplitude clamp (zooms y of wave plot) */

    /* Click-drag pan state. Captured on MOUSE_DOWN inside the plots
     * vbox; consumed by MOUSE_DRAG / MOUSE_MOVE while held. Anchoring
     * the view to drag-start values (not incremental dx) avoids drift
     * from accumulated rounding. */
    int dragging;
    float drag_start_mx; /* pixel x at MOUSE_DOWN */
    double drag_start_t_min;
    double drag_start_t_max;
    float drag_plot_w; /* plots-vbox width in pixels (fixed during a drag) */

    /* View-changed flag. Set by drag / wheel / Prev / Next so the
     * (expensive) waveform re-decimation runs at most once per render
     * frame. Without this, each high-frequency mouse-move event would
     * drive a full mmap walk and the worker thread would never reach
     * the render call — freezing the UI. */
    int view_dirty;

    /* --- Asynchronous load ------------------------------------------
     * The window + a centred progress bar come up first; the heavy WAV
     * open / envelope / interval passes then run on a yworkpool worker so
     * the event loop stays free to repaint the bar. The worker publishes
     * load_progress / load_phase (read on the loop thread in the RENDER
     * handler) and posts throttled RENDER events to wake the loop;
     * load_state + load_err carry the outcome to yaudio_load_done(), which
     * builds the real UI on the loop thread. */
    struct yetty_yplatform_yworkpool *load_pool;
    struct yetty_ycore_xthread_event_pipe *input_pipe; /* worker → loop wake */
    const struct yetty_yplatform_gpu_context *gpu;     /* for build_widgets() in done() */

    volatile double load_progress; /* 0..1, monotonic; worker writes, loop reads */
    volatile int load_phase;       /* enum yaudio_load_phase */
    volatile int load_state;       /* enum yaudio_load_state */
    char load_err[160];            /* failure message (worker writes, done() reads) */
    double last_posted_progress;   /* worker-local: throttles RENDER posts */

    /* Loading-screen widgets, hidden once the real plot UI is built. */
    struct yetty_yclass_object *load_bar;
    struct yetty_yclass_object *load_label;
    int ui_built; /* real plot UI constructed yet? */
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yaudio_app_ptr, struct yetty_yaudio_app *);
struct yetty_yclass_ptr_result yetty_yaudio_app_class_get(void);
struct yetty_yaudio_app_ptr_result yetty_yaudio_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yaudio_app_create(struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols. yaudio owns its own main() (positional WAV
 * path), so it drives this sequence directly rather than via ymain/glfw.c. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

#define MOD_SHIFT 0x0001u
#define MOD_CTRL 0x0002u

/* ----------------------------------------------------------------------- */
/* Loading                                                                  */
/* ----------------------------------------------------------------------- */

/* Wake the event loop so it repaints the progress bar. Safe to call from
 * the worker thread: the xthread input pipe is the same mechanism the PTY
 * threads use, and its write() is a full barrier — any load_progress /
 * load_phase store sequenced before it is visible to the loop thread by
 * the time it dispatches the RENDER. */
static void yaudio_worker_wake(struct yetty_yaudio_app *app)
{
    yetty_yevent_post_async(app->input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});
}

/* Library progress callback — runs on the worker thread. Folds each pass's
 * local [0,1] fraction into the global bar range and wakes the loop when it
 * advanced enough to be worth a repaint (~0.5% steps → a couple hundred
 * RENDERs across the whole load, not one per analysis frame). */
static void yaudio_progress_cb(void *ud, double f)
{
    struct yetty_yaudio_app *app = ud;
    double g;
    switch (app->load_phase) {
    case YAUDIO_PHASE_ENVELOPE:
        g = 0.02 + f * 0.49;
        break; /* envelope:  2%..51%  */
    case YAUDIO_PHASE_INTERVALS:
        g = 0.51 + f * 0.49;
        break; /* intervals: 51%..100% */
    default:
        g = f;
        break;
    }
    app->load_progress = g;
    if (g - app->last_posted_progress >= 0.005 || g >= 0.999) {
        app->last_posted_progress = g;
        yaudio_worker_wake(app);
    }
}

static struct yetty_ycore_void_result yaudio_load(struct yetty_yaudio_app *app)
{
    app->load_phase = YAUDIO_PHASE_OPEN;
    app->load_progress = 0.0;
    yaudio_worker_wake(app);

    struct yetty_yaudio_wav_ptr_result wr = yetty_yaudio_wav_open(app->wav_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yaudio: open WAV failed");
    app->wav = wr.value;

    app->load_phase = YAUDIO_PHASE_ENVELOPE;
    yaudio_worker_wake(app);
    struct yetty_yaudio_envelope_ptr_result er =
        yetty_yaudio_envelope_create(app->wav, 0, 0, 0, yaudio_progress_cb, app);
    if (YETTY_IS_ERR(er)) {
        yetty_yaudio_wav_close(app->wav);
        app->wav = NULL;
        return YETTY_ERR(yetty_ycore_void, "yaudio: envelope failed", er);
    }
    app->env = er.value;

    app->load_phase = YAUDIO_PHASE_INTERVALS;
    yaudio_worker_wake(app);
    struct yetty_yaudio_intervals_ptr_result ir =
        yetty_yaudio_intervals_find(app->wav, 0, NULL, yaudio_progress_cb, app);
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
    app->env_dbfs = malloc(app->env_dbfs_n * sizeof(float));
    if (!app->env_dbfs) {
        return YETTY_ERR(yetty_ycore_void, "yaudio: env_dbfs alloc failed");
    }
    for (size_t i = 0; i < app->env_dbfs_n; i++) {
        float v = app->env->rms[i];
        if (v < 1e-5f) {
            v = 1e-5f;
        }
        app->env_dbfs[i] = (float)(20.0 * log10((double)v));
    }

    yinfo("yaudio: loaded %s — %.1f s, %zu intervals", app->wav_path,
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
static int load_waveform_window(struct yetty_yaudio_app *app, double t_min, double t_max)
{
    if (!app->wav || t_max <= t_min) {
        return 0;
    }
    if (t_min < 0.0) {
        t_min = 0.0;
    }
    double dur_total = (double)app->wav->frames / (double)app->wav->sample_rate;
    if (t_max > dur_total) {
        t_max = dur_total;
    }

    double sr = (double)app->wav->sample_rate;
    size_t f_min = (size_t)(t_min * sr);
    size_t f_max = (size_t)(t_max * sr);
    if (f_max <= f_min) {
        return 0;
    }
    size_t span = f_max - f_min;

    /* ---- Tight zoom: raw samples straight into wave_dec. ---- */
    if (span <= (size_t)WAVE_BUCKETS) {
        struct yetty_ycore_size_result rr =
            yetty_yaudio_wav_read_channel_f32(app->wav, 0, f_min, app->wave_dec, span);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            app->wave_dec_n = 0;
            return 0;
        }
        app->wave_dec_n = rr.value;
        return app->wave_dec_n >= 2;
    }

    /* ---- Wide zoom: min/max envelope per bucket. ---- */
    size_t buckets = WAVE_BUCKETS;
    size_t max_bucket = (span + buckets - 1) / buckets + 1;
    if (max_bucket < 16) {
        max_bucket = 16;
    }
    if (max_bucket > app->wave_raw_cap) {
        float *nb = realloc(app->wave_raw, max_bucket * sizeof(float));
        if (!nb) {
            yerror("yaudio: wave_raw realloc(%zu) failed", max_bucket);
            return 0;
        }
        app->wave_raw = nb;
        app->wave_raw_cap = max_bucket;
    }

    for (size_t i = 0; i < buckets; i++) {
        size_t bs = f_min + (span * i) / buckets;
        size_t be = f_min + (span * (i + 1)) / buckets;
        if (be <= bs) {
            be = bs + 1;
        }
        if (be > f_max) {
            be = f_max;
        }
        size_t n = (be > bs) ? (be - bs) : 0;
        float mn = 0.0f, mx = 0.0f;
        if (n > 0) {
            struct yetty_ycore_size_result rr =
                yetty_yaudio_wav_read_channel_f32(app->wav, 0, bs, app->wave_raw, n);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            } else if (rr.value > 0) {
                size_t got = rr.value;
                mn = mx = app->wave_raw[0];
                for (size_t k = 1; k < got; k++) {
                    float v = app->wave_raw[k];
                    if (v < mn) {
                        mn = v;
                    }
                    if (v > mx) {
                        mx = v;
                    }
                }
            }
        }
        app->wave_dec[2 * i] = mn;
        app->wave_dec[2 * i + 1] = mx;
    }
    app->wave_dec_n = 2 * buckets;
    return 1;
}

static void update_status_label(struct yetty_yaudio_app *app)
{
    char buf[192];
    if (app->iv->n == 0) {
        snprintf(buf, sizeof(buf), "no intervals detected");
    } else {
        const struct yetty_yaudio_interval *it = &app->iv->items[app->selected];
        snprintf(buf, sizeof(buf),
                 "interval %d / %zu   start %.3f s   end %.3f s   "
                 "dur %.3f s   peak %.1f dBFS",
                 app->selected + 1, app->iv->n, it->start_sec, it->end_sec,
                 it->end_sec - it->start_sec, (double)it->peak_dbfs);
    }
    if (app->status_label) {
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(app->status_label, buf));
    }
    if (app->yui) {
        yetty_yui_set_status_right(app->yui, buf);
    }
    yinfo("status: %s", buf);
}

static void clamp_view(struct yetty_yaudio_app *app)
{
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    double span = app->view_t_max - app->view_t_min;
    if (span < 0.001) {
        span = 0.001; /* never go below 1 ms */
    }
    if (span > dur) {
        span = dur;
    }
    if (app->view_t_min < 0.0) {
        app->view_t_min = 0.0;
        app->view_t_max = span;
    }
    if (app->view_t_max > dur) {
        app->view_t_max = dur;
        app->view_t_min = dur - span;
        if (app->view_t_min < 0.0) {
            app->view_t_min = 0.0;
        }
    }
    if (app->wave_y_max < 0.001f) {
        app->wave_y_max = 0.001f;
    }
    if (app->wave_y_max > 1.0f) {
        app->wave_y_max = 1.0f;
    }
}

/* Cheap path — mark the view dirty and update the status label. Heavy
 * waveform decimation is deferred to flush_view_if_dirty(), which the
 * render loop calls once per frame. Drag/wheel handlers use this so a
 * burst of MOUSE_MOVE events doesn't choke the worker. */
static void request_view_update(struct yetty_yaudio_app *app)
{
    if (!app->wav) {
        return;
    }
    clamp_view(app);
    update_status_label(app);
    app->view_dirty = 1;
}

static void apply_view(struct yetty_yaudio_app *app)
{
    if (!app->wav) {
        return;
    }
    clamp_view(app);
    update_status_label(app);
    app->view_dirty = 0;

    double t_min = app->view_t_min;
    double t_max = app->view_t_max;

    /* Envelope plot — slice of env_dbfs spanning [t_min..t_max]. */
    if (app->plot_widget) {
        double hop_sec = (double)app->env->hop_samples / (double)app->env->sample_rate;
        size_t i_min = (size_t)(t_min / hop_sec);
        size_t i_max = (size_t)(t_max / hop_sec);
        if (i_max > app->env_dbfs_n) {
            i_max = app->env_dbfs_n;
        }
        if (i_min >= i_max) {
            i_min = i_max > 0 ? i_max - 1 : 0;
        }

        struct yetty_ygui_yplot_config pc = {0};
        pc.x_min = (float)t_min;
        pc.x_max = (float)t_max;
        pc.y_min = -90.0f;
        pc.y_max = 0.0f;
        struct yetty_yplot_buffer_input bi = {
            .samples = app->env_dbfs + i_min,
            .count = i_max - i_min,
            .color = 0,
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
            wpc.y_max = (float)app->wave_y_max;
            struct yetty_yplot_buffer_input wbi = {
                .samples = app->wave_dec,
                .count = app->wave_dec_n,
                .color = 0xFFE0E5E4u,
            };
            yetty_ycore_error_destroy_safe(
                yetty_ygui_yplot_set_buffers(app->wave_widget, NULL, 0, &wbi, 1, &wpc));
        }
    }
}

static void recenter_plot_on_selected(struct yetty_yaudio_app *app)
{
    if (!app->plot_widget || app->iv->n == 0) {
        return;
    }

    const struct yetty_yaudio_interval *it = &app->iv->items[app->selected];
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    double span = app->view_t_max - app->view_t_min;

    /* Choose how wide a window to frame the interval in. When the view
     * still spans (almost) the whole file there is no room to pan — keeping
     * that span just lets clamp_view snap straight back to [0, dur] and
     * Prev/Next appear to do nothing. So in that case (and the degenerate
     * span<=0 case) zoom to an 8×-interval window — min 30 s, capped at the
     * file length — so each step visibly jumps to its interval. A genuine
     * user zoom (span well under the file) is preserved. */
    if (span <= 0.0 || span >= dur * 0.98) {
        double iv_d = it->end_sec - it->start_sec;
        span = iv_d * 8.0;
        if (span < 2.0) {
            span = 2.0; /* minimum context for very short blips */
        }
        if (span > dur) {
            span = dur;
        }
    }

    /* Centre the interval in the window so the jump is obvious; clamp_view
     * (via request_view_update) keeps the window inside the file. */
    double mid = 0.5 * (it->start_sec + it->end_sec);
    app->view_t_min = mid - 0.5 * span;
    app->view_t_max = mid + 0.5 * span;
    request_view_update(app);
}

/* Mouse-wheel routing:
 *   plain wheel        — scroll in time (pan)
 *   Ctrl + wheel       — zoom amplitude (waveform y range)
 *   Ctrl+Shift + wheel — zoom in time (around current center) */
static void on_wheel(struct yetty_yaudio_app *app, float dy, int mods)
{
    if (dy == 0.0f) {
        return;
    }
    double span = app->view_t_max - app->view_t_min;
    if (span <= 0.0) {
        return;
    }

    int ctrl = (mods & MOD_CTRL) != 0;
    int shift = (mods & MOD_SHIFT) != 0;

    if (ctrl && shift) {
        /* Time zoom: dy > 0 → zoom in. */
        double factor = exp(-(double)dy * 0.2);
        double mid = 0.5 * (app->view_t_min + app->view_t_max);
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

static struct yetty_ycore_void_result on_prev_click(struct yetty_yclass_object *w, void *userdata)
{
    (void)w;
    struct yetty_yaudio_app *app = userdata;
    if (app->iv->n == 0) {
        return YETTY_OK_VOID();
    }
    app->selected = app->selected == 0 ? (int)app->iv->n - 1 : app->selected - 1;
    recenter_plot_on_selected(app);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_next_click(struct yetty_yclass_object *w, void *userdata)
{
    (void)w;
    struct yetty_yaudio_app *app = userdata;
    if (app->iv->n == 0) {
        return YETTY_OK_VOID();
    }
    app->selected = (app->selected + 1) % (int)app->iv->n;
    recenter_plot_on_selected(app);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
build_widgets(struct yetty_yaudio_app *app, const struct yetty_yplatform_gpu_context *gpu)
{
    struct yetty_ygui_framework *engine = yetty_yui_engine(app->yui);
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yaudio: yui engine is NULL — yui allocation failed");
    }
    struct yetty_yclass_object *root = yetty_ygui_framework_root(engine);
    if (!root) {
        return YETTY_ERR(yetty_ycore_void, "yaudio: yui engine has no root");
    }

    float W = (float)gpu->surface_width;
    float H = (float)gpu->surface_height;
    float top = 36.0f; /* room for yui's tabbar */
    struct yetty_ycore_float_result sb_h_res = yetty_yui_statusbar_height(app->yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sb_h_res, "yaudio: statusbar_height");
    float sb_h = sb_h_res.value;
    float btn_strip_h = 48.0f;
    float plots_h = H - top - btn_strip_h - sb_h - 16.0f;
    float plots_w = W - 32.0f;

    /* Initial plot config — full duration. */
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    app->view_t_min = 0.0;
    app->view_t_max = dur;
    app->wave_y_max = 1.0f;

    /* Flex-column container hosts the two plots + the status label that
     * sits between them. ygui handles the gap automatically — the gap
     * value is what leaves room for axis labels / inline text between
     * the plot widgets. Padding is zero; gap eats all the inter-row
     * space. */
    /* Flex-column container hosting the two plots + the status label.
     * Absolutely positioned over the workspace area (below yui's
     * titlebar, above its statusbar + the button strip). */
    struct yetty_yclass_object *col = NULL;
    {
        struct yetty_yclass_object_ptr_result cr =
            yetty_ygui_widget_add(root, yetty_ygui_vbox_class_get().value);
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
        .count = app->env_dbfs_n,
        .color = 0,
    };
    if (col) {
        struct yetty_yclass_object_ptr_result pr =
            yetty_ygui_widget_add(col, yetty_ygui_yplot_class_get().value);
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
        struct yetty_yclass_object_ptr_result lr =
            yetty_ygui_widget_add(col, yetty_ygui_label_class_get().value);
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
    wpc.y_max = 1.0f;
    struct yetty_yplot_buffer_input wbi = {
        .samples = app->wave_dec,
        .count = app->wave_dec_n,
        .color = 0xFFE0E5E4u,
    };
    if (col) {
        struct yetty_yclass_object_ptr_result wr =
            yetty_ygui_widget_add(col, yetty_ygui_yplot_class_get().value);
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
        struct yetty_yclass_object_ptr_result br =
            yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
        if (YETTY_IS_OK(br)) {
            app->prev_btn = br.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(app->prev_btn, "◀ Prev"));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(app->prev_btn, 16.0f, btn_y));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(app->prev_btn, btn_w, btn_h));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(app->prev_btn, on_prev_click, app));
        } else {
            yetty_ycore_error_destroy(br.error);
        }
    }
    {
        struct yetty_yclass_object_ptr_result br =
            yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
        if (YETTY_IS_OK(br)) {
            app->next_btn = br.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(app->next_btn, "Next ▶"));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(app->next_btn, 16.0f + btn_w + 12.0f, btn_y));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(app->next_btn, btn_w, btn_h));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(app->next_btn, on_next_click, app));
        } else {
            yetty_ycore_error_destroy(br.error);
        }
    }

    /* Mouse/key cheatsheet, to the right of the buttons. Static text — ygui
     * owns it in the tree, so it needs no app-side handle. Mirrors the
     * gestures handled in on_wheel() and the MOUSE_DRAG path. */
    {
        float help_x = 16.0f + 2.0f * btn_w + 12.0f + 24.0f; /* past Next button + gap */
        struct yetty_yclass_object_ptr_result hr =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        if (YETTY_IS_OK(hr)) {
            struct yetty_yclass_object *help = hr.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(
                help, "◀ ▶ / ←→: prev·next interval    Drag: pan    Wheel: scroll    "
                      "Ctrl+Wheel: amplitude    Ctrl+Shift+Wheel: time zoom"));
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_font_size(help, 13.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(help, help_x, btn_y + 10.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_size(help, W - help_x - 16.0f, btn_h));
        } else {
            yetty_ycore_error_destroy(hr.error);
        }
    }

    char status[160];
    snprintf(status, sizeof(status), "%s  •  %.1f s  •  %zu intervals", app->wav_path, dur,
             app->iv->n);
    yetty_yui_set_status_left(app->yui, status);

    update_status_label(app);
    return YETTY_OK_VOID();
}

/* ----------------------------------------------------------------------- */
/* Loading screen                                                           */
/* ----------------------------------------------------------------------- */

/* A caption + a progress bar, centred over an otherwise empty window. Built
 * before any file work so the window can present immediately; hidden by
 * yaudio_load_done() once the real plot UI replaces it. */
static void build_loading_ui(struct yetty_yaudio_app *app,
                             const struct yetty_yplatform_gpu_context *gpu)
{
    struct yetty_ygui_framework *engine = yetty_yui_engine(app->yui);
    if (!engine) {
        yerror("yaudio: yui engine is NULL — yui allocation failed");
        return;
    }
    struct yetty_yclass_object *root = yetty_ygui_framework_root(engine);
    if (!root) {
        yerror("yaudio: yui engine has no root");
        return;
    }

    float W = (float)gpu->surface_width;
    float H = (float)gpu->surface_height;
    float bw = 460.0f;
    if (bw > W - 64.0f) {
        bw = W - 64.0f;
    }
    float cx = (W - bw) * 0.5f;
    float cy = H * 0.5f;

    {
        struct yetty_yclass_object_ptr_result lr =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        if (YETTY_IS_OK(lr)) {
            app->load_label = lr.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(app->load_label, "Loading…"));
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_font_size(app->load_label, 20.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_set_position(app->load_label, cx, cy - 36.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(app->load_label, bw, 26.0f));
        } else {
            yetty_ycore_error_destroy(lr.error);
        }
    }
    {
        struct yetty_yclass_object_ptr_result pr =
            yetty_ygui_widget_add(root, yetty_ygui_progress_class_get().value);
        if (YETTY_IS_OK(pr)) {
            app->load_bar = pr.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(app->load_bar, 0.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(app->load_bar, cx, cy));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(app->load_bar, bw, 22.0f));
        } else {
            yetty_ycore_error_destroy(pr.error);
            yerror("yaudio: progress widget create failed");
        }
    }

    char status[256];
    snprintf(status, sizeof(status), "Loading %s …", app->wav_path);
    yetty_yui_set_status_left(app->yui, status);
}

/* Pull the latest worker-published progress into the bar + caption. Runs on
 * the loop thread, once per RENDER while the load is in flight. */
static void update_loading_ui(struct yetty_yaudio_app *app)
{
    if (!app->load_bar) {
        return;
    }

    double p = app->load_progress;
    if (p < 0.0) {
        p = 0.0;
    }
    if (p > 1.0) {
        p = 1.0;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(app->load_bar, (float)p));

    if (app->load_label) {
        const char *phase;
        switch (app->load_phase) {
        case YAUDIO_PHASE_ENVELOPE:
            phase = "computing energy envelope";
            break;
        case YAUDIO_PHASE_INTERVALS:
            phase = "detecting intervals";
            break;
        default:
            phase = "opening file";
            break;
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "%s — %s … %d%%", app->wav_path, phase, (int)(p * 100.0 + 0.5));
        yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(app->load_label, buf));
    }
}

/* ----------------------------------------------------------------------- */
/* Input handling                                                           */
/* ----------------------------------------------------------------------- */

/* Registered on runtime->event_loop via yetty_yevent_register_default_listeners
 * — the libuv event loop reads events off the platform_input_pipe and
 * dispatches them here. RENDER does the per-frame work; SHUTDOWN /
 * WINDOW_CLOSE / Escape stop the loop so yaudio_worker returns. */
static struct yetty_ycore_int_result yaudio_event_handler(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *ev)
{
    struct yetty_yaudio_app *app = container_of(listener, struct yetty_yaudio_app, listener);

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
        if (!app->ui_built) {
            update_loading_ui(app);
        } else if (app->view_dirty) {
            apply_view(app);
        }
        struct yetty_ycore_void_result cl = app->render_target->ops->clear(app->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }

        struct yetty_ycore_void_result rr = yetty_yui_render(app->yui, app->render_target);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }

        struct yetty_ycore_void_result pp = app->render_target->ops->present(app->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (app->runtime && app->runtime->event_loop && app->runtime->event_loop->ops->stop) {
            app->runtime->event_loop->ops->stop(app->runtime->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE:
        /* Mirror the new size into the runtime + texture target so
         * surface present uses the live framebuffer dims. */
        yetty_yframework_reconfigure_surface(app->runtime, (uint32_t)ev->resize.width,
                                             (uint32_t)ev->resize.height);
        if (app->render_target && app->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            app->render_target->ops->resize(app->render_target, vp);
        }
        if (app->yui) {
            yetty_yui_resize(app->yui, (uint32_t)ev->resize.width, (uint32_t)ev->resize.height);
        }
        break;
    case YETTY_YCORE_MOUSE_SCROLL:
        on_wheel(app, ev->mouse_scroll.dy, ev->mouse_scroll.mods);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_DOWN:
        if (ev->mouse.button == 0 && app->plots_vbox && app->wav) {
            struct yetty_ycore_rectangle_result box_res = yetty_ygui_widget_rect(app->plots_vbox);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, box_res, "yaudio: plots_vbox rect");
            struct yetty_ycore_rectangle box = box_res.value;
            if (ev->mouse.x >= box.min.x && ev->mouse.x <= box.max.x && ev->mouse.y >= box.min.y &&
                ev->mouse.y <= box.max.y && box.max.x > box.min.x) {
                app->dragging = 1;
                app->drag_start_mx = ev->mouse.x;
                app->drag_start_t_min = app->view_t_min;
                app->drag_start_t_max = app->view_t_max;
                app->drag_plot_w = box.max.x - box.min.x;
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        break;
    case YETTY_YCORE_MOUSE_DRAG:
    case YETTY_YCORE_MOUSE_MOVE:
        if (app->dragging && app->drag_plot_w > 0.0f) {
            double dx = (double)(ev->mouse.x - app->drag_start_mx);
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
            if (app->runtime && app->runtime->event_loop && app->runtime->event_loop->ops->stop) {
                app->runtime->event_loop->ops->stop(app->runtime->event_loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ev->key.key == 262) {
            yetty_ycore_error_destroy_safe(on_next_click(NULL, app));
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ev->key.key == 263) {
            yetty_ycore_error_destroy_safe(on_prev_click(NULL, app));
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
    if (app->runtime && app->runtime->event_loop && app->runtime->event_loop->ops->request_render) {
        app->runtime->event_loop->ops->request_render(app->runtime->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* ----------------------------------------------------------------------- */
/* Background load                                                          */
/* ----------------------------------------------------------------------- */

/* Worker-thread body: the heavy open / envelope / interval passes. Stores
 * the outcome in load_state / load_err; yaudio_load_done() (loop thread)
 * reads it. On failure yaudio_load() has already freed any partial state
 * and NULLed the pointers, so the teardown's unconditional destroys are
 * safe either way. */
static void yaudio_load_run(void *ctx)
{
    struct yetty_yaudio_app *app = ctx;

    struct yetty_ycore_void_result r = yaudio_load(app);
    if (YETTY_IS_ERR(r)) {
        snprintf(app->load_err, sizeof(app->load_err), "%s",
                 r.error.msg ? r.error.msg : "load failed");
        yetty_ycore_error_destroy(r.error);
        app->load_state = YAUDIO_LOAD_FAILED;
    } else {
        app->load_state = YAUDIO_LOAD_OK;
    }
    app->load_progress = 1.0;
    /* The pool posts done() to the loop thread; the RENDER it triggers
     * repaints the final state. */
}

/* Loop-thread continuation: build the real UI (or surface the error) and
 * retire the loading screen. */
static void yaudio_load_done(void *ctx)
{
    struct yetty_yaudio_app *app = ctx;

    if (app->load_state == YAUDIO_LOAD_OK) {
        struct yetty_ycore_void_result build_res = build_widgets(app, app->gpu);
        if (YETTY_IS_ERR(build_res)) {
            yerror("yaudio: build_widgets failed: %s",
                   build_res.error.msg ? build_res.error.msg : "(no message)");
            yetty_ycore_error_destroy(build_res.error);
            yetty_yui_set_status_left(app->yui, "Failed to build UI");
        }
        app->ui_built = 1;
        if (app->load_bar) {
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(app->load_bar, 0));
        }
        if (app->load_label) {
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(app->load_label, 0));
        }
    } else {
        /* Keep the window up; show why on the loading caption + status. */
        if (app->load_bar) {
            yetty_ycore_error_destroy_safe(yetty_ygui_progress_set_value(app->load_bar, 1.0f));
        }
        if (app->load_label) {
            char buf[224];
            snprintf(buf, sizeof(buf), "Failed to load %s: %s", app->wav_path, app->load_err);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(app->load_label, buf));
        }
        yetty_yui_set_status_left(app->yui, app->load_err);
    }

    if (app->runtime && app->runtime->event_loop && app->runtime->event_loop->ops->request_render) {
        app->runtime->event_loop->ops->request_render(app->runtime->event_loop);
    }
}

/* ----------------------------------------------------------------------- */
/* Render loop                                                              */
/* ----------------------------------------------------------------------- */

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result yaudio_app_init(struct yetty_yclass_object *obj,
                                                      struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result yaudio_app_run(struct yetty_yclass_object *obj,
                                                     struct yetty_yclass_object *platform)
{
    struct yetty_yaudio_app_ptr_result app_res = yetty_yaudio_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yaudio:app:run: app_from");
    struct yetty_yaudio_app *app = app_res.value;

    const struct yetty_yplatform_gpu_context *gpu = yetty_yplatform_platform_gpu_context(platform);
    struct yetty_ycore_xthread_event_pipe *input_pipe =
        yetty_yplatform_platform_input_pipe(platform);
    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yaudio:app:run: platform state not populated");
    }

    /* Standard GPU/event/render-target bring-up FIRST — the window and a
     * progress bar must come up before any heavy file work. Same call
     * yetty's own main uses (adapter, device, queue, allocator, msdf
     * generator, surface configuration, event loop, render target); the
     * runtime owns all of it and we borrow via app->ctx.runtime->X. */
    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yrt_res, "yaudio: yframework_create failed");
    app->runtime = yrt_res.value;
    app->ctx.runtime = app->runtime;
    app->ctx.pty_factory = NULL; /* yaudio has no terminal */
    app->ctx.event_loop = app->runtime->event_loop;
    app->render_target = app->runtime->render_target;
    app->gpu = gpu;
    app->input_pipe = input_pipe;

    /* yui — owns the scene-canvas + ygui engine. cell_w/h are mostly
     * arbitrary for our use; pick the same defaults yui's tabbar uses. */
    struct yetty_yui_ptr_result yr =
        yetty_yui_create(&app->ctx, gpu->surface_width, gpu->surface_height, 8.0f, 16.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yui_create failed");
    app->yui = yr.value;

    /* Loading screen only — the real plot UI is built later, from
     * yaudio_load_done(), once the worker has the data. */
    app->last_posted_progress = -1.0;
    build_loading_ui(app, gpu);

    /* Wire up our event handler on the runtime's libuv event loop. The
     * loop drives the platform_input_pipe; events dispatch into
     * yaudio_event_handler, which renders frames on YETTY_YCORE_RENDER
     * and stops the loop on SHUTDOWN / WINDOW_CLOSE / Esc. */
    app->listener.handler = yaudio_event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(app->runtime->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "register_default_listeners failed");

    /* Offload the load to a worker thread so the loop keeps repainting the
     * progress bar; done() builds the real UI back on the loop thread. */
    struct yetty_yplatform_yworkpool_ptr_result pres =
        yetty_yplatform_yworkpool_create(app->runtime->event_loop, "yaudio-load", 1);
    if (YETTY_IS_OK(pres)) {
        app->load_pool = pres.value;
        struct yetty_yplatform_yworkpool_job job = {
            .run = yaudio_load_run,
            .done = yaudio_load_done,
            .ctx = app,
        };
        struct yetty_ycore_void_result sres = yetty_yplatform_yworkpool_submit(app->load_pool, job);
        if (YETTY_IS_ERR(sres)) {
            ywarn("yaudio: load submit failed (%s); loading synchronously", sres.error.msg);
            yetty_ycore_error_destroy(sres.error);
            yaudio_load_run(app);
            yaudio_load_done(app);
        }
    } else {
        ywarn("yaudio: worker pool create failed (%s); loading synchronously", pres.error.msg);
        yetty_ycore_error_destroy(pres.error);
        yaudio_load_run(app);
        yaudio_load_done(app);
    }

    /* Present the first frame (the loading screen) immediately. */
    yetty_yevent_post_async(input_pipe,
                            &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    /* Run the libuv loop until SHUTDOWN. Input-driven render coalesced via
     * libuv's idle handle; the x11-tile target's tile-diff readback
     * callbacks fire here too. */
    struct yetty_ycore_void_result run_res =
        app->runtime->event_loop->ops->start(app->runtime->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        ywarn("yaudio: event_loop start returned error: %s", run_res.error.msg);
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown: join the worker first (so nothing touches app after the
     * frees), then yui (it holds GPU resources owned by the runtime), then
     * the runtime (render target, msdf, allocator, event loop, surface,
     * queue, device, adapter, in reverse-creation order). */
    if (app->load_pool) {
        yetty_yplatform_yworkpool_destroy(app->load_pool);
    }
    if (app->yui) {
        yetty_yui_destroy(app->yui);
    }
    if (app->runtime) {
        yetty_yframework_destroy(app->runtime);
    }

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
        if (argv[i][0] != '-' && !wav_path) {
            wav_path = argv[i];
        }
    }
    if (!wav_path) {
        usage(stderr, argv[0]);
        return 2;
    }

    /* Drive the platform bring-up directly. yconfig parses the argv we pass and
     * exits on unknown flags, so hand it a clean argv (program name only) — the
     * WAV path travels on the app object instead. */
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "yaudio: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "yaudio: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_yaudio_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yaudio: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }
    struct yetty_yaudio_app_ptr_result app_data = yetty_yaudio_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        yetty_ycore_error_print(stderr, "yaudio: app data", app_data.error);
        yetty_ycore_error_destroy(app_data.error);
        return 1;
    }
    app_data.value->wav_path = wav_path;

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yaudio: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    char *clean_argv[] = {argv[0], NULL};
    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, 1, clean_argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yaudio: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}

#include "main.gen.c"
