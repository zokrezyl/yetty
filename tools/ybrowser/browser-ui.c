/*
 * browser-ui.c — ybrowser's interactive Chrome-like mode, built on the
 * ygui widget framework.
 *
 * Layout (root vbox):
 *   ┌ tabbar ─────────────────────────────────────────────── + ┐
 *   │ [<] [>] [Reload] [ address bar ............................ ] │  toolbar
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

#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yface/yface.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yimage/yimage.h>
#include <yetty/ysvg/ysvg.h>
#include <yetty/yplatform/pty.h>
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

struct cache_entry {
    char *url;
    uint8_t *data;
    size_t len;
    char *eff; /* effective (post-redirect) URL, or NULL */
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
    char *title;      /* tab label (owned) */
    char **back;      /* history stacks (owned strings) */
    size_t n_back, cap_back;
    char **fwd;
    size_t n_fwd, cap_fwd;
    float rendered_w; /* embed/image width last laid out at */
    int needs_render; /* re-render the active content next tick */
    uint64_t dl_hash; /* hash of the last draw list we shipped */
    size_t dl_size;   /* size of the last draw list we shipped */
};

struct app {
    struct yetty_ygui_framework *fw;
    struct yetty_ygui_object *root;
    struct yetty_ygui_object *tabbar;
    struct yetty_ygui_object *address; /* textinput */
    struct yetty_ygui_object *scroll;  /* scrollarea */
    struct yetty_ygui_object *page;    /* ydraw_embed (HTML/SVG) — shared */
    struct yetty_ygui_object *image;   /* yimage widget (raster) — shared */
    int showing_image;                 /* which content widget is visible */

    struct tab tabs[MAX_TABS];
    int n_tabs;
    int active;

    struct url_cache cache;

    int address_focused;
    int pending_render;
    int running;

    float viewport_w, viewport_h;
    float font_size;

    /* Standalone (own-window) mode only — set so the quit shortcut can stop
	 * the GPU event loop. NULL in the in-yetty client loop. */
    struct yetty_yevent_event_loop *event_loop;
};

static inline void err_ok(struct yetty_ycore_void_result r)
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

/* Current pane pixel size from the controlling tty. The host updates the
 * pty winsize as the pane resizes, so polling this each tick tracks the
 * real size even when no pane-wide resize OSC is delivered. Prefers the
 * pixel fields; falls back to a cell-grid approximation. */
static int pick_pane_px(int *w, int *h)
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
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            *w = ws.ws_xpixel;
            *h = ws.ws_ypixel;
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
static void go_back(struct app *a);
static void go_forward(struct app *a);
static void reload(struct app *a);
static void sync_active_ui(struct app *a);
static void render_active(struct app *a);
static int build_ui(struct app *a);

/* ===========================================================================
 * Output bridge — the framework writes OSC envelopes here; we blocking-
 * write them to stdout and flush after each emit.
 * ===========================================================================*/
struct stdout_pty {
    struct yetty_platform_pty base;
};

static struct yetty_ycore_size_result so_write(struct yetty_platform_pty *self, const char *data,
                                               size_t len)
{
    (void)self;
    if (len) {
        fwrite(data, 1, len, stdout);
    }
    return YETTY_OK(yetty_ycore_size, len);
}
static struct yetty_ycore_size_result so_read(struct yetty_platform_pty *self, char *buf, size_t n)
{
    (void)self;
    (void)buf;
    (void)n;
    return YETTY_OK(yetty_ycore_size, 0);
}
static struct yetty_ycore_void_result so_noop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result so_resize(struct yetty_platform_pty *self, uint32_t c,
                                                uint32_t r, uint32_t pw, uint32_t ph)
{
    (void)self;
    (void)c;
    (void)r;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}
static struct yetty_platform_pty_pipe_source *so_pipe(struct yetty_platform_pty *self)
{
    (void)self;
    return NULL;
}
static const struct yetty_platform_pty_ops *stdout_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = so_noop,
        .read = so_read,
        .write = so_write,
        .resize = so_resize,
        .stop = so_noop,
        .pipe_source = so_pipe,
    };
    return &ops;
}

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
                        const char *eff)
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
        c->e[i].data = nd;
        c->e[i].len = len;
        c->e[i].eff = eff ? str_dup(eff) : NULL;
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
    }
    memset(c, 0, sizeof(*c));
}

