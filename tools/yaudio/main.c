/*
 * tools/yaudio/main.c - audio analyzer GUI.
 *
 * Opens a window via yinit_run. The worker builds a full yetty_context
 * (adapter / device / queue / gpu_allocator / msdf_generator /
 * event_loop), creates a yui (which owns the scene-canvas + ygui
 * engine), then attaches a yplot widget showing the RMS envelope and
 * Prev/Next buttons that pan the plot's x range across the detected
 * noise intervals.
 *
 * Render loop: drain input pipe → update yplot view if selection
 * changed → clear target → yui_render → present.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/yaudio/wav.h>
#include <yetty/yaudio/envelope.h>
#include <yetty/yaudio/intervals.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/ywebgpu/request.h>
#include <yetty/ywebgpu/limits.h>
#include <yetty/webgpu/error.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>

#include <yetty/yplot/yplot.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yplot.h>

/* yui.h lives in src/yetty/yui/yui.h — already on the -I src path. */
#include <yetty/yui/yui.h>

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WAVE_DEC_POINTS = 4096,   /* decimated waveform sample count */
};

struct yaudio_app {
    const char *wav_path;
    struct yetty_yaudio_wav       *wav;
    struct yetty_yaudio_envelope  *env;
    struct yetty_yaudio_intervals *iv;
    float                         *env_dbfs;  /* env->rms in dBFS, for plotting */
    size_t                         env_dbfs_n;
    int    selected;
    int    quit;

    /* GPU + UI state. */
    struct yetty_yetty_app_gpu_context  app_gpu;
    struct yetty_context                ctx;
    struct yetty_ydraw_gpu_allocator   *allocator;
    struct yetty_ymsdf_generator       *msdf;
    struct yetty_yevent_event_loop     *event_loop;

    struct yetty_yui              *yui;
    struct yetty_ygui_widget      *plot_widget;        /* energy envelope (dBFS) */
    struct yetty_ygui_widget      *wave_widget;        /* raw waveform of zoomed window */
    struct yetty_ygui_widget      *prev_btn;
    struct yetty_ygui_widget      *next_btn;
    struct yetty_ygui_widget      *status_label;
    struct yetty_ydraw_target     *render_target;

    /* Scratch buffers for the waveform widget. `wave_raw` holds raw
     * samples read from the WAV for the current zoom window; `wave_dec`
     * is the max-abs-per-bucket decimation handed to yplot (so the
     * curve uses ~WAVE_DEC_POINTS points regardless of zoom span). */
    float *wave_raw;
    size_t wave_raw_cap;
    float *wave_dec;

    /* Current view window into the file. Updated by Prev/Next clicks
     * AND mouse-wheel events (plain=scroll, ctrl=amp zoom, ctrl+shift=
     * time zoom). apply_view() pushes this into both yplot widgets. */
    double   view_t_min;
    double   view_t_max;
    float    wave_y_max;          /* waveform amplitude clamp (zooms y of wave plot) */
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
/* WGPU + yetty_context setup                                               */
/* ----------------------------------------------------------------------- */

static struct yetty_ycore_void_result
init_gpu_context(struct yaudio_app *app, struct yetty_yinit_runtime *rt)
{
    WGPUInstance instance = (WGPUInstance)rt->instance;
    WGPUSurface  surface  = (WGPUSurface)rt->surface;

    /* Adapter. */
    WGPURequestAdapterOptions opts = {0};
    opts.compatibleSurface = surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;
    WGPUAdapter adapter = NULL;
    int adapter_ready = 0;
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowSpontaneous;
    acb.callback = yetty_ywebgpu_adapter_request_callback;
    acb.userdata1 = &adapter;
    acb.userdata2 = &adapter_ready;
    wgpuInstanceRequestAdapter(instance, &opts, acb);
    if (!adapter) return YETTY_ERR(yetty_ycore_void, "wgpu adapter request failed");

    /* Device. */
    WGPULimits limits;
    yetty_ywebgpu_fill_default_limits(adapter, rt->config, &limits);
    WGPUStringView dlabel = {.data = "yaudio device", .length = 13};
    WGPUStringView qlabel = {.data = "yaudio queue",  .length = 12};
    WGPUDeviceDescriptor dd = {0};
    dd.label                       = dlabel;
    dd.requiredLimits              = &limits;
    dd.defaultQueue.label          = qlabel;
    dd.uncapturedErrorCallbackInfo = yetty_ywebgpu_get_error_callback_info();
    dd.deviceLostCallbackInfo      = yetty_ywebgpu_get_device_lost_callback_info();
    WGPUDevice device = NULL;
    struct yetty_ywebgpu_device_request_state dcb_state = {{0}, 0};
    WGPURequestDeviceCallbackInfo dcb = {0};
    dcb.mode      = WGPUCallbackMode_AllowSpontaneous;
    dcb.callback  = yetty_ywebgpu_device_request_callback;
    dcb.userdata1 = &device;
    dcb.userdata2 = &dcb_state;
    wgpuAdapterRequestDevice(adapter, &dd, dcb);
    if (!device) return YETTY_ERR(yetty_ycore_void, "wgpu device request failed");
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    /* Surface format + configure. */
    WGPUTextureFormat surface_format = WGPUTextureFormat_BGRA8Unorm;
    if (surface) {
        WGPUSurfaceCapabilities caps = {0};
        if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success
         && caps.formatCount > 0) {
            surface_format = caps.formats[0];
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        WGPUSurfaceConfiguration sc = {0};
        sc.device      = device;
        sc.format      = surface_format;
        sc.usage       = WGPUTextureUsage_RenderAttachment;
        sc.width       = rt->surface_width;
        sc.height      = rt->surface_height;
        sc.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &sc);
    }

