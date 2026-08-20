/*
 * session.c — the ymux session model: class@ymux:session (#695 phase 5).
 *
 * The daemon-side authority over windows, panes, attachments, and the
 * controller/input policy. tmux ownership semantics: panes (and their
 * PTY-facing engines/history) belong to the session and outlive every
 * attachment; an attachment is a VIEW plus permissions.
 *
 * V1 policy (#695): the FIRST eligible attachment controls canonical pane
 * geometry until explicit authorized takeover, detach, or lease expiry;
 * reconnect with the same token may resume control; attaching never
 * resizes as a side effect; non-controllers crop/pad; input and resize
 * permissions are separate.
 *
 * Phase-5 scope here: the model (windows/panes/attachments/policy). The
 * daemon socket lifecycle (daemon.c) drives it; PTY supervision plugs in
 * through the pane host seam.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>

enum {
    YMUX_SESSION_MAX_PANES = 64,
    YMUX_SESSION_MAX_ATTACHMENTS = 32,
    YMUX_SESSION_TOKEN_MAX = 63,
};

enum YETTY_ANNOTATE("expose") yetty_ymux_permission {
    YETTY_YMUX_PERMISSION_INPUT = 1u << 0,
    YETTY_YMUX_PERMISSION_RESIZE = 1u << 1,
};

struct session_pane_slot {
    struct yetty_yclass_object *pane; /* owned; NULL = free slot */
    uint32_t pane_id;
};

struct session_attachment_slot {
    struct yetty_yclass_object *attachment; /* owned; NULL = free slot */
    struct yetty_yclass_object *projector;  /* owned, paired */
    uint32_t attachment_id;
    uint32_t pane_id; /* viewed pane */
    uint32_t permissions;
    char token[YMUX_SESSION_TOKEN_MAX + 1];
};

/* The session — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:session") yetty_ymux_session {
    struct session_pane_slot panes[YMUX_SESSION_MAX_PANES];
    uint32_t next_pane_id;

    struct session_attachment_slot attachments[YMUX_SESSION_MAX_ATTACHMENTS];
    uint32_t next_attachment_id;

    /* Controller: the attachment id owning canonical geometry (0 = none —
     * the first eligible attach claims it). */
    uint32_t controller_attachment_id;
    char controller_token[YMUX_SESSION_TOKEN_MAX + 1];

    uint32_t active_pane_id;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_session_class_get(void);
struct yetty_ymux_session_ptr_result yetty_ymux_session_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_session_ptr, struct yetty_ymux_session *);

static struct session_pane_slot *session_find_pane(struct yetty_ymux_session *session,
                                                   uint32_t pane_id)
{
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_PANES; ++index) {
        if (session->panes[index].pane && session->panes[index].pane_id == pane_id) {
            return &session->panes[index];
        }
    }
    return NULL;
}

static struct session_attachment_slot *session_find_attachment(struct yetty_ymux_session *session,
                                                               uint32_t attachment_id)
{
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_ATTACHMENTS; ++index) {
        if (session->attachments[index].attachment &&
            session->attachments[index].attachment_id == attachment_id) {
            return &session->attachments[index];
        }
    }
    return NULL;
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_session_make(void)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_session_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux session_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux session_make: alloc");
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(object_res.value);
    if (YETTY_IS_ERR(session_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session_make: from_obj", session_res);
    }
    session_res.value->next_pane_id = 1;
    session_res.value->next_attachment_id = 1;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_dispose: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_ATTACHMENTS; ++index) {
        struct session_attachment_slot *slot = &session->attachments[index];
        if (slot->projector) {
            struct yetty_ycore_void_result projector_res =
                yetty_ymux_projector_dispose(slot->projector);
            if (YETTY_IS_ERR(projector_res)) {
                yetty_ycore_error_destroy(projector_res.error);
            }
        }
        if (slot->attachment) {
            struct yetty_ycore_void_result attachment_res =
                yetty_ymux_attachment_dispose(slot->attachment);
            if (YETTY_IS_ERR(attachment_res)) {
                yetty_ycore_error_destroy(attachment_res.error);
            }
        }
        memset(slot, 0, sizeof(*slot));
    }
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_PANES; ++index) {
        if (session->panes[index].pane) {
            struct yetty_ycore_void_result pane_res =
                yetty_ymux_pane_dispose(session->panes[index].pane);
            if (YETTY_IS_ERR(pane_res)) {
                yetty_ycore_error_destroy(pane_res.error);
            }
            session->panes[index].pane = NULL;
        }
    }
    return yetty_yclass_object_free(obj);
}