/* Fetch `url`, preferring the cache. Always returns owned bytes (caller
 * frees *out and *out_eff); NULL on failure. Misses are fetched and cached. */
static uint8_t *cache_fetch(struct url_cache *c, const char *url, size_t *out_len, char **out_eff)
{
    *out_len = 0;
    *out_eff = NULL;
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
        return copy;
    }
    size_t len = 0;
    char *eff = NULL;
    char *raw = ybrowser_slurp_file(url, &len, &eff);
    if (!raw) {
        free(eff);
        return NULL;
    }
    cache_store(c, url, (const uint8_t *)raw, len, eff); /* keeps its own copy */
    *out_len = len;
    *out_eff = eff; /* transfer to caller */
    return (uint8_t *)raw;
}

/* Sniff the content kind from the leading bytes. */
static enum content_kind detect_kind(const uint8_t *d, size_t n)
{
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
    /* SVG: an "<svg" tag near the top (after an optional xml decl/doctype). */
    size_t scan = n < 512 ? n : 512;
    for (size_t i = 0; i + 4 <= scan; i++) {
        if (d[i] == '<' && (d[i + 1] | 0x20) == 's' && (d[i + 2] | 0x20) == 'v' &&
            (d[i + 3] | 0x20) == 'g') {
            return CK_SVG;
        }
    }
    return CK_HTML;
}

/* ===========================================================================
 * Engine + document.
 * ===========================================================================*/
static int tab_ensure_engine(struct app *a, struct tab *t)
{
    if (t->engine) {
        return 0;
    }
    struct yetty_ylexbor_config cfg = {
        .viewport_width = a->viewport_w > 0 ? (int)a->viewport_w : 1024,
        .viewport_height = a->viewport_h > 0 ? (int)a->viewport_h : 768,
        .default_font_size = a->font_size,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return -1;
    }
    t->engine = r.value;
    t->rendered_w = 0.0f;
    return 0;
}

/* Install fetched content into the tab, detecting whether it's HTML (→
 * ylexbor), an SVG (→ ysvg) or a raster image (→ yimage). */