    /* gpu_allocator. */
    struct yetty_yrender_gpu_allocator_result ar =
        yetty_yrender_gpu_allocator_create(device);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "wgpu gpu_allocator failed");
    app->allocator = ar.value;

    /* msdf. shaders_dir is set in the yconfig paths. */
    const char *shaders_dir = rt->config->ops->get_string(rt->config, "paths/shaders", "");
    struct yetty_ymsdf_generator_ptr_result mr =
        yetty_ymsdf_generator_create_from_config(rt->config, device, instance, shaders_dir);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "msdf_generator create failed");
    app->msdf = mr.value;

    /* libuv event loop (yui_create uses it for the memory-pty wake). */
    struct yetty_ycore_event_loop_result lr =
        yetty_ycore_event_loop_create(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "event_loop create failed");
    app->event_loop = lr.value;

    /* Build the contexts. */
    app->app_gpu.instance       = instance;
    app->app_gpu.surface        = surface;
    app->app_gpu.surface_width  = rt->surface_width;
    app->app_gpu.surface_height = rt->surface_height;
    app->app_gpu.content_scale  = rt->content_scale;
    app->app_gpu.x11_display    = rt->x11_display;
    app->app_gpu.x11_window     = rt->x11_window;

    app->ctx.app_context.app_gpu_context     = app->app_gpu;
    app->ctx.app_context.config              = rt->config;
    app->ctx.app_context.platform_input_pipe = rt->platform_input_pipe;
    app->ctx.app_context.clipboard_manager   = rt->clipboard_manager;
    app->ctx.app_context.window_manager      = rt->window_manager;
    app->ctx.app_context.pty_factory         = NULL;

    app->ctx.gpu_context.app_gpu_context = app->app_gpu;
    app->ctx.gpu_context.adapter         = adapter;
    app->ctx.gpu_context.device          = device;
    app->ctx.gpu_context.queue           = queue;
    app->ctx.gpu_context.surface_format  = surface_format;
    app->ctx.gpu_context.allocator       = app->allocator;
    app->ctx.gpu_context.msdf_generator  = app->msdf;

    app->ctx.event_loop = app->event_loop;
    return YETTY_OK_VOID();
}

/* ----------------------------------------------------------------------- */
/* Widget layout + interval navigation                                      */
/* ----------------------------------------------------------------------- */

