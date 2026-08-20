/*
 * ycat-real-test.c — the REAL ycat binary through a REAL ymux session
 * (#699 review cycle 19, blocking-regression coverage).
 *
 * The gap this closes: every prior test manufactured YPB1/DCS bytes by
 * hand, so the production seam
 *
 *   real ycat process in pane PTY
 *     -> ymux rich-DCS intake/store/projector
 *     -> attach client rich publication
 *     -> content yscene application
 *
 * could break while all gates stayed green (and did: the daemon SEGV on
 * tall figures and the zero scene_max_y markdown bounds both lived on
 * this seam). Here the pane PTY is a real forkpty running the BUILT ycat
 * executable on committed fixtures; nothing on the producer side is
 * synthesized.
 *
 * Env (set by CTest): YCAT_BINARY — the built ycat; YMUX_FIXTURE_DIR —
 * test/fixtures/ymux. Missing env skips (exit 77) for manual runs.
 */

#include <yetty/api/yfigure/figure.h>
#include <yetty/api/ymux/client.h>
#include <yetty/api/ymux/daemon.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/rich.h>
#include <yetty/api/ymux/session.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/pty.h>

#include "ytest.h"

#include "../../../src/yetty/ymux/proto.h"

#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*---------------------------------------------------------------------------
 * Spawn host: a real forkpty running `/bin/sh -c '<ycat> <fixture>; sleep'`.
 * The trailing sleep keeps the pane (and with it the session) alive while
 * the test pumps; the command line is served through the yconfig shell-argv
 * vtable exactly as the production ymux server does.
 *-------------------------------------------------------------------------*/

struct ycat_spawn_config {
    struct yetty_yconfig_config base;
    char command[1024];
    struct yetty_platform_pty *pane_pty; /* borrowed observation handle */
};

static struct yetty_ycore_void_result ycat_config_get_shell_argv(
    const struct yetty_yconfig_config *self, struct yetty_yconfig_shell_argv *out)
{
    const struct ycat_spawn_config *config = (const struct ycat_spawn_config *)self;
    memset(out, 0, sizeof(*out));
    size_t used = 0;
    const char *parts[3] = {"/bin/sh", "-c", config->command};
    for (int index = 0; index < 3; ++index) {
        size_t part_len = strlen(parts[index]) + 1;
        if (used + part_len > sizeof(out->buf)) {
            return YETTY_ERR(yetty_ycore_void, "ycat spawn: argv overflow");
        }
        memcpy(out->buf + used, parts[index], part_len);
        out->argv[index] = out->buf + used;
        used += part_len;
    }
    out->argv[3] = NULL;
    out->argc = 3;
    return YETTY_OK_VOID();
}

static const char *ycat_config_get_string(const struct yetty_yconfig_config *self, const char *path,
                                          const char *default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int ycat_config_get_int(const struct yetty_yconfig_config *self, const char *path,
                               int default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int ycat_config_get_bool(const struct yetty_yconfig_config *self, const char *path,
                                int default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int ycat_config_has(const struct yetty_yconfig_config *self, const char *path)
{
    (void)self;
    (void)path;
    return 0;
}

static const struct yetty_yconfig_config_ops *ycat_config_ops(void)
{
    static const struct yetty_yconfig_config_ops ops = {
        .get_string = ycat_config_get_string,
        .get_int = ycat_config_get_int,
        .get_bool = ycat_config_get_bool,
        .has = ycat_config_has,
        .get_shell_argv = ycat_config_get_shell_argv,
    };
    return &ops;
}

/* forkpty.c (compiled into this test) */
struct yetty_yplatform_pty_ptr_result yetty_yplatform_fork_pty_create(
    struct yetty_yconfig_config *config);

static struct yetty_yplatform_pty_ptr_result ycat_spawn(uint32_t rows, uint32_t cols,
                                                        void *userdata)
{
    struct ycat_spawn_config *config = userdata;
    struct yetty_yplatform_pty_ptr_result pty_res = yetty_yplatform_fork_pty_create(&config->base);
    YETTY_RETURN_IF_ERR(yetty_yplatform_pty_ptr, pty_res, "ycat spawn: forkpty");
    struct yetty_ycore_void_result resize_res =
        pty_res.value->ops->resize(pty_res.value, cols, rows, 0, 0);
    if (YETTY_IS_ERR(resize_res)) {
        yetty_ycore_error_destroy(resize_res.error);
    }
    config->pane_pty = pty_res.value;
    return pty_res;
}

/*---------------------------------------------------------------------------
 * Pump with a real deadline: the pane child is a real process (exec +
 * render take wall time, more under ASAN).
 *-------------------------------------------------------------------------*/
static void pump_once(struct ytest *test, struct yetty_yclass_object *daemon,
                      struct yetty_yclass_object *client)
{
    struct yetty_ycore_int_result daemon_res = yetty_ymux_daemon_step(daemon);
    YTEST_REQUIRE_OK(test, daemon_res);
    struct yetty_ycore_int_result client_res = yetty_ymux_client_step(client);
    YTEST_REQUIRE_OK(test, client_res);
}

/* Pump until `predicate` reports done or the deadline passes. The pane child
 * is a real process — exec + render take wall time. */
typedef int (*ycat_pump_done_fn)(struct yetty_yclass_object *daemon,
                                 struct yetty_yclass_object *client, void *userdata);

static int pump_until(struct ytest *test, struct yetty_yclass_object *daemon,
                      struct yetty_yclass_object *client, ycat_pump_done_fn predicate,
                      void *userdata, int deadline_seconds)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        pump_once(test, daemon, client);
        if (predicate(daemon, client, userdata)) {
            return 1;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec > deadline_seconds) {
            return 0;
        }
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 2 * 1000 * 1000};
        nanosleep(&nap, NULL);
    }
}