static void tab_set_document(struct app *a, struct tab *t, const char *data, size_t len,
                             const char *base_url)
{
    t->kind = detect_kind((const uint8_t *)data, len);
    free(t->raw);
    t->raw = NULL;
    t->raw_len = 0;
    t->img_w = t->img_h = 0;

    if (t->kind == CK_HTML) {
        if (tab_ensure_engine(a, t) < 0) {
            return;
        }
        err_ok(yetty_ylexbor_load_html(t->engine, data, len));
        if (base_url) {
            err_ok(yetty_ylexbor_set_base_url(t->engine, base_url));
        }
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
    tab_set_document(a, t, START_HTML, strlen(START_HTML), NULL);
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

/* Load `url` (owned — navigate takes ownership) into tab `t`. With
 * push_to_back, the previous location is pushed onto the back stack and the
 * forward stack cleared (a fresh navigation, not a history move). */
static void navigate(struct app *a, struct tab *t, char *url, int push_to_back)
{
    if (push_to_back && t->url && strcmp(t->url, START_URL) != 0) {
        hist_push(&t->back, &t->n_back, &t->cap_back, str_dup(t->url));
        hist_clear(t->fwd, &t->n_fwd);
    }

    size_t len = 0;
    char *eff = NULL;
    uint8_t *data = cache_fetch(&a->cache, url, &len, &eff);
    if (data) {
        const char *base = eff ? eff : (ybrowser_looks_like_url(url) ? url : NULL);
        tab_set_document(a, t, (const char *)data, len, base);
        free(data);
    } else {
        char err[2048];
        int n = snprintf(err, sizeof(err), ERROR_HTML_FMT, url);
        if (n < 0) {
            n = 0;
        }
        tab_set_document(a, t, err, (size_t)n, NULL);
    }
    free(eff);

    free(t->url);
    t->url = url; /* take ownership */
    free(t->title);
    t->title = derive_title(url);

    if (t == &a->tabs[a->active]) {
        sync_active_ui(a);
    }
    a->pending_render = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
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
        navigate(a, t, str_dup(t->url), 0);
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
    struct yetty_ygui_object_ptr_result hr = yetty_ygui_tabbar_add_tab(a->tabbar, "New Tab");
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
static struct yetty_ycore_void_result on_back_click(struct yetty_yclass_ctx *c,
                                                    struct yetty_yclass_object *o, void *ud)
{
    (void)c;
    (void)o;
    go_back((struct app *)ud);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_fwd_click(struct yetty_yclass_ctx *c,
                                                   struct yetty_yclass_object *o, void *ud)
{
    (void)c;
    (void)o;
    go_forward((struct app *)ud);
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result on_reload_click(struct yetty_yclass_ctx *c,
                                                      struct yetty_yclass_object *o, void *ud)
{
    (void)c;
    (void)o;
    reload((struct app *)ud);
    return YETTY_OK_VOID();
}

static void on_new_tab_cb(struct yetty_ygui_object *tb, void *ud)
{
    (void)tb;
    ui_new_tab((struct app *)ud);
}
static void on_close_cb(struct yetty_ygui_object *tb, int idx, void *ud)
{
    (void)tb;
    ui_close_tab((struct app *)ud, idx);
}
static struct yetty_ycore_void_result on_tab_changed(struct yetty_yclass_ctx *c,
                                                     struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event, void *ud)
{
    (void)c;
    (void)target;
    struct app *a = ud;
    if (event && event->i0 >= 0 && event->i0 != a->active) {
        switch_tab(a, event->i0);
    }
    return YETTY_OK_VOID();
}

static void focus_address(struct app *a)
{
    err_ok(yetty_ygui_textinput_set_focus(a->address, 1));
    a->address_focused = 1;
    yetty_ygui_framework_mark_dirty(a->fw);
}

static int key_cb(struct yetty_ygui_framework *fw, uint32_t key, int mods, void *ud)
{
    struct app *a = ud;
    (void)mods;
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
            yetty_ygui_textinput_handle_key(a->address, key);
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
    int in_addr = pt_in_rect(yetty_ygui_widget_rect(a->address), x, y);
    err_ok(yetty_ygui_textinput_set_focus(a->address, in_addr ? 1 : 0));
    a->address_focused = in_addr;

    struct yetty_ycore_rectangle pr = yetty_ygui_widget_rect(a->page);
    if (!pt_in_rect(pr, x, y)) {
        return;
    }
    struct tab *t = &a->tabs[a->active];
    if (!t->engine) {
        return;
    }
    /* The embed rect already includes the scroll slide, so subtracting
	 * rect.min yields document coords. A plain <a href> navigates;
	 * otherwise dispatch a JS click. */
    float lx = x - pr.min.x;
    float ly = y - pr.min.y;
    char *href = yetty_ylexbor_link_at(t->engine, lx, ly);
    if (href) {
        navigate(a, t, href, 1);
    } else if (yetty_ylexbor_dispatch_click(t->engine, lx, ly) &&
               yetty_ylexbor_dom_dirty(t->engine)) {
        t->needs_render = 1;
        a->pending_render = 1;
    }
}

/* Lay out the tree against the current viewport and (re)render the active
 * tab's page into the embed when needed. Idempotent / cheap when nothing
 * changed. */
static void render_pass(struct app *a)
{
    struct yetty_ycore_rectangle root_rect = {{0.0f, 0.0f}, {a->viewport_w, a->viewport_h}};
    err_ok(yetty_ygui_layout_compute(a->root, root_rect));
    render_active(a);
    a->pending_render = 0;
}

/* Pump the active tab's JS timers; flag a re-render if the DOM changed.
 * Returns ms until the next timer (clamped), or 100 when idle. */
static int pump_active(struct app *a)
{
    struct tab *t = &a->tabs[a->active];
    int wait_ms = 100;
    if (t->engine) {
        int next = yetty_ylexbor_pump_timers(t->engine);
        if (next >= 0) {
            wait_ms = next < 100 ? next : 100;
        }
        if (yetty_ylexbor_dom_dirty(t->engine)) {
            t->needs_render = 1;
            a->pending_render = 1;
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
        if (m->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            err_ok(yetty_ygui_framework_feed_mouse_motion(a->fw, m->x, m->y));
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
            err_ok(yetty_ygui_framework_feed_mouse_button(a->fw, m->x, m->y, m->button, m->pressed,
                                                          0));
            if (m->pressed) {
                page_click(a, m->x, m->y);
            }
            yetty_ygui_framework_mark_dirty(a->fw);
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
            err_ok(yetty_ygui_framework_feed_mouse_scroll(a->fw, m->x, m->y, 0.0f, m->wheel_dy));
        }
        return;
    }

    if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE ||
        osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) {
        if (payload_len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *rz =
            (const struct yetty_client_input_resize *)payload;
        if (rz->width > 0 && rz->height > 0) {
            a->viewport_w = rz->width;
            a->viewport_h = rz->height;
            err_ok(yetty_ygui_framework_set_viewport(a->fw, rz->width, rz->height));
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
static struct yetty_ygui_object *add_nav_button(struct yetty_ygui_object *parent, const char *label,
                                                float w, yetty_ygui_click_cb cb, void *ud)
{
    struct yetty_ygui_object_ptr_result r =
        yetty_ygui_add(yetty_ygui_button_class_get().value, parent);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    err_ok(yetty_ygui_button_set_label(r.value, label));
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(r.value);
    l.width = w;
    l.flex_grow = 0.0f;
    l.flex_shrink = 0.0f;
    err_ok(yetty_ygui_widget_layout_set(r.value, &l));
    if (cb) {
        err_ok(yetty_ygui_clickable_on_click_set(r.value, cb, ud));
    }
    return r.value;
}

static int build_ui(struct app *a)
{
    struct yetty_ygui_object_ptr_result rr =
        yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return -1;
    }
    a->root = rr.value;
    err_ok(yetty_ygui_widget_set_bg_color(a->root, BR_BG));
    err_ok(yetty_ygui_framework_set_root(a->fw, a->root));

    /* Tab strip. */
    struct yetty_ygui_object_ptr_result tr =
        yetty_ygui_add(yetty_ygui_tabbar_class_get().value, a->root);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
        return -1;
    }
    a->tabbar = tr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(a->tabbar);
        l.height = 36.0f;
        l.gap = 4.0f;
        err_ok(yetty_ygui_widget_layout_set(a->tabbar, &l));
    }
    err_ok(yetty_ygui_tabbar_set_on_new_tab(a->tabbar, on_new_tab_cb, a));
    err_ok(yetty_ygui_tabbar_set_on_close(a->tabbar, on_close_cb, a));
    err_ok(
        yetty_ygui_object_subscribe(a->tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_changed, a));

    /* Toolbar: nav buttons + address bar. */
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_hbox_class_get().value, a->root);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_destroy(br.error);
        return -1;
    }
    struct yetty_ygui_object *toolbar = br.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(toolbar);
        l.height = 44.0f;
        l.gap = 6.0f;
        l.padding_top = 7.0f;
        l.padding_bottom = 7.0f;
        l.padding_left = 8.0f;
        l.padding_right = 8.0f;
        err_ok(yetty_ygui_widget_layout_set(toolbar, &l));
    }
    err_ok(yetty_ygui_widget_set_bg_color(toolbar, BR_TOOLBAR));

    add_nav_button(toolbar, "<", 40.0f, on_back_click, a);
    add_nav_button(toolbar, ">", 40.0f, on_fwd_click, a);
    add_nav_button(toolbar, "Reload", 72.0f, on_reload_click, a);

    struct yetty_ygui_object_ptr_result ar =
        yetty_ygui_add(yetty_ygui_textinput_class_get().value, toolbar);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
        return -1;
    }
    a->address = ar.value;
    err_ok(yetty_ygui_textinput_set_placeholder(a->address, "Type a URL and press Enter"));
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(a->address);
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(a->address, &l));
    }

    /* Scrollable page area. */
    struct yetty_ygui_object_ptr_result sr =
        yetty_ygui_add(yetty_ygui_scrollarea_class_get().value, a->root);
    if (YETTY_IS_ERR(sr)) {
        yetty_ycore_error_destroy(sr.error);
        return -1;
    }
    a->scroll = sr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(a->scroll);
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(a->scroll, &l));
    }

    struct yetty_ygui_object_ptr_result pr =
        yetty_ygui_add(yetty_ygui_ydraw_embed_class_get().value, a->scroll);
    if (YETTY_IS_ERR(pr)) {
        yetty_ycore_error_destroy(pr.error);
        return -1;
    }
    a->page = pr.value;

    /* Raster-image content uses a dedicated yimage widget (its own figure)
	 * — a yimage prim can't be painted by the ydraw_embed, and nesting the
	 * figure inside the scrollarea doesn't composite. So it's a sibling of
	 * the scrollarea; exactly one of {scrollarea, image} is visible. */
    struct yetty_ygui_object_ptr_result ir =
        yetty_ygui_add(yetty_ygui_yimage_class_get().value, a->root);
    if (YETTY_IS_ERR(ir)) {
        yetty_ycore_error_destroy(ir.error);
        return -1;
    }
    a->image = ir.value;
    err_ok(yetty_ygui_widget_set_visible(a->image, 0));
    a->showing_image = 0;
    return 0;
}