/*===========================================================================
 * Panes.
 *=========================================================================*/

/* Create a pane (its engine host is the caller's — the daemon wires PTY
 * output there). Returns the pane id. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_session_pane_create(
    struct yetty_yclass_object *obj, uint32_t rows, uint32_t cols, uint32_t hot_rows,
    uint64_t total_row_cap, const struct yetty_ymux_engine_host *host)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, session_res, "ymux session_pane_create: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_PANES; ++index) {
        if (session->panes[index].pane) {
            continue;
        }
        struct yetty_yclass_object_ptr_result pane_res =
            yetty_ymux_pane_make(rows, cols, hot_rows, total_row_cap, host);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, pane_res, "ymux session_pane_create: pane");
        session->panes[index].pane = pane_res.value;
        session->panes[index].pane_id = session->next_pane_id++;
        if (session->active_pane_id == 0) {
            session->active_pane_id = session->panes[index].pane_id;
        }
        return YETTY_OK(yetty_ycore_uint32, session->panes[index].pane_id);
    }
    return YETTY_ERR(yetty_ycore_uint32, "ymux session_pane_create: pane table full");
}

/* Close a pane: its attachments detach first (projectors die with them). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_pane_close(struct yetty_yclass_object *obj,
                                                             uint32_t pane_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_pane_close: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    struct session_pane_slot *slot = session_find_pane(session, pane_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_pane_close: unknown pane");
    }
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_ATTACHMENTS; ++index) {
        struct session_attachment_slot *attachment_slot = &session->attachments[index];
        if (!attachment_slot->attachment || attachment_slot->pane_id != pane_id) {
            continue;
        }
        if (attachment_slot->projector) {
            struct yetty_ycore_void_result projector_res =
                yetty_ymux_projector_dispose(attachment_slot->projector);
            if (YETTY_IS_ERR(projector_res)) {
                yetty_ycore_error_destroy(projector_res.error);
            }
        }
        struct yetty_ycore_void_result attachment_res =
            yetty_ymux_attachment_dispose(attachment_slot->attachment);
        if (YETTY_IS_ERR(attachment_res)) {
            yetty_ycore_error_destroy(attachment_res.error);
        }
        if (session->controller_attachment_id == attachment_slot->attachment_id) {
            session->controller_attachment_id = 0;
        }
        memset(attachment_slot, 0, sizeof(*attachment_slot));
    }
    struct yetty_ycore_void_result pane_res = yetty_ymux_pane_dispose(slot->pane);
    if (YETTY_IS_ERR(pane_res)) {
        yetty_ycore_error_destroy(pane_res.error);
    }
    slot->pane = NULL;
    if (session->active_pane_id == pane_id) {
        session->active_pane_id = 0;
        for (uint32_t index = 0; index < YMUX_SESSION_MAX_PANES; ++index) {
            if (session->panes[index].pane) {
                session->active_pane_id = session->panes[index].pane_id;
                break;
            }
        }
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_session_pane(struct yetty_yclass_object *obj,
                                                              uint32_t pane_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, session_res, "ymux session_pane: from_obj");
    struct session_pane_slot *slot = session_find_pane(session_res.value, pane_id);
    if (!slot) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session_pane: unknown pane");
    }
    return YETTY_OK(yetty_yclass_object_ptr, slot->pane);
}

/*===========================================================================
 * Attachments + controller policy.
 *=========================================================================*/

