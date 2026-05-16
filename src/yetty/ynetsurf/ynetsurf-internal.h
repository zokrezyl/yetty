#ifndef YETTY_YNETSURF_INTERNAL_H
#define YETTY_YNETSURF_INTERNAL_H

/* Shared between ynetsurf.c (boot/tables) and ynetsurf-plotters.c
 * (drawing callbacks). Not installed. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <yetty/ynetsurf/ynetsurf.h>

#include "utils/errors.h"
#include "netsurf/types.h"
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"

struct browser_window;
struct yetty_ydraw_core_draw_list;

struct yetty_ynetsurf_schedule_entry {
    int64_t deadline_ms;
    void (*cb)(void *p);
    void *p;
    struct yetty_ynetsurf_schedule_entry *next;
};

struct gui_window {
    struct yetty_ynetsurf *owner;
    struct browser_window *bw;
    int width, height;
    int scroll_x, scroll_y;
    bool needs_redraw;
    char *title;
};

struct yetty_ynetsurf {
    struct gui_window *gw;
    struct yetty_ynetsurf_schedule_entry *schedule_head;

    /* Viewport set via yetty_ynetsurf_set_size before the bw / gw
	 * exists. ns_win_create() reads this when NetSurf first calls
	 * back to mint the window. */
    int pending_width;
    int pending_height;

    /* Current redraw target (set by yetty_ynetsurf_redraw, read by plotter
	 * callbacks via redraw_context.priv). */
    struct yetty_ydraw_core_draw_list *cur_buf;

    /* Current clip rect (CPU-tracked, used to AABB-cull primitives). */
    struct rect cur_clip;

    /* Default font id registered into cur_buf. -1 means "none yet". */
    int32_t default_font_id;
    uint32_t z_counter;
};

extern const struct plotter_table yetty_ynetsurf_plotters;

/* schedule.c (inlined into ynetsurf.c) */
nserror yetty_ynetsurf_schedule(int t, void (*cb)(void *p), void *p);
int yetty_ynetsurf_schedule_run(struct yetty_ynetsurf *ns);
void yetty_ynetsurf_schedule_clear(struct yetty_ynetsurf *ns);

/* Visible to the host; defined in ynetsurf.c. The plotters use this to
 * reach the current ynetsurf instance through redraw_context.priv. */
struct yetty_ynetsurf *yetty_ynetsurf_singleton(void);

#endif