/* Set the shared content widget's height (drives the scrollarea's scroll
 * range); width stays unset so the vbox stretch keeps it as wide as the
 * scrollarea (and the page follows pane resizes). */
static void set_content_height(struct app *a, struct yetty_ygui_object *w, int height_px)
{
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
    l.width = -1.0f;
    l.height = (float)height_px;
    err_ok(yetty_ygui_widget_layout_set(w, &l));
}

/* HTML (ylexbor) or SVG (ysvg) → a draw list painted by the page embed. */
static void render_doc(struct app *a, struct tab *t)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(a->page);
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
            int vh = (int)(h > a->viewport_h ? h : a->viewport_h);
            err_ok(yetty_ylexbor_set_viewport(t->engine, (int)w, vh));
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

    /* Embed takes ownership of dl (frees the previous buffer). */
    err_ok(yetty_ygui_ydraw_embed_set_buffer(a->page, dl));
    if (content_h < (int)h) {
        content_h = (int)h; /* short content still fills the viewport */
    }
    set_content_height(a, a->page, content_h);
    t->needs_render = 0;
    t->rendered_w = w;
    yetty_ygui_framework_mark_dirty(a->fw);
}

/* Raster image → the yimage widget, sized to fit the width at the source
 * aspect ratio (so tall images scroll). */
