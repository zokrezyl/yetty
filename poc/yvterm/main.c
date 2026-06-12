/*
 * poc/yvterm/main.c — standalone text-grid performance probe.
 *
 * Boots a window + WebGPU device via yinit_run + yframework_create (the same
 * path tools/ymaze uses), spawns $SHELL on a PTY, drives a poc_yvterm_grid
 * from the libvterm state layer, and renders it through the production
 * MSDF/cdb text path every frame. Prints throughput stats once a second.
 *
 * Keys: Ctrl-Q quits; everything else is forwarded to the child PTY.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <webgpu/webgpu.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/yevent/event.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/time.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yetty/yetty.h>

#include "grid.h"

struct poc_app {
    int quit;
    int stress;
    const char *child_cmd; /* NULL → interactive $SHELL */

    struct yetty_context ctx;
    struct yetty_yframework *framework;
    struct yetty_ydraw_target *target;
    struct yetty_yfont_ms_font *font;
    struct poc_yvterm_grid *grid;

    int pty_master;
    uint32_t cols;
    uint32_t rows;
};

/*===========================================================================
 * PTY
 *=========================================================================*/

static struct yetty_ycore_int_result spawn_pty(struct poc_app *app)
{
    struct winsize ws = {
        .ws_row = (unsigned short)app->rows,
        .ws_col = (unsigned short)app->cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };
    int master = -1;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) {
        return YETTY_ERR(yetty_ycore_int, "spawn_pty: forkpty failed");
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        const char *shell = getenv("SHELL");
        if (!shell || !shell[0]) {
            shell = "/bin/sh";
        }
        if (app->child_cmd) {
            execlp(shell, shell, "-c", app->child_cmd, (char *)NULL);
        } else {
            execlp(shell, shell, "-i", (char *)NULL);
        }
        _exit(127);
    }

    /* Parent: non-blocking master so the poll loop never stalls. */
    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    return YETTY_OK(yetty_ycore_int, master);
}

