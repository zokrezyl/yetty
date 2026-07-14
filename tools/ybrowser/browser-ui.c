/*
 * browser-ui.c — ybrowser's interactive Chrome-like mode, built on the
 * ygui widget framework.
 *
 * Layout (root vbox):
 *   ┌ tabbar ─────────────────────────────────────────────── + ┐
 *   │ [←] [→] [⟳/✕] [ address bar .............................. ] │  toolbar
 *   ├──────────────────────────────────────────────────────────┤
 *   │ scrollarea → ydraw_embed  (the rendered page)              │
 *   └──────────────────────────────────────────────────────────┘
 *
 * The tool runs as a ygui client of the host yetty: the framework writes
 * its OSC envelopes to stdout (a thin blocking pty), and the host forwards
 * pane-wide mouse / resize events back as OSC envelopes which a yetty_yface
 * decoder turns into framework input. Non-OSC bytes (real keystrokes) go
 * straight to the framework's own decoder.
 *
 * Each tab owns a *persistent* yetty_ylexbor engine (so JS, DOM, timers and
 * click dispatch survive across frames); the active tab is rendered into a
 * fresh draw list and handed to a shared ydraw_embed. We deliberately do
 * NOT use the ygui:ybrowser widget — it recreates a throwaway engine every
 * frame, which loses all of that state.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yface/yface.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yimage/yimage.h>
#include <yetty/ysvg/ysvg.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/yclipboard/clipboard.h>
#include <yetty/yplatform/yworkpool.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>

#include "browser-ui.h"

/* ===========================================================================
 * Brand palette. Backgrounds use the packed 0xAABBGGRR form the widget
 * bg-color setter wants; label text uses struct rgba.
 * ===========================================================================*/
#define BR_BG 0xFF14100Bu        /* #0B1014 */
#define BR_BG_LIFTED 0xFF1F1A14u /* #141A1F */
#define BR_TOOLBAR 0xFF1F1A14u   /* #141A1F — lifted, so the address field reads as sunken */
#define BR_BORDER 0xFF474A36u    /* #364A47 */
#define BR_BG_ROW 0xFF2C261Eu    /* #1E262C — active DevTools tab */

/* DevTools console text colors, packed 0xAABBGGRR to match the ydraw text
 * pipeline (see pack_rgba in ybrowser-paint.c). The level colors reuse the
 * brand palette; warn/error are functional severity colors the brand set does
 * not define (a console must read amber for warnings and red for errors). */
#define CONSOLE_TEXT 0xFFE4E5E0u   /* #E0E5E4 brand text-primary — log/info */
#define CONSOLE_MUTED 0xFF626155u  /* #556162 brand text-muted — debug */
#define CONSOLE_WARN 0xFF4DA3E5u   /* #E5A34D amber */
#define CONSOLE_ERROR 0xFF4D57E5u  /* #E5574D red */
#define CONSOLE_INPUT 0xFFA8A79Fu  /* #9FA7A8 brand text-secondary — typed input */
#define CONSOLE_ACCENT 0xFF92A86Bu /* #6BA892 brand accent — prompt/marker glyphs */
#define CONSOLE_RESULT 0xFFA5C574u /* #74C5A5 brand accent-bright — eval result */
#define CONSOLE_FONT_SIZE 13.0f
#define DEVTOOLS_HEIGHT 300.0f

#define MAX_TABS 24

/* ===========================================================================
 * Built-in pages.
 * ===========================================================================*/
#define START_HTML                                                                                 \
    "<!doctype html><html><head><meta charset=utf-8><style>"                                       \
    "html,body{margin:0;background:#0B1014;color:#E0E5E4;font-family:sans-serif;}"                 \
    "body{padding:48px 44px;}"                                                                     \
    "h1{color:#74C5A5;font-size:30px;margin:0 0 6px;}"                                             \
    ".sub{color:#9FA7A8;font-size:15px;margin:0 0 28px;}"                                          \
    ".card{background:#141A1F;border:1px solid #364A47;border-radius:10px;"                        \
    "padding:18px 20px;margin:0 0 14px;}"                                                          \
    ".card h2{color:#6BA892;font-size:16px;margin:0 0 10px;}"                                      \
    "kbd{background:#1E262C;border:1px solid #364A47;border-radius:5px;"                           \
    "padding:2px 7px;color:#E0E5E4;}"                                                              \
    "p{margin:6px 0;color:#9FA7A8;}"                                                               \
    "</style></head><body>"                                                                        \
    "<h1>yetty &middot; ybrowser</h1>"                                                             \
    "<div class=sub>A real HTML / CSS / JavaScript engine, in your terminal.</div>"                \
    "<div class=card><h2>Get started</h2>"                                                         \
    "<p>Type a URL in the address bar and press <kbd>Enter</kbd>.</p></div>"                       \
    "<div class=card><h2>Shortcuts</h2>"                                                           \
    "<p><kbd>Ctrl</kbd>+<kbd>L</kbd> address &nbsp; <kbd>Ctrl</kbd>+<kbd>T</kbd> new tab "         \
    "&nbsp; <kbd>Ctrl</kbd>+<kbd>W</kbd> close tab &nbsp; <kbd>Ctrl</kbd>+<kbd>Q</kbd> "           \
    "quit</p></div>"                                                                               \
    "</body></html>"

#define ERROR_HTML_FMT                                                                             \
    "<!doctype html><html><head><meta charset=utf-8><style>"                                       \
    "html,body{margin:0;background:#0B1014;color:#E0E5E4;font-family:sans-serif;}"                 \
    "body{padding:48px 44px;}h1{color:#74C5A5;}p{color:#9FA7A8;}"                                  \
    "code{color:#74C5A5;background:#141A1F;padding:2px 6px;border-radius:5px;}"                    \
    "</style></head><body><h1>Can&rsquo;t reach this page</h1>"                                    \
    "<p>Failed to load <code>%s</code>.</p>"                                                       \
    "<p>Check the address and your connection, then reload.</p></body></html>"

#define START_URL "about:start"

/* ===========================================================================
 * State.
 * ===========================================================================*/

/* What a tab is currently showing — drives which renderer + content widget
 * is used. Detected from the fetched bytes (magic numbers). */
enum content_kind {
    CK_HTML = 0, /* ylexbor engine → ydraw_embed */
    CK_SVG,      /* ysvg            → ydraw_embed */
    CK_IMAGE,    /* yimage widget (own figure)   */
};

/* ---------------------------------------------------------------------------
 * Tiny LRU URL → bytes cache, shared across tabs. Avoids re-fetching on
 * reload / back-forward / re-render. cache_fetch always returns an owned
 * copy; the cache keeps its own copy.
 * ------------------------------------------------------------------------- */
#define CACHE_MAX_ENTRIES 16
#define CACHE_MAX_ENTRY (16u * 1024u * 1024u) /* don't cache > 16 MB items */
#define CACHE_MAX_TOTAL (64u * 1024u * 1024u)

/* Coalesce window for async image arrivals. A page streaming many images
 * lands one completion per loop tick; repainting on each would re-run a full
 * relayout + repaint + GPU re-upload per image (O(N^2) — seconds of stall on
 * a slow connection). Instead we repaint at most once per this window while
 * images keep arriving, then once more when the last one lands. */
#define IMG_RENDER_DEBOUNCE_MS 120.0

struct cache_entry {
    char *url;
    uint8_t *data;
    size_t len;
    char *eff;   /* effective (post-redirect) URL, or NULL */
    char *ctype; /* response Content-Type, or NULL */
    uint64_t seq;
};

struct url_cache {
    struct cache_entry e[CACHE_MAX_ENTRIES];
    int n;
    uint64_t seq;
    size_t total;
};

struct tab {
    struct yetty_ylexbor *engine; /* persistent per-tab HTML engine */
    enum content_kind kind;
    uint8_t *raw; /* IMAGE/SVG: fetched bytes (owned) */
    size_t raw_len;
    int img_w, img_h; /* IMAGE: source pixel dimensions (aspect) */
    char *url;        /* current location (owned) */

    /* Async navigation state. nav_id identifies the navigation this tab is
	 * showing/awaiting; a completing fetch job applies only when its own id
	 * still matches (Stop, a superseding navigation, or closing the tab
	 * orphans the job). nav_cancel_cell is a borrowed view of the in-flight
	 * job's cancel cell (the job owns it) — flipping it aborts the transfer
	 * at its next progress tick; NULL when no navigation is in flight. */
    uint64_t nav_id;
    _Atomic uint64_t *nav_cancel_cell;
    char *title; /* tab label (owned) */
    char **back; /* history stacks (owned strings) */
    size_t n_back, cap_back;
    char **fwd;
    size_t n_fwd, cap_fwd;
    float rendered_w; /* embed/image width last laid out at */
    int needs_render; /* re-render the active content next tick */
    /* Progressive rendering: load_html parsed + laid out the page WITHOUT
	 * running its <script> blocks (defer-scripts mode), so the first paint
	 * shows HTML+CSS immediately. Set after such a load; pump_active runs the
	 * deferred scripts once the first paint has happened, then repaints. */
    int scripts_pending;
    uint64_t dl_hash; /* hash of the last draw list we shipped */
    size_t dl_size;   /* size of the last draw list we shipped */
};

struct app {
    struct yetty_yclass_object *fw;
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *tabbar;
    struct yetty_yclass_object *address;    /* textinput */
    struct yetty_yclass_object *btn_reload; /* reload/stop toggle button */
    int loading;                            /* active tab is loading — reload button shows an X */
    struct yetty_yclass_object *scroll;     /* scrollarea */
    struct yetty_yclass_object *page;       /* ydraw_embed (HTML/SVG) — shared */
    struct yetty_yclass_object *image;      /* yimage widget (raster) — shared */
    int showing_image;                      /* which content widget is visible */

    /* `--no-ui`: hide the tab strip + toolbar so only the page content is
	 * rendered (the whole window is the page). Useful for clean recordings /
	 * screenshots of just the rendered page. */
    int no_ui;

    struct tab tabs[MAX_TABS];
    int n_tabs;
    int active;

    uint64_t nav_seq; /* navigation id source — see struct tab */

    struct url_cache cache;

    int address_focused;
    int pending_render;
    int running;

    /* Page form inputs promoted to REAL ygui textinput widgets, overlaid on
     * the ydraw_embed at each <input> box's document position (the embed
     * rect already carries the scroll slide, so overlay rect = embed
     * rect.min + box xy). Pool rebuilt after every ship — element/box
     * indices die on re-parse. `page_input_focused` is the pool slot with
     * key focus, -1 when none. */
#define MAX_PAGE_INPUTS 16
    struct {
        int box_index;                      /* engine box index this overlays */
        float doc_x, doc_y, w, h;           /* document-coord rect of the box */
        struct yetty_yclass_object *widget; /* ygui textinput (created once) */
    } page_inputs[MAX_PAGE_INPUTS];
    int n_page_inputs;
    int page_input_focused;

    /* DevTools: a bottom-docked panel with a live JavaScript console. Built
     * once in build_ui, collapsed to zero height until toggled with F12. The
     * `console_log` rich widget is redrawn from the active engine's console
     * ring; `console_input` is the REPL prompt. `console_seen_total` tracks
     * how many console lines we last rendered so pump_active can notice new
     * page output. */
    struct yetty_yclass_object *devtools;      /* vbox panel (child of root) */
    struct yetty_yclass_object *tab_console;   /* button — "Console" tab */
    struct yetty_yclass_object *tab_elements;  /* button — "Elements" tab */
    struct yetty_yclass_object *console_pane;  /* vbox — console log + prompt */
    struct yetty_yclass_object *console_log;   /* rich — color-coded log */
    struct yetty_yclass_object *console_input; /* textinput — REPL prompt */
    struct yetty_yclass_object *elements_pane; /* vbox — DOM tree scroller */
    struct yetty_yclass_object *tree_box;      /* vbox — tree_nodes attach here */
    int devtools_open;
    int devtools_tab; /* 0 = console, 1 = elements */
    int console_focused;
    int console_dirty;
    uint64_t console_seen_total;

    float viewport_w, viewport_h;
    float font_size;

    /* HiDPI factor learned from the host via SC_CLIENT_INPUT_FIGURE_RESIZE.
     * 1.0 until the first envelope arrives — everything the client authors
     * in logical px (ygui viewport, mouse hit-test) divides framebuffer-px
     * inputs (ws_xpixel, forwarded mouse coords) by this. The very first
     * frame ships at framebuffer scale (mismatched on HiDPI, but visible)
     * because we don't have a local way to observe the host's scale until
     * the RESIZE OSC arrives after the mouse-subscribe handshake. */
    float host_content_scale;

    /* Standalone (own-window) mode only — set so the quit shortcut can stop
	 * the GPU event loop. NULL in the in-yetty client loop. */
    struct yetty_yevent_event_loop *event_loop;

    /* Borrowed platform clipboard (yplatform:clipboard yclass object) for the
	 * address bar's copy/cut/paste. Set from the yframework in standalone mode;
	 * NULL in the in-yetty client loop (which has no direct clipboard), where
	 * the clipboard chords simply do nothing. */
    struct yetty_yclass_object *clipboard;

    /* Worker pool for parallel async image fetch+decode (standalone only). The
	 * engine submits each <img> to it; completions post back to the loop and
	 * trigger a repaint. NULL in the in-yetty client (falls back to the
	 * synchronous one-image-per-frame path). */
    struct yetty_yplatform_yworkpool *img_pool;

    /* Profiling: number of full render passes since start (each relayouts +
	 * re-ships the whole drawable list to the GPU). */
    int render_count;

    /* Async-image repaint coalescing (standalone img_pool path). on_img_ready
	 * sets img_dirty; pump_active turns it into at most one repaint per
	 * IMG_RENDER_DEBOUNCE_MS window (img_last_render_ms = last such repaint). */
    int img_dirty;
    double img_last_render_ms;

    /* Set by the image-fetch paths instead of `needs_render` when only
	 * streamed images have landed. render_active tries to ship per-image
	 * CMD_GROUP deltas (O(changed) bytes) to the page figure rather than
	 * repainting + reshipping the whole page; it falls back to a full render
	 * if the relayout shifted anything. */
    int img_delta_pending;

    /* Shared network loader — one per app so the page prefetch, every
	 * tab's engine, and the image workers all reuse the same connection
	 * pool / TLS sessions / Alt-Svc cache. Owned; destroyed after every
	 * engine is gone. NULL when creation failed (fetches still work,
	 * just without reuse). */
    struct yetty_ybrowser_loader *loader;
};

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        /* Deliberately-tolerated failures still deserve a trace line —
		 * a silent swallow here cost a day of debugging a black pane. */
        char chain[512];
        yetty_ycore_error_snprint(chain, sizeof(chain), r.error);
        ydebug("ybrowser: tolerated error: %s", chain);
        yetty_ycore_error_destroy(r.error);
    }
}

/* Same tolerated-error swallow for int-returning calls (e.g. the consumed flag
 * from feed_mouse_scroll, which the browser ignores). */
static inline void err_ok_int(struct yetty_ycore_int_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static int pt_in_rect(struct yetty_ycore_rectangle r, float x, float y)
{
    return x >= r.min.x && x < r.max.x && y >= r.min.y && y < r.max.y;
}

/* FNV-1a over a byte range — used to detect when a re-rendered page is
 * byte-identical to the last one we shipped (so we can skip re-emitting it). */
static uint64_t fnv1a(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}

/* Host display HiDPI factor (framebuffer_px / logical_px), learned from the
 * SC_CLIENT_INPUT_FIGURE_RESIZE OSC. Falls back to 1.0 before the envelope
 * arrives — the very first frame renders at framebuffer scale, then RESIZE
 * lands and the viewport snaps to logical. The ygui widget layer + lexbor's
 * CSS viewport both run in LOGICAL px once RESIZE has arrived; the host's
 * absolute-coords ygrid multiplies our records by content_scale at add-record
 * time to reach framebuffer. So the only places that divide by this scale
 * are the fb→logical boundaries: pick_pane_px (ws_xpixel), on_osc for RESIZE
 * (rz->width) and MOUSE (m->x). Widget-rect reads are already logical and
 * pass through render_doc / page_click unmodified. */
static float pane_host_scale_from(const struct app *a)
{
    if (!a) {
        return 1.0f;
    }
    return a->host_content_scale > 0.0f ? a->host_content_scale : 1.0f;
}

/* Current pane LOGICAL pixel size (framebuffer / host_content_scale) from
 * the controlling tty. The host writes ws_xpixel / ws_ypixel in framebuffer
 * pixels each time the pane resizes; ygui runs in logical, so divide by the
 * host scale (learned from the resize OSC — 1.0 before it arrives, in which
 * case this returns framebuffer, matching the framebuffer-sized initial
 * viewport seeded by ybrowser_ui_run). */
static int pick_pane_px(const struct app *a, int *w, int *h)
{
    int fds[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
        if (!isatty(fds[i])) {
            continue;
        }
        struct winsize ws = {0};
        if (ioctl(fds[i], TIOCGWINSZ, &ws) != 0) {
            continue;
        }
        float scale = pane_host_scale_from(a);
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            *w = (int)((float)ws.ws_xpixel / scale);
            *h = (int)((float)ws.ws_ypixel / scale);
            return 1;
        }
        if (ws.ws_col > 0 && ws.ws_row > 0) {
            *w = (int)ws.ws_col * 9;
            *h = (int)ws.ws_row * 18;
            return 1;
        }
    }
    return 0;
}

/* Forward declarations — callbacks reference the tab/navigation helpers
 * defined further down. */
static void ui_new_tab(struct app *a);
static void ui_close_tab(struct app *a, int idx);
static void switch_tab(struct app *a, int idx);
static void navigate(struct app *a, struct tab *t, char *url, int push_to_back);
/* UTF-8 + special-key encoders (defined with the standalone loop below;
 * the client key-envelope decode reuses them). */
static size_t utf8_encode(uint32_t cp, char *out);
static const char *encode_special_key(uint32_t key, int glfw_mods, char *scratch, size_t scratch_n,
                                      size_t *out_n);
static void nav_abort(struct tab *t);
static void go_back(struct app *a);
static void go_forward(struct app *a);
static void reload(struct app *a);
static void sync_active_ui(struct app *a);
static void render_active(struct app *a);
static int build_ui(struct app *a);
static void dt_layout(struct yetty_yclass_object *w, float height, float flex_grow, float pad_x,
                      float pad_y);
static void dt_set_hidden(struct yetty_yclass_object *w, int hidden);
static struct yetty_yclass_object *dt_add_label(struct yetty_yclass_object *parent,
                                                const char *text, struct yetty_ycore_rgba color,
                                                float font_size);
static void toggle_devtools(struct app *a);
static void switch_devtools_tab(struct app *a, int tab);
static void dom_tree_rebuild(struct app *a);
static void console_submit(struct app *a);
static void console_refresh(struct app *a);

/* ===========================================================================
 * History stacks.
 * ===========================================================================*/
static void hist_push(char ***arr, size_t *n, size_t *cap, char *s /*owned*/)
{
    if (!s) {
        return;
    }
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        char **na = realloc(*arr, nc * sizeof(char *));
        if (!na) {
            free(s);
            return;
        }
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*n)++] = s;
}
static char *hist_pop(char **arr, size_t *n) /* transfers ownership */
{
    return *n == 0 ? NULL : arr[--(*n)];
}
static void hist_clear(char **arr, size_t *n)
{
    for (size_t i = 0; i < *n; i++) {
        free(arr[i]);
    }
    *n = 0;
}

/* ===========================================================================
 * URL / title helpers.
 * ===========================================================================*/
