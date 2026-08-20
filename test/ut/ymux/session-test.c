/*
 * ymux session contract test (#695 phase 5 model) — headless, no GPU, no
 * yvterm. Sessions own panes across attach/detach; the first eligible
 * attachment controls canonical geometry; attach never resizes for
 * non-controllers; token reconnect resumes control; takeover is explicit;
 * input is permission-gated; detach-while-output-continues reconnects to
 * the live state (the tmux contract).
 */

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>
#include <yetty/api/ymux/session.h>
#include <yetty/ycore/types.h>

#include "ytest.h"

#include <stdint.h>
#include <string.h>

static uint32_t make_pane(struct ytest *test, struct yetty_yclass_object *session)
{
    struct yetty_ycore_uint32_result pane_res =
        yetty_ymux_session_pane_create(session, 4, 20, 8, 0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    return pane_res.value;
}

static void feed_pane(struct ytest *test, struct yetty_yclass_object *session, uint32_t pane_id,
                      const char *bytes)
{
    struct yetty_yclass_object_ptr_result pane_res = yetty_ymux_session_pane(session, pane_id);
    YTEST_REQUIRE_OK(test, pane_res);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane_res.value, bytes, strlen(bytes)));
}

/*---------------------------------------------------------------------------
 * Controller policy: first attach controls + resizes canonically; second
 * attach does NOT resize; token reconnect resumes control; takeover moves
 * it explicitly.
 *-------------------------------------------------------------------------*/
static void test_controller_policy(struct ytest *test)
{
    struct yetty_yclass_object *session = yetty_ymux_session_make().value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t pane_id = make_pane(test, session);

    uint32_t first = yetty_ymux_session_attach(session, pane_id, 6, 30, "alpha").value;
    YTEST_REQUIRE(test, first != 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_session_controller(session).value, (int)first);
    /* The controller's geometry became canonical. */
    struct yetty_yclass_object *pane = yetty_ymux_session_pane(session, pane_id).value;
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    uint32_t rows = 0, cols = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 6);
    YTEST_CHECK_EQ_INT(test, cols, 30);

    /* Second attach with a DIFFERENT size: no canonical resize. */
    uint32_t second = yetty_ymux_session_attach(session, pane_id, 10, 40, "beta").value;
    YTEST_REQUIRE(test, second != 0);
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 6);
    YTEST_CHECK_EQ_INT(test, cols, 30);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_session_controller(session).value, (int)first);

    /* Non-controller resize: view-only (crop/pad), canonical unchanged. */
    YTEST_REQUIRE_OK(test, yetty_ymux_session_resize(session, second, 12, 50));
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, cols, 30);

    /* Controller resize changes the canonical pane. */
    YTEST_REQUIRE_OK(test, yetty_ymux_session_resize(session, first, 8, 32));
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 8);
    YTEST_CHECK_EQ_INT(test, cols, 32);

    /* Explicit takeover: second becomes controller, ITS view becomes
     * canonical. */
    YTEST_REQUIRE_OK(test, yetty_ymux_session_takeover(session, second));
    YTEST_CHECK_EQ_INT(test, yetty_ymux_session_controller(session).value, (int)second);
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 12);
    YTEST_CHECK_EQ_INT(test, cols, 50);
    /* The old controller lost resize permission. */
    uint32_t first_permissions = yetty_ymux_session_permissions(session, first).value;
    YTEST_CHECK(test, !(first_permissions & YETTY_YMUX_PERMISSION_RESIZE));

    yetty_ymux_session_dispose(session);
}

/*---------------------------------------------------------------------------
 * Detach while output continues; reconnect sees the live state; token
 * reconnect resumes control.
 *-------------------------------------------------------------------------*/
static void test_detach_reconnect(struct ytest *test)
{
    struct yetty_yclass_object *session = yetty_ymux_session_make().value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t pane_id = make_pane(test, session);
    uint32_t first = yetty_ymux_session_attach(session, pane_id, 4, 20, "tok-1").value;
    YTEST_REQUIRE(test, first != 0);
    feed_pane(test, session, pane_id, "before-detach\r\n");

    YTEST_REQUIRE_OK(test, yetty_ymux_session_detach(session, first));
    YTEST_CHECK_EQ_INT(test, yetty_ymux_session_controller(session).value, 0);

    /* Output continues detached — the pane survives. */
    feed_pane(test, session, pane_id, "WHILE_DETACHED\r\n");

    /* Reconnect with the SAME token: resumes control; a projection FULL
     * carries the content produced while detached. */
    uint32_t resumed = yetty_ymux_session_attach(session, pane_id, 4, 20, "tok-1").value;
    YTEST_REQUIRE(test, resumed != 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_session_controller(session).value, (int)resumed);

    struct yetty_yclass_object *projector = yetty_ymux_session_projector(session, resumed).value;
    YTEST_REQUIRE_NOT_NULL(test, projector);
    /* The detached-time line is in the reconstructed VT redraw (#699.3: the
     * paint surface is retired — the VT byte stream IS the screen). */
    struct yetty_ycore_buffer_result vt_res = yetty_ycore_buffer_create(16384);
    YTEST_REQUIRE_OK(test, vt_res);
    struct yetty_ycore_buffer vt_buffer = vt_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(projector, &vt_buffer));
    YTEST_REQUIRE(test, vt_buffer.size > 0);
    int found = 0;
    for (size_t offset = 0; offset + 1 < vt_buffer.size && !found; ++offset) {
        if (vt_buffer.data[offset] == 'W') {
            found = 1;
        }
    }
    YTEST_CHECK(test, found);

    yetty_ycore_buffer_destroy(&vt_buffer);
    yetty_ymux_session_dispose(session);
}

