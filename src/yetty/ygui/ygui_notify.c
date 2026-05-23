/*
 * ygui_notify.c — toast-style notification stack for any ygui engine.
 *
 * Cards stack top-down on the right edge of the canvas. Each card is a
 * regular ygui widget tree (vbox + hbox row + label + close button); the
 * engine renders them like any other top-level widget, so dirty tracking,
 * mouse hit-testing, and the standard click dispatch all "just work".
 *
 * The piece that's specific to notifications is:
 *   - the per-engine state (struct ygui_notification[] on the engine),
 *   - the layout helper that anchors each card relative to the engine's
 *     right edge and stacks them vertically with a fixed gap,
 *   - the TTL tick, run from engine_render before serialise so any
 *     expired cards are dismissed and the resulting frame already shows
 *     the compacted stack.
 */

#include "ygui_internal.h"

#include <yetty/yplatform/time.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Geometry. The dialog dialog gap was 14; notifications use a slightly
 * larger 8-px inter-card gap (the cards themselves carry their padding)
 * so they're easier to distinguish at a glance. */
#define NOTIF_CARD_W      340.0f
#define NOTIF_RIGHT_PAD   12.0f
#define NOTIF_TOP_PAD     12.0f
#define NOTIF_CARD_GAP    8.0f
#define NOTIF_CARD_PAD    "10 12 10 12" /* T R B L — keep stripe visible left */
#define NOTIF_DEFAULT_TTL_MS       4000u
#define NOTIF_DEFAULT_TTL_ERROR_MS 10000u

static uint64_t now_monotonic_ms(void)
{
    return (uint64_t)(yetty_yplatform_ytime_monotonic_sec() * 1000.0);
}

/* Severity → stripe colour (ARGB). Background of the card is brand
 * BG_LIFTED; only the left-edge stripe carries the severity colour so
 * a wall of toasts doesn't shout. */
static uint32_t severity_stripe_color(int sev)
{
    switch (sev) {
    case YETTY_YGUI_SEV_INFO:  return 0xFF6BA892u; /* BRAND_ACCENT mint */
    case YETTY_YGUI_SEV_WARN:  return 0xFFD2A45Au; /* warm amber */
    case YETTY_YGUI_SEV_ERROR: return 0xFFC25E5Eu; /* muted crimson */
    default:                   return 0xFF6BA892u;
    }
}

static const char *severity_tag(int sev)
{
    switch (sev) {
    case YETTY_YGUI_SEV_INFO:  return "INFO";
    case YETTY_YGUI_SEV_WARN:  return "WARN";
    case YETTY_YGUI_SEV_ERROR: return "ERROR";
    default:                   return "INFO";
    }
}

/* Default per-severity TTL when the caller doesn't pin one. Errors get
 * longer than info/warn so the user actually reads them. */
static uint32_t severity_default_ttl_ms(int sev)
{
    if (sev == YETTY_YGUI_SEV_ERROR) return NOTIF_DEFAULT_TTL_ERROR_MS;
    return NOTIF_DEFAULT_TTL_MS;
}

/* Re-anchor every alive card relative to the engine's current width.
 * Called after every push / dismiss and on resize. Cards are positioned
 * by index: i=0 lands at the top, i=N-1 at the bottom. */
static void notify_relayout(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return;
    }
    float canvas_w = engine->width > 0.0f ? engine->width : 0.0f;
    float y = NOTIF_TOP_PAD;
    for (int i = 0; i < engine->notification_count; i++) {
        struct ygui_notification *n = &engine->notifications[i];
        if (!n->card) {
            continue;
        }
        float card_h = n->card->h > 0.0f ? n->card->h : 56.0f;
        float x = canvas_w - NOTIF_CARD_W - NOTIF_RIGHT_PAD;
        if (x < 0.0f) {
            x = 0.0f;
        }
        yetty_ygui_widget_set_position(n->card, x, y);
        y += card_h + NOTIF_CARD_GAP;
    }
    engine->dirty = 1;
}

/* Find a notification by its close-button widget — used by the close
 * callback. Returns the slot index or -1. */
static int notify_find_by_close_btn(const struct yetty_ygui_engine *engine,
                                    const struct yetty_ygui_widget *btn)
{
    for (int i = 0; i < engine->notification_count; i++) {
        if (engine->notifications[i].close_btn == btn) {
            return i;
        }
    }
    return -1;
}

/* Hard-dismiss: free the message + remove the card widget tree from the
 * engine, then compact the array so positions stay packed. The engine
 * exposes a remove-widget API (see yetty_ygui_widget_destroy) — we use
 * it on the top-level card; its children go down with it. */