static char *str_dup(const char *s)
{
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n + 1);
    }
    return p;
}

/* Turn a raw address-bar string into a URL/path we can fetch. Already-
 * qualified URLs and existing file paths pass through; a bare host like
 * "example.com" gets an http:// scheme. Returns an owned string. */
static char *normalize_url(const char *in)
{
    while (*in == ' ') {
        in++;
    }
    if (!*in) {
        return str_dup(START_URL);
    }
    if (strstr(in, "://")) {
        return str_dup(in);
    }
    if (in[0] == '/' || in[0] == '~' || (in[0] == '.' && in[1] == '/')) {
        return str_dup(in);
    }
    struct stat st;
    if (stat(in, &st) == 0) {
        return str_dup(in);
    }
    size_t n = strlen(in);
    char *u = malloc(n + 8);
    if (!u) {
        return str_dup(in);
    }
    memcpy(u, "http://", 7);
    memcpy(u + 7, in, n + 1);
    return u;
}

/* A short label for the tab pill: the host of a URL, or the basename of a
 * path. Owned string. */
static char *derive_title(const char *url)
{
    if (!url || strcmp(url, START_URL) == 0) {
        return str_dup("New Tab");
    }
    const char *scheme = strstr(url, "://");
    const char *start;
    if (scheme) {
        start = scheme + 3;
    } else {
        const char *slash = strrchr(url, '/');
        start = slash ? slash + 1 : url;
    }
    if (!*start) {
        start = url;
    }
    size_t n = strcspn(start, "/");
    if (n == 0 || n > 48) {
        n = n > 48 ? 48 : strlen(start);
    }
    char *t = malloc(n + 1);
    if (!t) {
        return str_dup("page");
    }
    memcpy(t, start, n);
    t[n] = '\0';
    return t;
}

/* ===========================================================================
 * URL → bytes LRU cache.
 * ===========================================================================*/
static void cache_store(struct url_cache *c, const char *url, const uint8_t *data, size_t len,
                        const char *eff, const char *ctype)
{
    if (len == 0 || len > CACHE_MAX_ENTRY) {
        return;
    }
    /* Replace an existing entry for the same URL. */
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->e[i].url, url) != 0) {
            continue;
        }
        uint8_t *nd = malloc(len);
        if (!nd) {
            return;
        }
        memcpy(nd, data, len);
        c->total -= c->e[i].len;
        free(c->e[i].data);
        free(c->e[i].eff);
        free(c->e[i].ctype);
        c->e[i].data = nd;
        c->e[i].len = len;
        c->e[i].eff = eff ? str_dup(eff) : NULL;
        c->e[i].ctype = ctype ? str_dup(ctype) : NULL;
        c->e[i].seq = ++c->seq;
        c->total += len;
        return;
    }
    /* Evict least-recently-used until there is room. */
    while (c->n >= CACHE_MAX_ENTRIES || (c->n > 0 && c->total + len > CACHE_MAX_TOTAL)) {
        int lru = 0;
        for (int i = 1; i < c->n; i++) {
            if (c->e[i].seq < c->e[lru].seq) {
                lru = i;
            }
        }
        c->total -= c->e[lru].len;
        free(c->e[lru].url);
        free(c->e[lru].data);
        free(c->e[lru].eff);
        free(c->e[lru].ctype);
        c->e[lru] = c->e[c->n - 1];
        c->n--;
    }
    uint8_t *nd = malloc(len);
    if (!nd) {
        return;
    }
    memcpy(nd, data, len);
    struct cache_entry *e = &c->e[c->n];
    e->url = str_dup(url);
    e->data = nd;
    e->len = len;
    e->eff = eff ? str_dup(eff) : NULL;
    e->ctype = ctype ? str_dup(ctype) : NULL;
    e->seq = ++c->seq;
    c->total += len;
    c->n++;
}

static void cache_free(struct url_cache *c)
{
    for (int i = 0; i < c->n; i++) {
        free(c->e[i].url);
        free(c->e[i].data);
        free(c->e[i].eff);
        free(c->e[i].ctype);
    }
    memset(c, 0, sizeof(*c));
}

/* Cache lookup only — returns an owned copy on a hit (caller frees *out,
 * *out_eff, *out_ctype), NULL on a miss. */
static uint8_t *cache_lookup(struct url_cache *c, const char *url, size_t *out_len, char **out_eff,
                             char **out_ctype)
{
    *out_len = 0;
    *out_eff = NULL;
    *out_ctype = NULL;
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->e[i].url, url) != 0) {
            continue;
        }
        c->e[i].seq = ++c->seq;
        uint8_t *copy = malloc(c->e[i].len ? c->e[i].len : 1);
        if (!copy) {
            return NULL;
        }
        memcpy(copy, c->e[i].data, c->e[i].len);
        *out_len = c->e[i].len;
        *out_eff = c->e[i].eff ? str_dup(c->e[i].eff) : NULL;
        *out_ctype = c->e[i].ctype ? str_dup(c->e[i].ctype) : NULL;
        return copy;
    }
    return NULL;
}

/* Fetch `url`, preferring the cache. Always returns owned bytes (caller
 * frees *out, *out_eff and *out_ctype); NULL on failure. Misses are fetched
 * and cached. `bypass_cache` skips the lookup (Reload must observe server
 * and local-file changes) — the fresh bytes still replace the stored entry
 * so history moves stay hits. */
static uint8_t *cache_fetch(struct yetty_ybrowser_loader *loader, struct url_cache *c,
                            const char *url, int bypass_cache, size_t *out_len, char **out_eff,
                            char **out_ctype)
{
    if (!bypass_cache) {
        uint8_t *hit = cache_lookup(c, url, out_len, out_eff, out_ctype);
        if (hit) {
            return hit;
        }
    }
    *out_len = 0;
    *out_eff = NULL;
    *out_ctype = NULL;
    size_t len = 0;
    char *eff = NULL;
    char *ctype = NULL;
    double t_fetch = yetty_ylexbor_prof_now_ms();
    char *raw = ybrowser_slurp_file(loader, url, &len, &eff, &ctype);
    yetty_ylexbor_prof("HTML fetch     %.0f ms  bytes=%zu  %.80s",
                       yetty_ylexbor_prof_now_ms() - t_fetch, len, url);
    if (!raw) {
        free(eff);
        free(ctype);
        return NULL;
    }
    cache_store(c, url, (const uint8_t *)raw, len, eff, ctype); /* keeps its own copy */
    *out_len = len;
    *out_eff = eff;     /* transfer to caller */
    *out_ctype = ctype; /* transfer to caller */
    return (uint8_t *)raw;
}

/* Sniff the content kind. The response Content-Type wins when present;
 * otherwise magic bytes, then root-element sniffing. */
static enum content_kind detect_kind(const char *content_type, const uint8_t *d, size_t n)
{
    if (content_type) {
        /* Prefix match — the header may carry ";charset=...". */
        if (strncasecmp(content_type, "text/html", 9) == 0 ||
            strncasecmp(content_type, "application/xhtml", 17) == 0) {
            return CK_HTML;
        }
        if (strncasecmp(content_type, "image/svg", 9) == 0) {
            return CK_SVG;
        }
        if (strncasecmp(content_type, "image/", 6) == 0) {
            return CK_IMAGE;
        }
        /* Anything else (text/plain, application/json, servers that lie)
		 * falls through to sniffing — the HTML engine is the safe default. */
    }
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) {
        return CK_IMAGE; /* JPEG */
    }
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {
        return CK_IMAGE; /* PNG */
    }
    if (n >= 6 && memcmp(d, "GIF8", 4) == 0) {
        return CK_IMAGE; /* GIF */
    }
    if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0) {
        return CK_IMAGE; /* WebP */
    }
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') {
        return CK_IMAGE; /* BMP */
    }
    /* SVG only when <svg> is the DOCUMENT element — skip whitespace,
	 * comments, doctype, and processing instructions, then look at the
	 * first real tag. An inline <svg> icon inside early HTML must NOT
	 * reclassify the whole document (it used to: any "<svg" in the first
	 * 512 bytes sent normal pages to the standalone SVG renderer). */
    size_t scan = n < 1024 ? n : 1024;
    size_t i = 0;
    while (i < scan) {
        while (i < scan &&
               (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n' || d[i] == '\f')) {
            i++;
        }
        if (i >= scan || d[i] != '<') {
            break; /* leading text content — not a standalone SVG document */
        }
        if (i + 4 <= scan && memcmp(d + i, "<!--", 4) == 0) {
            const uint8_t *close = NULL;
            for (size_t j = i + 4; j + 3 <= scan; j++) {
                if (memcmp(d + j, "-->", 3) == 0) {
                    close = d + j;
                    break;
                }
            }
            if (!close) {
                break;
            }
            i = (size_t)(close - d) + 3;
            continue;
        }
        if (i + 1 < scan && (d[i + 1] == '!' || d[i + 1] == '?')) {
            /* <!DOCTYPE ...> or <?xml ...?> — skip to the closing '>'. */
            size_t j = i + 2;
            while (j < scan && d[j] != '>') {
                j++;
            }
            if (j >= scan) {
                break;
            }
            i = j + 1;
            continue;
        }
        /* First real element. */
        if (i + 4 < scan && (d[i + 1] | 0x20) == 's' && (d[i + 2] | 0x20) == 'v' &&
            (d[i + 3] | 0x20) == 'g' &&
            (d[i + 4] == '>' || d[i + 4] == ' ' || d[i + 4] == '\t' || d[i + 4] == '\r' ||
             d[i + 4] == '\n' || d[i + 4] == '/')) {
            return CK_SVG;
        }
        break;
    }
    return CK_HTML;
}

/* Shared with the one-shot path (main.c): standalone-SVG detection with
 * the same Content-Type + root-element rules the interactive shell uses. */
int ybrowser_content_is_svg(const char *content_type, const uint8_t *data, size_t len)
{
    return detect_kind(content_type, data, len) == CK_SVG;
}

/* ===========================================================================
 * Engine + document.
 * ===========================================================================*/

/* Loop-thread callback the engine fires when an async image fetch lands.
 * Do NOT force a repaint here: a page streaming many images would trigger one
 * full relayout + repaint + GPU re-upload per image (O(N^2) — seconds of
 * stall on a slow connection). Just flag that pixels are waiting and wake the
 * loop; pump_active() coalesces these into at most one repaint per debounce
 * window (see IMG_RENDER_DEBOUNCE_MS). */
static void on_img_ready(void *user)
{
    struct app *a = user;
    a->img_dirty = 1;
    if (a->event_loop && a->event_loop->ops->request_render) {
        a->event_loop->ops->request_render(a->event_loop);
    }
}

static int tab_ensure_engine(struct app *a, struct tab *t)
{
    if (t->engine) {
        return 0;
    }
    struct yetty_ylexbor_config cfg = {
        .viewport_width = a->viewport_w > 0 ? (int)a->viewport_w : 1024,
        .viewport_height = a->viewport_h > 0 ? (int)a->viewport_h : 768,
        .default_font_size = a->font_size,
        .loader = a->loader,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return -1;
    }
    t->engine = r.value;
    /* Interactive UI: never let the paint pass block the event loop on
     * image HTTP. Render text + placeholders immediately; the per-frame
     * pump streams images in one at a time (see pump_active). */
    yetty_ylexbor_set_defer_image_fetch(t->engine, 1);
    /* Progressive rendering: don't run <script> blocks during load_html — paint
	 * the initial HTML+CSS first, then run scripts on a later pump tick and
	 * repaint. Without this the first frame waits for every external script (on
	 * github, ~75 brotli chunks → seconds of blank window). */
    yetty_ylexbor_set_defer_scripts(t->engine, 1);
    /* Standalone: hand the engine the worker pool so images fetch+decode in
	 * parallel on background threads and stream in as they land. */
    if (a->img_pool) {
        yetty_ylexbor_set_async_image_fetch(t->engine, a->img_pool, on_img_ready, a);
    }
    /* We render with a monospace MSDF font (DejaVu Sans Mono, advance
	 * 1233/2048 ≈ 0.602 em). Tell the engine so its line-wrap and
	 * inline-fragment positioning use the same advance the canvas does —
	 * required for per-element styled runs (colored links, bold/italic) to
	 * line up instead of drifting. */
    yetty_ylexbor_set_glyph_advance_ratio(t->engine, 0.602f);
    t->rendered_w = 0.0f;
    return 0;
}

/* Install fetched content into the tab, detecting whether it's HTML (→
 * ylexbor), an SVG (→ ysvg) or a raster image (→ yimage). */
static void tab_set_document(struct app *a, struct tab *t, const char *data, size_t len,
                             const char *base_url, const char *content_type)
{
    t->kind = detect_kind(content_type, (const uint8_t *)data, len);
    free(t->raw);
    t->raw = NULL;
    t->raw_len = 0;
    t->img_w = t->img_h = 0;

    if (t->kind == CK_HTML) {
        if (tab_ensure_engine(a, t) < 0) {
            return;
        }
        /* Set the base URL BEFORE load_html. load_html fetches external
		 * <link rel=stylesheet> and <script src> during parse, and those
		 * URLs are almost always root- or path-relative (e.g. Wikipedia's
		 * /w/load.php skin CSS). Without the base set first they can't be
		 * resolved, so the page renders with only its inline <style> —
		 * which is why interactive mode looked far more broken than the
		 * one-shot path that already ordered these correctly.
		 *
		 * ALWAYS replace the base — a NULL base_url must CLEAR the old one,
		 * or generated start/error documents and local files inherit the
		 * previous page's origin and their relative references (CSS,
		 * images, fetch()) silently target the wrong site. */
        err_ok(yetty_ylexbor_set_base_url(t->engine, base_url));
        err_ok(yetty_ylexbor_load_html(t->engine, data, len));
        /* Scripts were deferred (see tab_ensure_engine) — the first paint shows
		 * HTML+CSS; pump_active runs the scripts once that paint has landed. */
        t->scripts_pending = 1;
    } else {
        /* IMAGE/SVG: keep the raw bytes; the renderer consumes them. */
        t->raw = malloc(len ? len : 1);
        if (t->raw) {
            memcpy(t->raw, data, len);
            t->raw_len = len;
        }
        if (t->kind == CK_IMAGE && t->raw) {
            yetty_yimage_probe_size(t->raw, t->raw_len, &t->img_w, &t->img_h);
        }
    }
    t->needs_render = 1;
    t->rendered_w = 0.0f; /* force a fresh layout + render on the next tick */
}

static void tab_free(struct tab *t)
{
    nav_abort(t); /* orphan any in-flight navigation — its completion
                   * finds no tab with this nav_id and discards */
    if (t->engine) {
        err_ok(yetty_ylexbor_destroy(t->engine));
    }
    free(t->raw);
    free(t->url);
    free(t->title);
    hist_clear(t->back, &t->n_back);
    free(t->back);
    hist_clear(t->fwd, &t->n_fwd);
    free(t->fwd);
    memset(t, 0, sizeof(*t));
}

static void load_start_page(struct app *a, struct tab *t)
{
    tab_set_document(a, t, START_HTML, strlen(START_HTML), NULL, NULL);
    free(t->url);
    t->url = str_dup(START_URL);
    free(t->title);
    t->title = str_dup("New Tab");
    if (t == &a->tabs[a->active]) {
        sync_active_ui(a);
    }
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Google News wraps every story link in a JS-only redirect trampoline
 * (news.google.com/read/<token>) whose target resolves only inside their
 * script framework — the page renders blank without it. The listing card
 * carries the real article URL in its `jslog` attribute as a `5:<base64>`
 * segment holding a JSON array with the URL. Unwrap at click time so a
 * story click lands on the publisher page. Returns a malloc'd URL, or
 * NULL when `href` is not a read-trampoline / no URL is recoverable. */
static char *unwrap_gnews_read_link(struct yetty_ylexbor *engine, const char *href, float x,
                                    float y)
{
    /* Relative story hrefs ("./read/<token>") resolve without dot-segment
	 * normalisation, so the wire form is often "news.google.com/./read/".
	 * Match host and path separately to catch both shapes. */
    if (href == NULL || strstr(href, "news.google.com/") == NULL ||
        strstr(href, "/read/") == NULL) {
        return NULL;
    }
    /* The nearest jslog is usually the headline's interaction-tracking one
	 * ("…; 3:<id>"); the article URL lives in the CARD's jslog further up
	 * ("…; 5:<base64>"). Filter the ancestor walk to the payload we need. */
    char *jslog = yetty_ylexbor_ancestor_attr_at(engine, x, y, "jslog", "5:");
    if (jslog == NULL) {
        return NULL;
    }
    char *result = NULL;
    for (char *segment = strstr(jslog, "5:"); segment != NULL && result == NULL;
         segment = strstr(segment + 2, "5:")) {
        const char *blob = segment + 2;
        size_t blob_len = strcspn(blob, "; \t");
        if (blob_len < 24) { /* too short to hold a URL payload */
            continue;
        }
        /* The blob may use URL-safe base64 — normalise for the decoder. */
        char *base64_copy = malloc(blob_len + 1);
        if (base64_copy == NULL) {
            break;
        }
        for (size_t i = 0; i < blob_len; i++) {
            char ch = blob[i];
            base64_copy[i] = ch == '-' ? '+' : (ch == '_' ? '/' : ch);
        }
        base64_copy[blob_len] = '\0';
        char *decoded = malloc(blob_len + 1); /* decoded is always shorter */
        if (decoded == NULL) {
            free(base64_copy);
            break;
        }
        size_t decoded_len = yetty_ycore_base64_decode(base64_copy, blob_len, decoded, blob_len);
        free(base64_copy);
        /* First quoted non-Google http(s) URL inside the decoded JSON. */
        for (size_t i = 0; i + 6 < decoded_len && result == NULL; i++) {
            if (memcmp(decoded + i, "\"http", 5) != 0) {
                continue;
            }
            size_t url_start = i + 1;
            size_t url_end = url_start;
            while (url_end < decoded_len && decoded[url_end] != '"') {
                url_end++;
            }
            if (url_end >= decoded_len) {
                break;
            }
            size_t url_len = url_end - url_start;
            char *candidate = malloc(url_len + 1);
            if (candidate == NULL) {
                break;
            }
            memcpy(candidate, decoded + url_start, url_len);
            candidate[url_len] = '\0';
            if (strstr(candidate, "google.") == NULL && strstr(candidate, "gstatic.") == NULL) {
                result = candidate;
            } else {
                free(candidate);
                i = url_end;
            }
        }
        free(decoded);
    }
    free(jslog);
    return result;
}

/* Reload/stop toggle (Chrome-style): the toolbar's reload button shows a
 * circular-arrow glyph when idle and an X while the active tab loads. On
 * entering the loading state the frame is emitted eagerly — the document
 * fetch in navigate() blocks the UI loop, so without the flush the X
 * would only appear once the page had already arrived. */
static void set_loading(struct app *a, int loading)
{
    if (a->loading == loading || a->btn_reload == NULL) {
        return;
    }
    a->loading = loading;
    err_ok(yetty_ygui_button_set_chrome_icon(a->btn_reload, loading ? 7 : 6));
    yetty_ygui_framework_mark_dirty(a->fw);
    if (loading) {
        err_ok(yetty_ygui_framework_emit(a->fw));
        fflush(stdout);
    }
}

/* Escape text for interpolation into generated HTML (the error page shows
 * the failed URL — an attacker-controlled string must not close the <code>
 * element and inject markup). Truncates to fit; always NUL-terminates. */
static void html_escape_into(char *dst, size_t dst_size, const char *src)
{
    size_t used = 0;
    for (const char *p = src; *p && used + 8 < dst_size; p++) {
        const char *rep = NULL;
        switch (*p) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        case '\'':
            rep = "&#39;";
            break;
        default:
            dst[used++] = *p;
            continue;
        }
        size_t rep_len = strlen(rep);
        memcpy(dst + used, rep, rep_len);
        used += rep_len;
    }
    dst[used] = '\0';
}

