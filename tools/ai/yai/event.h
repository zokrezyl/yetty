/*
 * event.h — yai's provider-neutral conversation event model.
 *
 * The engines (claude.c / codex.c / gemini.c) parse their
 * provider-specific JSONL into these normalized events and emit them
 * through yai_event_dispatch; the renderer, HUD and session accounting
 * consume them. This keeps every provider quirk inside its engine and
 * gives the UI ONE stable surface to render — and one choke point to,
 * later, record for transcript replay and renderer regression tests.
 *
 * An event is a plain value: a tagged union of borrowed pointers and
 * scalars, valid only for the duration of the dispatch call. Pointer
 * payloads (tool input, tool-result content, hook frames) borrow the
 * engine's parsed JSON; the dispatcher must not retain them past the
 * call.
 */
#ifndef YAI_EVENT_H
#define YAI_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct yai_app;
struct yyjson_val;

enum yai_event_kind {
    /* An assistant message turn started (stream-json message_start). */
    YAI_EVENT_MESSAGE_BEGIN,
    /* A run of streamed assistant text; ends any open thinking block. */
    YAI_EVENT_TEXT_DELTA,
    /* A run of streamed extended-thinking text. */
    YAI_EVENT_THINKING_DELTA,
    /* The current thinking block ended (content-block stop, or the
     * first non-thinking content of a message). */
    YAI_EVENT_THINKING_END,
    /* The model invoked a tool. */
    YAI_EVENT_TOOL_CALL,
    /* A tool produced a result (a figure is detected downstream). */
    YAI_EVENT_TOOL_RESULT,
    /* A hook lifecycle frame. */
    YAI_EVENT_HOOK,
    /* The turn's streamed output is complete: flush the open line and
     * clear the activity glyph. Emitted before YAI_EVENT_USAGE. */
    YAI_EVENT_TURN_END,
    /* The turn's accounting (tokens / cost / duration). */
    YAI_EVENT_USAGE,
};

struct yai_event_text {
    const char *text; /* not necessarily NUL-terminated */
    size_t len;
};

struct yai_event_tool_call {
    const char *name;
    struct yyjson_val *input; /* borrowed; may be NULL */
};

struct yai_event_tool_result {
    struct yyjson_val *content; /* borrowed; string or block list */
    int is_error;
    const char *tool_name; /* may be NULL when the call wasn't tracked */
};

struct yai_event_hook {
    struct yyjson_val *event; /* borrowed */
};

/* Normalized per-turn accounting. Unknown fields are 0 (codex/gemini
 * report neither cost nor duration). */
struct yai_event_usage {
    uint64_t input;
    uint64_t output;
    uint64_t cache_read;
    uint64_t cache_creation;
    double cost;    /* USD; valid only when has_cost */
    double seconds; /* turn wall-clock; 0 when unknown (no timing shown) */
    /* The provider reports a real cost (claude). When 0 the per-turn and
     * session lines omit the "$…" segment entirely rather than print a
     * misleading "$0.0000" for providers that don't report cost. */
    int has_cost;
};

struct yai_event {
    enum yai_event_kind kind;
    union {
        struct yai_event_text text;               /* TEXT/THINKING_DELTA */
        struct yai_event_tool_call tool_call;     /* TOOL_CALL */
        struct yai_event_tool_result tool_result; /* TOOL_RESULT */
        struct yai_event_hook hook;               /* HOOK */
        struct yai_event_usage usage;             /* USAGE */
    };
};

/* Consume one normalized event: drive the renderer, HUD and session
 * accounting. The single choke point every engine routes through. */
struct yetty_ycore_void_result yai_event_dispatch(struct yai_app *app,
                                                  const struct yai_event *event);

#endif /* YAI_EVENT_H */
