/*
 * ynetsurf — NetSurf 3.11 frontend → ypaint primitives.
 *
 * Implements the five mandatory NetSurf gui tables (misc, window,
 * fetch, bitmap, layout) plus a plotter_table that drains drawing
 * commands into a yetty_ypaint_core_buffer.
 */

#include "ynetsurf-internal.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* NetSurf core */
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "netsurf/browser_window.h"
#include "netsurf/bitmap.h"
#include "netsurf/clipboard.h"
#include "netsurf/cookie_db.h"
#include "netsurf/fetch.h"
#include "netsurf/keypress.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/mouse.h"
#include "netsurf/netsurf.h"
#include "netsurf/url_db.h"
#include "netsurf/window.h"
#include "desktop/gui_table.h"
#include "content/fetch.h"

/* yetty */
#include <yetty/ypaint-core/buffer.h>

/* Singleton — NetSurf's table registration is process-wide and the
 * plotter callbacks reach this via redraw_context.priv. We keep one
 * ynetsurf per process. */
static struct yetty_ynetsurf *g_ynetsurf = NULL;

struct yetty_ynetsurf *yetty_ynetsurf_singleton(void) { return g_ynetsurf; }

/* ===========================================================================
 * Schedule (gui_misc_table.schedule)
 * ===========================================================================*/

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

nserror yetty_ynetsurf_schedule(int t, void (*cb)(void *p), void *p)
{
	struct yetty_ynetsurf *ns = g_ynetsurf;
	if (ns == NULL) return NSERROR_INIT_FAILED;

	struct yetty_ynetsurf_schedule_entry **pp = &ns->schedule_head;
	while (*pp != NULL) {
		if ((*pp)->cb == cb && (*pp)->p == p) {
			struct yetty_ynetsurf_schedule_entry *dead = *pp;
			*pp = dead->next;
			free(dead);
			break;
		}
		pp = &(*pp)->next;
	}

	if (t < 0) return NSERROR_OK;

	struct yetty_ynetsurf_schedule_entry *e = calloc(1, sizeof(*e));
	if (e == NULL) return NSERROR_NOMEM;
	e->deadline_ms = now_ms() + t;
	e->cb = cb;
	e->p = p;
	e->next = ns->schedule_head;
	ns->schedule_head = e;
	return NSERROR_OK;
}

int yetty_ynetsurf_schedule_run(struct yetty_ynetsurf *ns)
{
	int64_t now = now_ms();

	/* Detach all ready entries first, then dispatch — callbacks are
	 * allowed to mutate the list (re-schedule themselves, schedule
	 * new entries) without breaking iteration. */
	struct yetty_ynetsurf_schedule_entry *ready = NULL;
	struct yetty_ynetsurf_schedule_entry **pp = &ns->schedule_head;
	while (*pp != NULL) {
		struct yetty_ynetsurf_schedule_entry *e = *pp;
		if (e->deadline_ms <= now) {
			*pp = e->next;
			e->next = ready;
			ready = e;
		} else {
			pp = &e->next;
		}
	}

	while (ready != NULL) {
		struct yetty_ynetsurf_schedule_entry *e = ready;
		ready = e->next;
		void (*cb)(void *) = e->cb;
		void *p = e->p;
		free(e);
		if (cb != NULL) cb(p);
	}

	int64_t next_deadline = -1;
	for (struct yetty_ynetsurf_schedule_entry *e = ns->schedule_head;
	     e != NULL; e = e->next) {
		if (next_deadline < 0 || e->deadline_ms < next_deadline)
			next_deadline = e->deadline_ms;
	}
	if (next_deadline < 0) return -1;
	int64_t delta = next_deadline - now_ms();
	return delta < 0 ? 0 : (int)delta;
}

void yetty_ynetsurf_schedule_clear(struct yetty_ynetsurf *ns)
{
	struct yetty_ynetsurf_schedule_entry *e = ns->schedule_head;
	while (e != NULL) {
		struct yetty_ynetsurf_schedule_entry *next = e->next;
		free(e);
		e = next;
	}
	ns->schedule_head = NULL;
}