/* Install fetched (or failed) navigation results into the tab — shared by
 * the synchronous path and the async job's completion. Borrows everything. */
static void navigate_apply(struct app *a, struct tab *t, const char *url, const uint8_t *data,
                           size_t len, const char *eff, const char *ctype)
{
    if (data) {
        /* Base priority: post-redirect URL, the URL itself when absolute,
		 * else a file:// base for local paths — never a stale carry-over. */
        char *file_base = NULL;
        const char *base = NULL;
        if (eff) {
            base = eff;
        } else if (ybrowser_looks_like_url(url)) {
            base = url;
        } else {
            file_base = ybrowser_local_file_url(url);
            base = file_base;
        }
        tab_set_document(a, t, (const char *)data, len, base, ctype);
        free(file_base);
    } else {
        char escaped_url[1024];
        html_escape_into(escaped_url, sizeof(escaped_url), url);
        char err[2048];
        int n = snprintf(err, sizeof(err), ERROR_HTML_FMT, escaped_url);
        if (n < 0) {
            n = 0;
        }
        tab_set_document(a, t, err, (size_t)n, NULL, NULL);
    }
}

/* Finish a navigation: location, title, chrome sync, repaint. Takes
 * ownership of `url`. */
static void navigate_finish(struct app *a, struct tab *t, char *url)
{
    free(t->url);
    t->url = url;
    free(t->title);
    t->title = derive_title(url);
    if (t == &a->tabs[a->active]) {
        sync_active_ui(a);
    }
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Abort the tab's in-flight navigation, if any: flip the cancel cell (the
 * transfer aborts at its next progress tick / chunk) and detach — the
 * orphaned job's completion frees its results and touches nothing. */