static void notify_dismiss_at(struct yetty_ygui_engine *engine, int idx)
{
    if (!engine || idx < 0 || idx >= engine->notification_count) {
        return;
    }
    struct ygui_notification *n = &engine->notifications[idx];
    if (n->card) {
        yetty_ygui_widget_remove(n->card);
        n->card = NULL;
        n->close_btn = NULL;
    }
    free(n->message);
    n->message = NULL;

    int last = engine->notification_count - 1;
    for (int i = idx; i < last; i++) {
        engine->notifications[i] = engine->notifications[i + 1];
    }
    memset(&engine->notifications[last], 0, sizeof(struct ygui_notification));
    engine->notification_count--;
    notify_relayout(engine);
}

/* Click on a card's close ✕. The userdata is the engine pointer; we
 * recover the index by widget identity rather than baking the index in
 * (indices shift on dismiss). */
static void notify_close_clicked(struct yetty_ygui_widget *btn, void *userdata)
{
    struct yetty_ygui_engine *engine = (struct yetty_ygui_engine *)userdata;
    if (!engine || !btn) {
        return;
    }
    int idx = notify_find_by_close_btn(engine, btn);
    if (idx >= 0) {
        notify_dismiss_at(engine, idx);
    }
}

/* Build the widget tree for one notification card. Layout:
 *
 *   ┌─┬──────────────────────────────────────────────┬───┐
 *   │S│ TAG  message ......................          │ ✕ │
 *   └─┴──────────────────────────────────────────────┴───┘
 *
 * S is a 4-px-wide stripe coloured by severity. The actual brand
 * palette goes into the card via apply_css. */
static struct yetty_ygui_widget *notify_build_card(struct yetty_ygui_engine *engine,
                                                   int slot, int severity,
                                                   const char *message)
{
    char figure_id[48], stripe_id[48], lbl_id[48], close_id[48], row_id[48];
    snprintf(figure_id,   sizeof(figure_id),   "ygui_notif_%d_card",   slot);
    snprintf(row_id,    sizeof(row_id),    "ygui_notif_%d_row",    slot);
    snprintf(stripe_id, sizeof(stripe_id), "ygui_notif_%d_stripe", slot);
    snprintf(lbl_id,    sizeof(lbl_id),    "ygui_notif_%d_lbl",    slot);
    snprintf(close_id,  sizeof(close_id),  "ygui_notif_%d_close",  slot);

    /* Card: a vbox that fills NOTIF_CARD_W and grows vertically with the
     * label. background BRAND_BG_LIFTED, rounded corners, 1-px BRAND_BORDER. */
    struct yetty_ygui_widget *card =
        yetty_ygui_engine_vbox(engine, figure_id, 0, 0, NOTIF_CARD_W, 0);
    if (!card) {
        return NULL;
    }
    yetty_ygui_widget_set_size(card, NOTIF_CARD_W, 56);
    char card_css[256];
    snprintf(card_css, sizeof(card_css),
             "background:#141A1F;border-radius:8;border:1 #364A47;"
             "padding:%s;", NOTIF_CARD_PAD);
    yetty_ygui_widget_apply_css(card, card_css);

    struct yetty_ygui_widget *row =
        yetty_ygui_engine_hbox(engine, row_id, 0, 0, 0, 0);
    if (!row) {
        yetty_ygui_widget_remove(card);
        return NULL;
    }
    yetty_ygui_widget_apply_css(row,
        "display:flex;flex-direction:row;gap:10;align-items:start;");
    yetty_ygui_widget_add_child(card, row);

    /* Severity stripe — a thin vertical bar. yetty_ygui_engine_separator
     * is the lightest widget for "a coloured rectangle". */
    struct yetty_ygui_widget *stripe =
        yetty_ygui_engine_separator(engine, stripe_id, 0, 0, 4, 0);
    if (stripe) {
        char css[64];
        snprintf(css, sizeof(css), "background:#%06X;flex:0 0 4;align-self:stretch;",
                 severity_stripe_color(severity) & 0xFFFFFFu);
        yetty_ygui_widget_apply_css(stripe, css);
        yetty_ygui_widget_add_child(row, stripe);
    }

    /* "TAG  message" — single label with the severity tag prepended.
     * Multi-line labels grow the card vertically via the engine's flex
     * layout, which is what we want here. */
    char composed[1024];
    snprintf(composed, sizeof(composed), "%s  %s", severity_tag(severity),
             message ? message : "");
    struct yetty_ygui_widget *lbl =
        yetty_ygui_engine_label(engine, lbl_id, 0, 0, composed);
    if (lbl) {
        yetty_ygui_widget_apply_css(lbl, "flex:1 0 0;");
        yetty_ygui_widget_add_child(row, lbl);
    }

    /* Close ✕ — small button anchored on the right. Pixel-art "x" via
     * the label glyph is enough; a fancier render comes later. */
    struct yetty_ygui_widget *close_btn =
        yetty_ygui_engine_button(engine, close_id, 0, 0, 22, 22, "x");
    if (close_btn) {
        yetty_ygui_widget_apply_css(close_btn,
            "flex:0 0 22;background:transparent;");
        yetty_ygui_widget_button_on_click(close_btn, notify_close_clicked, engine);
        yetty_ygui_widget_add_child(row, close_btn);
        engine->notifications[slot].close_btn = close_btn;
    }
    return card;
}