static int pump_done_attached(struct yetty_yclass_object *daemon,
                              struct yetty_yclass_object *client, void *userdata)
{
    (void)daemon;
    (void)userdata;
    return yetty_ymux_client_attached(client).value == 1;
}

/* Content arrival: the DAEMON store minted the real envelope AND the client
 * received a rich body carrying at least one record (the first projection
 * always emits an empty body, so generation alone is not arrival). */
static int pump_done_content(struct yetty_yclass_object *daemon, struct yetty_yclass_object *client,
                             void *userdata)
{
    (void)daemon;
    struct yetty_yclass_object *store = userdata;
    if (yetty_ymux_rich_count(store).value < 1) {
        return 0;
    }
    uint32_t body_words = 0;
    struct yetty_ycore_const_uint32_ptr_result body_res =
        yetty_ymux_client_rich_body(client, &body_words);
    if (YETTY_IS_ERR(body_res)) {
        yetty_ycore_error_destroy(body_res.error);
        return 0;
    }
    return body_res.value && body_words > 3 && body_res.value[2] >= 1;
}

/*---------------------------------------------------------------------------
 * One fixture through the whole seam. Returns the client rich body's
 * first-record YPB1 scene_max_y (the producer-declared content height).
 *-------------------------------------------------------------------------*/
enum { YCAT_TEST_ROWS = 30, YCAT_TEST_COLS = 100, YCAT_TEST_CELL_H = 16 };