static void nav_abort(struct tab *t)
{
    if (t->nav_cancel_cell) {
        atomic_fetch_add_explicit(t->nav_cancel_cell, 1, memory_order_release);
        t->nav_cancel_cell = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Async document navigation: the fetch runs as a worker-pool job so the UI
 * loop keeps handling input, resize, tab switches and Stop while the
 * network works (a navigation used to freeze the window for up to the full
 * transfer timeout). Completion runs on the loop thread and applies only
 * when the tab still awaits THIS navigation.
 * ------------------------------------------------------------------------- */
struct nav_job {
    struct app *app; /* loader read on the worker; everything else loop-only */
    uint64_t nav_id;
    char *url;                     /* owned until applied */
    _Atomic uint64_t *cancel_cell; /* owned; holds nav_id while wanted */
    /* Worker results: */
    char *data;
    size_t len;
    char *eff;
    char *ctype;
};

/* WORKER THREAD — touches only the job and the (stable) loader. */
static void nav_job_run(void *job_ptr)
{
    struct nav_job *job = job_ptr;
    if (atomic_load_explicit(job->cancel_cell, memory_order_acquire) != job->nav_id) {
        return; /* cancelled before the fetch started */
    }
    double t_fetch = yetty_ylexbor_prof_now_ms();
    job->data = ybrowser_slurp_document(job->app->loader, job->url, job->nav_id, job->cancel_cell,
                                        &job->len, &job->eff, &job->ctype);
    yetty_ylexbor_prof("HTML fetch     %.0f ms  bytes=%zu  %.80s (async)",
                       yetty_ylexbor_prof_now_ms() - t_fetch, job->len, job->url);
}

/* LOOP THREAD. Signature dictated by the work pool (void (*)(void *)) —
 * absorb inner Results at this boundary. */
YETTY_EXTERNAL_CALLBACK
static void nav_job_done(void *job_ptr)
{
    struct nav_job *job = job_ptr;
    struct app *a = job->app;
    struct tab *t = NULL;
    for (int i = 0; i < a->n_tabs; i++) {
        if (a->tabs[i].nav_id == job->nav_id) {
            t = &a->tabs[i];
            break;
        }
    }
    int still_wanted = t != NULL && t->nav_cancel_cell == job->cancel_cell &&
                       atomic_load_explicit(job->cancel_cell, memory_order_acquire) == job->nav_id;
    if (still_wanted) {
        t->nav_cancel_cell = NULL;
        if (job->data) {
            cache_store(&a->cache, job->url, (const uint8_t *)job->data, job->len, job->eff,
                        job->ctype);
        }
        navigate_apply(a, t, job->url, (const uint8_t *)job->data, job->len, job->eff, job->ctype);
        navigate_finish(a, t, job->url); /* takes url ownership */
        job->url = NULL;
    }
    free(job->url);
    free(job->data);
    free(job->eff);
    free(job->ctype);
    free(job->cancel_cell);
    free(job);
}

/* Load `url` (owned — navigate takes ownership) into tab `t`. With
 * push_to_back, the previous location is pushed onto the back stack and the
 * forward stack cleared (a fresh navigation, not a history move). With
 * bypass_cache, the shell cache is skipped so Reload observes server and
 * local-file changes (history moves keep using the cache). */
static void navigate_full(struct app *a, struct tab *t, char *url, int push_to_back,
                          int bypass_cache)
{
    set_loading(a, 1);
    nav_abort(t); /* a superseding navigation kills the in-flight one */
    if (push_to_back && t->url && strcmp(t->url, START_URL) != 0) {
        hist_push(&t->back, &t->n_back, &t->cap_back, str_dup(t->url));
        hist_clear(t->fwd, &t->n_fwd);
    }

    size_t len = 0;
    char *eff = NULL;
    char *ctype = NULL;
    uint8_t *data = NULL;
    if (!bypass_cache) {
        data = cache_lookup(&a->cache, url, &len, &eff, &ctype);
    }

    /* Cache miss with a worker pool (standalone): fetch asynchronously.
	 * The in-yetty client has no pool and stays synchronous. */
    if (!data && a->img_pool) {
        struct nav_job *job = calloc(1, sizeof(*job));
        _Atomic uint64_t *cancel_cell = job ? malloc(sizeof(*cancel_cell)) : NULL;
        if (job && cancel_cell) {
            t->nav_id = ++a->nav_seq;
            atomic_store_explicit(cancel_cell, t->nav_id, memory_order_release);
            job->app = a;
            job->nav_id = t->nav_id;
            job->url = url;
            job->cancel_cell = cancel_cell;
            struct yetty_yplatform_yworkpool_job pool_job = {
                .run = nav_job_run,
                .done = nav_job_done,
                .ctx = job,
            };
            struct yetty_ycore_void_result submit_res =
                yetty_yplatform_yworkpool_submit(a->img_pool, pool_job);
            if (YETTY_IS_OK(submit_res)) {
                t->nav_cancel_cell = cancel_cell;
                return; /* completion applies on the loop thread */
            }
            yetty_ycore_error_destroy(submit_res.error);
        }
        free(cancel_cell);
        free(job);
        /* Fall through to the synchronous path. */
    }

    if (!data) {
        data = cache_fetch(a->loader, &a->cache, url, bypass_cache, &len, &eff, &ctype);
    }
    navigate_apply(a, t, url, data, len, eff, ctype);
    free(data);
    free(eff);
    free(ctype);
    navigate_finish(a, t, url);
}

/* Plain navigation — cache-preferring (link clicks, address bar, history). */
static void navigate(struct app *a, struct tab *t, char *url, int push_to_back)
{
    a->page_input_focused = -1;
    navigate_full(a, t, url, push_to_back, 0);
}

static void go_back(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    if (t->n_back == 0) {
        return;
    }
    if (t->url) {
        hist_push(&t->fwd, &t->n_fwd, &t->cap_fwd, str_dup(t->url));
    }
    navigate(a, t, hist_pop(t->back, &t->n_back), 0);
}

static void go_forward(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    if (t->n_fwd == 0) {
        return;
    }
    if (t->url) {
        hist_push(&t->back, &t->n_back, &t->cap_back, str_dup(t->url));
    }
    navigate(a, t, hist_pop(t->fwd, &t->n_fwd), 0);
}

static void reload(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    if (t->url && strcmp(t->url, START_URL) != 0) {
        /* Bypass the shell cache — Reload's whole point is observing
		 * server-side and local-file changes. */
        navigate_full(a, t, str_dup(t->url), 0, 1);
    }
}

/* ===========================================================================
 * UI sync + tab management.
 * ===========================================================================*/
static void sync_active_ui(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    const char *shown = (t->url && strcmp(t->url, START_URL) == 0) ? "" : (t->url ? t->url : "");
    err_ok(yetty_ygui_textinput_set_text(a->address, shown));
    err_ok(yetty_ygui_tabbar_set_label(a->tabbar, a->active, t->title ? t->title : "New Tab"));
}

static void switch_tab(struct app *a, int idx)
{
    if (idx < 0 || idx >= a->n_tabs) {
        return;
    }
    a->active = idx;
    struct tab *t = &a->tabs[idx];
    /* The shared embed currently holds another tab's buffer — force a
	 * re-render of this tab's engine into it. */
    t->needs_render = 1;
    t->rendered_w = 0.0f;
    sync_active_ui(a);
    a->console_dirty = 1; /* console shows the newly-active tab's engine */
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

static void ui_new_tab(struct app *a)
{
    if (a->n_tabs >= MAX_TABS) {
        return;
    }
    int idx = a->n_tabs;
    memset(&a->tabs[idx], 0, sizeof(a->tabs[idx]));
    struct yetty_yclass_object_ptr_result hr = yetty_ygui_tabbar_add_tab(a->tabbar, "New Tab");
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
        return;
    }
    a->n_tabs++;
    a->active = idx;
    /* set_active fires VALUE_CHANGED → on_tab_changed → switch_tab when the
	 * index actually changes; setting a->active first keeps both in step. */
    err_ok(yetty_ygui_tabbar_set_active(a->tabbar, idx));
    load_start_page(a, &a->tabs[idx]);
}

static void ui_close_tab(struct app *a, int idx)
{
    if (idx < 0 || idx >= a->n_tabs) {
        return;
    }
    if (a->n_tabs <= 1) {
        a->running = 0; /* closing the last tab quits */
        return;
    }
    tab_free(&a->tabs[idx]);
    for (int i = idx; i < a->n_tabs - 1; i++) {
        a->tabs[i] = a->tabs[i + 1];
    }
    memset(&a->tabs[a->n_tabs - 1], 0, sizeof(a->tabs[0]));
    a->n_tabs--;
    err_ok(yetty_ygui_tabbar_remove_tab(a->tabbar, idx));

    int na = a->active;
    if (idx < a->active) {
        na = a->active - 1;
    }
    if (na >= a->n_tabs) {
        na = a->n_tabs - 1;
    }
    a->active = na;
    err_ok(yetty_ygui_tabbar_set_active(a->tabbar, na));
    /* set_active may be a no-op (index unchanged after the clamp) — render
	 * + sync the new active tab explicitly. */
    switch_tab(a, na);
}

/* ===========================================================================
 * Callbacks.
 * ===========================================================================*/
static struct yetty_ycore_void_result on_back_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    go_back((struct app *)ud);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_fwd_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    go_forward((struct app *)ud);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_devtools_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    toggle_devtools((struct app *)ud);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_tab_console_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    switch_devtools_tab((struct app *)ud, 0);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_tab_elements_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    switch_devtools_tab((struct app *)ud, 1);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_reload_click(struct yetty_yclass_object *o, void *ud)
{
    (void)o;
    struct app *a = ud;
    if (a->loading) {
        /* Stop: abort the in-flight document fetch (the transfer dies at
		 * its next progress tick), drop the pending deferred-script run,
		 * and leave the page as painted. Images already in flight finish
		 * on their own; no new work is started for this load. */
        nav_abort(&a->tabs[a->active]);
        a->tabs[a->active].scripts_pending = 0;
        set_loading(a, 0);
    } else {
        reload(a);
    }
    return YETTY_OK_VOID();
}

static void on_new_tab_cb(struct yetty_yclass_object *tb, void *ud)
{
    (void)tb;
    ui_new_tab((struct app *)ud);
}
static void on_close_cb(struct yetty_yclass_object *tb, int idx, void *ud)
{
    (void)tb;
    ui_close_tab((struct app *)ud, idx);
}
static struct yetty_ycore_void_result on_tab_changed(struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event, void *ud)
{
    (void)target;
    struct app *a = ud;
    if (event && event->i0 >= 0 && event->i0 != a->active) {
        switch_tab(a, event->i0);
    }
    return YETTY_OK_VOID();
}

static void focus_address(struct app *a)
{
    if (a->page_input_focused >= 0 && a->page_input_focused < a->n_page_inputs &&
        a->page_inputs[a->page_input_focused].widget) {
        err_ok(yetty_ygui_textinput_set_focus(a->page_inputs[a->page_input_focused].widget, 0));
    }
    a->page_input_focused = -1;
    err_ok(yetty_ygui_textinput_set_focus(a->address, 1));
    /* Select the whole URL on focus, like every desktop browser — the next
     * keystroke replaces it, or the user can copy it straight away. */
    err_ok(yetty_ygui_textinput_select_all(a->address));
    a->address_focused = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Copy the address bar's current selection to the clipboard; when `cut`, drop
 * the selection from the field afterwards. No-op when nothing is selected or no
 * clipboard is available (in-yetty client mode). */
static void address_copy(struct app *a, int cut)
{
    if (!a->clipboard) {
        return;
    }
    struct yetty_ycore_char_ptr_result sel_res = yetty_ygui_textinput_get_selection(a->address);
    if (YETTY_IS_ERR(sel_res)) {
        yetty_ycore_error_destroy(sel_res.error);
        return;
    }
    char *selection = sel_res.value;
    if (!selection) {
        return; /* no selection */
    }
    struct yetty_ycore_void_result set_res =
        yetty_yplatform_clipboard_set_text(a->clipboard, selection, strlen(selection));
    if (YETTY_IS_ERR(set_res)) {
        yetty_ycore_error_destroy(set_res.error);
    }
    free(selection);
    if (cut) {
        err_ok(yetty_ygui_textinput_insert_text(a->address, "")); /* delete the selection */
        yetty_ygui_framework_mark_dirty(a->fw);
    }
}

static int key_cb(struct yetty_yclass_object *fw, uint32_t key, int mods, void *ud)
{
    struct app *a = ud;
    if (key == YETTY_YGUI_KEY_F12) { /* F12 → toggle DevTools (Chrome-style) */
        toggle_devtools(a);
        return 1;
    }
    /* Clipboard chords belong to the address bar while it holds focus, and must
     * be handled before the global quit/nav shortcuts — otherwise Ctrl-C would
     * quit the browser instead of copying the URL. */
    if (a->address_focused) {
        switch (key) {
        case 0x03: /* Ctrl-C — copy */
            address_copy(a, 0);
            return 1;
        case 0x18: /* Ctrl-X — cut */
            address_copy(a, 1);
            return 1;
        case 0x16: /* Ctrl-V — paste (async; text arrives as a PASTE event) */
            if (a->clipboard) {
                err_ok(yetty_yplatform_clipboard_request_paste(a->clipboard));
            }
            return 1;
        default:
            break;
        }
    }
    if (key == 0x03 || key == 0x04 || key == 0x11) { /* Ctrl-C / Ctrl-D / Ctrl-Q */
        a->running = 0;
        if (a->event_loop && a->event_loop->ops->stop) {
            err_ok(a->event_loop->ops->stop(a->event_loop));
        }
        return 1;
    }
    if (key == 0x0C) { /* Ctrl-L → focus the address bar */
        focus_address(a);
        return 1;
    }
    if (key == 0x14) { /* Ctrl-T → new tab */
        ui_new_tab(a);
        return 1;
    }
    if (key == 0x17) { /* Ctrl-W → close tab */
        ui_close_tab(a, a->active);
        return 1;
    }
    if (!a->address_focused && !a->console_focused && a->page_input_focused >= 0 &&
        a->page_input_focused < a->n_page_inputs && a->page_inputs[a->page_input_focused].widget) {
        struct yetty_yclass_object *widget = a->page_inputs[a->page_input_focused].widget;
        if (key == 0x1B) { /* Esc — release the page input */
            err_ok(yetty_ygui_textinput_set_focus(widget, 0));
            a->page_input_focused = -1;
            yetty_ygui_framework_mark_dirty(fw);
            return 1;
        }
        struct yetty_ycore_int_result page_key_res =
            yetty_ygui_textinput_handle_key(widget, key, mods);
        int page_consumed = 0;
        if (YETTY_IS_ERR(page_key_res)) {
            yetty_ycore_error_destroy(page_key_res.error);
        } else {
            page_consumed = page_key_res.value;
        }
        if (page_consumed) {
            yetty_ygui_framework_mark_dirty(fw);
            return 1;
        }
        /* fall through for keys the edit box doesn't take (Ctrl chords…) */
    }
    if (a->address_focused) {
        if (key == '\r' || key == '\n') {
            struct yetty_ycore_const_char_ptr_result txt_r =
                yetty_ygui_textinput_get_text(a->address);
            const char *txt = NULL;
            if (YETTY_IS_OK(txt_r)) {
                txt = txt_r.value;
            } else {
                yetty_ycore_error_destroy(txt_r.error);
            }
            char *norm = normalize_url(txt ? txt : "");
            a->address_focused = 0;
            err_ok(yetty_ygui_textinput_set_focus(a->address, 0));
            navigate(a, &a->tabs[a->active], norm, 1);
            return 1;
        }
        struct yetty_ycore_int_result handle_result =
            yetty_ygui_textinput_handle_key(a->address, key, mods);
        if (YETTY_IS_ERR(handle_result)) {
            yetty_ycore_error_print(stderr, "ybrowser: textinput handle_key", handle_result.error);
            yetty_ycore_error_destroy(handle_result.error);
            return 0;
        }
        int consumed = handle_result.value;
        if (consumed) {
            yetty_ygui_framework_mark_dirty(fw);
        }
        return consumed;
    }
    if (a->console_focused) {
        if (key == 0x1B) { /* Esc → close DevTools */
            toggle_devtools(a);
            return 1;
        }
        if (key == '\r' || key == '\n') {
            console_submit(a);
            return 1;
        }
        struct yetty_ycore_int_result handle_result =
            yetty_ygui_textinput_handle_key(a->console_input, key, mods);
        if (YETTY_IS_ERR(handle_result)) {
            yetty_ycore_error_destroy(handle_result.error);
            return 0;
        }
        int consumed = handle_result.value;
        if (consumed) {
            yetty_ygui_framework_mark_dirty(fw);
        }
        return consumed;
    }
    return 0;
}

/* ===========================================================================
 * Shared per-frame + pointer helpers (used by both the in-yetty client loop
 * and the standalone GPU loop).
 * ===========================================================================*/

/* A pointer press at viewport coords (x, y): track address-bar focus, and if
 * it lands on the page, navigate a link or dispatch a JS click. Call AFTER
 * feeding the press to the framework so chrome widgets see it first. */
static void page_click(struct app *a, float x, float y)
{
    struct yetty_ycore_rectangle_result address_rect_res = yetty_ygui_widget_rect(a->address);
    if (YETTY_IS_ERR(address_rect_res)) {
        yetty_ycore_error_destroy(address_rect_res.error);
        return;
    }
    int in_addr = pt_in_rect(address_rect_res.value, x, y);
    err_ok(yetty_ygui_textinput_set_focus(a->address, in_addr ? 1 : 0));
    a->address_focused = in_addr;

    /* When DevTools is open, a click inside the REPL input takes keyboard
     * focus; a click anywhere else (address bar / page) releases it. */
    if (a->devtools_open && a->console_input) {
        struct yetty_ycore_rectangle_result console_rect_res =
            yetty_ygui_widget_rect(a->console_input);
        if (YETTY_IS_OK(console_rect_res)) {
            int in_console = pt_in_rect(console_rect_res.value, x, y);
            err_ok(yetty_ygui_textinput_set_focus(a->console_input, in_console ? 1 : 0));
            a->console_focused = in_console;
            if (in_console) {
                a->address_focused = 0;
                err_ok(yetty_ygui_textinput_set_focus(a->address, 0));
            }
        } else {
            yetty_ycore_error_destroy(console_rect_res.error);
        }
    }

    struct yetty_ycore_rectangle_result page_rect_res = yetty_ygui_widget_rect(a->page);
    if (YETTY_IS_ERR(page_rect_res)) {
        yetty_ycore_error_destroy(page_rect_res.error);
        return;
    }
    struct yetty_ycore_rectangle pr = page_rect_res.value;
    if (!pt_in_rect(pr, x, y)) {
        return;
    }
    struct tab *t = &a->tabs[a->active];
    if (!t->engine) {
        return;
    }
    /* Promoted form inputs sit on top of the page — a press inside one
     * focuses that edit box (and lets the widget place the caret); a press
     * anywhere else on the page releases it. */
    {
        int hit = -1;
        for (int i = 0; i < a->n_page_inputs; i++) {
            float vx = pr.min.x + a->page_inputs[i].doc_x;
            float vy = pr.min.y + a->page_inputs[i].doc_y;
            if (a->page_inputs[i].widget && x >= vx && x < vx + a->page_inputs[i].w && y >= vy &&
                y < vy + a->page_inputs[i].h) {
                hit = i;
                break;
            }
        }
        for (int i = 0; i < a->n_page_inputs; i++) {
            if (a->page_inputs[i].widget) {
                err_ok(yetty_ygui_textinput_set_focus(a->page_inputs[i].widget, i == hit));
            }
        }
        a->page_input_focused = hit;
        if (hit >= 0) {
            a->address_focused = 0;
            err_ok(yetty_ygui_textinput_set_focus(a->address, 0));
            yetty_ygui_framework_mark_dirty(a->fw);
            return; /* the widget's own click handling places the caret */
        }
    }
    /* The embed rect already includes the scroll slide, so subtracting
	 * rect.min yields document coords. A plain <a href> navigates;
	 * otherwise dispatch a JS click. */
    float lx = x - pr.min.x;
    float ly = y - pr.min.y;
    char *href = yetty_ylexbor_link_at(t->engine, lx, ly);
    if (href) {
        char *unwrapped = unwrap_gnews_read_link(t->engine, href, lx, ly);
        if (unwrapped) {
            free(href);
            href = unwrapped;
        }
        navigate(a, t, href, 1);
    } else if (yetty_ylexbor_dispatch_click(t->engine, lx, ly) &&
               yetty_ylexbor_dom_dirty(t->engine)) {
        t->needs_render = 1;
        a->pending_render = 1;
    }
}

/* Remove and destroy every child of a container (used to rebuild the DOM tree
 * on demand — widget_destroy detaches the child from its parent). */
static void dt_clear_children(struct yetty_yclass_object *parent)
{
    for (;;) {
        struct yetty_yclass_object_ptr_result first_res = yetty_ygui_widget_first_child(parent);
        if (YETTY_IS_ERR(first_res)) {
            yetty_ycore_error_destroy(first_res.error);
            return;
        }
        if (!first_res.value) {
            return;
        }
        err_ok(yetty_ygui_widget_destroy(first_res.value));
    }
}

/* Cap on how many DOM nodes the Elements tree materialises — one widget per
 * node, so an unbounded page (thousands of nodes) would be slow to build and
 * lay out. Beyond this the tree is truncated with a note. */
#define DOM_TREE_MAX_NODES 3000
#define DOM_TREE_MAX_DEPTH 64

/* Per-rebuild state threaded through the DOM walk: a stack mapping tree depth to
 * the widget new nodes at that depth attach under, plus the deeper branch nodes
 * to collapse once their children exist (folding is a no-op before then). */
struct dom_tree_builder {
    struct yetty_yclass_object *parent_at_depth[DOM_TREE_MAX_DEPTH];
    struct yetty_yclass_object *to_collapse[DOM_TREE_MAX_NODES];
    int collapse_count;
    int count;
};

/* yetty_ylexbor_dom_visit_fn: create a tree_node (branch) or label (leaf) for
 * one DOM node under the correct parent. Text nodes are muted, elements accent. */
static int dom_tree_visit(void *user, int depth, int has_children, const char *label)
{
    struct dom_tree_builder *builder = user;
    if (builder->count >= DOM_TREE_MAX_NODES) {
        return 1; /* stop the walk */
    }
    if (depth < 0 || depth + 1 >= DOM_TREE_MAX_DEPTH) {
        return 0; /* too deep to nest — skip this node, keep walking */
    }
    struct yetty_yclass_object *parent = builder->parent_at_depth[depth];
    if (!parent) {
        return 0;
    }
    builder->count++;
    if (has_children) {
        struct yetty_yclass_object_ptr_result node_res =
            yetty_ygui_widget_add(parent, yetty_ygui_tree_node_class_get().value);
        if (YETTY_IS_ERR(node_res)) {
            yetty_ycore_error_destroy(node_res.error);
            return 0;
        }
        err_ok(yetty_ygui_tree_node_set_label(node_res.value, label));
        /* Tree nodes are created open (the ctor default) so their children can
         * be attached; deeper nodes (below html → head/body) are folded in a
         * second pass after the subtree exists — see dom_tree_rebuild. */
        if (depth >= 2 && builder->collapse_count < DOM_TREE_MAX_NODES) {
            builder->to_collapse[builder->collapse_count++] = node_res.value;
        }
        builder->parent_at_depth[depth + 1] = node_res.value;
    } else {
        struct yetty_ycore_rgba color =
            label[0] == '"' ? (struct yetty_ycore_rgba){159, 167, 168, 255}  /* text */
                            : (struct yetty_ycore_rgba){116, 197, 165, 255}; /* elem */
        struct yetty_yclass_object *leaf = dt_add_label(parent, label, color, CONSOLE_FONT_SIZE);
        if (leaf) {
            /* Explicit row height: labels report no intrinsic height to the flex
             * layout, so without it leaf rows would stack at the same y. The
             * left pad aligns leaf text under a branch's label (past its chevron). */
            dt_layout(leaf, 20.0f, 0.0f, 20.0f, 0.0f);
        }
    }
    return 0;
}

/* Rebuild the Elements tree from the active tab's parsed DOM. */
static void dom_tree_rebuild(struct app *a)
{
    if (!a->tree_box) {
        return;
    }
    dt_clear_children(a->tree_box);
    struct yetty_ycore_rgba muted = {85, 97, 98, 255};
    struct tab *t = &a->tabs[a->active];
    if (!t->engine) {
        struct yetty_yclass_object *note =
            dt_add_label(a->tree_box, "(no HTML document for this tab)", muted, CONSOLE_FONT_SIZE);
        if (note) {
            dt_layout(note, 20.0f, 0.0f, 6.0f, 0.0f);
        }
        return;
    }
    struct dom_tree_builder builder = {0};
    builder.parent_at_depth[0] = a->tree_box;
    yetty_ylexbor_dom_walk(t->engine, dom_tree_visit, &builder);
    /* Fold the deeper branches now that their children exist. */
    for (int i = 0; i < builder.collapse_count; i++) {
        err_ok(yetty_ygui_tree_node_set_open(builder.to_collapse[i], 0));
    }
    const char *note_text = NULL;
    if (builder.count == 0) {
        note_text = "(empty document)";
    } else if (builder.count >= DOM_TREE_MAX_NODES) {
        note_text = "… tree truncated (too many nodes)";
    }
    if (note_text) {
        struct yetty_yclass_object *note =
            dt_add_label(a->tree_box, note_text, muted, CONSOLE_FONT_SIZE);
        if (note) {
            dt_layout(note, 20.0f, 0.0f, 6.0f, 0.0f);
        }
    }
}

/* Switch the DevTools panel between the Console (0) and Elements (1) tabs:
 * swap which pane is in the flow, recolor the tab buttons, move focus, and
 * (for Elements) rebuild the DOM tree from the current document. */
static void switch_devtools_tab(struct app *a, int tab)
{
    a->devtools_tab = tab;
    int elements = (tab == 1);
    dt_set_hidden(a->console_pane, elements);
    dt_set_hidden(a->elements_pane, !elements);
    err_ok(yetty_ygui_widget_set_bg_color(a->tab_console, elements ? BR_BG_LIFTED : BR_BG_ROW));
    err_ok(yetty_ygui_widget_set_bg_color(a->tab_elements, elements ? BR_BG_ROW : BR_BG_LIFTED));
    if (elements) {
        a->console_focused = 0;
        err_ok(yetty_ygui_textinput_set_focus(a->console_input, 0));
        dom_tree_rebuild(a);
    } else {
        a->console_focused = 1;
        err_ok(yetty_ygui_textinput_set_focus(a->console_input, 1));
        a->console_dirty = 1;
    }
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Reveal or hide the DevTools panel. Opening docks it at DEVTOOLS_HEIGHT and
 * restores the active tab (focusing the REPL prompt on the Console tab);
 * closing collapses it and returns focus to the page. */
static void toggle_devtools(struct app *a)
{
    a->devtools_open = !a->devtools_open;
    err_ok(yetty_ygui_widget_set_visible(a->devtools, a->devtools_open));
    dt_layout(a->devtools, a->devtools_open ? DEVTOOLS_HEIGHT : 0.0f, 0.0f, 0.0f, 0.0f);
    if (a->devtools_open) {
        a->address_focused = 0;
        err_ok(yetty_ygui_textinput_set_focus(a->address, 0));
        switch_devtools_tab(a, a->devtools_tab);
    } else {
        a->console_focused = 0;
        err_ok(yetty_ygui_textinput_set_focus(a->console_input, 0));
    }
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Append one console entry as rich lines, splitting embedded newlines (e.g.
 * exception stacks) into separate rows since the rich widget draws each span
 * on a single line. The accent marker prefixes only the first row. */
static void console_add_wrapped(struct app *a, const char *marker, const char *text, uint32_t color)
{
    const char *segment = text;
    int first_segment = 1;
    while (segment) {
        const char *newline = strchr(segment, '\n');
        size_t segment_len = newline ? (size_t)(newline - segment) : strlen(segment);
        char line[1024];
        size_t copy_len = segment_len < sizeof(line) - 1 ? segment_len : sizeof(line) - 1;
        memcpy(line, segment, copy_len);
        line[copy_len] = '\0';
        err_ok(yetty_ygui_rich_add_line(a->console_log));
        if (first_segment && marker && marker[0]) {
            err_ok(yetty_ygui_rich_add_span(a->console_log, marker, CONSOLE_FONT_SIZE,
                                            CONSOLE_ACCENT));
        }
        err_ok(yetty_ygui_rich_add_span(a->console_log, line, CONSOLE_FONT_SIZE, color));
        first_segment = 0;
        segment = newline ? newline + 1 : NULL;
    }
}

/* Rebuild the console log from the active engine's ring. The rich widget paints
 * top-down and clips at its bottom edge, so we feed only the newest lines that
 * fit — the latest line lands just above the prompt, terminal-style. Must run
 * after layout_compute so the log widget's height is known. */
static void console_refresh(struct app *a)
{
    if (!a->console_log) {
        return;
    }
    err_ok(yetty_ygui_rich_clear(a->console_log));
    a->console_dirty = 0;

    struct tab *t = &a->tabs[a->active];
    struct yetty_ylexbor *engine = t->engine;
    a->console_seen_total = engine ? yetty_ylexbor_console_total(engine) : 0;
    if (!engine) {
        return;
    }
    int count = yetty_ylexbor_console_count(engine);
    if (count == 0) {
        return;
    }
    /* Lines that fit: rich advances rows by font_size * 1.25. */
    int fit = count;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(a->console_log);
    if (YETTY_IS_OK(rect_res)) {
        float height = rect_res.value.max.y - rect_res.value.min.y;
        int rows = (int)(height / (CONSOLE_FONT_SIZE * 1.25f));
        if (rows < 1) {
            rows = 1;
        }
        if (rows < fit) {
            fit = rows;
        }
    }
    for (int i = count - fit; i < count; i++) {
        struct yetty_ylexbor_console_view entry = yetty_ylexbor_console_at(engine, i);
        if (!entry.text) {
            continue;
        }
        uint32_t color = CONSOLE_TEXT;
        const char *marker = "";
        switch (entry.level) {
        case YETTY_YLEXBOR_CONSOLE_WARN:
            color = CONSOLE_WARN;
            break;
        case YETTY_YLEXBOR_CONSOLE_ERROR:
            color = CONSOLE_ERROR;
            break;
        case YETTY_YLEXBOR_CONSOLE_DEBUG:
            color = CONSOLE_MUTED;
            break;
        case YETTY_YLEXBOR_CONSOLE_INPUT:
            color = CONSOLE_INPUT;
            marker = "\xe2\x80\xba "; /* › */
            break;
        case YETTY_YLEXBOR_CONSOLE_RESULT:
            color = CONSOLE_RESULT;
            marker = "\xe2\x80\xb9 "; /* ‹ */
            break;
        default:
            break;
        }
        console_add_wrapped(a, marker, entry.text, color);
    }
}

/* Evaluate the REPL input against the active tab's engine, echo + result land
 * in the console ring, then clear the prompt. */
static void console_submit(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    struct yetty_ycore_const_char_ptr_result txt_res =
        yetty_ygui_textinput_get_text(a->console_input);
    const char *src = NULL;
    if (YETTY_IS_OK(txt_res)) {
        src = txt_res.value;
    } else {
        yetty_ycore_error_destroy(txt_res.error);
    }
    if (src && src[0] && t->engine) {
        struct yetty_ycore_char_ptr_result eval_res = yetty_ylexbor_eval_js(t->engine, src);
        if (YETTY_IS_OK(eval_res)) {
            free(eval_res.value);
        } else {
            yetty_ycore_error_destroy(eval_res.error);
        }
        /* The expression may have mutated the DOM (document.body.style…, etc.)
         * — repaint the page too, not just the console. */
        if (yetty_ylexbor_dom_dirty(t->engine)) {
            t->needs_render = 1;
        }
    }
    err_ok(yetty_ygui_textinput_set_text(a->console_input, ""));
    a->console_dirty = 1;
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Lay out the tree against the current viewport and (re)render the active
 * tab's page into the embed when needed. Idempotent / cheap when nothing
 * changed. */
static void render_pass(struct app *a)
{
    double t_render = yetty_ylexbor_prof_now_ms();
    struct yetty_ycore_rectangle root_rect = {{0.0f, 0.0f}, {a->viewport_w, a->viewport_h}};
    err_ok(yetty_ygui_layout_compute(a->root, root_rect));
    if (a->devtools_open && a->console_dirty) {
        /* Rebuild the console after layout so the log widget height is known,
         * then re-run layout so the fresh rich content is placed this frame. */
        console_refresh(a);
        err_ok(yetty_ygui_layout_compute(a->root, root_rect));
    }
    render_active(a);
    a->pending_render = 0;
    /* Each render relayouts the page and re-ships the WHOLE drawable list to
     * the GPU. A flood of these (one per image as it streams in) is a prime
     * suspect for "slow load". `render_count` is app state, not a global. */
    a->render_count++;
    yetty_ylexbor_prof("  render_pass #%d  %.0f ms", a->render_count,
                       yetty_ylexbor_prof_now_ms() - t_render);
}

/* Page-input promotion: every visible text-like <input> box in the active
 * engine gets a REAL ygui textinput overlaid at its document position (the
 * same edit widget the address bar and demo 44 use — caret, selection,
 * word-drag, the lot). The pool is rebuilt after each ship: box indices and
 * element pointers die on re-parse. Widgets are created once, parented to
 * the root, absolutely placed (set_rect + raise) each pump so they ride
 * scroll via the embed rect, and hidden when the pool shrinks. */
static void reposition_page_inputs(struct app *a)
{
    if (a->n_page_inputs == 0 || a->showing_image || a->no_ui) {
        return;
    }
    /* Absolute children of the scrollarea are placed at content_min + pos
     * and are NOT slid by the scroll offset (layout.c places them outside
     * the flex bookkeeping). The embed rect carries the slide, so anchoring
     * pos to (embed.min - scroll.min) + doc keeps the overlays glued to the
     * page as it scrolls. Re-run every pump tick. */
    struct yetty_ycore_rectangle_result pr_res = yetty_ygui_widget_rect(a->page);
    struct yetty_ycore_rectangle_result sr_res = yetty_ygui_widget_rect(a->scroll);
    if (YETTY_IS_ERR(pr_res) || YETTY_IS_ERR(sr_res)) {
        if (YETTY_IS_ERR(pr_res)) {
            yetty_ycore_error_destroy(pr_res.error);
        }
        if (YETTY_IS_ERR(sr_res)) {
            yetty_ycore_error_destroy(sr_res.error);
        }
        return;
    }
    float base_x = pr_res.value.min.x - sr_res.value.min.x;
    float base_y = pr_res.value.min.y - sr_res.value.min.y;
    float view_h = sr_res.value.max.y - sr_res.value.min.y;
    for (int i = 0; i < a->n_page_inputs; i++) {
        struct yetty_yclass_object *widget = a->page_inputs[i].widget;
        if (!widget) {
            continue;
        }
        float px = base_x + a->page_inputs[i].doc_x;
        float py = base_y + a->page_inputs[i].doc_y;
        /* The scrollarea scissor clips the figure, but a widget fully above/
         * below the viewport must also stop eating clicks - hide it. */
        int visible = py + a->page_inputs[i].h > 0.0f && py < view_h;
        err_ok(yetty_ygui_widget_set_visible(widget, visible));
        if (!visible) {
            continue;
        }
        struct yetty_ygui_layout_const_ptr_result lay_res = yetty_ygui_widget_layout_get(widget);
        if (YETTY_IS_ERR(lay_res)) {
            yetty_ycore_error_destroy(lay_res.error);
            continue;
        }
        struct yetty_ygui_layout lay = *lay_res.value;
        lay.absolute = 1;
        lay.pos_x = px;
        lay.pos_y = py;
        lay.width = a->page_inputs[i].w;
        lay.height = a->page_inputs[i].h;
        err_ok(yetty_ygui_widget_layout_set(widget, &lay));
        err_ok(yetty_ygui_widget_raise(widget));
    }
}

static void promote_page_inputs(struct app *a, struct tab *t)
{
    int used = 0;
    /* Re-ships happen constantly (hover washes, streamed images) — keep the
     * focused slot and its in-progress text across the rebuild; only a
     * navigation (different document) genuinely invalidates them. */
    int keep_focus = a->page_input_focused;
    if (t->engine && !a->showing_image && !a->no_ui) {
        int count = yetty_ylexbor_test_box_count(t->engine);
        for (int bi = 0; bi < count && used < MAX_PAGE_INPUTS; bi++) {
            float x = 0, y = 0, w = 0, h = 0;
            char tag[32] = {0};
            if (yetty_ylexbor_test_box_at(t->engine, bi, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
                continue;
            }
            if (strcmp(tag, "input") != 0 || w < 24.0f || h < 10.0f) {
                continue;
            }
            /* Hidden forms (webauthn shells, 2FA variants) carry inputs too -
             * promoting those grabs focus for invisible widgets and shuffles
             * the slots under the user typing. */
            float box_opacity = 1.0f;
            int box_hidden = 0;
            (void)yetty_ylexbor_test_box_paint_at(t->engine, bi, &box_opacity, &box_hidden);
            if (box_opacity < 0.02f || box_hidden) {
                continue;
            }
            /* Text-like types only (default when the attr is absent). */
            char type[24] = {0};
            (void)yetty_ylexbor_test_box_attr_at(t->engine, bi, "type", type, sizeof(type));
            if (type[0] != '\0' && strcmp(type, "text") != 0 && strcmp(type, "email") != 0 &&
                strcmp(type, "password") != 0 && strcmp(type, "search") != 0 &&
                strcmp(type, "url") != 0 && strcmp(type, "tel") != 0) {
                continue;
            }
            struct yetty_yclass_object *widget = a->page_inputs[used].widget;
            if (!widget) {
                struct yetty_yclass_ptr_result cls = yetty_ygui_textinput_class_get();
                if (YETTY_IS_ERR(cls)) {
                    yetty_ycore_error_destroy(cls.error);
                    break;
                }
                struct yetty_yclass_object_ptr_result add_res =
                    yetty_ygui_widget_add(a->scroll, cls.value);
                if (YETTY_IS_ERR(add_res)) {
                    yetty_ycore_error_destroy(add_res.error);
                    break;
                }
                widget = add_res.value;
                a->page_inputs[used].widget = widget;
                /* Absolute: exempt from the root vbox layout — same escape
                 * hatch the dialog panel uses; reposition_page_inputs owns
                 * the rect. */
                struct yetty_ygui_layout_const_ptr_result lay_res =
                    yetty_ygui_widget_layout_get(widget);
                if (YETTY_IS_OK(lay_res)) {
                    struct yetty_ygui_layout lay = *lay_res.value;
                    lay.absolute = 1;
                    err_ok(yetty_ygui_widget_layout_set(widget, &lay));
                } else {
                    yetty_ycore_error_destroy(lay_res.error);
                }
            }
            char placeholder[128] = {0};
            (void)yetty_ylexbor_test_box_attr_at(t->engine, bi, "placeholder", placeholder,
                                                 sizeof(placeholder));
            err_ok(yetty_ygui_textinput_set_placeholder(widget, placeholder));
            if (used != keep_focus) {
                char value[256] = {0};
                (void)yetty_ylexbor_test_box_attr_at(t->engine, bi, "value", value, sizeof(value));
                err_ok(yetty_ygui_textinput_set_text(widget, value));
                err_ok(yetty_ygui_textinput_set_focus(widget, 0));
            }
            a->page_inputs[used].box_index = bi;
            a->page_inputs[used].doc_x = x;
            a->page_inputs[used].doc_y = y;
            a->page_inputs[used].w = w;
            a->page_inputs[used].h = h;
            used++;
        }
    }
    /* Hide pool slots beyond this page's input count. */
    for (int i = used; i < a->n_page_inputs; i++) {
        if (a->page_inputs[i].widget) {
            err_ok(yetty_ygui_widget_set_visible(a->page_inputs[i].widget, 0));
        }
    }
    a->n_page_inputs = used;
    a->page_input_focused = (keep_focus >= 0 && keep_focus < used) ? keep_focus : -1;
    reposition_page_inputs(a);
}

/* Pump the active tab's JS timers; flag a re-render if the DOM changed.
 * Returns ms until the next timer (clamped), or 100 when idle. */
static int pump_active(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    int wait_ms = 100;
    int images_fetched = 0;
    /* Overlaid form inputs ride the embed rect (scroll slide included) —
     * re-place them every pump so scrolling doesn't leave them behind. */
    reposition_page_inputs(a);
    if (t->engine) {
        /* Progressive rendering: scripts were deferred so the initial HTML+CSS
		 * could paint immediately. Once that first paint has landed
		 * (rendered_w > 0), run them now, then repaint with the scripted result.
		 * Done before the timer pump so any timers the scripts schedule are
		 * picked up the same tick. */
        if (t->scripts_pending && t->rendered_w > 0.0f) {
            t->scripts_pending = 0;
            err_ok(yetty_ylexbor_run_deferred_scripts(t->engine));
            t->needs_render = 1;
            a->pending_render = 1;
            wait_ms = 0;
        }
        int next = yetty_ylexbor_pump_timers(t->engine);
        if (next >= 0) {
            wait_ms = next < 100 ? next : 100;
        }
        if (yetty_ylexbor_dom_dirty(t->engine)) {
            t->needs_render = 1;
            a->pending_render = 1;
        }
        /* New page console.* output while DevTools is open → refresh the log. */
        if (a->devtools_open && yetty_ylexbor_console_total(t->engine) != a->console_seen_total) {
            a->console_dirty = 1;
            a->pending_render = 1;
        }
        if (a->img_pool) {
            /* Async path: submit every pending <img> to the worker pool — they
             * fetch + decode in parallel on background threads (sharing
             * libcurl's HTTP/2 connection pool) and stream in via on_img_ready.
             * Nothing blocks the loop; already-loading/cached images are
             * skipped, so calling it each pump is cheap and idempotent. */
            struct yetty_ycore_int_result fetch_res = yetty_ylexbor_start_image_fetch(t->engine);
            if (YETTY_IS_ERR(fetch_res)) {
                yetty_ycore_error_destroy(fetch_res.error);
            }
            /* Coalesce the resulting repaints: render at most once per window
             * while images keep landing, instead of once per image. */
            if (a->img_dirty) {
                double now = yetty_ylexbor_prof_now_ms();
                if (now - a->img_last_render_ms >= IMG_RENDER_DEBOUNCE_MS) {
                    a->img_delta_pending = 1;
                    a->pending_render = 1;
                    a->img_dirty = 0;
                    a->img_last_render_ms = now;
                }
            }
            /* While fetches are still in flight, keep the loop ticking by
             * asking for a zero wait. The standalone handler re-arms a RENDER
             * event whenever pump returns 0, and select() in the in-yetty loop
             * won't block — so a completion that lands on a busy GPU frame (and
             * gets dropped by the is_busy guard) is retried next tick instead
             * of stalling until an unrelated event wakes the loop. */
            if (yetty_ylexbor_images_in_flight(t->engine) > 0 || a->img_dirty) {
                wait_ms = 0;
            }
        } else {
            /* In-yetty client (no pool): stream a small parallel batch per
             * pump — one multiplexed round-trip for four images instead of
             * one blocking round-trip for one. */
            images_fetched = yetty_ylexbor_fetch_pending_images(t->engine, 4);
            if (images_fetched > 0) {
                a->img_delta_pending = 1;
                a->pending_render = 1;
                wait_ms = 0;
            }
        }
    }

    /* Reload/stop toggle: `loading` is raised by navigate() and lowered
	 * here on the first quiet tick — deferred scripts done and no image
	 * work this round. One-way per navigation (Chrome-like): image churn
	 * that page JS causes later does not re-raise the X, otherwise script
	 * heavy pages (Wikipedia's lazy loader) would show "loading" forever. */
    if (a->loading) {
        int quiet = t->engine == NULL ||
                    (t->nav_cancel_cell == NULL && t->scripts_pending == 0 && images_fetched == 0 &&
                     (a->img_pool == NULL ||
                      (yetty_ylexbor_images_in_flight(t->engine) == 0 && a->img_dirty == 0)));
        yetty_ylexbor_prof("pump loading: scripts_pending=%d images_fetched=%d quiet=%d",
                           t->engine ? t->scripts_pending : -1, images_fetched, quiet);
        if (quiet) {
            set_loading(a, 0);
        }
    }

    return wait_ms;
}

/* ===========================================================================
 * Inbound OSC decode (host → client mouse / resize).
 * ===========================================================================*/
static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct app *a = user;

    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE ||
        osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) {
        if (payload_len < sizeof(struct yetty_client_input_mouse)) {
            return;
        }
        const struct yetty_client_input_mouse *m = (const struct yetty_client_input_mouse *)payload;
        /* Host forwards mouse coords in framebuffer px; widgets hit-test in
         * logical, so divide by the host content_scale learned from the
         * resize OSC. */
        float scale = pane_host_scale_from(a);
        float mx = (float)m->x / scale;
        float my = (float)m->y / scale;
        if (m->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            struct yetty_ycore_int_result fr =
                yetty_ygui_framework_feed_mouse_motion(a->fw, mx, my);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
            struct yetty_ycore_int_result fr =
                yetty_ygui_framework_feed_mouse_button(a->fw, mx, my, m->button, m->pressed, 0);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (m->pressed) {
                page_click(a, mx, my);
            }
            yetty_ygui_framework_mark_dirty(a->fw);
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
            err_ok_int(yetty_ygui_framework_feed_mouse_scroll(a->fw, mx, my, 0.0f, m->wheel_dy));
        }
        return;
    }

    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_KEY ||
        osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY) {
        /* After a mouse press focuses our figure, the HOST routes keyboard
         * input here as key envelopes instead of writing bytes to the PTY —
         * dropping them left the address bar / page inputs deaf right after
         * any click. Mirror the standalone translation: CHAR -> UTF-8 text,
         * DOWN -> encoded special key; both through framework_feed_input,
         * which drives the same key_cb as the PTY byte path. */
        if (payload_len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *k = (const struct yetty_client_input_key *)payload;
        if (k->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint != 0) {
            char utf8_buf[8];
            size_t n = utf8_encode(k->codepoint, utf8_buf);
            if (n > 0) {
                err_ok(yetty_ygui_framework_feed_input(a->fw, utf8_buf, n));
            }
        } else if (k->kind == YETTY_YMGUI_INPUT_KEY_DOWN) {
            char scratch[8];
            size_t n = 0;
            const char *bytes =
                encode_special_key((uint32_t)k->key, k->mods, scratch, sizeof(scratch), &n);
            if (bytes && n > 0) {
                err_ok(yetty_ygui_framework_feed_input(a->fw, bytes, n));
            }
        }
        yetty_ygui_framework_mark_dirty(a->fw);
        return;
    }
    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE ||
        osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) {
        if (payload_len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *rz =
            (const struct yetty_client_input_resize *)payload;
        /* Learn the host's HiDPI factor first so pane_host_scale_from below
         * sees the new value. The host's default-kind ygrid factory runs in
         * absolute-coords mode: our LOGICAL-px widget prims get scaled up to
         * framebuffer at receiver add-record time, so the viewport we hand
         * ygui must ALSO be logical (fb / scale). Widget rects come back
         * from ygui in the same logical space and feed lexbor's CSS viewport
         * directly — no second divide (render_doc). */
        if (rz->content_scale > 0.0f) {
            a->host_content_scale = rz->content_scale;
        }
        if (rz->width > 0 && rz->height > 0) {
            float scale = pane_host_scale_from(a);
            float logical_w = rz->width / scale;
            float logical_h = rz->height / scale;
            a->viewport_w = logical_w;
            a->viewport_h = logical_h;
            err_ok(yetty_ygui_framework_set_viewport(a->fw, logical_w, logical_h));
            a->pending_render = 1; /* content width may have changed */
            yetty_ygui_framework_mark_dirty(a->fw);
        }
        return;
    }
}

static void on_raw(void *user, const char *bytes, size_t n)
{
    struct app *a = user;
    /* Fallback quit detection in case the decoder doesn't surface the
	 * control byte as a key event. */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == 0x03 || c == 0x04) {
            a->running = 0;
        }
    }
    err_ok(yetty_ygui_framework_feed_input(a->fw, bytes, n));
}

/* ===========================================================================
 * UI construction + render pipeline.
 * ===========================================================================*/
static struct yetty_yclass_object *add_nav_button(struct yetty_yclass_object *parent,
                                                  const char *label, float w,
                                                  yetty_ygui_click_cb cb, void *ud)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(parent, yetty_ygui_button_class_get().value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_button_set_label(r.value, label));
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(r.value);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return NULL;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = w;
    l.flex_grow = 0.0f;
    l.flex_shrink = 0.0f;
    err_ok(yetty_ygui_widget_layout_set(r.value, &l));
    if (cb) {
        err_ok(yetty_ygui_clickable_on_click_set(r.value, cb, ud));
    }
    return r.value;
}

/* Apply the common layout knobs for a DevTools widget in one shot (the raw
 * layout_get / mutate / layout_set dance is otherwise repeated per widget).
 * A negative height leaves the current height untouched. */
static void dt_layout(struct yetty_yclass_object *w, float height, float flex_grow, float pad_x,
                      float pad_y)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    if (height >= 0.0f) {
        l.height = height;
    }
    l.flex_grow = flex_grow;
    l.padding_left = pad_x;
    l.padding_right = pad_x;
    l.padding_top = pad_y;
    l.padding_bottom = pad_y;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* Pin a fixed width on a widget (flex_grow 0). ygui labels don't report an
 * intrinsic content width to the flex layout, so a non-growing label collapses
 * to zero width and overlaps its neighbour unless its width is set explicitly. */
static void dt_set_width(struct yetty_yclass_object *w, float width)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = width;
    l.flex_grow = 0.0f;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* Add a label child with text, color and font size set in one call. Returns
 * the widget, or NULL on failure (tolerated — a missing label just doesn't
 * paint). */
static struct yetty_yclass_object *dt_add_label(struct yetty_yclass_object *parent,
                                                const char *text, struct yetty_ycore_rgba color,
                                                float font_size)
{
    struct yetty_yclass_object_ptr_result lr =
        yetty_ygui_widget_add(parent, yetty_ygui_label_class_get().value);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return NULL;
    }
    err_ok(yetty_ygui_label_set_text(lr.value, text));
    err_ok(yetty_ygui_label_set_color(lr.value, color));
    err_ok(yetty_ygui_label_set_font_size(lr.value, font_size));
    return lr.value;
}

/* Build the bottom-docked DevTools panel: a header strip, the color-coded
 * console log, and the REPL prompt. Created collapsed (zero height, hidden);
 * toggle_devtools() reveals it. */
/* Toggle a widget in/out of the flex flow (hidden takes no space and paints
 * nothing) — used to swap the Console and Elements panes. */
static void dt_set_hidden(struct yetty_yclass_object *w, int hidden)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.hidden = hidden ? 1 : 0;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
    err_ok(yetty_ygui_widget_set_visible(w, hidden ? 0 : 1));
}