/* Public: push a new notification. Auto-dismiss after `ttl_ms`; 0 means
 * sticky (user must click ✕). If the stack is full, the oldest entry is
 * dropped to make room. */
static void notify_push(struct yetty_ygui_engine *engine, int severity,
                        uint32_t ttl_ms, const char *message)
{
    if (!engine || !message) {
        return;
    }
    int cap = (int)(sizeof(engine->notifications) / sizeof(engine->notifications[0]));

    /* Drop oldest if full. */
    if (engine->notification_count >= cap) {
        notify_dismiss_at(engine, 0);
    }

    int idx = engine->notification_count;
    struct ygui_notification *n = &engine->notifications[idx];
    n->message    = strdup(message);
    n->severity   = severity;
    n->created_ms = now_monotonic_ms();
    n->ttl_ms     = ttl_ms;
    n->card       = notify_build_card(engine, idx, severity, message);
    if (!n->card) {
        free(n->message);
        n->message = NULL;
        return;
    }
    engine->notification_count++;
    notify_relayout(engine);
}

void yetty_ygui_engine_notify(struct yetty_ygui_engine *engine,
                              enum yetty_ygui_severity sev, const char *fmt, ...)
{
    if (!engine || !fmt) {
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_push(engine, (int)sev, severity_default_ttl_ms((int)sev), buf);
}

void yetty_ygui_engine_notify_ttl(struct yetty_ygui_engine *engine,
                                  enum yetty_ygui_severity sev, uint32_t ttl_ms,
                                  const char *fmt, ...)
{
    if (!engine || !fmt) {
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_push(engine, (int)sev, ttl_ms, buf);
}

/* Engine-internal hook called at the top of engine_render. Walks the
 * notification array and dismisses any whose TTL has elapsed. Sticky
 * cards (ttl_ms == 0) are skipped. If any card was dismissed the engine
 * is marked dirty so the next frame already shows the compacted stack.
 *
 * Also keeps the engine "dirty enough to render again on the next tick"
 * while any non-sticky notification is alive — without that, a quiet UI
 * after the toast appears would never repaint, and the toast wouldn't
 * actually disappear when its TTL elapses. */
void yetty_ygui_engine_notify_tick(struct yetty_ygui_engine *engine)
{
    if (!engine || engine->notification_count <= 0) {
        return;
    }
    uint64_t now = now_monotonic_ms();
    int i = 0;
    while (i < engine->notification_count) {
        struct ygui_notification *n = &engine->notifications[i];
        if (n->ttl_ms > 0 && (now - n->created_ms) >= (uint64_t)n->ttl_ms) {
            notify_dismiss_at(engine, i);
            continue; /* compacted; re-check same idx */
        }
        i++;
    }
    /* While any auto-dismiss card is alive, force a render next tick so
     * the TTL check fires again on schedule. Sticky cards alone don't
     * need this. */
    for (int j = 0; j < engine->notification_count; j++) {
        if (engine->notifications[j].ttl_ms > 0) {
            engine->dirty = 1;
            break;
        }
    }
}

/* Called by engine on resize so the stack re-pins to the new right
 * edge. Symmetric with notify_relayout but exposed for engine_render. */
void yetty_ygui_engine_notify_on_resize(struct yetty_ygui_engine *engine)
{
    notify_relayout(engine);
}

/* Free every notification on engine destroy. The card widgets are torn
 * down by the engine's normal widget-tree teardown; we only need to
 * release the message strings here. */
void yetty_ygui_engine_notify_shutdown(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return;
    }
    for (int i = 0; i < engine->notification_count; i++) {
        free(engine->notifications[i].message);
        engine->notifications[i].message = NULL;
        engine->notifications[i].card = NULL;
        engine->notifications[i].close_btn = NULL;
    }
    engine->notification_count = 0;
}