/* Forward one input event to the PTY as bytes. Ctrl-Q is intercepted as quit. */
static void forward_event_to_pty(struct poc_app *app, const struct yetty_yui_event *ev)
{
    if (ev->type == YETTY_YCORE_CHAR) {
        uint32_t cp = ev->chr.codepoint;
        uint8_t utf8[4];
        size_t len = 0;
        if (cp < 0x80) {
            utf8[len++] = (uint8_t)cp;
        } else if (cp < 0x800) {
            utf8[len++] = (uint8_t)(0xC0 | (cp >> 6));
            utf8[len++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8[len++] = (uint8_t)(0xE0 | (cp >> 12));
            utf8[len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else {
            utf8[len++] = (uint8_t)(0xF0 | (cp >> 18));
            utf8[len++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | (cp & 0x3F));
        }
        ssize_t wrote = write(app->pty_master, utf8, len);
        (void)wrote;
        return;
    }

    if (ev->type != YETTY_YCORE_KEY_DOWN) {
        return;
    }

    int key = ev->key.key;
    int ctrl = (ev->key.mods & 2) != 0; /* GLFW_MOD_CONTROL */
    if (ctrl && (key == 'Q' || key == 'q')) {
        app->quit = 1;
        return;
    }

    char byte = 0;
    switch (key) {
    case 257: /* Enter */
        byte = '\r';
        break;
    case 258: /* Tab */
        byte = '\t';
        break;
    case 259: /* Backspace */
        byte = 0x7f;
        break;
    case 256: /* Escape */
        byte = 0x1b;
        break;
    default:
        if (ctrl && key >= 'A' && key <= 'Z') {
            byte = (char)(key & 0x1f); /* Ctrl-letter control code */
        }
        break;
    }
    if (byte) {
        ssize_t wrote = write(app->pty_master, &byte, 1);
        (void)wrote;
    }
}

/*===========================================================================
 * Frame
 *=========================================================================*/

static struct yetty_ycore_void_result render_frame(struct poc_app *app)
{
    struct yetty_ydraw_target *target = app->target;
    struct yetty_ycore_void_result cl = target->ops->clear(target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cl, "render_frame: clear");

    struct yetty_yrender_terminal_layer *layer = poc_yvterm_grid_as_layer(app->grid);
    struct yetty_ycore_int_result rr = layer->ops->render(layer, target, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "render_frame: layer render");

    struct yetty_ycore_void_result pp = target->ops->present(target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pp, "render_frame: present");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Worker
 *=========================================================================*/

static struct yetty_ycore_void_result poc_worker(struct yetty_yinit_runtime *runtime, void *user)
{
    struct poc_app *app = user;

    struct yetty_yframework_ptr_result framework_res = yetty_yframework_create(runtime);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yframework_create");
    app->framework = framework_res.value;

    app->ctx.runtime = app->framework;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->framework->event_loop;

    uint32_t surface_w = runtime->surface_width;
    uint32_t surface_h = runtime->surface_height;

    /* Swap the async surface target for a plain texture target the poll loop
     * can drive directly (same move as tools/ymaze / ycompositor). */
    app->framework->render_target->ops->destroy(app->framework->render_target);
    app->framework->render_target = NULL;
    struct yetty_yrender_viewport viewport = {
        .x = 0, .y = 0, .w = (float)surface_w, .h = (float)surface_h};
    struct yetty_yrender_target_ptr_result target_res = yetty_yrender_target_texture_create(
        app->framework->gpu.device, app->framework->gpu.queue, app->framework->gpu.surface_format,
        app->framework->gpu.allocator, (WGPUSurface)runtime->surface, viewport);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, target_res, "texture target create");
    app->target = target_res.value;

    /* MSDF font from the cdb — the real slow path. */
    struct yetty_yconfig_config *config = app->framework->config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char cdb_path[512];
    char font_shader_path[512];
    char text_shader_path[512];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb",
             fonts_dir);
    snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
    snprintf(text_shader_path, sizeof(text_shader_path), "%s/text-layer.wgsl", shaders_dir);

    float content_scale = app->framework->gpu.app_gpu_context.content_scale;
    if (content_scale <= 0.0f) {
        content_scale = 1.0f;
    }
    float font_size = (float)config->ops->get_int(config, "terminal/text-layer/font/size", 14) *
                      content_scale;
    struct yetty_yfont_ms_padding padding = {0};

    struct yetty_font_ms_font_result font_res =
        yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_res, "ms_msdf_font_create");
    app->font = font_res.value;

    struct pixel_size_result cell = app->font->ops->get_cell_size(app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell, "font get_cell_size");
    app->cols = (uint32_t)((float)surface_w / cell.value.width);
    app->rows = (uint32_t)((float)surface_h / cell.value.height);
    if (app->cols == 0) {
        app->cols = 1;
    }
    if (app->rows == 0) {
        app->rows = 1;
    }

    struct poc_yvterm_grid_ptr_result grid_res =
        poc_yvterm_grid_create(app->cols, app->rows, app->font, text_shader_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "grid create");
    app->grid = grid_res.value;

    fprintf(stderr,
            "poc-yvterm: %ux%u cells (%.1fx%.1f px), surface %ux%u, stress=%d — Ctrl-Q quits\n",
            app->cols, app->rows, cell.value.width, cell.value.height, surface_w, surface_h,
            app->stress);

    struct yetty_ycore_int_result pty_res = spawn_pty(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pty_res, "spawn_pty");
    app->pty_master = pty_res.value;

    struct yetty_ycore_int_result pipe_fd_res =
        runtime->platform_input_pipe->ops->read_fd(runtime->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pipe_fd_res, "input pipe read_fd");
    int pipe_fd = pipe_fd_res.value;

    /* Perf accounting. */
    double stats_start = yetty_yplatform_ytime_monotonic_sec();
    uint64_t frames = 0;
    double frame_time_accum = 0.0;

    while (!app->quit) {
        struct pollfd pfds[2] = {
            {.fd = app->pty_master, .events = POLLIN},
            {.fd = pipe_fd, .events = POLLIN},
        };
        /* Stress wants to render flat-out; interactive can sleep until input. */
        int timeout_ms = app->stress ? 0 : 8;
        int ready = poll(pfds, 2, timeout_ms);
        if (ready < 0 && errno != EINTR) {
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char buf[65536];
            for (;;) {
                ssize_t got = read(app->pty_master, buf, sizeof(buf));
                if (got > 0) {
                    struct yetty_ycore_void_result fr =
                        poc_yvterm_grid_feed(app->grid, buf, (size_t)got);
                    if (YETTY_IS_ERR(fr)) {
                        yetty_ycore_error_destroy(fr.error);
                    }
                    if (got < (ssize_t)sizeof(buf)) {
                        break;
                    }
                } else if (got == 0) {
                    app->quit = 1; /* child exited */
                    break;
                } else {
                    break; /* EAGAIN */
                }
            }
        }

        if (pfds[1].revents & POLLIN) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr = runtime->platform_input_pipe->ops->read(
                    runtime->platform_input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                if (ev.type == YETTY_YCORE_SHUTDOWN || ev.type == YETTY_YCORE_WINDOW_CLOSE) {
                    app->quit = 1;
                } else if (ev.type == YETTY_YCORE_RESIZE) {
                    uint32_t width = (uint32_t)ev.resize.width;
                    uint32_t height = (uint32_t)ev.resize.height;
                    if (width && height) {
                        struct yetty_ycore_void_result sr =
                            yetty_yframework_reconfigure_surface(app->framework, width, height);
                        if (YETTY_IS_ERR(sr)) {
                            yetty_ycore_error_destroy(sr.error);
                        }
                        struct yetty_yrender_viewport vp = {
                            .x = 0, .y = 0, .w = (float)width, .h = (float)height};
                        struct yetty_ycore_void_result tr =
                            app->target->ops->resize(app->target, vp);
                        if (YETTY_IS_ERR(tr)) {
                            yetty_ycore_error_destroy(tr.error);
                        }
                    }
                } else {
                    forward_event_to_pty(app, &ev);
                }
            }
        }

        if (app->quit) {
            break;
        }
        if (runtime->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)runtime->instance);
        }

        if (app->stress) {
            poc_yvterm_grid_force_full_dirty(app->grid);
        }

        if (poc_yvterm_grid_is_dirty(app->grid)) {
            double frame_begin = yetty_yplatform_ytime_monotonic_sec();
            struct yetty_ycore_void_result fr = render_frame(app);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            frame_time_accum += yetty_yplatform_ytime_monotonic_sec() - frame_begin;
            frames++;
        }

        double now = yetty_yplatform_ytime_monotonic_sec();
        double elapsed = now - stats_start;
        if (elapsed >= 1.0) {
            struct poc_yvterm_grid_stats stats = poc_yvterm_grid_stats_take(app->grid);
            double fps = (double)frames / elapsed;
            double avg_ms = frames ? (frame_time_accum / (double)frames) * 1000.0 : 0.0;
            double uploaded_mb =
                (double)stats.cells_uploaded * 16.0 / (1024.0 * 1024.0);
            fprintf(stderr,
                    "poc-yvterm: %.0f fps | %.2f ms/frame | %llu frames | "
                    "%.1f MB uploaded | dirty_rows=%u | scroll fast=%llu mmove=%llu\n",
                    fps, avg_ms, (unsigned long long)frames, uploaded_mb, stats.dirty_rows_last,
                    (unsigned long long)stats.scroll_fastpath,
                    (unsigned long long)stats.scroll_memmove);
            stats_start = now;
            frames = 0;
            frame_time_accum = 0.0;
        }
    }

    /* Teardown. */
    if (app->pty_master >= 0) {
        close(app->pty_master);
    }
    struct yetty_ycore_void_result gd = poc_yvterm_grid_destroy(app->grid);
    if (YETTY_IS_ERR(gd)) {
        yetty_ycore_error_destroy(gd.error);
    }
    app->grid = NULL;
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    app->target->ops->destroy(app->target);
    app->target = NULL;
    yetty_yframework_destroy(app->framework);
    app->framework = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct poc_app app = {0};
    app.pty_master = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stress") == 0) {
            app.stress = 1;
        } else if (strcmp(argv[i], "--cmd") == 0 && i + 1 < argc) {
            app.child_cmd = argv[++i];
        }
    }

    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    struct yetty_ycore_int_result run_res =
        yetty_yinit_run(argc, argv, &cfg, poc_worker, &app);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "poc-yvterm", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }
    return run_res.value;
}