/*---------------------------------------------------------------------------
 * Input permission gating.
 *-------------------------------------------------------------------------*/
static void test_input_permissions(struct ytest *test)
{
    struct yetty_yclass_object *session = yetty_ymux_session_make().value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t pane_id = make_pane(test, session);
    uint32_t viewer = yetty_ymux_session_attach(session, pane_id, 4, 20, NULL).value;
    YTEST_REQUIRE(test, viewer != 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_session_input_char(session, viewer, 'x', 0));
    /* Revoke input: rejected. */
    YTEST_REQUIRE_OK(test, yetty_ymux_session_set_permissions(session, viewer, 0));
    struct yetty_ycore_void_result denied_res =
        yetty_ymux_session_input_char(session, viewer, 'y', 0);
    YTEST_REQUIRE_ERR(test, denied_res);

    yetty_ymux_session_dispose(session);
}

/*---------------------------------------------------------------------------
 * Pane close detaches its attachments; other panes unaffected.
 *-------------------------------------------------------------------------*/
static void test_pane_close(struct ytest *test)
{
    struct yetty_yclass_object *session = yetty_ymux_session_make().value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    uint32_t first_pane = make_pane(test, session);
    uint32_t second_pane = make_pane(test, session);
    uint32_t attachment = yetty_ymux_session_attach(session, first_pane, 4, 20, NULL).value;
    YTEST_REQUIRE(test, attachment != 0);

    YTEST_REQUIRE_OK(test, yetty_ymux_session_pane_close(session, first_pane));
    struct yetty_yclass_object_ptr_result gone_res = yetty_ymux_session_pane(session, first_pane);
    YTEST_REQUIRE_ERR(test, gone_res);
    struct yetty_yclass_object_ptr_result alive_res = yetty_ymux_session_pane(session, second_pane);
    YTEST_REQUIRE_OK(test, alive_res);
    struct yetty_yclass_object_ptr_result orphan_res =
        yetty_ymux_session_attachment(session, attachment);
    YTEST_REQUIRE_ERR(test, orphan_res);

    yetty_ymux_session_dispose(session);
}

/*---------------------------------------------------------------------------
 * First attach with a geometry FAR from the pane's creation size, onto a
 * pane that already holds content (the live-bridge scenario: session
 * created 24x80, shell prompt printed, then the controller attaches at
 * the real pane size e.g. 66x279 → canonical resize with content).
 *-------------------------------------------------------------------------*/
static void test_attach_resize_with_content(struct ytest *test)
{
    struct yetty_yclass_object *session = yetty_ymux_session_make().value;
    YTEST_REQUIRE_NOT_NULL(test, session);
    struct yetty_ycore_uint32_result pane_res =
        yetty_ymux_session_pane_create(session, 24, 80, 1024, 0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    uint32_t pane_id = pane_res.value;
    feed_pane(test, session, pane_id, "prompt$ echo something\r\nsomething\r\nprompt$ ");

    uint32_t attachment = yetty_ymux_session_attach(session, pane_id, 66, 279, "big").value;
    YTEST_REQUIRE(test, attachment != 0);
    struct yetty_yclass_object *pane = yetty_ymux_session_pane(session, pane_id).value;
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    uint32_t rows = 0, cols = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 66);
    YTEST_CHECK_EQ_INT(test, cols, 279);

    /* Regression (libvterm lineinfo realloc): after growing 24→66 rows,
     * feed output that reaches the new high rows AND a CSI that writes
     * per-row lineinfo (erase-below "\x1b[J" fills every row past the
     * cursor). Without a state resize callback growing lineinfo, this is
     * a heap-buffer-overflow inside libvterm set_lineinfo — the exact
     * crash a real shell triggered on attach. Under ASAN this line is
     * the sentinel; on a plain build it must simply not corrupt. */
    for (int line = 0; line < 64; ++line) {
        feed_pane(test, session, pane_id, "filler line to reach the bottom rows\r\n");
    }
    feed_pane(test, session, pane_id, "\x1b[H\x1b[J");
    feed_pane(test, session, pane_id, "\x1b[60;1Hlow row content");
    /* And project it — the full live-bridge first-frame path. */
    struct yetty_yclass_object *projector = yetty_ymux_session_projector(session, attachment).value;
    YTEST_REQUIRE_NOT_NULL(test, projector);
    struct yetty_ycore_buffer vt_first = yetty_ycore_buffer_create(1u << 20).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_vt(projector, &vt_first));
    YTEST_CHECK(test, vt_first.size > 0);
    yetty_ycore_buffer_destroy(&vt_first);
    yetty_ymux_session_dispose(session);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_session");
    YTEST_RUN(&test, test_controller_policy);
    YTEST_RUN(&test, test_detach_reconnect);
    YTEST_RUN(&test, test_input_permissions);
    YTEST_RUN(&test, test_pane_close);
    YTEST_RUN(&test, test_attach_resize_with_content);
    return ytest_end(&test);
}