/* Attach a client to a pane. First eligible attachment becomes controller
 * (and only then does the canonical pane resize to ITS geometry — attach
 * itself never resizes for non-controllers); reconnecting with the
 * controller's token resumes control. Returns the attachment id. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_session_attach(struct yetty_yclass_object *obj,
                                                           uint32_t pane_id, uint32_t view_rows,
                                                           uint32_t view_cols, const char *token)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, session_res, "ymux session_attach: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    struct session_pane_slot *pane_slot = session_find_pane(session, pane_id);
    if (!pane_slot) {
        return YETTY_ERR(yetty_ycore_uint32, "ymux session_attach: unknown pane");
    }
    for (uint32_t index = 0; index < YMUX_SESSION_MAX_ATTACHMENTS; ++index) {
        struct session_attachment_slot *slot = &session->attachments[index];
        if (slot->attachment) {
            continue;
        }
        struct yetty_yclass_object_ptr_result attachment_res =
            yetty_ymux_attachment_make(pane_slot->pane, view_rows, view_cols);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, attachment_res, "ymux session_attach: attach");
        struct yetty_yclass_object_ptr_result projector_res =
            yetty_ymux_projector_make(pane_slot->pane, attachment_res.value);
        if (YETTY_IS_ERR(projector_res)) {
            struct yetty_ycore_void_result dispose_res =
                yetty_ymux_attachment_dispose(attachment_res.value);
            if (YETTY_IS_ERR(dispose_res)) {
                yetty_ycore_error_destroy(dispose_res.error);
            }
            return YETTY_ERR(yetty_ycore_uint32, "ymux session_attach: projector", projector_res);
        }
        slot->attachment = attachment_res.value;
        slot->projector = projector_res.value;
        slot->attachment_id = session->next_attachment_id++;
        slot->pane_id = pane_id;
        slot->permissions = YETTY_YMUX_PERMISSION_INPUT;
        slot->token[0] = 0;
        if (token) {
            strncpy(slot->token, token, YMUX_SESSION_TOKEN_MAX);
            slot->token[YMUX_SESSION_TOKEN_MAX] = 0;
        }

        int becomes_controller = 0;
        if (session->controller_attachment_id == 0) {
            /* First eligible attachment, or a token-matched reconnect. */
            if (session->controller_token[0] == 0 ||
                (token && strncmp(session->controller_token, token, YMUX_SESSION_TOKEN_MAX) == 0)) {
                becomes_controller = 1;
            } else if (session->controller_token[0] != 0) {
                /* The prior controller detached; a new first attachment
                 * claims control. */
                becomes_controller = 1;
            }
        }
        if (becomes_controller) {
            session->controller_attachment_id = slot->attachment_id;
            strncpy(session->controller_token, slot->token, YMUX_SESSION_TOKEN_MAX);
            session->controller_token[YMUX_SESSION_TOKEN_MAX] = 0;
            slot->permissions |= YETTY_YMUX_PERMISSION_RESIZE;
            /* The CONTROLLER's geometry becomes canonical. */
            struct yetty_ycore_void_result resize_res =
                yetty_ymux_pane_resize(pane_slot->pane, view_rows, view_cols);
            if (YETTY_IS_ERR(resize_res)) {
                yetty_ycore_error_destroy(resize_res.error);
            }
        }
        return YETTY_OK(yetty_ycore_uint32, slot->attachment_id);
    }
    return YETTY_ERR(yetty_ycore_uint32, "ymux session_attach: attachment table full");
}

/* Detach: the pane survives; controller role frees (token remembered for
 * resume-on-reconnect). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_detach(struct yetty_yclass_object *obj,
                                                         uint32_t attachment_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_detach: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    struct session_attachment_slot *slot = session_find_attachment(session, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_detach: unknown attachment");
    }
    if (slot->projector) {
        struct yetty_ycore_void_result projector_res =
            yetty_ymux_projector_dispose(slot->projector);
        if (YETTY_IS_ERR(projector_res)) {
            yetty_ycore_error_destroy(projector_res.error);
        }
    }
    struct yetty_ycore_void_result attachment_res = yetty_ymux_attachment_dispose(slot->attachment);
    if (YETTY_IS_ERR(attachment_res)) {
        yetty_ycore_error_destroy(attachment_res.error);
    }
    if (session->controller_attachment_id == attachment_id) {
        session->controller_attachment_id = 0; /* token stays for resume */
    }
    memset(slot, 0, sizeof(*slot));
    return YETTY_OK_VOID();
}