static int build_devtools(struct app *a)
{
    struct yetty_yclass_object_ptr_result dr =
        yetty_ygui_widget_add(a->root, yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
        return -1;
    }
    a->devtools = dr.value;
    err_ok(yetty_ygui_widget_set_bg_color(a->devtools, BR_BG));
    dt_layout(a->devtools, 0.0f, 0.0f, 0.0f, 0.0f);
    err_ok(yetty_ygui_widget_set_visible(a->devtools, 0));

    /* Header strip: the Console / Elements tab buttons + an F12 hint. */
    struct yetty_yclass_object_ptr_result hr =
        yetty_ygui_widget_add(a->devtools, yetty_ygui_hbox_class_get().value);
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
        return -1;
    }
    err_ok(yetty_ygui_widget_set_bg_color(hr.value, BR_BG_LIFTED));
    dt_layout(hr.value, 26.0f, 0.0f, 6.0f, 2.0f);
    a->tab_console = add_nav_button(hr.value, "Console", 86.0f, on_tab_console_click, a);
    a->tab_elements = add_nav_button(hr.value, "Elements", 90.0f, on_tab_elements_click, a);
    struct yetty_yclass_object *hint = dt_add_label(
        hr.value, "F12 to close", (struct yetty_ycore_rgba){85, 97, 98, 255}, CONSOLE_FONT_SIZE);
    if (hint) {
        dt_layout(hint, -1.0f, 1.0f, 10.0f, 0.0f);
    }

    /* ---- Console pane: color-coded log + REPL prompt. ---- */
    struct yetty_yclass_object_ptr_result cpr =
        yetty_ygui_widget_add(a->devtools, yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_ERR(cpr)) {
        yetty_ycore_error_destroy(cpr.error);
        return -1;
    }
    a->console_pane = cpr.value;
    dt_layout(a->console_pane, -1.0f, 1.0f, 0.0f, 0.0f);

    struct yetty_yclass_object_ptr_result lr =
        yetty_ygui_widget_add(a->console_pane, yetty_ygui_rich_class_get().value);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return -1;
    }
    a->console_log = lr.value;
    dt_layout(a->console_log, -1.0f, 1.0f, 8.0f, 4.0f);

    struct yetty_yclass_object_ptr_result ir =
        yetty_ygui_widget_add(a->console_pane, yetty_ygui_hbox_class_get().value);
    if (YETTY_IS_ERR(ir)) {
        yetty_ycore_error_destroy(ir.error);
        return -1;
    }
    err_ok(yetty_ygui_widget_set_bg_color(ir.value, BR_BG_LIFTED));
    dt_layout(ir.value, 30.0f, 0.0f, 8.0f, 3.0f);
    struct yetty_yclass_object *chevron = dt_add_label(
        ir.value, "›", (struct yetty_ycore_rgba){107, 168, 146, 255}, CONSOLE_FONT_SIZE + 2.0f);
    if (chevron) {
        dt_set_width(chevron, 14.0f);
    }
    struct yetty_yclass_object_ptr_result tir =
        yetty_ygui_widget_add(ir.value, yetty_ygui_textinput_class_get().value);
    if (YETTY_IS_ERR(tir)) {
        yetty_ycore_error_destroy(tir.error);
        return -1;
    }
    a->console_input = tir.value;
    err_ok(yetty_ygui_textinput_set_placeholder(a->console_input, "Evaluate JavaScript"));
    dt_layout(a->console_input, -1.0f, 1.0f, 0.0f, 0.0f);

    /* ---- Elements pane: a scrollable DOM tree (built on demand). ---- */
    struct yetty_yclass_object_ptr_result epr =
        yetty_ygui_widget_add(a->devtools, yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_ERR(epr)) {
        yetty_ycore_error_destroy(epr.error);
        return -1;
    }
    a->elements_pane = epr.value;
    dt_layout(a->elements_pane, -1.0f, 1.0f, 0.0f, 0.0f);

    struct yetty_yclass_object_ptr_result esr =
        yetty_ygui_widget_add(a->elements_pane, yetty_ygui_scrollarea_class_get().value);
    if (YETTY_IS_ERR(esr)) {
        yetty_ycore_error_destroy(esr.error);
        return -1;
    }
    dt_layout(esr.value, -1.0f, 1.0f, 4.0f, 4.0f);

    struct yetty_yclass_object_ptr_result tbr =
        yetty_ygui_widget_add(esr.value, yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_ERR(tbr)) {
        yetty_ycore_error_destroy(tbr.error);
        return -1;
    }
    a->tree_box = tbr.value;

    /* Start on the Console tab: Elements pane hidden, tab buttons colored. */
    dt_set_hidden(a->elements_pane, 1);
    err_ok(yetty_ygui_widget_set_bg_color(a->tab_console, BR_BG_ROW));
    err_ok(yetty_ygui_widget_set_bg_color(a->tab_elements, BR_BG_LIFTED));
    a->devtools_tab = 0;
    return 0;
}