/* Stream-decimate channel 0 over [t_min..t_max] into app->wave_dec
 * (WAVE_DEC_POINTS floats). Per output bucket: read just the samples
 * in that bucket from the mmap, keep the largest-abs (signed) so peaks
 * survive. Memory usage = max-bucket-size × sizeof(float), bounded
 * even for full-file ranges. Returns 1 on success, 0 on empty range. */
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

    size_t max_bucket = (span + WAVE_DEC_POINTS - 1) / WAVE_DEC_POINTS + 1;
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

    for (size_t i = 0; i < WAVE_DEC_POINTS; i++) {
        size_t bs = f_min + (span * i)       / WAVE_DEC_POINTS;
        size_t be = f_min + (span * (i + 1)) / WAVE_DEC_POINTS;
        if (be <= bs) be = bs + 1;
        if (be > f_max) be = f_max;
        size_t n = be - bs;
        if (n == 0) {
            app->wave_dec[i] = 0.0f;
            continue;
        }
        struct yetty_ycore_size_result rr = yetty_yaudio_wav_read_channel_f32(
            app->wav, 0, bs, app->wave_raw, n);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            app->wave_dec[i] = 0.0f;
            continue;
        }
        size_t got  = rr.value;
        float  best = 0.0f;
        for (size_t k = 0; k < got; k++) {
            float v = app->wave_raw[k];
            if (fabsf(v) > fabsf(best)) best = v;
        }
        app->wave_dec[i] = best;
    }
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
        yetty_ygui_widget_label_set_text(app->status_label, buf);
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

static void apply_view(struct yaudio_app *app)
{
    if (!app->wav) return;
    clamp_view(app);
    update_status_label(app);

    double t_min = app->view_t_min;
    double t_max = app->view_t_max;

    /* Envelope plot — slice of env_dbfs spanning [t_min..t_max]. */
    if (app->plot_widget) {
        double hop_sec = (double)app->env->hop_samples / (double)app->env->sample_rate;
        size_t i_min   = (size_t)(t_min / hop_sec);
        size_t i_max   = (size_t)(t_max / hop_sec);
        if (i_max > app->env_dbfs_n) i_max = app->env_dbfs_n;
        if (i_min >= i_max) i_min = i_max > 0 ? i_max - 1 : 0;

        struct yetty_yplot_render_config pc = {0};
        pc.x_min = (float)t_min;
        pc.x_max = (float)t_max;
        pc.y_min = -90.0f;
        pc.y_max = 0.0f;
        struct yetty_yplot_buffer_input bi = {
            .samples = app->env_dbfs + i_min,
            .count   = i_max - i_min,
            .color   = 0,
        };
        yetty_ygui_widget_yplot_set_buffers(app->plot_widget, NULL, 0, &bi, 1, &pc);
    }

    /* Waveform plot — freshly streamed from the mmap and decimated. */
    if (app->wave_widget && app->wave_dec) {
        if (load_waveform_window(app, t_min, t_max)) {
            struct yetty_yplot_render_config wpc = {0};
            wpc.x_min = (float)t_min;
            wpc.x_max = (float)t_max;
            wpc.y_min = -(double)app->wave_y_max;
            wpc.y_max =  (double)app->wave_y_max;
            struct yetty_yplot_buffer_input wbi = {
                .samples = app->wave_dec,
                .count   = WAVE_DEC_POINTS,
                .color   = 0xFFE0E5E4u,
            };
            yetty_ygui_widget_yplot_set_buffers(app->wave_widget, NULL, 0, &wbi, 1, &wpc);
        }
    }
}

static void recenter_plot_on_selected(struct yaudio_app *app)
{
    if (!app->plot_widget || app->iv->n == 0) return;

    double dur_total = (double)app->wav->frames / (double)app->wav->sample_rate;
    const struct yetty_yaudio_interval *it = &app->iv->items[app->selected];
    double mid  = 0.5 * (it->start_sec + it->end_sec);
    double iv_d = it->end_sec - it->start_sec;
    double span = iv_d * 8.0;
    if (span < 30.0)      span = 30.0;
    if (span > dur_total) span = dur_total;

    app->view_t_min = mid - 0.5 * span;
    app->view_t_max = mid + 0.5 * span;
    apply_view(app);
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
    apply_view(app);
}