/* Explicit authorized takeover: the attachment becomes controller and its
 * geometry becomes canonical. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_takeover(struct yetty_yclass_object *obj,
                                                           uint32_t attachment_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_takeover: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    struct session_attachment_slot *slot = session_find_attachment(session, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_takeover: unknown attachment");
    }
    struct session_attachment_slot *previous =
        session_find_attachment(session, session->controller_attachment_id);
    if (previous && previous != slot) {
        previous->permissions &= ~(uint32_t)YETTY_YMUX_PERMISSION_RESIZE;
    }
    session->controller_attachment_id = attachment_id;
    strncpy(session->controller_token, slot->token, YMUX_SESSION_TOKEN_MAX);
    session->controller_token[YMUX_SESSION_TOKEN_MAX] = 0;
    slot->permissions |= YETTY_YMUX_PERMISSION_RESIZE;
    struct session_pane_slot *pane_slot = session_find_pane(session, slot->pane_id);
    if (pane_slot) {
        uint32_t view_rows = 0, view_cols = 0;
        struct yetty_ycore_void_result size_res =
            yetty_ymux_attachment_view_size(slot->attachment, &view_rows, &view_cols);
        if (YETTY_IS_OK(size_res)) {
            struct yetty_ycore_void_result resize_res =
                yetty_ymux_pane_resize(pane_slot->pane, view_rows, view_cols);
            if (YETTY_IS_ERR(resize_res)) {
                yetty_ycore_error_destroy(resize_res.error);
            }
        }
    }
    return YETTY_OK_VOID();
}

/* Controller-driven resize; non-controllers are rejected (their VIEW size
 * is attachment state — set_view_size — and crops/pads instead). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_resize(struct yetty_yclass_object *obj,
                                                         uint32_t attachment_id, uint32_t rows,
                                                         uint32_t cols)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_resize: from_obj");
    struct yetty_ymux_session *session = session_res.value;
    struct session_attachment_slot *slot = session_find_attachment(session, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_resize: unknown attachment");
    }
    struct yetty_ycore_void_result view_res =
        yetty_ymux_attachment_set_view_size(slot->attachment, rows, cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "ymux session_resize: view size");
    if (!(slot->permissions & YETTY_YMUX_PERMISSION_RESIZE)) {
        return YETTY_OK_VOID(); /* view-only: crop/pad at projection */
    }
    struct session_pane_slot *pane_slot = session_find_pane(session, slot->pane_id);
    if (!pane_slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_resize: pane gone");
    }
    return yetty_ymux_pane_resize(pane_slot->pane, rows, cols);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_session_controller(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, session_res, "ymux session_controller: from_obj");
    return YETTY_OK(yetty_ycore_uint32, session_res.value->controller_attachment_id);
}

/* The active (most recently relevant) pane id, 0 when the session has no
 * panes — the default attach target (tmux `attach` semantics: reuse the
 * running session; only a pane-less session spawns fresh). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_session_active_pane(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, session_res, "ymux session_active_pane: from_obj");
    return YETTY_OK(yetty_ycore_uint32, session_res.value->active_pane_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_session_permissions(struct yetty_yclass_object *obj,
                                                                uint32_t attachment_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, session_res, "ymux session_permissions: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_uint32, "ymux session_permissions: unknown attachment");
    }
    return YETTY_OK(yetty_ycore_uint32, slot->permissions);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_set_permissions(struct yetty_yclass_object *obj,
                                                                  uint32_t attachment_id,
                                                                  uint32_t permissions)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_set_permissions: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_set_permissions: unknown attachment");
    }
    slot->permissions = permissions;
    return YETTY_OK_VOID();
}

/* Input from an attachment: permission-gated; encoding happens in the
 * pane's canonical engine. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_input_char(struct yetty_yclass_object *obj,
                                                             uint32_t attachment_id,
                                                             uint32_t codepoint, int mods)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_input_char: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_char: unknown attachment");
    }
    if (!(slot->permissions & YETTY_YMUX_PERMISSION_INPUT)) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_char: input not permitted");
    }
    struct session_pane_slot *pane_slot = session_find_pane(session_res.value, slot->pane_id);
    if (!pane_slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_char: pane gone");
    }
    struct yetty_yclass_object_ptr_result engine_res = yetty_ymux_pane_engine(pane_slot->pane);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_res, "ymux session_input_char: engine");
    return yetty_ymux_engine_input_char(engine_res.value, codepoint, mods);
}

/* Special-key input from an attachment: permission-gated exactly like
 * session_input_char — a read-only (non-INPUT) attachment must not be able to
 * inject Enter/arrows/delete/etc. into the application PTY. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_input_key(struct yetty_yclass_object *obj,
                                                            uint32_t attachment_id, int key,
                                                            int mods)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_input_key: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_key: unknown attachment");
    }
    if (!(slot->permissions & YETTY_YMUX_PERMISSION_INPUT)) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_key: input not permitted");
    }
    struct session_pane_slot *pane_slot = session_find_pane(session_res.value, slot->pane_id);
    if (!pane_slot) {
        return YETTY_ERR(yetty_ycore_void, "ymux session_input_key: pane gone");
    }
    struct yetty_yclass_object_ptr_result engine_res = yetty_ymux_pane_engine(pane_slot->pane);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_res, "ymux session_input_key: engine");
    return yetty_ymux_engine_input_key(engine_res.value, key, mods);
}

/* Resolve the input-permitted attachment's pane engine, or an error. Shared by
 * the mouse/paste input paths (same gate as char/key input). */