static void render_image(struct app *a, struct tab *t)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(a->image);
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
    } else {
        render_doc(a, t);
    }
}

/* Create the first tab and load `initial_url` (or the start page). */
static void open_first_tab(struct app *a, const char *initial_url)
{
    struct yetty_ygui_object_ptr_result hr = yetty_ygui_tabbar_add_tab(a->tabbar, "New Tab");
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
    a.viewport_w = (float)viewport_w;
    a.viewport_h = (float)viewport_h;
    a.font_size = font_size > 0.0f ? font_size : 16.0f;

    struct stdout_pty out = {0};
    out.base.ops = stdout_pty_ops();
    struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(&out.base);
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "ybrowser: framework_create: %s\n", fr.error.msg);
        yetty_ycore_error_destroy(fr.error);
        if (raw_ok) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
        return 1;
    }
    a.fw = fr.value;
    err_ok(yetty_ygui_framework_set_viewport(a.fw, (float)viewport_w, (float)viewport_h));
    yetty_ygui_framework_set_key_cb(a.fw, key_cb, &a);

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
        /* Track the real pane size from the tty (the host may never send a
		 * pane-wide resize OSC, and the initial size is a guess). */
        int pw = 0, ph = 0;
        if (pick_pane_px(&pw, &ph) && ((float)pw != a.viewport_w || (float)ph != a.viewport_h)) {
            a.viewport_w = (float)pw;
            a.viewport_h = (float)ph;
            err_ok(yetty_ygui_framework_set_viewport(a.fw, (float)pw, (float)ph));
            a.tabs[a.active].needs_render = 1;
            a.pending_render = 1;
            yetty_ygui_framework_mark_dirty(a.fw);
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
            err_ok(yetty_ygui_framework_emit(a.fw));
            fflush(stdout);
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = {wait_ms / 1000, (wait_ms % 1000) * 1000};
        int rc = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
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
    err_ok(yetty_ygui_framework_clear_remote_fd(a.fw, STDOUT_FILENO));
    fflush(stdout);
    for (int i = 0; i < a.n_tabs; i++) {
        tab_free(&a.tabs[i]);
    }
    cache_free(&a.cache);
    err_ok(yetty_ygui_framework_destroy(a.fw));
    if (yf) {
        yetty_yface_destroy(yf);
    }
    if (raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return 0;
}

/* ===========================================================================
 * Standalone mode — own GPU window (no host yetty). Mirrors demo/ygui's
 * runner: yinit_run brings up the window + WebGPU; we build a local
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
#include <yetty/yfigure/rpc.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ychrome/chrome.h> /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/ychrome/methods.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ywire/wire-statemachine.h>

struct standalone {
    struct app app;
    struct yetty_yframework *yframework;
    struct yetty_yfigure_container *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct yetty_yplatform_memory_pty_pair pty_pair;
    int has_pty_pair;
    struct yetty_yfont_font *font;
    struct yetty_ychrome_host *chrome; /* draggable/resizable titlebar + min/max/close */
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_yevent_event_listener listener;
    struct yetty_ydraw_target *render_target;
    const char *initial_url;
};