static int build_ui(struct app *a)
{
    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return -1;
    }
    a->root = rr.value;
    err_ok(yetty_ygui_widget_set_bg_color(a->root, BR_BG));
    err_ok(yetty_ygui_framework_set_root(a->fw, a->root));

    /* Tab strip. */
    struct yetty_yclass_object_ptr_result tr =
        yetty_ygui_widget_add(a->root, yetty_ygui_tabbar_class_get().value);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
        return -1;
    }
    a->tabbar = tr.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(a->tabbar);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 36.0f;
        l.gap = 4.0f;
        /* Leave room on the right for the window controls (min/max/close) the
         * chrome overlays on this row — the client's responsibility, so a tab
         * never sits under a control. */
        l.padding_right = 3.0f * 46.0f;
        err_ok(yetty_ygui_widget_layout_set(a->tabbar, &l));
    }
    err_ok(yetty_ygui_tabbar_set_on_new_tab(a->tabbar, on_new_tab_cb, a));
    err_ok(yetty_ygui_tabbar_set_on_close(a->tabbar, on_close_cb, a));
    err_ok(
        yetty_ygui_widget_subscribe(a->tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_changed, a));

    /* --no-ui: collapse the tab strip to zero height and hide it. The widget
	 * stays created so tab bookkeeping elsewhere keeps working; it just takes
	 * no space and never paints. */
    if (a->no_ui) {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(a->tabbar);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 0.0f;
        err_ok(yetty_ygui_widget_layout_set(a->tabbar, &l));
        err_ok(yetty_ygui_widget_set_visible(a->tabbar, 0));
    }

    /* Toolbar: nav buttons + address bar. */
    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(a->root, yetty_ygui_hbox_class_get().value);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_destroy(br.error);
        return -1;
    }
    struct yetty_yclass_object *toolbar = br.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(toolbar);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 44.0f;
        l.gap = 6.0f;
        l.padding_top = 7.0f;
        l.padding_bottom = 7.0f;
        l.padding_left = 8.0f;
        l.padding_right = 8.0f;
        err_ok(yetty_ygui_widget_layout_set(toolbar, &l));
    }
    err_ok(yetty_ygui_widget_set_bg_color(toolbar, BR_TOOLBAR));

    /* --no-ui: collapse + hide the address/nav toolbar too. */
    if (a->no_ui) {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(toolbar);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 0.0f;
        l.padding_top = 0.0f;
        l.padding_bottom = 0.0f;
        err_ok(yetty_ygui_widget_layout_set(toolbar, &l));
        err_ok(yetty_ygui_widget_set_visible(toolbar, 0));
    }

    /* Nav buttons carry Chrome-style SDF icons (labels are a fallback for
	 * builds where the icon mode is unavailable): back / forward arrows and
	 * the reload ring that set_loading() swaps to an X while loading. */
    struct yetty_yclass_object *btn_back = add_nav_button(toolbar, "<", 40.0f, on_back_click, a);
    if (btn_back) {
        err_ok(yetty_ygui_button_set_chrome_icon(btn_back, 4));
    }
    struct yetty_yclass_object *btn_forward = add_nav_button(toolbar, ">", 40.0f, on_fwd_click, a);
    if (btn_forward) {
        err_ok(yetty_ygui_button_set_chrome_icon(btn_forward, 5));
    }
    a->btn_reload = add_nav_button(toolbar, "Reload", 40.0f, on_reload_click, a);
    if (a->btn_reload) {
        err_ok(yetty_ygui_button_set_chrome_icon(a->btn_reload, 6));
    }

    struct yetty_yclass_object_ptr_result ar =
        yetty_ygui_widget_add(toolbar, yetty_ygui_textinput_class_get().value);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
        return -1;
    }
    a->address = ar.value;
    err_ok(yetty_ygui_textinput_set_placeholder(a->address, "Type a URL and press Enter"));
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(a->address);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(a->address, &l));
    }

    /* DevTools toggle button, at the right end of the toolbar. The label reads
     * as a code marker; clicking it (or F12) opens the JavaScript console. This
     * is the discoverable, mouse-driven way in — F12 relies on the host
     * terminal forwarding the key, which not every host does. */
    add_nav_button(toolbar, "</>", 44.0f, on_devtools_click, a);

    /* Scrollable page area. */
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(a->root, yetty_ygui_scrollarea_class_get().value);
    if (YETTY_IS_ERR(sr)) {
        yetty_ycore_error_destroy(sr.error);
        return -1;
    }
    a->scroll = sr.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(a->scroll);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_destroy(layout_res.error);
            return -1;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(a->scroll, &l));
    }

    struct yetty_yclass_object_ptr_result pr =
        yetty_ygui_widget_add(a->scroll, yetty_ygui_ydraw_embed_class_get().value);
    if (YETTY_IS_ERR(pr)) {
        yetty_ycore_error_destroy(pr.error);
        return -1;
    }
    a->page = pr.value;

    /* Raster-image content uses a dedicated yimage widget (its own figure)
	 * — a yimage prim can't be painted by the ydraw_embed, and nesting the
	 * figure inside the scrollarea doesn't composite. So it's a sibling of
	 * the scrollarea; exactly one of {scrollarea, image} is visible. */
    struct yetty_yclass_object_ptr_result ir =
        yetty_ygui_widget_add(a->root, yetty_ygui_yimage_class_get().value);
    if (YETTY_IS_ERR(ir)) {
        yetty_ycore_error_destroy(ir.error);
        return -1;
    }
    a->image = ir.value;
    err_ok(yetty_ygui_widget_set_visible(a->image, 0));
    a->showing_image = 0;

    if (build_devtools(a) != 0) {
        return -1;
    }
    return 0;
}

/* Set the shared content widget's height (drives the scrollarea's scroll
 * range); width stays unset so the vbox stretch keeps it as wide as the
 * scrollarea (and the page follows pane resizes). */
static void set_content_height(struct app *a, struct yetty_yclass_object *w, int height_px)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = -1.0f;
    l.height = (float)height_px;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* HTML (ylexbor) or SVG (ysvg) → a draw list painted by the page embed. */
static void render_doc(struct app *a, struct tab *t)
{
    struct yetty_ycore_rectangle_result page_rect_res = yetty_ygui_widget_rect(a->page);
    if (YETTY_IS_ERR(page_rect_res)) {
        yetty_ycore_error_destroy(page_rect_res.error);
        return;
    }
    struct yetty_ycore_rectangle r = page_rect_res.value;
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 1.0f) {
        return; /* layout hasn't given the embed a width yet */
    }
    int dom_dirty = (t->kind == CK_HTML && t->engine && yetty_ylexbor_dom_dirty(t->engine));
    if (!t->needs_render && w == t->rendered_w && !dom_dirty) {
        return;
    }

    struct yetty_ydraw_drawable_list *dl = NULL;
    int content_h = (int)h;

    if (t->kind == CK_SVG) {
        struct yetty_ysvg_render_config cfg = {
            .cell_width = 8,
            .cell_height = 16,
            .width_cells = (uint32_t)(w / 8.0f),
            .height_cells = (uint32_t)(h / 16.0f),
        };
        struct yetty_ysvg_render_result sr =
            yetty_ysvg_render((const char *)t->raw, t->raw_len, NULL, 0, &cfg);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
            return;
        }
        dl = sr.value.buffer;
        content_h = (int)sr.value.scene_height;
    } else { /* CK_HTML */
        if (!t->engine) {
            return;
        }
        if (w != t->rendered_w) {
            /* w and h are ygui widget-rect coords, already in LOGICAL px
             * because the ygui framework viewport is set from
             * ws_xpixel/host_content_scale (see pick_pane_px + on_osc).
             * Lexbor's CSS viewport IS logical, so pass them through. Any
             * further /scale divide here halves the CSS canvas on HiDPI. */
            int vw = (int)w;
            int vh = (int)(h > a->viewport_h ? h : a->viewport_h);
            if (vw < 1) {
                vw = 1;
            }
            if (vh < 1) {
                vh = 1;
            }
            err_ok(yetty_ylexbor_set_viewport(t->engine, vw, vh));
        }
        err_ok(yetty_ylexbor_relayout(t->engine));
        struct yetty_ydraw_drawable_list_result dlr =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(dlr)) {
            yetty_ycore_error_destroy(dlr.error);
            return;
        }
        dl = dlr.value;
        struct yetty_ycore_void_result rd = yetty_ylexbor_render(t->engine, dl);
        if (YETTY_IS_ERR(rd)) {
            yetty_ycore_error_destroy(rd.error);
            yetty_ydraw_drawable_list_destroy(dl);
            return;
        }
        content_h = yetty_ylexbor_content_height(t->engine);
    }

    /* Skip the re-emit when the output is byte-identical to what we last
	 * shipped. Pages whose JS keeps mutating the DOM (timers, polling,
	 * analytics) re-render to the SAME draw list — without this we'd
	 * re-ship the whole page every frame (tens of MB/s for a real site).
	 * needs_render forces a ship (tab switch / fresh document). */
    const void *dl_bytes = yetty_ydraw_drawable_list_data(dl);
    size_t dl_sz = yetty_ydraw_drawable_list_size(dl);
    uint64_t h2 = fnv1a(dl_bytes, dl_sz);
    if (!t->needs_render && dl_sz == t->dl_size && h2 == t->dl_hash) {
        yetty_ydraw_drawable_list_destroy(dl);
        t->rendered_w = w;
        return;
    }
    t->dl_hash = h2;
    t->dl_size = dl_sz;

    /* In-yetty the page ships as one figure-body RPC, and the wire caps
	 * bodies at 64 MiB (#437; raised from 16 once images started shipping
	 * at display resolution) — ONE oversized body aborts the whole widget
	 * emit walk, silently freezing every other widget (reload/stop icon,
	 * hover washes, tab titles). Keep the previous page frame instead of
	 * shipping an oversized one: the page stays at its last good state
	 * while the chrome keeps updating. Standalone (own GPU window, no RPC)
	 * has no such cap. */
    if (a->event_loop == NULL && dl_sz > 60u * 1024u * 1024u) {
        yetty_ylexbor_prof("render_doc: draw list %zu bytes exceeds the wire cap — frame kept",
                           dl_sz);
        yetty_ydraw_drawable_list_destroy(dl);
        t->needs_render = 0;
        t->rendered_w = w;
        return;
    }

    /* Embed takes ownership of dl (frees the previous buffer). */
    err_ok(yetty_ygui_ydraw_embed_set_buffer(a->page, dl));
    if (content_h < (int)h) {
        content_h = (int)h; /* short content still fills the viewport */
    }
    set_content_height(a, a->page, content_h);
    t->needs_render = 0;
    t->rendered_w = w;
    /* Every ship rebuilds the page's box tree — re-promote the form inputs
     * so the overlay widgets track the fresh box indices/positions. */
    promote_page_inputs(a, t);
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Raster image → the yimage widget, sized to fit the width at the source
 * aspect ratio (so tall images scroll). */
static void render_image(struct app *a, struct tab *t)
{
    struct yetty_ycore_rectangle_result image_rect_res = yetty_ygui_widget_rect(a->image);
    if (YETTY_IS_ERR(image_rect_res)) {
        yetty_ycore_error_destroy(image_rect_res.error);
        return;
    }
    struct yetty_ycore_rectangle r = image_rect_res.value;
    float w = r.max.x - r.min.x;
    if (w <= 1.0f) {
        return;
    }
    if (t->needs_render) {
        err_ok(yetty_ygui_yimage_set_bytes(a->image, t->raw, t->raw_len));
        t->needs_render = 0;
        t->rendered_w = 0.0f; /* force the size recompute below */
    }
    if (w != t->rendered_w) {
        float ih = (t->img_w > 0 && t->img_h > 0) ? w * (float)t->img_h / (float)t->img_w : w;
        set_content_height(a, a->image, (int)ih);
        t->rendered_w = w;
        yetty_ygui_framework_mark_dirty(a->fw);
    }
}

/* Render the active tab's content into the shared content widget. Swaps the
 * visible widget (page embed vs image) to match the content kind. */
/* Attempt an incremental image-group ship: repaint only the image groups
 * whose pixels landed since the last full render and ship them straight to the
 * page figure (no CMD_ZERO, no full-page repaint/reship). Returns 1 if the
 * page was updated this way; 0 if a full render is needed (layout shifted, no
 * baseline yet, or missing handles) — the caller then falls back to
 * render_doc. */
static int try_image_delta(struct app *a, struct tab *t)
{
    if (t->kind != CK_HTML || !t->engine || t->rendered_w <= 0.0f) {
        return 0;
    }
    /* The delta lands directly on the page figure, so it must carry the same
	 * content offset the embed applies on a full render: the page widget's
	 * top-left (its buffer scene origin is 0,0). */
    struct yetty_ycore_rectangle_result page_rect_res = yetty_ygui_widget_rect(a->page);
    if (YETTY_IS_ERR(page_rect_res)) {
        yetty_ycore_error_destroy(page_rect_res.error);
        return 0;
    }
    float dx = page_rect_res.value.min.x;
    float dy = page_rect_res.value.min.y;

    struct yetty_ydraw_drawable_list_result delta_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(delta_res)) {
        yetty_ycore_error_destroy(delta_res.error);
        return 0;
    }
    struct yetty_ydraw_drawable_list *delta = delta_res.value;

    struct yetty_ylexbor_incremental_result inc =
        yetty_ylexbor_paint_image_deltas(t->engine, delta, dx, dy);
    if (!inc.is_delta) {
        yetty_ydraw_drawable_list_destroy(delta);
        return 0; /* layout changed / no baseline — caller does a full render */
    }

    if (inc.changed_groups > 0) {
        struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(a->scroll);
        const uint8_t *body = yetty_ydraw_drawable_list_data(delta);
        size_t body_len = yetty_ydraw_drawable_list_size(delta);
        if (!YETTY_IS_ERR(id_res) && id_res.value != 0 && body && body_len > 0) {
            struct yetty_ycore_void_result ship = yetty_ygui_framework_ship_figure_delta(
                a->fw, id_res.value, body, (uint32_t)body_len);
            if (YETTY_IS_ERR(ship)) {
                yetty_ycore_error_destroy(ship.error);
            } else {
                yetty_ylexbor_prof("  image delta  fig=%u groups=%d bytes=%zu", id_res.value,
                                   inc.changed_groups, body_len);
            }
        }
        if (YETTY_IS_ERR(id_res)) {
            yetty_ycore_error_destroy(id_res.error);
        }
        /* Nudge the loop so the standalone GPU window presents the updated
		 * figure; the in-yetty host redraws on receiving the body. */
        if (a->event_loop && a->event_loop->ops->request_render) {
            a->event_loop->ops->request_render(a->event_loop);
        }
    }

    yetty_ydraw_drawable_list_destroy(delta);
    return 1;
}

static void render_active(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    int want_image = (t->kind == CK_IMAGE);
    if (want_image != a->showing_image) {
        err_ok(yetty_ygui_widget_set_visible(a->scroll, !want_image));
        err_ok(yetty_ygui_widget_set_visible(a->image, want_image));
        a->showing_image = want_image;
        a->pending_render = 1; /* layout geometry changed */
    }
    if (want_image) {
        render_image(a, t);
        a->img_delta_pending = 0;
        return;
    }
    /* Fast path: only streamed images changed since the last full render. Ship
	 * per-image group deltas instead of a whole-page repaint + reship. If the
	 * relayout shifted anything (or there's no baseline yet) fall through to a
	 * full render. */
    if (a->img_delta_pending && !t->needs_render) {
        if (try_image_delta(a, t)) {
            a->img_delta_pending = 0;
            return;
        }
        t->needs_render = 1; /* delta bailed — force render_doc to repaint */
    }
    a->img_delta_pending = 0;
    render_doc(a, t);
}

/* Create the first tab and load `initial_url` (or the start page). */
static void open_first_tab(struct app *a, const char *initial_url)
{
    struct yetty_yclass_object_ptr_result hr = yetty_ygui_tabbar_add_tab(a->tabbar, "New Tab");
    if (YETTY_IS_OK(hr)) {
        a->n_tabs = 1;
        a->active = 0;
    } else {
        yetty_ycore_error_destroy(hr.error);
        return;
    }
    if (initial_url) {
        navigate(a, &a->tabs[0], normalize_url(initial_url), 0);
    } else {
        load_start_page(a, &a->tabs[0]);
    }
}

/* ===========================================================================
 * Subscription + loop.
 * ===========================================================================*/
/* Enable/disable pixel-precise mouse forwarding from the host. The host
 * watches DEC private modes 1500 (button) and 1501 (move); flipping them on
 * makes it re-emit pointer events as YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE
 * (and, on the rising edge, one FIGURE_RESIZE carrying the pane pixel size).
 * This is the path ygreeter / ymgui use — CLIENT_INPUT_SUB does not drive
 * it. Keystrokes arrive on stdin normally and need no subscription. */
static void set_mouse_forwarding(int on)
{
    const char *seq = on ? "\033[?1500h\033[?1501h" : "\033[?1500l\033[?1501l";
    fwrite(seq, 1, strlen(seq), stdout);
    fflush(stdout);
}

/* Minimal event-loop adapter for the in-yetty client's select() loop. The
 * worker pool needs exactly ONE op — post_to_loop — to hand completions to
 * the loop thread. Each post is one atomic write of a (fn, ctx) pair
 * through a self-pipe (16 bytes, far under PIPE_BUF); the select loop
 * drains the pipe and invokes the completions in order. */
struct client_pipe_completion {
    void (*fn)(void *);
    void *ctx;
};

struct client_pipe_loop {
    struct yetty_yevent_event_loop base;
    int read_fd;
    int write_fd;
};

/* ops-vtable impl — signature fixed by the event-loop ops table. */
static void client_pipe_post_to_loop(struct yetty_yevent_event_loop *self, void (*fn)(void *),
                                     void *arg)
{
    struct client_pipe_loop *loop = (struct client_pipe_loop *)self;
    struct client_pipe_completion completion = {.fn = fn, .ctx = arg};
    ssize_t wrote = write(loop->write_fd, &completion, sizeof(completion));
    if (wrote != (ssize_t)sizeof(completion)) {
        /* Pipe full/broken: run inline as a last resort — the callbacks
		 * only set repaint flags, and losing one wedges a navigation. */
        fn(arg);
    }
}

static const struct yetty_yevent_event_loop_ops *client_pipe_loop_ops(void)
{
    static const struct yetty_yevent_event_loop_ops ops = {
        .post_to_loop = client_pipe_post_to_loop,
    };
    return &ops;
}

/* Drain every queued completion. Returns the number invoked. */
static int client_pipe_drain(struct client_pipe_loop *loop)
{
    int drained = 0;
    struct client_pipe_completion completion;
    for (;;) {
        ssize_t got = read(loop->read_fd, &completion, sizeof(completion));
        if (got != (ssize_t)sizeof(completion)) {
            break;
        }
        completion.fn(completion.ctx);
        drained++;
    }
    return drained;
}

/* Measurement font for the client framework — same DejaVu Mono MSDF the
 * host renders with, so textinput carets, click hit-tests and label widths
 * are computed with the REAL glyph advances (the standalone path wires a
 * font at window creation; without one the widgets fall back to the crude
 * fixed advance and the address bar caret lands between the wrong glyphs).
 * Mirrors yguiapp's client measure font. */
static struct yetty_yfont_font *client_measure_font_create(void)
{
    struct yetty_yplatform_paths_ptr_result paths_res = yetty_yplatform_paths_create();
    if (YETTY_IS_ERR(paths_res)) {
        yetty_ycore_error_destroy(paths_res.error);
        return NULL;
    }
    char cdb_path[768];
    char shader_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb",
             paths_res.value->fonts_dir_buf, "DejaVuSansMNerdFontMono");
    snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl",
             paths_res.value->shaders_dir_buf);
    struct yetty_font_font_result font_res =
        yetty_yfont_msdf_font_create(cdb_path, shader_path, "ybrowser_measure");
    struct yetty_ycore_void_result paths_destroy = yetty_yplatform_paths_destroy(paths_res.value);
    if (YETTY_IS_ERR(paths_destroy)) {
        yetty_ycore_error_destroy(paths_destroy.error);
    }
    if (YETTY_IS_ERR(font_res)) {
        yetty_ycore_error_destroy(font_res.error);
        return NULL;
    }
    struct yetty_ycore_void_result load = font_res.value->ops->load_basic_latin(font_res.value);
    if (YETTY_IS_ERR(load)) {
        yetty_ycore_error_destroy(load.error);
    }
    return font_res.value;
}

