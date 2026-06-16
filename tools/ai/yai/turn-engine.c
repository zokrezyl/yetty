/*
 * turn-engine.c — yclass class `yai:turn_engine`: shared child
 * lifecycle for engines that spawn ONE child process per turn (codex,
 * gemini) instead of holding a persistent session child (claude).
 *
 * The subclass spawns the child in its send_user_message override and
 * accounts the open uv handles in app->child_open_handles (process +
 * stdout pipe = 2). This class owns the other half of the lifecycle:
 * the exit/EOF policy and the close bookkeeping — the turn is over
 * only when BOTH handles have fully closed; only then is a respawn
 * safe, and only then does the turn boundary run.
 *
 * Turn failure (nonzero exit, turn.failed events) is flagged in
 * app->turn_failed by whoever observes it; yai_turn_finished renders
 * the distinct failed state at the boundary.
 */
#include "app.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

struct [[clang::annotate("class@yai:turn_engine")]] [[clang::annotate("parent@yai:engine")]]
yetty_yai_turn_engine {
    /* The class@ annotation needs a struct to sit on; the per-turn
     * lifecycle state (open-handle count, failure flag) lives in
     * struct yai_app where main.c can see it. */
    char unused;
};

YETTY_YRESULT_DECLARE(yetty_yai_turn_engine_ptr, struct yetty_yai_turn_engine *);

/* Both child handles closed → the turn is really over. uv close
 * callbacks return void: absorb inner Results here (the boundary). */
YETTY_EXTERNAL_CALLBACK
static void turn_engine_handle_closed_cb(uv_handle_t *handle)
{
    struct yai_app *app = handle->data;
    app->child_open_handles--;
    if (app->child_open_handles > 0) {
        return;
    }
    if (app->shutting_down) {
        uv_stop(&app->loop);
        return;
    }
    yai_report_error(app, "turn stream finish", yai_renderer_finish_turn(&app->renderer));
    yai_report_error(app, "turn boundary", yai_turn_finished(app));
}

[[clang::annotate("override@yai:turn_engine:on_child_exit")]]
static struct yetty_ycore_void_result turn_engine_on_child_exit(struct yetty_yclass_object *obj,
                                                                struct yai_app *app,
                                                                int64_t exit_status)
{
    (void)obj;
    /* Best-effort: the process handle MUST be closed whatever the
     * rendering does — stash the first error, surface it at the end. */
    struct yetty_ycore_void_result render_res = YETTY_OK_VOID();
    if (!app->shutting_down && exit_status != 0) {
        app->turn_failed = 1;
        render_res = yai_renderer_zone_suspend(&app->renderer);
        printf("\n" YAI_RED "✗ %s exited with status %lld — see stderr log under tmp/" YAI_RESET
               "\n",
               app->engine_name, (long long)exit_status);
        render_res = yetty_ycore_void_chain(render_res, yai_render_flush_stdout());
    }
    uv_close((uv_handle_t *)&app->child_process, turn_engine_handle_closed_cb);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "turn_engine on_child_exit: render");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:turn_engine:on_child_eof")]]
static struct yetty_ycore_void_result turn_engine_on_child_eof(struct yetty_yclass_object *obj,
                                                               struct yai_app *app)
{
    (void)obj;
    /* Normal end of a per-turn stream. Flush any unterminated tail —
     * best-effort: the pipe handle MUST be closed regardless. */
    struct yetty_ycore_void_result tail_res = YETTY_OK_VOID();
    if (app->child_out_len > 0) {
        app->child_out_buf[app->child_out_len] = '\0';
        tail_res = yai_handle_child_line(app, app->child_out_buf, app->child_out_len);
        app->child_out_len = 0;
    }
    uv_close((uv_handle_t *)&app->child_stdout_pipe, turn_engine_handle_closed_cb);
    /* The pipe is going away; clear the flag so the final cleanup in
     * main does not try to close it a second time. */
    app->child_stdout_pipe_initialized = 0;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tail_res, "turn_engine on_child_eof: tail line");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:turn_engine:interrupt")]]
static struct yetty_ycore_void_result turn_engine_interrupt(struct yetty_yclass_object *obj,
                                                            struct yai_app *app)
{
    (void)obj;
    if (!app->child_alive) {
        return YETTY_OK_VOID();
    }
    if (uv_process_kill(&app->child_process, SIGINT) != 0) {
        return YETTY_ERR(yetty_ycore_void, "turn_engine interrupt: uv_process_kill failed");
    }
    /* Escalate: a child that ignores SIGINT gets SIGKILL'd after the
     * backstop timeout, so an interrupt can never hang forever. */
    yai_arm_child_kill_timer(app);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_turn_engine_spawn(struct yai_app *app, const char *file,
                                                     const char *const *args)
{
    if (app->child_open_handles > 0 || app->child_alive) {
        return YETTY_ERR(yetty_ycore_void, "yai_turn_engine_spawn: previous turn still open");
    }
    if (uv_pipe_init(&app->loop, &app->child_stdout_pipe, 0) != 0) {
        return YETTY_ERR(yetty_ycore_void, "yai_turn_engine_spawn: uv_pipe_init failed");
    }
    app->child_stdout_pipe_initialized = 1;
    app->child_stdout_pipe.data = app;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE; /* prompt rides argv, not stdin */
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&app->child_stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = app->child_stderr_fd;

    uv_process_options_t options = {0};
    options.exit_cb = yai_child_exit_cb;
    options.file = file;
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;

    app->child_process.data = app;
    if (uv_spawn(&app->loop, &app->child_process, &options) != 0) {
        uv_close((uv_handle_t *)&app->child_stdout_pipe, yai_handle_closed_cb);
        app->child_stdout_pipe_initialized = 0;
        return YETTY_ERR(yetty_ycore_void, "yai_turn_engine_spawn: uv_spawn failed (not in PATH?)");
    }
    app->child_alive = 1;
    app->child_open_handles = 2; /* process + stdout pipe */
    if (uv_read_start((uv_stream_t *)&app->child_stdout_pipe, yai_child_stdout_alloc_cb,
                      yai_child_stdout_read_cb) != 0) {
        /* Post-spawn failure: terminate the child and route the close
         * through the normal callbacks so the turn ends as a failed
         * turn (no leaked process, no wedged handle state). */
        app->turn_failed = 1;
        uv_process_kill(&app->child_process, SIGKILL);
        uv_close((uv_handle_t *)&app->child_stdout_pipe, turn_engine_handle_closed_cb);
        app->child_stdout_pipe_initialized = 0;
    }
    return YETTY_OK_VOID();
}

#include "turn-engine.gen.c"