static void run_fixture(struct ytest *test, const char *tag, const char *fixture_path)
{
    const char *ycat_binary = getenv("YCAT_BINARY");
    YTEST_REQUIRE_NOT_NULL(test, ycat_binary);

    struct ycat_spawn_config spawn_config = {.base = {.ops = ycat_config_ops()}};
    /* Quiet stderr (trace-off in case the env carries YTRACE); keep the pane
     * alive after ycat exits so the session survives the pump. */
    /* Keep-alive tail: `cat` blocks on the pane PTY until the master closes
     * (daemon dispose) — no timer to race the pump. The env mirrors what the
     * production ymux server exports to panes (server_run): without
     * TERM_PROGRAM=yetty an inherited TERM_PROGRAM=tmux makes ycat wrap its
     * envelope in tmux passthrough, which the pane engine ignores. */
    snprintf(spawn_config.command, sizeof(spawn_config.command),
             "export TERM=xterm-256color TERM_PROGRAM=yetty COLORTERM=truecolor "
             "YTRACE_DEFAULT_ON=no; unset TMUX; '%s' '%s' 2>/dev/null; exec cat >/dev/null",
             ycat_binary, fixture_path);

    struct yetty_ymux_daemon_host host = {.spawn = ycat_spawn, .userdata = &spawn_config};
    char socket_path[128];
    snprintf(socket_path, sizeof(socket_path), "ymux-ycat-%s-%d.sock", tag, (int)getpid());
    struct yetty_yclass_object *daemon =
        yetty_ymux_daemon_make(socket_path, YCAT_TEST_ROWS, YCAT_TEST_COLS, &host).value;
    YTEST_REQUIRE_NOT_NULL(test, daemon);

    struct yetty_yclass_object *client = yetty_ymux_client_make(socket_path).value;
    YTEST_REQUIRE_NOT_NULL(test, client);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_client_session_new(client, tag, YCAT_TEST_ROWS, YCAT_TEST_COLS));
    YTEST_REQUIRE_OK(test, yetty_ymux_client_attach(
                               client, tag, 0, YCAT_TEST_ROWS, YCAT_TEST_COLS, YCAT_TEST_CELL_H,
                               YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_VT_TEXT, "tok-ycat-real"));
    /* Attach handshake over the real socket first. */
    YTEST_REQUIRE(test, pump_until(test, daemon, client, pump_done_attached, NULL, 15));

    struct yetty_yclass_object *session = yetty_ymux_daemon_session(daemon, tag).value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t pane_id = yetty_ymux_client_pane_id(client).value;
    struct yetty_yclass_object *pane = yetty_ymux_session_pane(session, pane_id).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *store = yetty_ymux_pane_rich_store(pane).value;
    YTEST_REQUIRE_NOT_NULL(test, store);

    /* The REAL producer: wait for its envelope to cross the whole pipe. */
    YTEST_REQUIRE(test, pump_until(test, daemon, client, pump_done_content, store, 120));

    /* -- ymux half: store metadata is coherent with the real payload. -- */
    YTEST_REQUIRE(test, yetty_ymux_rich_count(store).value >= 1);
    uint64_t rich_id = yetty_ymux_rich_id_at(store, 0).value;
    int anchor_kind = 0;
    uint64_t anchor_a = 0;
    uint32_t anchor_b = 0, span_rows = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_anchor(store, rich_id, &anchor_kind, &anchor_a,
                                                  &anchor_b, &span_rows));

    /* The creation payload is the decoded YPB1 container ycat produced:
     * its scene_max_y (word 4) is the content height, and the row
     * reservation must equal ceil(scene_max_y / cell_height). A zero
     * scene_max_y is the exact markdown regression this pins. */
    uint32_t creation_words = 0;
    struct yetty_ycore_const_uint32_ptr_result creation_res =
        yetty_ymux_rich_creation(store, rich_id, &creation_words);
    YTEST_REQUIRE_OK(test, creation_res);
    YTEST_REQUIRE_NOT_NULL(test, creation_res.value);
    YTEST_REQUIRE(test, creation_words >= 6);
    YTEST_CHECK(test, creation_res.value[0] == 0x31425059u); /* 'YPB1' */
    float scene_max_y = 0.0f;
    memcpy(&scene_max_y, &creation_res.value[4], sizeof(scene_max_y));
    fprintf(stderr, "ycat-real[%s]: scene_max_y=%.1f span_rows=%u creation_words=%u\n", tag,
            (double)scene_max_y, span_rows, creation_words);
    YTEST_CHECK(test, scene_max_y > 0.0f);
    uint32_t expected_span = ((uint32_t)scene_max_y + YCAT_TEST_CELL_H - 1u) / YCAT_TEST_CELL_H;
    if (expected_span == 0u) {
        expected_span = 1u;
    }
    YTEST_CHECK_EQ_INT(test, (int)span_rows, (int)expected_span);

    /* The reservation actually moved the cursor: for a figure taller than
     * the pane the cursor pins at the bottom row; a shorter figure lands
     * the cursor span rows below its anchor. */
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    YTEST_REQUIRE_NOT_NULL(test, engine);
    uint32_t cursor_row = 0, cursor_col = 0;
    int cursor_visible = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_engine_cursor(engine, &cursor_row, &cursor_col, &cursor_visible));
    if (span_rows >= YCAT_TEST_ROWS) {
        YTEST_CHECK_EQ_INT(test, (int)cursor_row, YCAT_TEST_ROWS - 1);
    } else {
        YTEST_CHECK(test, cursor_row >= span_rows);
    }

    /* -- publication half: the client received the projected rich body. -- */
    uint32_t body_words = 0;
    struct yetty_ycore_const_uint32_ptr_result body_res =
        yetty_ymux_client_rich_body(client, &body_words);
    YTEST_REQUIRE_OK(test, body_res);
    YTEST_REQUIRE_NOT_NULL(test, body_res.value);
    YTEST_CHECK(test, body_res.value[0] == 0x594D5052u); /* rich magic */
    YTEST_REQUIRE(test, body_res.value[2] >= 1);         /* >= one record */
    /* Record 0's payload carries the same YPB1 container end to end. */
    YTEST_REQUIRE(test, body_words > 3 + 7 + 6);
    uint32_t record_payload_words = body_res.value[3 + 6];
    YTEST_CHECK(test, record_payload_words >= 6);
    YTEST_CHECK(test, body_res.value[3 + 7] == 0x31425059u); /* payload[0] = YPB1 */

    /* -- scene half: the SAME body the bridge would hand over must not just
     * apply cleanly — it must MATERIALIZE renderable geometry. A transaction
     * that accepts and renders nothing used to pass this test; now we cross
     * the derive seam and assert the leaf tree actually grew and a content
     * point hit-tests to a leaf (review cycle 21). -- */
    struct yetty_yclass_object *scene = NULL;
    {
        struct yetty_ycore_rectangle rect = {{0, 0}, {800, 600}};
        struct yetty_yscene_scene_ptr_result scene_res = yetty_yscene_create(rect, NULL);
        YTEST_REQUIRE_OK(test, scene_res);
        struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_to(scene_res.value);
        YTEST_REQUIRE_OK(test, object_res);
        scene = object_res.value;
    }
    /* An empty scene derives to zero leaves — the before baseline. */
    YTEST_REQUIRE_OK(test, yetty_yscene_derive(scene));
    struct yetty_ycore_uint32_result leaves_before = yetty_yscene_leaf_count(scene);
    YTEST_REQUIRE_OK(test, leaves_before);
    YTEST_CHECK_EQ_INT(test, (int)leaves_before.value, 0);

    struct yetty_ycore_void_result apply_res =
        yetty_yscene_scene_apply_content_transaction(scene, body_res.value, body_words);
    YTEST_CHECK_OK(test, apply_res);

    /* Materialization: the applied body derives to a NON-EMPTY leaf tree
     * (the ycat figure's drawables became renderable geometry). */
    YTEST_REQUIRE_OK(test, yetty_yscene_derive(scene));
    struct yetty_ycore_uint32_result leaves_after = yetty_yscene_leaf_count(scene);
    YTEST_REQUIRE_OK(test, leaves_after);
    fprintf(stderr, "ycat-real scene: %u leaves after apply\n", leaves_after.value);
    YTEST_CHECK(test, leaves_after.value > 0);

    /* Geometry: a point inside the figure must be HITTABLE — hit_opaque
     * reports geometric leaf coverage (content leaves carry external id 0, so
     * hit_test's id return is NOT the right probe here). The red-box SVG is a
     * solid 200x400 fill at the origin, so an interior point is covered; the
     * markdown fixture's text leaves are sparse (glyph gaps), so only the SVG
     * gets the point assertion — leaf_count>0 above is the markdown proof. */
    if (strcmp(tag, "svg") == 0) {
        struct yetty_ycore_int_result opaque_res = yetty_yfigure_hit_opaque(scene, 20.0f, 20.0f);
        YTEST_REQUIRE_OK(test, opaque_res);
        YTEST_CHECK(test, opaque_res.value == 1); /* a leaf covers the interior point */
    }

    {
        struct yetty_ycore_void_result scene_destroy_res = yetty_yfigure_destroy(scene);
        if (YETTY_IS_ERR(scene_destroy_res)) {
            yetty_ycore_error_destroy(scene_destroy_res.error);
        }
    }

    YTEST_REQUIRE_OK(test, yetty_ymux_client_dispose(client));
    YTEST_REQUIRE_OK(test, yetty_ymux_daemon_dispose(daemon));
}

static void test_real_ycat_svg(struct ytest *test)
{
    const char *fixture_dir = getenv("YMUX_FIXTURE_DIR");
    YTEST_REQUIRE_NOT_NULL(test, fixture_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/red-box.svg", fixture_dir);
    run_fixture(test, "svg", path);
}

static void test_real_ycat_markdown(struct ytest *test)
{
    const char *fixture_dir = getenv("YMUX_FIXTURE_DIR");
    YTEST_REQUIRE_NOT_NULL(test, fixture_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/doc.md", fixture_dir);
    run_fixture(test, "md", path);
}

int main(void)
{
    /* Same contract as the production server: PTY/socket writes to a dead
     * peer surface as Result errors, never a process-killing SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);
    if (!getenv("YCAT_BINARY") || !getenv("YMUX_FIXTURE_DIR")) {
        fprintf(stderr, "SKIP: YCAT_BINARY / YMUX_FIXTURE_DIR not set "
                        "(run through ctest)\n");
        return 77;
    }
    struct ytest test = ytest_begin("ymux_ycat_real");
    YTEST_RUN(&test, test_real_ycat_svg);
    YTEST_RUN(&test, test_real_ycat_markdown);
    return ytest_end(&test);
}