/* ===========================================================================
 * gui_misc_table
 * ===========================================================================*/

static nserror ns_misc_schedule(int t, void (*cb)(void *p), void *p)
{
	return yetty_ynetsurf_schedule(t, cb, p);
}

static struct gui_misc_table misc_table = {
	.schedule = ns_misc_schedule,
};

/* ===========================================================================
 * gui_window_table
 * ===========================================================================*/

static struct gui_window *ns_win_create(struct browser_window *bw,
					struct gui_window *existing,
					gui_window_create_flags flags)
{
	(void)existing; (void)flags;
	struct yetty_ynetsurf *ns = g_ynetsurf;
	if (ns == NULL || ns->gw != NULL) return NULL;

	ns->gw = calloc(1, sizeof(*ns->gw));
	if (ns->gw == NULL) return NULL;
	ns->gw->owner = ns;
	ns->gw->bw = bw;
	ns->gw->width  = ns->pending_width  > 0 ? ns->pending_width  : 1024;
	ns->gw->height = ns->pending_height > 0 ? ns->pending_height : 768;
	return ns->gw;
}

static void ns_win_destroy(struct gui_window *gw)
{
	if (gw == NULL) return;
	free(gw->title);
	if (gw->owner != NULL && gw->owner->gw == gw)
		gw->owner->gw = NULL;
	free(gw);
}

static nserror ns_win_invalidate(struct gui_window *gw, const struct rect *r)
{
	(void)r;
	if (gw != NULL) gw->needs_redraw = true;
	return NSERROR_OK;
}

static bool ns_win_get_scroll(struct gui_window *gw, int *sx, int *sy)
{
	if (gw == NULL) return false;
	*sx = gw->scroll_x;
	*sy = gw->scroll_y;
	return true;
}

static nserror ns_win_set_scroll(struct gui_window *gw, const struct rect *r)
{
	if (gw == NULL || r == NULL) return NSERROR_BAD_PARAMETER;
	gw->scroll_x = r->x0;
	gw->scroll_y = r->y0;
	gw->needs_redraw = true;
	return NSERROR_OK;
}

static nserror ns_win_get_dimensions(struct gui_window *gw, int *w, int *h)
{
	if (gw == NULL) return NSERROR_BAD_PARAMETER;
	*w = gw->width;
	*h = gw->height;
	return NSERROR_OK;
}

static nserror ns_win_event(struct gui_window *gw, enum gui_window_event ev)
{
	(void)gw; (void)ev;
	return NSERROR_OK;
}

static void ns_win_set_title(struct gui_window *gw, const char *title)
{
	if (gw == NULL) return;
	free(gw->title);
	gw->title = title != NULL ? strdup(title) : NULL;
}

static struct gui_window_table window_table = {
	.create = ns_win_create,
	.destroy = ns_win_destroy,
	.invalidate = ns_win_invalidate,
	.get_scroll = ns_win_get_scroll,
	.set_scroll = ns_win_set_scroll,
	.get_dimensions = ns_win_get_dimensions,
	.event = ns_win_event,
	.set_title = ns_win_set_title,
};

/* ===========================================================================
 * gui_fetch_table — minimal MIME-by-extension
 * ===========================================================================*/

static const char *ns_fetch_filetype(const char *unix_path)
{
	const char *dot = strrchr(unix_path, '.');
	if (dot == NULL) return "text/html";
	dot++;
	if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm")) return "text/html";
	if (!strcasecmp(dot, "css")) return "text/css";
	if (!strcasecmp(dot, "txt")) return "text/plain";
	if (!strcasecmp(dot, "js"))  return "application/javascript";
	if (!strcasecmp(dot, "png")) return "image/png";
	if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return "image/jpeg";
	if (!strcasecmp(dot, "gif")) return "image/gif";
	if (!strcasecmp(dot, "bmp")) return "image/bmp";
	if (!strcasecmp(dot, "svg")) return "image/svg+xml";
	if (!strcasecmp(dot, "ico")) return "image/x-icon";
	return "text/html";
}