int ybrowser_ui_run(const char *initial_url, int viewport_w, int viewport_h, float font_size)
{
    ytrace_init();

    /* Raw tty BEFORE any OSC write — otherwise the slave echoes our ESC
	 * bytes and the host renders "^[" garbage. cfmakeraw also disables
	 * ISIG, so Ctrl-C/D arrive as bytes (0x03/0x04) on stdin instead of
	 * signals — that is how we quit, so no signal handlers are needed. */
    struct termios saved;
    int raw_ok = 0;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw = saved;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            raw_ok = 1;
        }
    }

    struct app a = {0};
    a.running = 1;
    a.host_content_scale = 1.0f;
    a.viewport_w = (float)viewport_w;
    a.viewport_h = (float)viewport_h;
    a.font_size = font_size > 0.0f ? font_size : 16.0f;
    {
        struct yetty_ybrowser_loader_ptr_result loader_res = yetty_ybrowser_loader_create();
        if (YETTY_IS_OK(loader_res)) {
            a.loader = loader_res.value;
        } else {
            yetty_ycore_error_destroy(loader_res.error);
        }
    }

    /* Self-pipe completion loop + worker pool: navigations and image
	 * fetches run on background threads in the client too, so a slow
	 * origin never freezes input handling. a.event_loop stays NULL — the
	 * client-specific paths (render pacing, the DCS size guard) key on
	 * it. */
    struct client_pipe_loop pipe_loop = {
        .base.ops = client_pipe_loop_ops(), .read_fd = -1, .write_fd = -1};
    {
        int pipe_fds[2];
        if (getenv("YBROWSER_SYNC_NAV") != NULL) {
            /* Debug escape hatch: forces the old synchronous client
			 * navigation (no worker pool, no completion pipe). */
        } else if (pipe(pipe_fds) == 0) {
            pipe_loop.read_fd = pipe_fds[0];
            pipe_loop.write_fd = pipe_fds[1];
            fcntl(pipe_loop.read_fd, F_SETFL, fcntl(pipe_loop.read_fd, F_GETFL, 0) | O_NONBLOCK);
            struct yetty_yplatform_yworkpool_ptr_result pool_res =
                yetty_yplatform_yworkpool_create(&pipe_loop.base, "ybrowser-client", 8);
            if (YETTY_IS_OK(pool_res)) {
                a.img_pool = pool_res.value;
            } else {
                yetty_ycore_error_destroy(pool_res.error);
            }
        }
    }

    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "ybrowser: framework_create: %s\n", fr.error.msg);
        yetty_ycore_error_destroy(fr.error);
        if (raw_ok) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
        return 1;
    }
    a.fw = fr.value;
    /* Initial viewport in FRAMEBUFFER px (probe_terminal_size reads raw
     * ws_xpixel). host_content_scale isn't known until the RESIZE OSC
     * arrives; on HiDPI the very first frame therefore lays chrome out at
     * the framebuffer size and the receiving ygrid scales it x content_scale
     * — visibly wrong for a tick, then RESIZE OSC arrives and everything
     * snaps to logical. Shipping a wrong-scale first frame is still better
     * than the alternative of not shipping widgets at all: without a
     * viewport set the framework layouts at its 800x600 default and every
     * downstream consumer (build_ui-time widget size hints, engine viewport)
     * uses stale numbers. */
    err_ok(yetty_ygui_framework_set_viewport(a.fw, (float)viewport_w, (float)viewport_h));
    yetty_ygui_framework_set_key_cb(a.fw, key_cb, &a);
    struct yetty_yfont_font *measure_font = client_measure_font_create();
    if (measure_font) {
        yetty_ygui_framework_set_font(a.fw, measure_font);
    }

    /* Attach to the host yetty's root figure container over the yclass-RPC
	 * DCS transport (stdin = responses, stdout = requests). The handshake
	 * reads stdin synchronously ONCE — it must run before this loop starts
	 * consuming stdin, and after raw mode is on (a cooked tty would echo the
	 * handshake bytes back through the parser). Without the attach the
	 * framework has no container and every emit fails with "no container" —
	 * the pane stays black while the client happily renders into the void. */
    {
        struct yetty_ycore_void_result attach_res =
            yetty_ygui_framework_attach(a.fw, STDIN_FILENO, STDOUT_FILENO, /*compressed=*/1);
        if (YETTY_IS_ERR(attach_res)) {
            fprintf(stderr, "ybrowser: framework_attach: %s\n", attach_res.error.msg);
            yetty_ycore_error_destroy(attach_res.error);
            err_ok(yetty_ygui_framework_destroy(a.fw));
            if (raw_ok) {
                tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            }
            return 1;
        }
    }

    if (build_ui(&a) < 0) {
        err_ok(yetty_ygui_framework_destroy(a.fw));
        if (raw_ok) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
        return 1;
    }

    open_first_tab(&a, initial_url);

    struct yetty_yface *yf = NULL;
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (YETTY_IS_OK(yr)) {
        yf = yr.value;
        yetty_yface_set_handlers(yf, on_osc, on_raw, &a);
    } else {
        yetty_ycore_error_destroy(yr.error);
    }

    set_mouse_forwarding(1);

    char buf[8192];
    while (a.running) {
        /* Poll pane size from the tty ONLY while we have no RESIZE OSC yet.
		 * Once host_content_scale is real, the RESIZE OSC carries the whole
		 * pane in fb (host publishes applied_w / applied_h) — trust it. The
		 * tty's ws_xpixel is `cols * cell_width`, i.e. only the character
		 * grid area, so it undercounts the pane by up to one cell of chrome
		 * padding. A yetty host sends RESIZE on every pane resize (the
		 * mouse-subscribe path in terminal.c), so we don't lose live resize
		 * either; foreign hosts that never send it fall back to this poll. */
        if (a.host_content_scale <= 0.0f || a.host_content_scale == 1.0f) {
            int pw = 0, ph = 0;
            if (pick_pane_px(&a, &pw, &ph) &&
                ((float)pw != a.viewport_w || (float)ph != a.viewport_h)) {
                a.viewport_w = (float)pw;
                a.viewport_h = (float)ph;
                err_ok(yetty_ygui_framework_set_viewport(a.fw, (float)pw, (float)ph));
                a.tabs[a.active].needs_render = 1;
                a.pending_render = 1;
                yetty_ygui_framework_mark_dirty(a.fw);
            }
        }

        /* Pump the active tab's JS timers; cap the select wait so timers
		 * fire promptly. Background tabs are not pumped (v1). */
        int wait_ms = pump_active(&a);

        /* Render whenever a frame is pending OR the active tab still owes a
		 * render (e.g. the embed had no width on an earlier attempt). */
        if (a.pending_render || a.tabs[a.active].needs_render) {
            render_pass(&a);
        }

        if (yetty_ygui_framework_is_dirty(a.fw)) {
            /* Emit failures matter here: a silently dropped frame leaves the
			 * host showing stale widget state (e.g. a reload/stop icon stuck
			 * on the previous glyph), so surface them to the profiler. */
            struct yetty_ycore_void_result emit_res = yetty_ygui_framework_emit(a.fw);
            if (YETTY_IS_ERR(emit_res)) {
                yetty_ycore_error_print(stderr, "ybrowser: framework_emit", emit_res.error);
                yetty_ycore_error_destroy(emit_res.error);
            }
            fflush(stdout);
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int max_fd = STDIN_FILENO;
        if (pipe_loop.read_fd >= 0) {
            FD_SET(pipe_loop.read_fd, &rfds);
            if (pipe_loop.read_fd > max_fd) {
                max_fd = pipe_loop.read_fd;
            }
        }
        struct timeval tv = {wait_ms / 1000, (wait_ms % 1000) * 1000};
        int rc = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc > 0 && pipe_loop.read_fd >= 0 && FD_ISSET(pipe_loop.read_fd, &rfds)) {
            /* Worker completions: nav results + image arrivals. They set
			 * repaint/apply state consumed at the top of the next tick. */
            (void)client_pipe_drain(&pipe_loop);
        }
        if (rc > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
            if (got > 0) {
                if (yf) {
                    err_ok(yetty_yface_feed_bytes(yf, buf, (size_t)got));
                }
            } else if (got == 0) {
                a.running = 0; /* host closed the pty */
            } else if (errno != EAGAIN && errno != EINTR) {
                a.running = 0;
            }
        }
    }

    /* Teardown — drop our figures on the host, then free everything. */
    set_mouse_forwarding(0);
    err_ok(yetty_ygui_framework_clear(a.fw));
    fflush(stdout);
    /* Stop the workers BEFORE the tabs/engines they reference die; then
	 * run any completions the shutdown flushed through the pipe. */
    if (a.img_pool) {
        yetty_yplatform_yworkpool_destroy(a.img_pool);
        a.img_pool = NULL;
    }
    if (pipe_loop.read_fd >= 0) {
        (void)client_pipe_drain(&pipe_loop);
        close(pipe_loop.read_fd);
        close(pipe_loop.write_fd);
    }
    for (int i = 0; i < a.n_tabs; i++) {
        tab_free(&a.tabs[i]);
    }
    cache_free(&a.cache);
    (void)yetty_ybrowser_loader_destroy(a.loader);
    a.loader = NULL;
    err_ok(yetty_ygui_framework_destroy(a.fw));
    if (measure_font) {
        measure_font->ops->destroy(measure_font);
    }
    if (yf) {
        yetty_yface_destroy(yf);
    }
    if (raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return 0;
}

/* Encode a Unicode codepoint to UTF-8. Returns the byte count (1..4). */
static size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* GLFW navigation/editing keycode → terminal byte sequence. Printable text
 * is NOT handled here — it arrives layout-translated via YETTY_YCORE_CHAR. */
static const char *encode_special_key(uint32_t key, int glfw_mods, char *scratch, size_t scratch_n,
                                      size_t *out_len)
{
    /* xterm modifier parameter for CSI sequences: 1 + bitset(shift=1, alt=2,
     * ctrl=4). mod_param == 0 means "no modifier" → emit the bare sequence so
     * unmodified keys look exactly as before. The ygui input decoder reads this
     * back out (see csi_decode_mods) and hands it to the widget as mods, which
     * is what makes Shift+Arrow extend the selection. */
    int mod_bits = 0;
    if (glfw_mods & 0x0001) { /* GLFW_MOD_SHIFT */
        mod_bits |= 1;
    }
    if (glfw_mods & 0x0004) { /* GLFW_MOD_ALT */
        mod_bits |= 2;
    }
    if (glfw_mods & 0x0002) { /* GLFW_MOD_CONTROL */
        mod_bits |= 4;
    }
    int mod_param = mod_bits ? mod_bits + 1 : 0;
    switch (key) {
    case 256:
        scratch[0] = 0x1B;
        *out_len = 1;
        return scratch; /* ESC */
    case 257:           /* Enter */
    case 335:
        scratch[0] = '\r';
        *out_len = 1;
        return scratch; /* KP Enter */
    case 258:
        scratch[0] = '\t';
        *out_len = 1;
        return scratch; /* Tab */
    case 259:
        scratch[0] = 0x7F;
        *out_len = 1;
        return scratch; /* Backspace */
    case 261:           /* Del */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[3;%d~", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[3~");
        return scratch;
    case 263: /* ← */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dD", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[D");
        return scratch;
    case 262: /* → */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dC", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[C");
        return scratch;
    case 265: /* ↑ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dA", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[A");
        return scratch;
    case 264: /* ↓ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dB", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[B");
        return scratch;
    case 268: /* Home */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dH", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[H");
        return scratch;
    case 269: /* End */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dF", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[F");
        return scratch;
    case 301: /* F12 (GLFW_KEY_F12) → DevTools toggle. xterm CSI 24~. */
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[24~");
        return scratch;
    default:
        *out_len = 0;
        return NULL;
    }
}

/* ===========================================================================
 * UTF-8 + special-key encoders. Small helpers used by BOTH modes: the client
 * on_osc path decodes forwarded key envelopes with them (any build), and the
 * standalone GLFW key handler further below reuses the same. Kept out of the
 * standalone ifdef so the macOS / Windows client-only builds link.
 * ===========================================================================*/

/* Encode a Unicode codepoint to UTF-8. Returns the byte count (1..4). */
static size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* GLFW navigation/editing keycode → terminal byte sequence. Printable text
 * is NOT handled here — it arrives layout-translated via YETTY_YCORE_CHAR. */
static const char *encode_special_key(uint32_t key, int glfw_mods, char *scratch, size_t scratch_n,
                                      size_t *out_len)
{
    /* xterm modifier parameter for CSI sequences: 1 + bitset(shift=1, alt=2,
     * ctrl=4). mod_param == 0 means "no modifier" → emit the bare sequence so
     * unmodified keys look exactly as before. The ygui input decoder reads this
     * back out (see csi_decode_mods) and hands it to the widget as mods, which
     * is what makes Shift+Arrow extend the selection. */
    int mod_bits = 0;
    if (glfw_mods & 0x0001) { /* GLFW_MOD_SHIFT */
        mod_bits |= 1;
    }
    if (glfw_mods & 0x0004) { /* GLFW_MOD_ALT */
        mod_bits |= 2;
    }
    if (glfw_mods & 0x0002) { /* GLFW_MOD_CONTROL */
        mod_bits |= 4;
    }
    int mod_param = mod_bits ? mod_bits + 1 : 0;
    switch (key) {
    case 256:
        scratch[0] = 0x1B;
        *out_len = 1;
        return scratch; /* ESC */
    case 257:           /* Enter */
    case 335:
        scratch[0] = '\r';
        *out_len = 1;
        return scratch; /* KP Enter */
    case 258:
        scratch[0] = '\t';
        *out_len = 1;
        return scratch; /* Tab */
    case 259:
        scratch[0] = 0x7F;
        *out_len = 1;
        return scratch; /* Backspace */
    case 261:           /* Del */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[3;%d~", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[3~");
        return scratch;
    case 263: /* ← */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dD", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[D");
        return scratch;
    case 262: /* → */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dC", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[C");
        return scratch;
    case 265: /* ↑ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dA", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[A");
        return scratch;
    case 264: /* ↓ */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dB", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[B");
        return scratch;
    case 268: /* Home */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dH", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[H");
        return scratch;
    case 269: /* End */
        *out_len = mod_param ? (size_t)snprintf(scratch, scratch_n, "\x1b[1;%dF", mod_param)
                             : (size_t)snprintf(scratch, scratch_n, "\x1b[F");
        return scratch;
    case 301: /* F12 (GLFW_KEY_F12) → DevTools toggle. xterm CSI 24~. */
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[24~");
        return scratch;
    default:
        *out_len = 0;
        return NULL;
    }
}

/* ===========================================================================
 * Standalone mode — own GPU window (no host yetty). Mirrors demo/ygui's
 * runner: the yplatform bootstrap brings up the window + WebGPU; we build a local
 * yfigure container, drive the ygui framework into it over an in-process
 * memory-pty + wire SM, and render that tree ourselves. Local GLFW input
 * (mouse button / move / WHEEL / keys) feeds straight into the framework —
 * which is why scrolling and clicking work here but not in the in-yetty
 * client (where the host eats the wheel for its own scrollback).
 * ===========================================================================*/
#ifdef YETTY_YBROWSER_HAS_STANDALONE

#include <yetty/yconfig/config.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/ychrome/chrome.h> /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yapp/app.h>
#include <yetty/yclass/class.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>

#include <pthread.h>

/*
 * Standalone browser UI as a yclass class `ybrowser:app` (subclass of yapp:app).
 * The whole block is gated by YETTY_YBROWSER_HAS_STANDALONE (only the
 * standalone, GPU-windowed build): in client / one-shot modes ybrowser has no
 * window. codegen sees this class because the Makefile passes the guard macro
 * via YCLASS_DEFINES; the generated browser-ui.gen.c is #included at the foot,
 * inside the same guard, so reduced builds never compile it.
 */
struct YETTY_ANNOTATE("class@ybrowser:app") YETTY_ANNOTATE("parent@yapp:app") yetty_ybrowser_app {
    struct app app;
    struct yetty_yframework *yframework;
    struct yetty_yclass_object *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_yfont_font *font;
    struct yetty_ychrome_host *chrome; /* draggable/resizable titlebar + min/max/close */
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_yevent_event_listener listener;
    struct yetty_ydraw_target *render_target;
    const char *initial_url;

