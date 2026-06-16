/*
 * engine.c — yclass base class `yai:engine`: the abstract AI-CLI engine.
 *
 * One engine drives one headless AI CLI as a child process and turns
 * its event stream into yai's scrollback. The concrete engines are
 * subclasses: yai:claude (claude.c — persistent bidirectional
 * stream-json child), and yai:codex / yai:gemini (one child per turn,
 * via the shared yai:turn_engine intermediate in turn-engine.c).
 *
 * Every slot is `local@` — an engine is an in-process object, never
 * proxied over RPC; the model still records the methods so the binding
 * generators can emit them. Slot contract (all Results, chained):
 *
 *   start(app)                    session start; claude spawns its
 *                                 persistent child here, per-turn
 *                                 engines only prepare the environment
 *   send_user_message(app, text)  begin a turn with the user's text
 *   handle_event(app, event)      one decoded JSONL event from the
 *                                 child's stdout
 *   interrupt(app)                Ctrl-C during a turn
 *   on_child_exit(app, status)    child process exited (uv exit_cb) —
 *                                 session death for claude, the normal
 *                                 turn boundary for per-turn engines
 *   on_child_eof(app)             child stdout closed
 *   resolve_permission(app, ok)   answer the pending can_use_tool
 *                                 request (claude protocol; default is
 *                                 a no-op for engines without one)
 *
 * The default impls error (resolve_permission excepted): the base
 * class is abstract, every concrete engine overrides the lot.
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yai/engine.h` — that header is a downstream artifact for
 * main.c and the subclasses. engine.gen.c (the accessor body) is
 * #included at the foot.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdlib.h>

struct yai_app;
struct yyjson_val;

/* The class@ annotation needs a struct to sit on; the engine carries
 * no instance state of its own (per-engine state lives in the subclass
 * data structs, shared turn state in struct yai_app). The struct's
 * size contributes 1 byte to the instance layout, which is harmless. */
struct [[clang::annotate("class@yai:engine")]] yetty_yai_engine {
    char unused;
};

/* Result wrapper for the engine data slice. Declared here (not pulled
 * from engine.h, which this TU does not include) so the appended
 * engine.gen.c — which defines yetty_yai_engine_from() returning it —
 * has the type in scope. */
YETTY_YRESULT_DECLARE(yetty_yai_engine_ptr, struct yetty_yai_engine *);

/*=============================================================================
 * Method slots — abstract defaults
 *===========================================================================*/

[[clang::annotate("virtual@yai:engine:start")]] [[clang::annotate("local@yai:start")]]
static struct yetty_ycore_void_result engine_start(struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    (void)obj;
    (void)app;
    return YETTY_ERR(yetty_ycore_void, "yai engine: start not implemented (abstract base)");
}

[[clang::annotate("virtual@yai:engine:send_user_message")]] [[clang::annotate(
    "local@yai:send_user_message")]]
static struct yetty_ycore_void_result engine_send_user_message(struct yetty_yclass_object *obj,
                                                               struct yai_app *app,
                                                               const char *text)
{
    (void)obj;
    (void)app;
    (void)text;
    return YETTY_ERR(yetty_ycore_void,
                     "yai engine: send_user_message not implemented (abstract base)");
}

[[clang::annotate("virtual@yai:engine:handle_event")]] [[clang::annotate("local@yai:handle_event")]]
static struct yetty_ycore_void_result engine_handle_event(struct yetty_yclass_object *obj,
                                                          struct yai_app *app,
                                                          struct yyjson_val *event)
{
    (void)obj;
    (void)app;
    (void)event;
    return YETTY_ERR(yetty_ycore_void, "yai engine: handle_event not implemented (abstract base)");
}

[[clang::annotate("virtual@yai:engine:interrupt")]] [[clang::annotate("local@yai:interrupt")]]
static struct yetty_ycore_void_result engine_interrupt(struct yetty_yclass_object *obj,
                                                       struct yai_app *app)
{
    (void)obj;
    (void)app;
    return YETTY_ERR(yetty_ycore_void, "yai engine: interrupt not implemented (abstract base)");
}