/* NetSurf core fetches its built-in stylesheets and chrome content via
 * URLs of the form `resource:default.css`. Translate those to a file://
 * URL pointing into the unpacked netsurf-all source tree, which is
 * always available at YETTY_NETSURF_RESOURCES_DIR (CMake-supplied) — or,
 * failing that, $NETSURF_RESOURCES from the environment. */
static struct nsurl *ns_fetch_get_resource_url(const char *path)
{
#ifndef YETTY_NETSURF_RESOURCES_DIR
#  define YETTY_NETSURF_RESOURCES_DIR ""
#endif
	const char *root = getenv("NETSURF_RESOURCES");
	if (root == NULL || root[0] == '\0')
		root = YETTY_NETSURF_RESOURCES_DIR;
	if (root == NULL || root[0] == '\0')
		return NULL;

	char buf[4096];
	int n = snprintf(buf, sizeof(buf), "file://%s/%s", root, path);
	if (n < 0 || (size_t)n >= sizeof(buf))
		return NULL;

	struct nsurl *u = NULL;
	if (nsurl_create(buf, &u) != NSERROR_OK)
		return NULL;
	return u;
}

static struct gui_fetch_table fetch_table = {
	.filetype = ns_fetch_filetype,
	.get_resource_url = ns_fetch_get_resource_url,
};

/* ===========================================================================
 * gui_bitmap_table — opaque RGBA8888 buffers
 * ===========================================================================*/

struct ns_bitmap {
	int width, height;
	bool opaque;
	unsigned char *pixels;
};

static void *ns_bm_create(int w, int h, enum gui_bitmap_flags flags)
{
	struct ns_bitmap *b = calloc(1, sizeof(*b));
	if (b == NULL) return NULL;
	b->width = w;
	b->height = h;
	b->opaque = (flags & BITMAP_OPAQUE) != 0;
	size_t bytes = (size_t)w * (size_t)h * 4;
	b->pixels = (flags & BITMAP_CLEAR) ? calloc(1, bytes) : malloc(bytes);
	if (b->pixels == NULL) { free(b); return NULL; }
	return b;
}

static void ns_bm_destroy(void *bm)
{
	struct ns_bitmap *b = bm;
	if (b == NULL) return;
	free(b->pixels);
	free(b);
}

static void ns_bm_set_opaque(void *bm, bool o) { ((struct ns_bitmap *)bm)->opaque = o; }
static bool ns_bm_get_opaque(void *bm) { return ((struct ns_bitmap *)bm)->opaque; }
static unsigned char *ns_bm_get_buffer(void *bm) { return ((struct ns_bitmap *)bm)->pixels; }
static size_t ns_bm_get_rowstride(void *bm) { return (size_t)((struct ns_bitmap *)bm)->width * 4; }
static int ns_bm_get_width(void *bm) { return ((struct ns_bitmap *)bm)->width; }
static int ns_bm_get_height(void *bm) { return ((struct ns_bitmap *)bm)->height; }
static void ns_bm_modified(void *bm) { (void)bm; }

static nserror ns_bm_render(struct bitmap *bm, struct hlcache_handle *h)
{
	(void)bm; (void)h;
	return NSERROR_NOT_IMPLEMENTED;
}

static struct gui_bitmap_table bitmap_table = {
	.create = ns_bm_create,
	.destroy = ns_bm_destroy,
	.set_opaque = ns_bm_set_opaque,
	.get_opaque = ns_bm_get_opaque,
	.get_buffer = ns_bm_get_buffer,
	.get_rowstride = ns_bm_get_rowstride,
	.get_width = ns_bm_get_width,
	.get_height = ns_bm_get_height,
	.modified = ns_bm_modified,
	.render = ns_bm_render,
};

/* ===========================================================================
 * gui_layout_table — naive UTF-8 width estimator
 *
 * NetSurf calls this at layout time, *before* any drawing. Returning
 * vaguely-correct widths is what makes wrapping look sane. A real impl
 * would read FreeType advances; this MVP uses 0.55 * size per UTF-8
 * codepoint (conservative monospace-ish estimate).
 * ===========================================================================*/