    /* Initial-page prefetch. Started on a background thread at sa_worker
     * entry so the HTML download overlaps the GPU/font/UI setup that follows,
     * then joined and folded into the page cache just before the first tab
     * opens — turning open_first_tab's fetch into a cache hit. */
    pthread_t prefetch_thread;
    int prefetch_started;
    char *prefetch_url;  /* normalized URL the thread fetched (owned) */
    char *prefetch_data; /* fetched bytes (owned); NULL on failure */
    size_t prefetch_len;
    char *prefetch_eff;   /* effective URL after redirects (owned) */
    char *prefetch_ctype; /* response Content-Type (owned), or NULL */
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_ybrowser_app_ptr, struct yetty_ybrowser_app *);
struct yetty_yclass_ptr_result yetty_ybrowser_app_class_get(void);
struct yetty_ybrowser_app_ptr_result yetty_ybrowser_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ybrowser_app_create(struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols. ybrowser builds a clean argv in its own
 * main() (URL/flags would otherwise trip yconfig), so it drives this sequence
 * directly rather than via the shared ymain/glfw.c. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

/* Background thread body: download the initial page while the main thread
 * sets up the GPU device, fonts, and UI. Touches only this standalone's own
 * prefetch_* fields; the main thread reads them only after pthread_join. */
static void *sa_prefetch_main(void *arg)
{
    struct yetty_ybrowser_app *s = arg;
    s->prefetch_data = ybrowser_slurp_file(s->app.loader, s->prefetch_url, &s->prefetch_len,
                                           &s->prefetch_eff, &s->prefetch_ctype);
    return NULL;
}

static void sa_request_render(struct yetty_ybrowser_app *s)
{
    if (s->yframework && s->yframework->event_loop &&
        s->yframework->event_loop->ops->request_render) {
        s->yframework->event_loop->ops->request_render(s->yframework->event_loop);
    }
}

/* HiDPI factor (framebuffer_px / logical_px). ygui + the ychrome engine both
 * author in logical px; the receiving ygrid multiplies by this at add-record
 * time to hit framebuffer resolution. Every ygui feed_mouse_* + set_viewport
 * call and the chrome-host create take LOGICAL inputs, so this is the divisor
 * used at each of those boundaries. Falls back to 1.0f before the yframework
 * is up so the pre-init path stays valid. */
static float sa_content_scale(const struct yetty_ybrowser_app *s)
{
    if (!s->yframework) {
        return 1.0f;
    }
    float scale = s->yframework->gpu.app_gpu_context.content_scale;
    return scale > 0.0f ? scale : 1.0f;
}

static struct yetty_ycore_int_result sa_event_handler(struct yetty_yevent_event_listener *listener,
                                                      const struct yetty_yui_event *ev)
{
    struct yetty_ybrowser_app *s = container_of(listener, struct yetty_ybrowser_app, listener);

    if (ev->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (s->render_target && s->render_target->ops->refresh_full) {
            s->render_target->ops->refresh_full(s->render_target);
        }
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return sa_event_handler(listener, &re);
    }

    if (ev->type == YETTY_YCORE_RENDER) {
        if (!s->render_target) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        if (s->render_target->ops->is_busy && s->render_target->ops->is_busy(s->render_target)) {
            /* GPU still presenting the previous frame. on_render_async already
             * cleared render_pending, so returning here drops this render. That
             * is fine for input/scroll-driven renders — the next event (or the
             * framework-dirty re-arm below) retries them — and re-arming on
             * EVERY busy frame storms render requests during heavy scrolling,
             * starving input handling. Only re-arm for an async IMAGE repaint,
             * which has no follow-up event and would otherwise freeze with
             * stale pixels until an unrelated wake. */
            struct tab *active_tab = &s->app.tabs[s->app.active];
            if (s->app.img_dirty ||
                (active_tab->engine && yetty_ylexbor_images_in_flight(active_tab->engine) > 0)) {
                sa_request_render(s);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Per-frame browser work: pump JS timers, re-render the active
		 * tab into the embed when needed. */
        int pump_wait = pump_active(&s->app);
        if (s->app.pending_render || s->app.tabs[s->app.active].needs_render) {
            render_pass(&s->app);
        }
        if (yetty_ygui_framework_is_dirty(s->app.fw)) {
            /* Direct dispatch: emit applies the figure tree straight into the
             * local root container (no wire statemachine to pump afterward). */
            err_ok(yetty_ygui_framework_emit(s->app.fw));
        }
        struct yetty_ycore_void_result cl = s->render_target->ops->clear(s->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (s->root_container) {
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(s->root_container, s->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set(s->root_container, 0);
        }
        struct yetty_ycore_void_result pp = s->render_target->ops->present(s->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        /* Keep ticking while the framework is dirty (JS timers/animation),
			 * or while there are still deferred images to stream in
			 * (pump_active returns 0 when it just fetched one). */
        if (yetty_ygui_framework_is_dirty(s->app.fw) || pump_wait == 0) {
            sa_request_render(s);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (s->yframework->event_loop->ops->stop) {
            err_ok(s->yframework->event_loop->ops->stop(s->yframework->event_loop));
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE: {
        yetty_yframework_reconfigure_surface(s->yframework, (uint32_t)ev->resize.width,
                                             (uint32_t)ev->resize.height);
        if (s->render_target && s->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            s->render_target->ops->resize(s->render_target, vp);
        }
        /* ygui viewport authors in LOGICAL px — divide the framebuffer resize
         * once here (previously the raw framebuffer size leaked through and the
         * whole layout ended up laid out for a 2× canvas on HiDPI). */
        float cs = sa_content_scale(s);
        s->app.viewport_w = (float)ev->resize.width / cs;
        s->app.viewport_h = (float)ev->resize.height / cs;
        err_ok(yetty_ygui_framework_set_viewport(s->app.fw, s->app.viewport_w, s->app.viewport_h));
        if (s->chrome) {
            /* Chrome host takes framebuffer inputs and converts internally. */
            struct yetty_ycore_void_result cr = yetty_ychrome_host_resized(
                s->chrome, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
        /* Size the compositor's root container directly. (The legacy route
		 * travelled this through the memory-pty pair so it reached the receiver
		 * like a real PTY's TIOCSWINSZ; with direct in-process dispatch the
		 * container is local, so we set its rect here.) */
        if (s->root_container) {
            struct yetty_ycore_rectangle rr = {
                .min = {0, 0}, .max = {(float)ev->resize.width, (float)ev->resize.height}};
            yetty_yfigure_figure_rect_set(s->root_container, rr);
            yetty_yfigure_figure_dirty_set(s->root_container, 1);
        }
        s->app.tabs[s->app.active].needs_render = 1;
        s->app.pending_render = 1;
        yetty_ygui_framework_mark_dirty(s->app.fw);
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_PASTE: {
        /* Async clipboard paste response (from a Ctrl-V request) — payload is a
		 * malloc'd string we own. Drop it into the address bar when focused. */
        char *paste_text = ev->payload;
        if (paste_text) {
            if (s->app.address_focused) {
                err_ok(yetty_ygui_textinput_insert_text(s->app.address, paste_text));
                yetty_ygui_framework_mark_dirty(s->app.fw);
            }
            free(paste_text);
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_CHAR: {
        /* Layout-correct text input: the platform already mapped the
		 * physical key through the OS keyboard layout. Ctrl+<letter>
		 * arrives here too (mods set) — fold it to its control byte. */
        char buf[8];
        size_t n = 0;
        uint32_t cp = ev->chr.codepoint;
        if ((ev->chr.mods & 0x0002 /* GLFW_MOD_CONTROL */) &&
            ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))) {
            buf[0] = (char)(cp & 0x1F);
            n = 1;
        } else if (cp >= 32 || cp == '\t' || cp == '\n' || cp == '\r') {
            n = utf8_encode(cp, buf);
        }
        if (n > 0) {
            err_ok(yetty_ygui_framework_feed_input(s->app.fw, buf, n));
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_KEY_DOWN: {
        /* Navigation / editing keys only — printable text comes via CHAR. */
        char scratch[8];
        size_t n = 0;
        const char *bytes =
            encode_special_key(ev->key.key, ev->key.mods, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            err_ok(yetty_ygui_framework_feed_input(s->app.fw, bytes, n));
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        int press = ev->type == YETTY_YCORE_MOUSE_DOWN ? 1 : 0;
        /* ygui hit-tests in LOGICAL px — scale the framebuffer event once. The
         * chrome-host feed below stays raw: it does its own fb→logical divide. */
        float cs = sa_content_scale(s);
        float logical_x = ev->mouse.x / cs;
        float logical_y = ev->mouse.y / cs;
        struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
            s->app.fw, logical_x, logical_y, ev->mouse.button, press, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, fr, "ybrowser: feed button");
        /* Client-first: only if no browser widget (tab / toolbar) took it does
         * the window chrome get the event (empty title-bar drag, edges,
         * min/max/close). */
        if (!fr.value && s->chrome) {
            struct yetty_ycore_int_result cr = yetty_ychrome_host_handle_event(s->chrome, ev);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
        if (press) {
            page_click(&s->app, logical_x, logical_y);
        }
        yetty_ygui_framework_mark_dirty(s->app.fw);
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        /* Title-bar maximize gesture — the browser UI has no double-click. */
        if (s->chrome) {
            struct yetty_ycore_int_result cr = yetty_ychrome_host_handle_event(s->chrome, ev);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_SCROLL:
        err_ok_int(yetty_ygui_framework_feed_mouse_scroll(s->app.fw, ev->mouse_scroll.x,
                                                          ev->mouse_scroll.y, ev->mouse_scroll.dx,
                                                          ev->mouse_scroll.dy));
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        float cs = sa_content_scale(s);
        struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_motion(
            s->app.fw, ev->mouse.x / cs, ev->mouse.y / cs);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, fr, "ybrowser: feed motion");
        if (!fr.value && s->chrome) {
            struct yetty_ycore_int_result cr = yetty_ychrome_host_handle_event(s->chrome, ev);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    default:
        break;
    }
    sa_request_render(s);
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result ybrowser_app_init(struct yetty_yclass_object *obj,
                                                        struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result sa_worker(struct yetty_yclass_object *obj,
                                                struct yetty_yclass_object *platform)
{
    struct yetty_ybrowser_app_ptr_result app_res = yetty_ybrowser_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "ybrowser:app:run: app_from");
    struct yetty_ybrowser_app *s = app_res.value;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "ybrowser:app:run: gpu_context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "ybrowser:app:run: input_pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser:app:run: platform state not populated");
    }

    yetty_ylexbor_prof("sa_worker START (GPU/window already up via platform)");

    /* Start the initial-page download now, on a background thread, so its
     * network latency overlaps the GPU/font/UI setup below instead of
     * stalling first paint after setup finishes. Joined before open_first_tab. */
    s->prefetch_started = 0;
    if (s->initial_url && s->initial_url[0]) {
        s->prefetch_url = normalize_url(s->initial_url);
        if (s->prefetch_url &&
            pthread_create(&s->prefetch_thread, NULL, sa_prefetch_main, s) == 0) {
            s->prefetch_started = 1;
            yetty_ylexbor_prof("prefetch START (bg thread) %.80s", s->prefetch_url);
        } else {
            free(s->prefetch_url);
            s->prefetch_url = NULL;
        }
    }

    double t_fw = yetty_ylexbor_prof_now_ms();
    struct yetty_yframework_ptr_result frr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "ybrowser standalone: yframework_create");
    s->yframework = frr.value;
    s->render_target = s->yframework->render_target;
    yetty_ylexbor_prof("yframework_create %.0f ms (GPU device/queue/allocator/targets)",
                       yetty_ylexbor_prof_now_ms() - t_fw);

    /* MSDF font for the receiver-side ygrid. */
    {
        const char *fonts_dir =
            s->yframework->config->ops->get_string(s->yframework->config, "paths/fonts", "");
        const char *shaders_dir =
            s->yframework->config->ops->get_string(s->yframework->config, "paths/shaders", "");
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 "DejaVuSansMNerdFontMono");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "ybrowser_default");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ybrowser standalone: msdf_font_create");
        s->font = fr.value;
        struct yetty_ycore_void_result load = s->font->ops->load_basic_latin(s->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load, "ybrowser standalone: load_basic_latin");
    }

    /* Raw figure factory + producer kinds. */
    {
        struct yetty_ydraw_composite_factory_ptr_result ffr = yetty_ydraw_composite_factory_create(
            s->yframework->gpu.device, s->yframework->gpu.queue, s->yframework->gpu.surface_format,
            s->yframework->gpu.allocator, s->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "ybrowser standalone: raw_composite_factory");
        s->composite_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            yplot_f->destroy = yetty_yplot_factory_destroy;
            err_ok(yetty_ydraw_composite_factory_register(s->composite_factory, yplot_f));
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            yimage_f->destroy = yetty_yimage_factory_destroy;
            err_ok(yetty_ydraw_composite_factory_register(s->composite_factory, yimage_f));
        }
    }

    /* Figure registry (ygrid + producer kinds). */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "ybrowser standalone: registry_create");
        s->figure_registry = reg.value;
        s->figure_args.default_font = s->font;
        s->figure_args.composite_factory = s->composite_factory;
        /* ygui chrome: producer figures laid out in logical px, scaled to
         * framebuffer by content_scale; widgets emit at absolute widget rect. */
        s->figure_args.absolute_coords = 1;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(s->figure_registry, &s->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "ybrowser standalone: ygrid_register_factory");
        const char *const producer_kind_names[] = {"yplot", "yimage"};
        for (size_t i = 0; i < sizeof(producer_kind_names) / sizeof(producer_kind_names[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                s->figure_registry, yetty_yfigure_kind_token(producer_kind_names[i]),
                &s->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr,
                                "ybrowser standalone: register_factory_for_kind");
        }
    }

    /* Local container. */
    struct yetty_context ctx = {.runtime = s->yframework, .event_loop = s->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)gpu->surface_width, (float)gpu->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "ybrowser standalone: container_create");
        s->root_container = obj_res.value;
        yetty_yfigure_container_set_context(s->root_container, &ctx);
        yetty_yfigure_container_set_registry(s->root_container, s->figure_registry);
        yetty_yfigure_container_set_rect(s->root_container, root_rect);
    }

    /* Window chrome: draggable/resizable titlebar + min/max/close. */
    {
        struct yetty_ychrome_host_ptr_result chrome_r = yetty_ychrome_host_create(
            s->root_container, s->font, &ctx, s->yframework->window_chrome,
            (float)gpu->surface_width, (float)gpu->surface_height, sa_content_scale(s), 36.0f, 8.0f,
            YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_r)) {
            s->chrome = chrome_r.value;
        } else {
            ywarn("ybrowser standalone: chrome host create failed: %s", chrome_r.error.msg);
            yetty_ycore_error_destroy(chrome_r.error);
        }
    }

    /* ygui framework, driven into the local root container by DIRECT
     * in-process yclass dispatch.
     *
     * Producer (the ygui framework) and receiver (the root figure container)
     * live in this one process on a single thread, so there is no out-of-process
     * transport: framework_create(NULL) leaves the output pty unset, and
     * set_container_obj wires the framework's typed yfigure_* stubs straight at
     * the local container object. framework_flush then applies every figure-tree
     * mutation inline via those stubs — the in-process equivalent of the
     * RPC-server path a terminal runs for an out-of-process subprocess producer.
     *
     * The deleted legacy route shipped a one-way figure-tree record stream over
     * an in-process memory-pty into a wire statemachine that decoded it with
     * yetty_yfigure_container_process_input. Direct dispatch removes the
     * serialize/parse round and the memory-pty + wire-statemachine entirely. A
     * blocking yclass-RPC DCS session is not usable here: the DCS transport needs
     * real fds (the memory-pty exposes none) and would block this single thread
     * on the get_root/RESOLVE_SLOT handshake waiting on a server that cannot run
     * until the producer yields. */
    {
        struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ybrowser standalone: framework_create");
        s->app.fw = fr.value;
        /* Hand the framework the same font the ygrid renders text with (font_id
         * 0) so widgets — the address bar in particular — place carets and map
         * clicks against real glyph advances instead of a fixed approximation. */
        yetty_ygui_framework_set_font(s->app.fw, s->font);
        struct yetty_ycore_void_result scr =
            yetty_ygui_framework_set_container_obj(s->app.fw, s->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "ybrowser standalone: set_container_obj");
        s->app.event_loop = s->yframework->event_loop;
        s->app.clipboard = s->yframework->clipboard; /* borrowed; may be NULL */
        /* Worker pool for parallel image fetch+decode (8 concurrent). Created
         * on the loop thread; destroyed before the tabs/engines at teardown. */
        {
            struct yetty_yplatform_yworkpool_ptr_result pr =
                yetty_yplatform_yworkpool_create(s->app.event_loop, "ybrowser-img", 8);
            if (YETTY_IS_OK(pr)) {
                s->app.img_pool = pr.value;
            } else {
                yetty_ycore_error_destroy(pr.error);
            }
        }
        /* ygui viewport in LOGICAL px so the browser layout maps 1:1 to the
         * physical window on HiDPI. The compositor container (set above) stays
         * in framebuffer px — ygrid absolute-coord children (widgets, chrome
         * caption) scissor against their own logical rects scaled by
         * content_scale, so the container rect is not the limiter here. */
        float cs = sa_content_scale(s);
        s->app.viewport_w = (float)gpu->surface_width / cs;
        s->app.viewport_h = (float)gpu->surface_height / cs;
        err_ok(yetty_ygui_framework_set_viewport(s->app.fw, s->app.viewport_w, s->app.viewport_h));
    }

    yetty_ygui_framework_set_key_cb(s->app.fw, key_cb, &s->app);
    if (build_ui(&s->app) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser standalone: build_ui");
    }
    /* Fold the prefetched page into the cache so open_first_tab's fetch is a
     * cache hit — the download already happened, in parallel with setup. */
    if (s->prefetch_started) {
        pthread_join(s->prefetch_thread, NULL);
        s->prefetch_started = 0;
        if (s->prefetch_data) {
            cache_store(&s->app.cache, s->prefetch_url, (const uint8_t *)s->prefetch_data,
                        s->prefetch_len, s->prefetch_eff, s->prefetch_ctype);
            yetty_ylexbor_prof("prefetch DONE  bytes=%zu (folded into cache)", s->prefetch_len);
        }
        free(s->prefetch_data);
        s->prefetch_data = NULL;
        free(s->prefetch_eff);
        s->prefetch_eff = NULL;
        free(s->prefetch_ctype);
        s->prefetch_ctype = NULL;
        free(s->prefetch_url);
        s->prefetch_url = NULL;
    }

    yetty_ylexbor_prof("UI ready — open first tab (triggers fetch + load_html)");
    open_first_tab(&s->app, s->initial_url);

    /* No memory-pty wake/resize wiring: with direct in-process dispatch the
     * framework applies into the root container synchronously inside the render
     * loop's framework_emit call, and the resize event handler sizes the root
     * container directly (see YETTY_YCORE_RESIZE below). */

    s->listener.handler = sa_event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(s->yframework->event_loop, &s->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "ybrowser standalone: register_listeners");

    yetty_yevent_post_async(input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        s->yframework->event_loop->ops->start(s->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown. Destroy the image pool FIRST: it joins the worker threads
     * (no fetch is mid-flight afterward), and any completed-job done()
     * callbacks already queued on the loop run during loop teardown against
     * still-live engines (which defer their own free until in-flight hits 0),
     * so there's no use-after-free. */
    if (s->app.img_pool) {
        yetty_yplatform_yworkpool_destroy(s->app.img_pool);
        s->app.img_pool = NULL;
    }
    for (int i = 0; i < s->app.n_tabs; i++) {
        tab_free(&s->app.tabs[i]);
    }
    cache_free(&s->app.cache);
    (void)yetty_ybrowser_loader_destroy(s->app.loader);
    s->app.loader = NULL;
    if (s->app.fw) {
        err_ok(yetty_ygui_framework_destroy(s->app.fw));
        s->app.fw = NULL;
    }
    if (s->chrome) {
        err_ok(yetty_ychrome_host_destroy(s->chrome));
        s->chrome = NULL;
    }
    if (s->root_container) {
        err_ok(yetty_yfigure_destroy(s->root_container));
        s->root_container = NULL;
    }
    if (s->figure_registry) {
        yetty_yfigure_registry_destroy(s->figure_registry);
        s->figure_registry = NULL;
    }
    if (s->composite_factory) {
        yetty_ydraw_composite_factory_destroy(s->composite_factory);
        s->composite_factory = NULL;
    }
    if (s->font) {
        s->font->ops->destroy(s->font);
        s->font = NULL;
    }
    if (s->yframework) {
        yetty_yframework_destroy(s->yframework);
        s->yframework = NULL;
    }
    return YETTY_OK_VOID();
}

int ybrowser_ui_run_standalone(const char *initial_url, float font_size, int no_ui, int argc,
                               char **argv)
{
    ytrace_init();
    /* Standalone runs in its own terminal, so surface page JS console
	 * output there (useful for debugging why a page renders oddly). */
    setenv("YBROWSER_JS_CONSOLE", "1", 0);

    /* Drive the platform bring-up directly. The caller already trimmed argv to a
     * yconfig-safe slice; the URL / font-size / no-ui travel on the app object. */
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "ybrowser: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "ybrowser: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_ybrowser_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "ybrowser: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }
    struct yetty_ybrowser_app_ptr_result app_data = yetty_ybrowser_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        yetty_ycore_error_print(stderr, "ybrowser: app data", app_data.error);
        yetty_ycore_error_destroy(app_data.error);
        return 1;
    }
    struct yetty_ybrowser_app *s = app_data.value;
    s->app.running = 1;
    s->app.font_size = font_size > 0.0f ? font_size : 16.0f;
    {
        struct yetty_ybrowser_loader_ptr_result loader_res = yetty_ybrowser_loader_create();
        if (YETTY_IS_OK(loader_res)) {
            s->app.loader = loader_res.value;
        } else {
            yetty_ycore_error_destroy(loader_res.error);
        }
    }
    s->app.no_ui = no_ui;
    s->initial_url = initial_url;

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "ybrowser: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    yetty_ylexbor_prof("=== standalone entry (platform: window+surface+GPU adapter next) ===");
    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ybrowser: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}

#include "browser-ui.gen.c"

#endif /* YETTY_YBROWSER_HAS_STANDALONE */