static size_t utf8_encode(uint32_t cp, char *out);
static const char *encode_special_key(uint32_t key, char *scratch, size_t scratch_n,
                                      size_t *out_len);

static void sa_request_render(struct standalone *s)
{
    if (s->yframework && s->yframework->event_loop &&
        s->yframework->event_loop->ops->request_render) {
        s->yframework->event_loop->ops->request_render(s->yframework->event_loop);
    }
}

/* Slave end of the memory-pty pair: a resize on the producer (ygui) endpoint
 * arrives here — the SIGWINCH analog — and we size the compositor's root
 * container from the pixel extent. cols/rows are terminal-grid concepts the
 * compositor has no use for. */
static void sa_pty_resize_cb(void *userdata, uint32_t cols, uint32_t rows, uint32_t pixel_w,
                             uint32_t pixel_h)
{
    struct standalone *s = userdata;
    (void)cols;
    (void)rows;
    if (!s->root_container) {
        return;
    }
    struct yetty_ycore_rectangle rr = {.min = {0, 0}, .max = {(float)pixel_w, (float)pixel_h}};
    struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(s->root_container);
    yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(rf)-1, rr);
    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
}

static struct yetty_ycore_int_result sa_event_handler(struct yetty_yevent_event_listener *listener,
                                                      const struct yetty_yui_event *ev)
{
    struct standalone *s = container_of(listener, struct standalone, listener);

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
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Per-frame browser work: pump JS timers, re-render the active
		 * tab into the embed when needed. */
        pump_active(&s->app);
        if (s->app.pending_render || s->app.tabs[s->app.active].needs_render) {
            render_pass(&s->app);
        }
        if (yetty_ygui_framework_is_dirty(s->app.fw)) {
            err_ok(yetty_ygui_framework_emit(s->app.fw));
        }
        if (s->wire_sm) {
            struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(s->wire_sm);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
            }
        }
        struct yetty_ycore_void_result cl = s->render_target->ops->clear(s->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (s->root_container) {
            struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(s->root_container);
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(NULL, (struct yetty_yclass_object *)rf - 1, s->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 0);
        }
        struct yetty_ycore_void_result pp = s->render_target->ops->present(s->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        /* Keep ticking while the framework is dirty (JS timers/animation). */
        if (yetty_ygui_framework_is_dirty(s->app.fw)) {
            sa_request_render(s);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Window chrome gets first dibs on pointer events (caption drag / edge
     * resize / its buttons); anything it doesn't claim falls through to the
     * browser UI below. */
    if (s->chrome &&
        (ev->type == YETTY_YCORE_MOUSE_DOWN || ev->type == YETTY_YCORE_MOUSE_UP ||
         ev->type == YETTY_YCORE_MOUSE_MOVE || ev->type == YETTY_YCORE_MOUSE_DRAG ||
         ev->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK)) {
        struct yetty_ycore_int_result cr =
            yetty_ychrome_host_handle_event(s->chrome, ev);
        int consumed = YETTY_IS_OK(cr) && cr.value;
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
        if (consumed) {
            sa_request_render(s);
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (s->yframework->event_loop->ops->stop) {
            err_ok(s->yframework->event_loop->ops->stop(s->yframework->event_loop));
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE:
        yetty_yframework_reconfigure_surface(s->yframework, (uint32_t)ev->resize.width,
                                             (uint32_t)ev->resize.height);
        if (s->render_target && s->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            s->render_target->ops->resize(s->render_target, vp);
        }
        s->app.viewport_w = (float)ev->resize.width;
        s->app.viewport_h = (float)ev->resize.height;
        err_ok(yetty_ygui_framework_set_viewport(s->app.fw, (float)ev->resize.width,
                                                 (float)ev->resize.height));
        if (s->chrome) {
            struct yetty_ycore_void_result cr = yetty_ychrome_host_resized(
                s->chrome, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
        /* Drive the compositor-side rect through the pty pair (→
		 * sa_pty_resize_cb) rather than poking the container directly, so
		 * the resize travels the pair like a real PTY's TIOCSWINSZ. */
        if (s->pty_pair.a && s->pty_pair.a->ops->resize) {
            struct yetty_ycore_void_result rrz = s->pty_pair.a->ops->resize(
                s->pty_pair.a, 0, 0, (uint32_t)ev->resize.width, (uint32_t)ev->resize.height);
            if (YETTY_IS_ERR(rrz)) {
                yetty_ycore_error_destroy(rrz.error);
            }
        }
        s->app.tabs[s->app.active].needs_render = 1;
        s->app.pending_render = 1;
        yetty_ygui_framework_mark_dirty(s->app.fw);
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
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
        const char *bytes = encode_special_key(ev->key.key, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            err_ok(yetty_ygui_framework_feed_input(s->app.fw, bytes, n));
        }
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        int press = ev->type == YETTY_YCORE_MOUSE_DOWN ? 1 : 0;
        err_ok(yetty_ygui_framework_feed_mouse_button(s->app.fw, ev->mouse.x, ev->mouse.y,
                                                      ev->mouse.button, press, ev->mouse.mods));
        if (press) {
            page_click(&s->app, ev->mouse.x, ev->mouse.y);
        }
        yetty_ygui_framework_mark_dirty(s->app.fw);
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_SCROLL:
        err_ok(yetty_ygui_framework_feed_mouse_scroll(s->app.fw, ev->mouse_scroll.x,
                                                      ev->mouse_scroll.y, ev->mouse_scroll.dx,
                                                      ev->mouse_scroll.dy));
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        err_ok(yetty_ygui_framework_feed_mouse_motion(s->app.fw, ev->mouse.x, ev->mouse.y));
        sa_request_render(s);
        return YETTY_OK(yetty_ycore_int, 1);
    default:
        break;
    }
    sa_request_render(s);
    return YETTY_OK(yetty_ycore_int, 0);
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
static const char *encode_special_key(uint32_t key, char *scratch, size_t scratch_n,
                                      size_t *out_len)
{
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
    case 261:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[3~");
        return scratch; /* Del */
    case 263:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[D");
        return scratch; /* ← */
    case 262:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[C");
        return scratch; /* → */
    case 265:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[A");
        return scratch; /* ↑ */
    case 264:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[B");
        return scratch; /* ↓ */
    case 268:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[H");
        return scratch; /* Home */
    case 269:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[F");
        return scratch; /* End */
    default:
        *out_len = 0;
        return NULL;
    }
}

static struct yetty_ycore_void_result sa_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct standalone *s = (struct standalone *)user;

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "ybrowser standalone: yframework_create");
    s->yframework = frr.value;
    s->render_target = s->yframework->render_target;

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
            err_ok(yetty_ydraw_composite_factory_register(s->composite_factory, yplot_f));
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
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
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(s->figure_registry, &s->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "ybrowser standalone: ygrid_register_factory");
        const uint32_t producer_kinds[] = {YETTY_YFIGURE_KIND_YPLOT, YETTY_YFIGURE_KIND_YIMAGE};
        for (size_t i = 0; i < sizeof(producer_kinds) / sizeof(producer_kinds[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                s->figure_registry, producer_kinds[i], &s->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr,
                                "ybrowser standalone: register_factory_for_kind");
        }
    }

    /* Local container. */
    struct yetty_context ctx = {.runtime = s->yframework, .event_loop = s->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)rt->surface_width, (float)rt->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "ybrowser standalone: container_create");
        s->root_container = yetty_yfigure_container_from(obj_res.value);
        yetty_yfigure_container_set_context(s->root_container, &ctx);
        yetty_yfigure_container_set_registry(s->root_container, s->figure_registry);
        yetty_yfigure_container_set_rect(s->root_container, root_rect);
    }

    /* Window chrome: draggable/resizable titlebar + min/max/close. */
    {
        struct yetty_ychrome_host_ptr_result chrome_r = yetty_ychrome_host_create(
            s->root_container, s->font, &ctx, s->yframework->window_manager,
            (float)rt->surface_width, (float)rt->surface_height, 34.0f, 8.0f,
            YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_r)) {
            s->chrome = chrome_r.value;
        } else {
            ywarn("ybrowser standalone: chrome host create failed: %s", chrome_r.error.msg);
            yetty_ycore_error_destroy(chrome_r.error);
        }
    }

    /* Memory pty pair → wire SM → container. */
    {
        struct yetty_yplatform_memory_pty_pair_result pr =
            yetty_yplatform_memory_pty_pair_create(0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ybrowser standalone: memory_pty_pair");
        s->pty_pair = pr.value;
        s->has_pty_pair = 1;
    }
    {
        struct yetty_ywire_wire_statemachine_ptr_result sr =
            yetty_ywire_wire_statemachine_create(s->pty_pair.b);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ybrowser standalone: wire_sm_create");
        s->wire_sm = sr.value;
        struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register(
            s->wire_sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YCOMPOSITOR_BIN,
            /*has_args=*/1, yetty_yfigure_container_process_input, s->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ybrowser standalone: wire_sm register");
    }

    /* ygui framework on the producer end → writes into the pty pair. */
    {
        struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(s->pty_pair.a);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ybrowser standalone: framework_create");
        s->app.fw = fr.value;
        s->app.event_loop = s->yframework->event_loop;
        s->app.viewport_w = (float)rt->surface_width;
        s->app.viewport_h = (float)rt->surface_height;
        err_ok(yetty_ygui_framework_set_viewport(s->app.fw, (float)rt->surface_width,
                                                 (float)rt->surface_height));
    }

    yetty_ygui_framework_set_key_cb(s->app.fw, key_cb, &s->app);
    if (build_ui(&s->app) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser standalone: build_ui");
    }
    open_first_tab(&s->app, s->initial_url);

    /* Producer writes wake the loop so emitted frames get rendered. */
    yetty_yplatform_memory_pty_set_wake(
        s->pty_pair.b,
        (yetty_yplatform_memory_pty_wake_fn)s->yframework->event_loop->ops->request_render,
        s->yframework->event_loop);

    /* Window-size changes reach the compositor end through the pair, the
	 * same way bytes do. */
    yetty_yplatform_memory_pty_set_resize(s->pty_pair.b, sa_pty_resize_cb, s);

    s->listener.handler = sa_event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(s->yframework->event_loop, &s->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "ybrowser standalone: register_listeners");

    yetty_yevent_post_async(rt->platform_input_pipe,
                            &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        s->yframework->event_loop->ops->start(s->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown. */
    for (int i = 0; i < s->app.n_tabs; i++) {
        tab_free(&s->app.tabs[i]);
    }
    cache_free(&s->app.cache);
    if (s->app.fw) {
        err_ok(yetty_ygui_framework_destroy(s->app.fw));
        s->app.fw = NULL;
    }
    if (s->wire_sm) {
        err_ok(yetty_ywire_wire_statemachine_destroy(s->wire_sm));
        s->wire_sm = NULL;
    }
    if (s->has_pty_pair) {
        if (s->pty_pair.a && s->pty_pair.a->ops->destroy) {
            err_ok(s->pty_pair.a->ops->destroy(s->pty_pair.a));
        }
        if (s->pty_pair.b && s->pty_pair.b->ops->destroy) {
            err_ok(s->pty_pair.b->ops->destroy(s->pty_pair.b));
        }
        s->has_pty_pair = 0;
    }
    if (s->chrome) {
        err_ok(yetty_ychrome_host_destroy(s->chrome));
        s->chrome = NULL;
    }
    if (s->root_container) {
        struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(s->root_container);
        err_ok(yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)rf - 1));
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

int ybrowser_ui_run_standalone(const char *initial_url, float font_size, int argc, char **argv)
{
    ytrace_init();
    /* Standalone runs in its own terminal, so surface page JS console
	 * output there (useful for debugging why a page renders oddly). */
    setenv("YBROWSER_JS_CONSOLE", "1", 0);
    struct standalone s = {0};
    s.app.running = 1;
    s.app.font_size = font_size > 0.0f ? font_size : 16.0f;
    s.initial_url = initial_url;
    struct yetty_yinit_app_config cfg = {.extract_assets_fn = yetty_platform_extract_assets};
    struct yetty_ycore_int_result run_result = yetty_yinit_run(argc, argv, &cfg, sa_worker, &s);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ybrowser: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}

#endif /* YETTY_YBROWSER_HAS_STANDALONE */