[[clang::annotate("virtual@yai:engine:on_child_exit")]] [[clang::annotate(
    "local@yai:on_child_exit")]]
static struct yetty_ycore_void_result engine_on_child_exit(struct yetty_yclass_object *obj,
                                                           struct yai_app *app,
                                                           int64_t exit_status)
{
    (void)obj;
    (void)app;
    (void)exit_status;
    return YETTY_ERR(yetty_ycore_void, "yai engine: on_child_exit not implemented (abstract base)");
}

[[clang::annotate("virtual@yai:engine:on_child_eof")]] [[clang::annotate("local@yai:on_child_eof")]]
static struct yetty_ycore_void_result engine_on_child_eof(struct yetty_yclass_object *obj,
                                                          struct yai_app *app)
{
    (void)obj;
    (void)app;
    return YETTY_ERR(yetty_ycore_void, "yai engine: on_child_eof not implemented (abstract base)");
}

/* describe_config: write the engine-specific configuration rows
 * (model, sandbox/permission knobs, resume token semantics) into
 * `out` as newline-separated "key: value  [ENV_KNOB]" lines. The
 * caller presents them — as labels in the ygui config dialog when the
 * HUD is up, or as scrollback text without one. Errors on truncation.
 * Default: an engine with no knobs writes an empty string. */
[[clang::annotate("virtual@yai:engine:describe_config")]] [[clang::annotate(
    "local@yai:describe_config")]]
static struct yetty_ycore_void_result engine_describe_config(struct yetty_yclass_object *obj,
                                                             struct yai_app *app, char *out,
                                                             size_t out_size)
{
    (void)obj;
    (void)app;
    if (!out || out_size == 0) {
        return YETTY_ERR(yetty_ycore_void, "engine describe_config: no output buffer");
    }
    out[0] = '\0';
    return YETTY_OK_VOID();
}

/* config_knob: the engine's ONE editable knob for the config dialog,
 * as `ENV_KEY|label|option1,option2,…|current-value`. The dialog
 * renders it as a radio group; a selection lands back in apply_config.
 * Default: no knob (empty string). */
[[clang::annotate("virtual@yai:engine:config_knob")]] [[clang::annotate("local@yai:config_knob")]]
static struct yetty_ycore_void_result engine_config_knob(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    (void)obj;
    (void)app;
    if (!out || out_size == 0) {
        return YETTY_ERR(yetty_ycore_void, "engine config_knob: no output buffer");
    }
    out[0] = '\0';
    return YETTY_OK_VOID();
}

/* apply_config: a knob edited in the config dialog. `key` is the engine
 * field name from config_knob, `value` the chosen option. The value is
 * already stored in app->config by the caller (yai_config_set); the per-turn
 * engines read that struct on every spawn, so the change takes effect on the
 * next turn. The base default therefore does nothing. Engines with a live
 * protocol override this (claude pushes the new permission mode into the
 * running session immediately). */
[[clang::annotate("virtual@yai:engine:apply_config")]] [[clang::annotate(
    "local@yai:apply_config")]]
static struct yetty_ycore_void_result engine_apply_config(struct yetty_yclass_object *obj,
                                                          struct yai_app *app, const char *key,
                                                          const char *value)
{
    (void)obj;
    (void)app;
    (void)key;
    (void)value;
    return YETTY_OK_VOID();
}

/* Engines without a permission protocol simply have nothing to answer
 * — a no-op default, NOT an error: main.c calls this unconditionally
 * at shutdown to never leave a CLI blocked on an unanswered prompt. */
[[clang::annotate("virtual@yai:engine:resolve_permission")]] [[clang::annotate(
    "local@yai:resolve_permission")]]
static struct yetty_ycore_void_result engine_resolve_permission(struct yetty_yclass_object *obj,
                                                                struct yai_app *app, int allowed)
{
    (void)obj;
    (void)app;
    (void)allowed;
    return YETTY_OK_VOID();
}

#include "engine.gen.c"
