/*
 * demo-ymgui-01-demo-window
 *
 * Runs inside a yetty session (`./yetty -e ./demo-ymgui-01-demo-window`).
 * Drives Dear ImGui through the shared yetty_yclient event loop with
 * one ymgui figure placed at the cursor row, w_cells=0 (= until the
 * right edge), h_cells=24.
 */

#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "imgui.h"
#include "imgui_impl_yetty.h"
#include <yetty/yclient/event-loop.h>

struct demo_state {
    struct yetty_yclient_event_loop *loop;
    uint32_t figure_id;
    int frames_rendered;
    int frames_max;
    uint64_t last_ns;
    bool window_open;
};

/* SIGWINCH set this so the next on_frame iteration re-emits a
 * SET_CHILD_RECT against the new pane size. We cannot do the wire work
 * inside the signal handler (it would race with the frontend's emit
 * pipeline). Volatile sig_atomic_t is the only safe type to touch from
 * a handler. */
static volatile sig_atomic_t g_winch_pending = 0;
/* Set after the event loop exists so on_sigwinch can wake it; until
 * then the handler just flips the flag and the explicit
 * request_frame at startup picks it up. */
static struct yetty_yclient_event_loop *volatile g_loop_for_signal = nullptr;
static void on_sigwinch(int signo)
{
    (void)signo;
    g_winch_pending = 1;
    /* libuv's uv_async_send (which event_loop_request_frame wraps) is
     * the one async-signal-safe libuv call. Without this poke the
     * render-on-input loop would sit idle after the handler returns —
     * the flag would be set but nothing would consume it. */
    if (g_loop_for_signal) {
        yetty_yclient_event_loop_request_frame(g_loop_for_signal);
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void on_frame(void *user)
{
    struct demo_state *S = (struct demo_state *)user;

    if (g_winch_pending) {
        g_winch_pending = 0;
        /* MoveFigure re-runs the producer's rect computation against
         * the current TIOCGWINSZ (yetty pushes new ws_xpixel/ws_ypixel
         * before SIGWINCH fires) and emits SET_CHILD_RECT. With
         * (col=0,row=0,w_cells=0,h_cells=0) the figure auto-fills the
         * pane. */
        yetty_ymgui_ImGui_ImplYetty_MoveFigure(
            S->figure_id, /*col=*/0, /*row=*/0,
            /*w_cells=*/0, /*h_cells=*/0);
    }

    yetty_ymgui_ImGui_ImplYetty_BeginFigureFrame(S->figure_id);
    ImGuiIO &io = ImGui::GetIO();

    /* Skip drawing until we know the figure's pixel size from the server's
     * RESIZE confirmation (otherwise DisplaySize is the placeholder). */
    if (io.DisplaySize.x <= 1.0f || io.DisplaySize.y <= 1.0f) {
        return;
    }

    uint64_t now = now_ns();
    if (S->last_ns == 0) {
        S->last_ns = now;
    }
    uint64_t dt_ns = now - S->last_ns;
    S->last_ns = now;
    if (dt_ns == 0) {
        dt_ns = 1;
    }
    io.DeltaTime = (float)((double)dt_ns / 1e9);

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    ImGui::ShowDemoWindow(&S->window_open);

    ImGui::Render();
    yetty_ymgui_ImGui_ImplYetty_RenderFigureDrawData(S->figure_id, ImGui::GetDrawData());

    S->frames_rendered++;
    /* User clicked the [x] in the demo window — exit. */
    if (!S->window_open) {
        yetty_yclient_event_loop_stop(S->loop);
        return;
    }
    if (S->frames_max > 0) {
        if (S->frames_rendered >= S->frames_max) {
            yetty_yclient_event_loop_stop(S->loop);
            return;
        }
        /* --frames N is CI/smoke-test mode: render full-speed until the
         * cap so the run completes regardless of input. Interactive runs
         * (frames_max == 0) keep the render-on-input model. */
        yetty_yclient_event_loop_request_frame(S->loop);
    }
}

int main(int argc, char **argv)
{
    int frames_max = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames_max = atoi(argv[++i]);
        }
    }

    IMGUI_CHECKVERSION();

    /* SIGWINCH disposition is "ignore" by default — if we wait until
     * after CreateFigure to install the handler we'll miss any
     * resize that fires in the window where the demo has been forked
     * but the YUI layout hasn't laid out the pane yet. Install first. */
    {
        struct sigaction sa = {};
        sa.sa_handler = on_sigwinch;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &sa, nullptr);
    }

    if (!yetty_ymgui_ImGui_ImplYetty_Init()) {
        fprintf(stderr, "ymgui: init failed\n");
        return 1;
    }
    if (!yetty_ymgui_ImGui_ImplYetty_PlatformInit()) {
        fprintf(stderr, "ymgui: platform init failed\n");
        yetty_ymgui_ImGui_ImplYetty_Shutdown();
        return 1;
    }

    struct demo_state state = {};
    state.frames_max = frames_max;
    state.window_open = true;

    /* Figure filling the whole pane (w_cells=0 → right edge dynamically;
     * h_cells=0 → bottom edge at placement time). */
    state.figure_id = yetty_ymgui_ImGui_ImplYetty_CreateFigure(
        /*figure_id=*/0, /*col=*/0, /*row=*/0,
        /*w_cells=*/0, /*h_cells=*/0);

    struct yetty_yclient_lib_event_loop_config cfg = {};
    cfg.in_fd = -1;
    cfg.user = &state;
    state.loop = yetty_yclient_event_loop_create(&cfg);
    if (!state.loop) {
        fprintf(stderr, "ymgui: event loop create failed\n");
        yetty_ymgui_ImGui_ImplYetty_PlatformShutdown();
        yetty_ymgui_ImGui_ImplYetty_Shutdown();
        return 1;
    }

    yetty_ymgui_ImGui_ImplYetty_AttachEventLoop(state.loop);
    yetty_yclient_event_loop_set_user(state.loop, &state);
    yetty_yclient_event_loop_set_frame_cb(state.loop, on_frame);

    /* Hand the event loop to the SIGWINCH handler so it can wake the
     * loop via the async-signal-safe request_frame. Must come before
     * any further winch can fire — but the signal could already have
     * fired between CreateFigure and now, in which case g_winch_pending
     * is set and the explicit kick below catches it. */
    g_loop_for_signal = state.loop;
    g_winch_pending = 1;
    yetty_yclient_event_loop_request_frame(state.loop);

    int rc = yetty_yclient_event_loop_run(state.loop);
    fprintf(stderr, "[demo] loop exited rc=%d frames=%d\n", rc, state.frames_rendered);

    yetty_yclient_event_loop_destroy(state.loop);
    /* The new figure-tree wire has no static-archive path: RemoveFigure
     * emits DELETE_CHILD and Clear emits CLEAR_ALL, both of which the
     * compositor honours by dropping the figure. If the user clicked
     * the [x] in the demo window we want the figure gone; otherwise
     * (frame cap reached, signal, normal exit) we want the last drawn
     * frame to remain visible after the process exits. */
    if (!state.window_open) {
        yetty_ymgui_ImGui_ImplYetty_RemoveFigure(state.figure_id, /*keep_visible=*/false);
        yetty_ymgui_ImGui_ImplYetty_Clear(/*keep_visible=*/false);
    }
    yetty_ymgui_ImGui_ImplYetty_PlatformShutdown();
    yetty_ymgui_ImGui_ImplYetty_Shutdown();
    return 0;
}