static int utf8_count(const char *s, size_t len)
{
	int n = 0;
	for (size_t i = 0; i < len;) {
		unsigned char c = (unsigned char)s[i];
		if      (c < 0x80) i += 1;
		else if ((c & 0xE0) == 0xC0) i += 2;
		else if ((c & 0xF0) == 0xE0) i += 3;
		else if ((c & 0xF8) == 0xF0) i += 4;
		else i += 1;
		n++;
	}
	return n;
}

static int px_per_glyph(const struct plot_font_style *fstyle)
{
	float pt = plot_style_fixed_to_float(fstyle->size);
	float px = pt * 0.55f;
	if (px < 1.0f) px = 1.0f;
	return (int)(px + 0.5f);
}

static nserror ns_layout_width(const struct plot_font_style *fstyle,
			       const char *string, size_t length, int *width)
{
	*width = utf8_count(string, length) * px_per_glyph(fstyle);
	return NSERROR_OK;
}

static nserror ns_layout_position(const struct plot_font_style *fstyle,
				  const char *string, size_t length,
				  int x, size_t *char_offset, int *actual_x)
{
	int gw = px_per_glyph(fstyle);
	if (gw < 1) gw = 1;
	int g = x / gw;
	size_t off = 0;
	int count = 0;
	while (off < length && count < g) {
		unsigned char c = (unsigned char)string[off];
		if      (c < 0x80) off += 1;
		else if ((c & 0xE0) == 0xC0) off += 2;
		else if ((c & 0xF0) == 0xE0) off += 3;
		else if ((c & 0xF8) == 0xF0) off += 4;
		else off += 1;
		count++;
	}
	*char_offset = off;
	*actual_x = count * gw;
	return NSERROR_OK;
}

static nserror ns_layout_split(const struct plot_font_style *fstyle,
			       const char *string, size_t length,
			       int x, size_t *char_offset, int *actual_x)
{
	int gw = px_per_glyph(fstyle);
	if (gw < 1) gw = 1;
	int max_glyphs = x / gw;
	if (max_glyphs < 1) max_glyphs = 1;

	size_t last_space = 0;
	int last_space_x = 0;
	size_t off = 0;
	int count = 0;
	while (off < length && count < max_glyphs) {
		if (string[off] == ' ' || string[off] == '\t') {
			last_space = off;
			last_space_x = count * gw;
		}
		unsigned char c = (unsigned char)string[off];
		if      (c < 0x80) off += 1;
		else if ((c & 0xE0) == 0xC0) off += 2;
		else if ((c & 0xF0) == 0xE0) off += 3;
		else if ((c & 0xF8) == 0xF0) off += 4;
		else off += 1;
		count++;
	}

	if (last_space > 0) {
		*char_offset = last_space + 1;
		*actual_x = last_space_x;
	} else if (off >= length) {
		*char_offset = length;
		*actual_x = count * gw;
	} else {
		*char_offset = off > 0 ? off : 1;
		*actual_x = count * gw;
	}
	return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
	.width = ns_layout_width,
	.position = ns_layout_position,
	.split = ns_layout_split,
};

/* ===========================================================================
 * Top-level table & lifecycle
 * ===========================================================================*/

static struct netsurf_table ynetsurf_table = {
	.misc = &misc_table,
	.window = &window_table,
	.fetch = &fetch_table,
	.bitmap = &bitmap_table,
	.layout = &layout_table,
};