YETTY_EXTERNAL_CALLBACK
static void on_prev_click(struct yetty_ygui_widget *w, void *userdata)
{
    yinfo("yaudio: PREV click fired (widget=%p)", (void *)w);
    fprintf(stderr, "yaudio: PREV click fired\n"); fflush(stderr);
    struct yaudio_app *app = userdata;
    if (app->iv->n == 0) return;
    app->selected = app->selected == 0 ? (int)app->iv->n - 1 : app->selected - 1;
    recenter_plot_on_selected(app);
}

YETTY_EXTERNAL_CALLBACK
static void on_next_click(struct yetty_ygui_widget *w, void *userdata)
{
    yinfo("yaudio: NEXT click fired (widget=%p)", (void *)w);
    fprintf(stderr, "yaudio: NEXT click fired\n"); fflush(stderr);
    struct yaudio_app *app = userdata;
    if (app->iv->n == 0) return;
    app->selected = (app->selected + 1) % (int)app->iv->n;
    recenter_plot_on_selected(app);
}

static void build_widgets(struct yaudio_app *app, struct yetty_yinit_runtime *rt)
{
    struct yetty_ygui_engine *engine = yetty_yui_engine(app->yui);
    if (!engine) {
        yerror("yaudio: yui engine is NULL — yui allocation failed");
        return;
    }

    float W = (float)rt->surface_width;
    float H = (float)rt->surface_height;
    float top         = 36.0f;     /* room for yui's tabbar */
    float sb_h        = yetty_yui_statusbar_height(app->yui);
    float btn_strip_h = 48.0f;
    float label_h     = 28.0f;
    float ctrl_h     = btn_strip_h + label_h + 8.0f;
    float plots_h     = H - top - ctrl_h - sb_h - 16.0f;
    float gap         = 8.0f;
    float plot_h      = (plots_h - gap) * 0.5f;       /* envelope (top) */
    float wave_h      = (plots_h - gap) - plot_h;     /* waveform (bottom) */
    float plot_w      = W - 32.0f;

    /* Initial plot config — full duration. */
    double dur = (double)app->wav->frames / (double)app->wav->sample_rate;
    app->view_t_min  = 0.0;
    app->view_t_max  = dur;
    app->wave_y_max  = 1.0f;

    struct yetty_yplot_render_config pc = {0};
    pc.x_min = (float)app->view_t_min;
    pc.x_max = (float)app->view_t_max;
    pc.y_min = -90.0f;
    pc.y_max = 0.0f;

    struct yetty_yplot_buffer_input bi = {
        .samples = app->env_dbfs,
        .count   = app->env_dbfs_n,
        .color   = 0,
    };
    app->plot_widget = yetty_ygui_engine_yplot_from_buffers(
        engine, "yaudio_plot", 16.0f, top, plot_w, plot_h,
        NULL, 0, &bi, 1, &pc);
    if (!app->plot_widget) {
        yerror("yaudio: yplot widget create failed");
    }

    /* Raw-waveform plot directly under the envelope. Initial data:
     * a max-abs-per-bucket decimation of the WHOLE file → ~4096 pts.
     * Recenter on Prev/Next reloads the slice for the zoomed window. */
    app->wave_dec = calloc(WAVE_DEC_POINTS, sizeof(float));
    if (app->wave_dec) {
        load_waveform_window(app, 0.0, dur);
    }
    struct yetty_yplot_render_config wpc = {0};
    wpc.x_min = 0.0f;
    wpc.x_max = (float)dur;
    wpc.y_min = -1.0f;
    wpc.y_max =  1.0f;
    struct yetty_yplot_buffer_input wbi = {
        .samples = app->wave_dec,
        .count   = WAVE_DEC_POINTS,
        .color   = 0xFFE0E5E4u,
    };
    app->wave_widget = yetty_ygui_engine_yplot_from_buffers(
        engine, "yaudio_wave",
        16.0f, top + plot_h + gap, plot_w, wave_h,
        NULL, 0, &wbi, 1, &wpc);
    if (!app->wave_widget) {
        yerror("yaudio: yplot wave widget create failed");
    }

    /* Status label — sits between plot and buttons. Bumped font so
     * "where we are" is the most readable thing in the window. */
    float label_y = top + plot_h + 4.0f;
    app->status_label = yetty_ygui_engine_label(engine, "yaudio_status",
                                                16.0f, label_y,
                                                "(loading…)");
    if (app->status_label) {
        yetty_ygui_widget_label_set_font_size(app->status_label, 18.0f);
    }

    /* Buttons live above yui's statusbar; padded so click rects are
     * fully above the chrome. */
    float btn_w = 120.0f;
    float btn_h = 36.0f;
    float btn_y = H - sb_h - btn_strip_h + 6.0f;
    app->prev_btn = yetty_ygui_engine_button(engine, "yaudio_prev",
                                             16.0f, btn_y, btn_w, btn_h, "◀ Prev");
    app->next_btn = yetty_ygui_engine_button(engine, "yaudio_next",
                                             16.0f + btn_w + 12.0f, btn_y,
                                             btn_w, btn_h, "Next ▶");
    yinfo("yaudio: widgets placed — plot=%p prev=%p next=%p label=%p "
          "btn_y=%.1f sb_h=%.1f W=%.0f H=%.0f",
          (void *)app->plot_widget, (void *)app->prev_btn,
          (void *)app->next_btn, (void *)app->status_label,
          (double)btn_y, (double)sb_h, (double)W, (double)H);
    if (app->prev_btn) yetty_ygui_widget_button_on_click(app->prev_btn, on_prev_click, app);
    if (app->next_btn) yetty_ygui_widget_button_on_click(app->next_btn, on_next_click, app);

    char status[160];
    snprintf(status, sizeof(status), "%s  •  %.1f s  •  %zu intervals",
             app->wav_path, dur, app->iv->n);
    yetty_yui_set_status_left(app->yui, status);

    update_status_label(app);
}