static struct yetty_yclass_object_ptr_result session_input_engine(
    struct yetty_ymux_session *session, uint32_t attachment_id)
{
    struct session_attachment_slot *slot = session_find_attachment(session, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session input: unknown attachment");
    }
    if (!(slot->permissions & YETTY_YMUX_PERMISSION_INPUT)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session input: not permitted");
    }
    struct session_pane_slot *pane_slot = session_find_pane(session, slot->pane_id);
    if (!pane_slot) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session input: pane gone");
    }
    return yetty_ymux_pane_engine(pane_slot->pane);
}

/* Mouse motion from an attachment (permission-gated); the engine encodes it per
 * the application's active mouse mode (or drops it when no mode is enabled). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_move(struct yetty_yclass_object *obj,
                                                                   uint32_t attachment_id,
                                                                   uint32_t row, uint32_t col,
                                                                   int mods)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_input_mouse_move: from_obj");
    struct yetty_yclass_object_ptr_result engine_res =
        session_input_engine(session_res.value, attachment_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_res, "ymux session_input_mouse_move: engine");
    return yetty_ymux_engine_input_mouse_move(engine_res.value, row, col, mods);
}

/* Mouse button press/release from an attachment (permission-gated). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_button(
    struct yetty_yclass_object *obj, uint32_t attachment_id, int button, int pressed, int mods)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_input_mouse_button: from_obj");
    struct yetty_yclass_object_ptr_result engine_res =
        session_input_engine(session_res.value, attachment_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_res, "ymux session_input_mouse_button: engine");
    return yetty_ymux_engine_input_mouse_button(engine_res.value, button, pressed, mods);
}

/* Bracketed-paste content from an attachment (permission-gated); the engine
 * wraps it per the app's paste mode. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_session_input_paste(struct yetty_yclass_object *obj,
                                                              uint32_t attachment_id,
                                                              const char *text, size_t len)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "ymux session_input_paste: from_obj");
    struct yetty_yclass_object_ptr_result engine_res =
        session_input_engine(session_res.value, attachment_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_res, "ymux session_input_paste: engine");
    return yetty_ymux_engine_input_paste(engine_res.value, text, len);
}

/* The attachment's projector (the daemon pumps it). Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_session_projector(struct yetty_yclass_object *obj,
                                                                   uint32_t attachment_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, session_res, "ymux session_projector: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session_projector: unknown attachment");
    }
    return YETTY_OK(yetty_yclass_object_ptr, slot->projector);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_session_attachment(struct yetty_yclass_object *obj,
                                                                    uint32_t attachment_id)
{
    struct yetty_ymux_session_ptr_result session_res = yetty_ymux_session_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, session_res, "ymux session_attachment: from_obj");
    struct session_attachment_slot *slot =
        session_find_attachment(session_res.value, attachment_id);
    if (!slot) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux session_attachment: unknown attachment");
    }
    return YETTY_OK(yetty_yclass_object_ptr, slot->attachment);
}

#include "yetty/gen/impl/ymux/session.c"