struct yetty_ynetsurf_ptr_result yetty_ynetsurf_create(
	const struct yetty_ynetsurf_config *cfg)
{
	if (g_ynetsurf != NULL)
		return YETTY_ERR(yetty_ynetsurf_ptr,
				 "ynetsurf singleton already created");

	struct yetty_ynetsurf *ns = calloc(1, sizeof(*ns));
	if (ns == NULL)
		return YETTY_ERR(yetty_ynetsurf_ptr, "alloc");

	ns->default_font_id = -1;
	g_ynetsurf = ns;

	nserror err = netsurf_register(&ynetsurf_table);
	if (err != NSERROR_OK) {
		free(ns); g_ynetsurf = NULL;
		return YETTY_ERR(yetty_ynetsurf_ptr,
				 "netsurf_register failed");
	}

	/* Set bitmap pixel layout — we expose RGBA8888 buffers (R first). */
	bitmap_fmt_t fmt = { .layout = BITMAP_LAYOUT_R8G8B8A8, .pma = false };
	bitmap_set_format(&fmt);

	/* Initialise the *global* nsoptions / nsoptions_default tables —
	 * NetSurf core (e.g. desktop/system_colour.c) reads them directly
	 * by name, so passing local shadows here would crash later. */
	err = nsoption_init(NULL, &nsoptions, &nsoptions_default);
	if (err != NSERROR_OK) {
		free(ns); g_ynetsurf = NULL;
		return YETTY_ERR(yetty_ynetsurf_ptr,
				 "nsoption_init failed");
	}

	int dummy_argc = 0;
	char *dummy_argv[1] = { NULL };
	nslog_init(NULL, &dummy_argc, dummy_argv);
	/* Default: only WARNING+; allow override via $YNETSURF_LOG. */
	const char *log_filter = getenv("YNETSURF_LOG");
	nslog_set_filter(log_filter != NULL ? log_filter : "level:WARNING");

	err = netsurf_init(NULL);
	if (err != NSERROR_OK) {
		free(ns); g_ynetsurf = NULL;
		return YETTY_ERR(yetty_ynetsurf_ptr,
				 "netsurf_init failed");
	}

	(void)cfg;  /* size lands on the gw at first create() */

	return YETTY_OK(yetty_ynetsurf_ptr, ns);
}

struct yetty_ycore_void_result yetty_ynetsurf_destroy(struct yetty_ynetsurf *ns)
{
	if (ns == NULL) return YETTY_OK_VOID();
	if (ns->gw != NULL && ns->gw->bw != NULL)
		browser_window_destroy(ns->gw->bw);
	yetty_ynetsurf_schedule_clear(ns);
	netsurf_exit();
	free(ns);
	if (g_ynetsurf == ns) g_ynetsurf = NULL;
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ynetsurf_navigate(
	struct yetty_ynetsurf *ns, const char *url)
{
	if (ns == NULL || url == NULL)
		return YETTY_ERR(yetty_ycore_void, "null");

	struct nsurl *nsu;
	nserror err = nsurl_create(url, &nsu);
	if (err != NSERROR_OK)
		return YETTY_ERR(yetty_ycore_void, "nsurl_create");

	if (ns->gw == NULL) {
		struct browser_window *bw = NULL;
		err = browser_window_create(BW_CREATE_HISTORY, nsu,
					    NULL, NULL, &bw);
	} else {
		err = browser_window_navigate(ns->gw->bw, nsu, NULL,
					      BW_NAVIGATE_HISTORY,
					      NULL, NULL, NULL);
	}
	nsurl_unref(nsu);

	if (err != NSERROR_OK)
		return YETTY_ERR(yetty_ycore_void, "navigate");
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ynetsurf_set_size(
	struct yetty_ynetsurf *ns, int width, int height)
{
	if (ns == NULL) return YETTY_ERR(yetty_ycore_void, "null");
	ns->pending_width = width;
	ns->pending_height = height;
	if (ns->gw != NULL) {
		ns->gw->width = width;
		ns->gw->height = height;
		if (ns->gw->bw != NULL) {
			browser_window_set_dimensions(ns->gw->bw, width, height);
			browser_window_schedule_reformat(ns->gw->bw);
		}
	}
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ynetsurf_set_scroll(
	struct yetty_ynetsurf *ns, int sx, int sy)
{
	if (ns == NULL || ns->gw == NULL)
		return YETTY_ERR(yetty_ycore_void, "no window");
	ns->gw->scroll_x = sx;
	ns->gw->scroll_y = sy;
	ns->gw->needs_redraw = true;
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ynetsurf_redraw(
	struct yetty_ynetsurf *ns, struct yetty_ypaint_core_buffer *buf)
{
	if (ns == NULL || buf == NULL)
		return YETTY_ERR(yetty_ycore_void, "null");
	if (ns->gw == NULL || ns->gw->bw == NULL)
		return YETTY_OK_VOID();

