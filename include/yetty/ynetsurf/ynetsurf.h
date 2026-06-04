#ifndef YETTY_YNETSURF_H
#define YETTY_YNETSURF_H

/*
 * ynetsurf — NetSurf 3.11 frontend that emits ydraw primitives.
 *
 * Plug a NetSurf core build (libnetsurf + libcss + libdom + libhubbub +
 * libnsutils + libwapcaplet + libnsbmp + libnsgif + libnslog +
 * libparserutils + libnspsl + libsvgtiny + libutf8proc) underneath, and
 * this library will:
 *
 *   - register the required gui_* tables with the NetSurf core
 *   - drive page load and re-layout
 *   - on each redraw, drain the page into a yetty_ydraw_drawable_list of
 *     ysdf primitives + TEXT_DRAWABLE_LIST drawable-list entries
 *
 * The host (e.g. tools/ynetsurf) owns the lifetime, the viewport size,
 * and the event loop pump.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <yetty/ycore/result.h>

struct yetty_ydraw_drawable_list;
struct nsurl;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ynetsurf;

YETTY_YRESULT_DECLARE(yetty_ynetsurf_ptr, struct yetty_ynetsurf *);

struct yetty_ynetsurf_config {
    int width;
    int height;
    const char *resource_path; /* colon-separated; may be NULL */
};

struct yetty_ynetsurf_ptr_result yetty_ynetsurf_create(const struct yetty_ynetsurf_config *cfg);

struct yetty_ycore_void_result yetty_ynetsurf_destroy(struct yetty_ynetsurf *ns);

struct yetty_ycore_void_result yetty_ynetsurf_navigate(struct yetty_ynetsurf *ns, const char *url);

struct yetty_ycore_void_result yetty_ynetsurf_set_size(struct yetty_ynetsurf *ns, int width,
                                                       int height);

struct yetty_ycore_void_result yetty_ynetsurf_set_scroll(struct yetty_ynetsurf *ns, int sx, int sy);

/* Drain the current page into buf. Caller owns buf; this function
 * appends primitives. Returns NSERROR-translated error on plotter
 * faults (rare; bail-on-first-error). */
struct yetty_ycore_void_result yetty_ynetsurf_redraw(struct yetty_ynetsurf *ns,
                                                     struct yetty_ydraw_drawable_list *buf);

/* Run any pending scheduled callbacks whose deadline has elapsed.
 * Call this from the host event loop. Returns the milliseconds until
 * the next deadline (-1 if none pending). */
int yetty_ynetsurf_pump(struct yetty_ynetsurf *ns);

void yetty_ynetsurf_mouse_move(struct yetty_ynetsurf *ns, int x, int y);
void yetty_ynetsurf_mouse_click(struct yetty_ynetsurf *ns, int x, int y, int button);
void yetty_ynetsurf_mouse_release(struct yetty_ynetsurf *ns, int x, int y, int button);
void yetty_ynetsurf_key_press(struct yetty_ynetsurf *ns, uint32_t codepoint);
void yetty_ynetsurf_scroll(struct yetty_ynetsurf *ns, int dx, int dy);

bool yetty_ynetsurf_get_extents(struct yetty_ynetsurf *ns, int *width, int *height);
const char *yetty_ynetsurf_get_title(struct yetty_ynetsurf *ns);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YNETSURF_H */