/* ----------------------------------------------------------------------- */
/* Input handling                                                           */
/* ----------------------------------------------------------------------- */

static void handle_event(struct yaudio_app *app, const struct yetty_yui_event *ev)
{
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE:
        if (app->yui) {
            yetty_yui_resize(app->yui,
                             (uint32_t)ev->resize.width,
                             (uint32_t)ev->resize.height);
        }
        break;
    case YETTY_YCORE_MOUSE_SCROLL:
        on_wheel(app, ev->mouse_scroll.dy, ev->mouse_scroll.mods);
        return;       /* don't forward — yui has no use for it here */
    case YETTY_YCORE_KEY_DOWN:
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
            return;
        }
        if (ev->key.key == 262) {
            on_next_click(NULL, app);
            return;
        }
        if (ev->key.key == 263) {
            on_prev_click(NULL, app);
            return;
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

    struct yetty_ycore_void_result gr = init_gpu_context(app, rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "yaudio: gpu init failed");

    /* yui — owns the scene-canvas + ygui engine. cell_w/h are mostly
     * arbitrary for our use; pick the same defaults yui's tabbar uses. */
    struct yetty_yui_ptr_result yr =
        yetty_yui_create(&app->ctx, rt->surface_width, rt->surface_height, 8.0f, 16.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yui_create failed");
    app->yui = yr.value;

    build_widgets(app, rt);

    /* Render target — texture-backed, blits to the surface on present. */
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0,
        .w = (float)rt->surface_width, .h = (float)rt->surface_height,
    };
    struct yetty_yrender_target_ptr_result tr =
        yetty_yrender_target_texture_create(app->ctx.gpu_context.device,
                                            app->ctx.gpu_context.queue,
                                            app->ctx.gpu_context.surface_format,
                                            app->allocator,
                                            (WGPUSurface)rt->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "render target create failed");
    app->render_target = tr.value;

    /* Pump events + render frames continuously. */
    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    while (!app->quit) {
        struct pollfd pfd = {.fd = pipe_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, 16);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe,
                                                       &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) break;
                handle_event(app, &ev);
            }
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
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
    }

    if (app->render_target) app->render_target->ops->destroy(app->render_target);
    if (app->yui)           yetty_yui_destroy(app->yui);
    if (app->event_loop && app->event_loop->ops && app->event_loop->ops->destroy)
        app->event_loop->ops->destroy(app->event_loop);
    /* msdf / allocator / device / queue / adapter cleanup omitted; the
     * process exits right after. */

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