	if (!browser_window_has_content(ns->gw->bw))
		return YETTY_ERR(yetty_ycore_void,
				 "browser_window has no content yet");
	if (!browser_window_redraw_ready(ns->gw->bw))
		return YETTY_ERR(yetty_ycore_void,
				 "browser_window not redraw-ready");

	ns->cur_buf = buf;
	ns->z_counter = 0;
	ns->cur_clip.x0 = 0;
	ns->cur_clip.y0 = 0;
	ns->cur_clip.x1 = ns->gw->width;
	ns->cur_clip.y1 = ns->gw->height;

	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &yetty_ynetsurf_plotters,
		.priv = ns,
	};
	struct rect clip = ns->cur_clip;
	bool ok = browser_window_redraw(ns->gw->bw,
				    -ns->gw->scroll_x, -ns->gw->scroll_y,
				    &clip, &ctx);
	ns->gw->needs_redraw = false;
	ns->cur_buf = NULL;
	if (!ok)
		return YETTY_ERR(yetty_ycore_void,
				 "browser_window_redraw returned false");
	return YETTY_OK_VOID();
}

int yetty_ynetsurf_pump(struct yetty_ynetsurf *ns)
{
	if (ns == NULL) return -1;
	return yetty_ynetsurf_schedule_run(ns);
}

void yetty_ynetsurf_mouse_move(struct yetty_ynetsurf *ns, int x, int y)
{
	if (ns == NULL || ns->gw == NULL || ns->gw->bw == NULL) return;
	browser_window_mouse_track(ns->gw->bw, BROWSER_MOUSE_HOVER,
				   x + ns->gw->scroll_x, y + ns->gw->scroll_y);
}

void yetty_ynetsurf_mouse_click(struct yetty_ynetsurf *ns, int x, int y, int button)
{
	if (ns == NULL || ns->gw == NULL || ns->gw->bw == NULL) return;
	browser_mouse_state st = button == 2
		? BROWSER_MOUSE_PRESS_2 | BROWSER_MOUSE_CLICK_2
		: BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_CLICK_1;
	browser_window_mouse_click(ns->gw->bw, st,
				   x + ns->gw->scroll_x, y + ns->gw->scroll_y);
}

void yetty_ynetsurf_mouse_release(struct yetty_ynetsurf *ns, int x, int y, int button)
{
	if (ns == NULL || ns->gw == NULL || ns->gw->bw == NULL) return;
	(void)button;
	browser_window_mouse_track(ns->gw->bw, BROWSER_MOUSE_HOVER,
				   x + ns->gw->scroll_x, y + ns->gw->scroll_y);
}

void yetty_ynetsurf_key_press(struct yetty_ynetsurf *ns, uint32_t cp)
{
	if (ns == NULL || ns->gw == NULL || ns->gw->bw == NULL) return;
	browser_window_key_press(ns->gw->bw, cp);
}

void yetty_ynetsurf_scroll(struct yetty_ynetsurf *ns, int dx, int dy)
{
	if (ns == NULL || ns->gw == NULL) return;
	ns->gw->scroll_x += dx;
	ns->gw->scroll_y += dy;
	if (ns->gw->scroll_x < 0) ns->gw->scroll_x = 0;
	if (ns->gw->scroll_y < 0) ns->gw->scroll_y = 0;
	ns->gw->needs_redraw = true;
}

bool yetty_ynetsurf_get_extents(struct yetty_ynetsurf *ns, int *w, int *h)
{
	if (ns == NULL || ns->gw == NULL || ns->gw->bw == NULL) return false;
	return browser_window_get_extents(ns->gw->bw, false, w, h) == NSERROR_OK;
}

const char *yetty_ynetsurf_get_title(struct yetty_ynetsurf *ns)
{
	if (ns == NULL || ns->gw == NULL) return NULL;
	if (ns->gw->bw != NULL) {
		const char *t = browser_window_get_title(ns->gw->bw);
		if (t != NULL) return t;
	}
	return ns->gw->title;
}
