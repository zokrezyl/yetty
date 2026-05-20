#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#include "rect.h"
#include "utf8.h"

#define UNICODE_SPACE 0x20
#define UNICODE_LINEFEED 0x0a

/* yetty: wide character continuation marker */
#define GLYPH_WIDE_CONT 0xFFFEu

#undef DEBUG_REFLOW

/* State of the pen at some moment in time - kept for attribute tracking */
typedef struct
{
  VTermColor   fg, bg; /* yetty: always RGB, converted immediately in setpenattr */

  unsigned int bold      : 1;
  unsigned int underline : 2;
  unsigned int italic    : 1;
  unsigned int blink     : 1;
  unsigned int reverse   : 1;
  unsigned int conceal   : 1;
  unsigned int strike    : 1;
  unsigned int font      : 4; /* 0 to 9 */
  unsigned int small     : 1;
  unsigned int baseline  : 2;

  unsigned int protected_cell : 1;
  unsigned int dwl            : 1; /* on a DECDWL or DECDHL line */
  unsigned int dhl            : 2; /* on a DECDHL line (1=top 2=bottom) */
} ScreenPen;

/* yetty: VTermScreenCell removed, using VTermScreenCell directly */

struct VTermScreen
{
  VTerm *vt;
  VTermState *state;

  const VTermScreenCallbacks *callbacks;
  void *cbdata;

  VTermDamageSize damage_merge;
  /* start_row == -1 => no damage */
  VTermRect damaged;
  VTermRect pending_scrollrect;
  int pending_scroll_downward, pending_scroll_rightward;

  int rows;
  int cols;

  unsigned int global_reverse : 1;
  unsigned int reflow : 1;

  /* Primary and Altscreen. buffers[1] is lazily allocated as needed.
   *
   * yetty: each buffer is allocated 2*rows tall — the "double-allocation
   * circular buffer" trick. The live screen is always rows[root_row[i]
   * .. root_row[i]+rows-1] within buffers[i]. On a full-screen scroll-up
   * we just bump root_row instead of memmoving rows-1 rows. When root_row
   * would exceed `rows`, we do one big memmove to linearise and reset.
   * Consumers pull root_row via vterm_screen_get_buffer_root_row(). */
  VTermScreenCell *buffers[2];
  int root_row[2];

  /* buffer will == buffers[0] or buffers[1], depending on altscreen */
  VTermScreenCell *buffer;

  /* buffer for a single screen row used in scrollback storage callbacks */
  VTermScreenCell *sb_buffer;

  ScreenPen pen;

  /* yetty: glyph resolver callback */
  VTermGlyphResolver glyph_resolver;
  void *resolver_user;
};

/* yetty: colors are now always RGB, no conversion needed */

/* yetty: modified for new cell format */
static inline void clearcell(VTermScreen *screen, VTermScreenCell *cell)
{
  cell->glyph_index = 0;
  cell->fg = screen->pen.fg;
  cell->bg = screen->pen.bg;
  cell->attrs.bold = screen->pen.bold;
  cell->attrs.underline = screen->pen.underline;
  cell->attrs.italic = screen->pen.italic;
  cell->attrs.blink = screen->pen.blink;
  cell->attrs.conceal = screen->pen.conceal;
  cell->attrs.strike = screen->pen.strike;
  cell->attrs.small = screen->pen.small;
  cell->attrs.baseline = screen->pen.baseline;
  cell->attrs.default_fg = 0;
  cell->attrs.default_bg = 0;
  cell->attrs.font_type = 0;
}

/* Active root_row index — depends on which buffer is live. Tells callers and
 * getcell where row 0 of the visible screen sits within the 2*rows-tall
 * underlying allocation. */
static inline int active_bufidx(const VTermScreen *screen)
{
  return (screen->buffers[1] && screen->buffer == screen->buffers[1]) ? 1 : 0;
}

static inline int active_root_row(const VTermScreen *screen)
{
  return screen->root_row[active_bufidx(screen)];
}

static inline VTermScreenCell *getcell(const VTermScreen *screen, int row, int col)
{
  if(row < 0 || row >= screen->rows)
    return NULL;
  if(col < 0 || col >= screen->cols)
    return NULL;
  return screen->buffer + (screen->cols * (active_root_row(screen) + row)) + col;
}

/* Allocate 2*rows*cols cells (the "double-allocation circular buffer"
 * trick) and clear all of them so the second half is well-defined at all
 * times. */
static VTermScreenCell *alloc_buffer(VTermScreen *screen, int rows, int cols)
{
  int alloc_rows = rows * 2;
  VTermScreenCell *new_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * alloc_rows * cols);

  for(int row = 0; row < alloc_rows; row++) {
    for(int col = 0; col < cols; col++) {
      clearcell(screen, &new_buffer[row * cols + col]);
    }
  }

  return new_buffer;
}

static void damagerect(VTermScreen *screen, VTermRect rect)
{
  VTermRect emit;

  switch(screen->damage_merge) {
  case VTERM_DAMAGE_CELL:
    /* Always emit damage event */
    emit = rect;
    break;

  case VTERM_DAMAGE_ROW:
    /* Emit damage longer than one row. Try to merge with existing damage in
     * the same row */
    if(rect.end_row > rect.start_row + 1) {
      // Bigger than 1 line - flush existing, emit this
      vterm_screen_flush_damage(screen);
      emit = rect;
    }
    else if(screen->damaged.start_row == -1) {
      // None stored yet
      screen->damaged = rect;
      return;
    }
    else if(rect.start_row == screen->damaged.start_row) {
      // Merge with the stored line
      if(screen->damaged.start_col > rect.start_col)
        screen->damaged.start_col = rect.start_col;
      if(screen->damaged.end_col < rect.end_col)
        screen->damaged.end_col = rect.end_col;
      return;
    }
    else {
      // Emit the currently stored line, store a new one
      emit = screen->damaged;
      screen->damaged = rect;
    }
    break;

  case VTERM_DAMAGE_SCREEN:
  case VTERM_DAMAGE_SCROLL:
    /* Never emit damage event */
    if(screen->damaged.start_row == -1)
      screen->damaged = rect;
    else {
      rect_expand(&screen->damaged, &rect);
    }
    return;

  default:
    DEBUG_LOG("TODO: Maybe merge damage for level %d\n", screen->damage_merge);
    return;
  }

  if(screen->callbacks && screen->callbacks->damage)
    (*screen->callbacks->damage)(emit, screen->cbdata);
}

static void damagescreen(VTermScreen *screen)
{
  VTermRect rect = {
    .start_row = 0,
    .end_row   = screen->rows,
    .start_col = 0,
    .end_col   = screen->cols,
  };

  damagerect(screen, rect);
}

/* yetty: modified for new cell format */
static int putglyph(VTermGlyphInfo *info, VTermPos pos, void *user)
{
  VTermScreen *screen = user;
  VTermScreenCell *cell = getcell(screen, pos.row, pos.col);

  if(!cell)
    return 0;

  /* count chars for resolver */
  int count = 0;
  while(count < VTERM_MAX_CHARS_PER_CELL && info->chars[count])
    count++;
  if(count == 0) count = 1;

  /* resolve glyph */
  uint32_t glyph_index;
  uint8_t font_type = 0;
  if(screen->glyph_resolver) {
    VTermResolvedGlyph resolved = screen->glyph_resolver(
      info->chars, count, screen->pen.bold, screen->pen.italic, screen->resolver_user);
    glyph_index = resolved.glyph_index;
    font_type = resolved.font_type;
  } else {
    glyph_index = info->chars[0] ? info->chars[0] : UNICODE_SPACE;
  }

  /* get colors, handle reverse */
  VTermColor fg = screen->pen.fg;
  VTermColor bg = screen->pen.bg;
  if(screen->pen.reverse ^ screen->global_reverse) {
    VTermColor tmp = fg; fg = bg; bg = tmp;
  }

  /* fill cell */
  cell->glyph_index = glyph_index;
  cell->fg = fg;
  cell->bg = bg;
  cell->attrs.bold = screen->pen.bold;
  cell->attrs.underline = screen->pen.underline;
  cell->attrs.italic = screen->pen.italic;
  cell->attrs.blink = screen->pen.blink;
  cell->attrs.conceal = screen->pen.conceal;
  cell->attrs.strike = screen->pen.strike;
  cell->attrs.small = screen->pen.small;
  cell->attrs.baseline = screen->pen.baseline;
  cell->attrs.default_fg = 0;
  cell->attrs.default_bg = 0;
  cell->attrs.font_type = font_type;

  /* wide char continuation */
  for(int col = 1; col < info->width; col++) {
    VTermScreenCell *cont = getcell(screen, pos.row, pos.col + col);
    if(cont) {
      cont->glyph_index = GLYPH_WIDE_CONT;
      cont->fg = fg;
      cont->bg = bg;
      cont->attrs = cell->attrs;
    }
  }

  VTermRect rect = {
    .start_row = pos.row,
    .end_row   = pos.row+1,
    .start_col = pos.col,
    .end_col   = pos.col+info->width,
  };

  damagerect(screen, rect);

  return 1;
}

static void sb_pushline_from_row(VTermScreen *screen, int row)
{
  VTermPos pos = { .row = row };
  for(pos.col = 0; pos.col < screen->cols; pos.col++)
    vterm_screen_get_cell(screen, pos, screen->sb_buffer + pos.col);

  (screen->callbacks->sb_pushline)(screen->cols, screen->sb_buffer, screen->cbdata);
}

static int moverect_internal(VTermRect dest, VTermRect src, void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->sb_pushline &&
     dest.start_row == 0 && dest.start_col == 0 &&        // starts top-left corner
     dest.end_col == screen->cols &&                      // full width
     screen->buffer == screen->buffers[BUFIDX_PRIMARY]) { // not altscreen
    for(int row = 0; row < src.start_row; row++)
      sb_pushline_from_row(screen, row);
  }

  int cols = src.end_col - src.start_col;
  int downward = src.start_row - dest.start_row;

  /* yetty fast path: full-screen scroll-up by N rows.
   *
   *   src  = (N..rows)   × full-width    (downward = N > 0)
   *   dest = (0..rows-N) × full-width
   *
   * Since the buffer is allocated 2*rows tall, we just bump root_row by N —
   * no per-row memmove. The N rows that "appear" at the bottom (rows that
   * the live window didn't see before) are pre-cleared at allocation time
   * and kept clean: the caller (state.c) issues an erase for the freshly
   * exposed bottom region after the move. When root_row would advance past
   * `rows` (i.e. the live window would walk off the end of the
   * 2*rows-tall buffer), we do one big memmove to copy the live rows back
   * to the start of the buffer and reset root_row to 0. */
  if(downward > 0 &&
     dest.start_row == 0 && src.start_row == downward &&
     dest.end_row == screen->rows - downward && src.end_row == screen->rows &&
     dest.start_col == 0 && src.start_col == 0 &&
     dest.end_col == screen->cols && src.end_col == screen->cols) {
    int N = downward;
    int bufidx = active_bufidx(screen);
    int new_root = screen->root_row[bufidx] + N;
    if(new_root + screen->rows > screen->rows * 2) {
      /* Linearise: the live N..rows window after this scroll would extend
       * past the 2*rows allocation. Memmove the live rows-N rows that we
       * are KEEPING (currently at root_row+N..root_row+rows) down to row 0,
       * then reset root_row. One memmove of (rows-N)*cols cells, instead of
       * (rows-1)*cols every scroll. */
      int kept_rows = screen->rows - N;
      memmove(screen->buffer,
              screen->buffer + (size_t)(screen->root_row[bufidx] + N) * screen->cols,
              (size_t)kept_rows * screen->cols * sizeof(VTermScreenCell));
      /* Clear the just-exposed N rows (caller will erase them anyway, but
       * we also want any later read of them to see clean cells, not stale
       * data from a previous wrap). */
      for(int row = kept_rows; row < screen->rows; row++)
        for(int col = 0; col < screen->cols; col++)
          clearcell(screen, screen->buffer + (size_t)row * screen->cols + col);
      /* The unused second half should also be clean. */
      for(int row = screen->rows; row < screen->rows * 2; row++)
        for(int col = 0; col < screen->cols; col++)
          clearcell(screen, screen->buffer + (size_t)row * screen->cols + col);
      screen->root_row[bufidx] = 0;
    }
    else {
      screen->root_row[bufidx] = new_root;
    }
    return 1;
  }

  int init_row, test_row, inc_row;
  if(downward < 0) {
    init_row = dest.end_row - 1;
    test_row = dest.start_row - 1;
    inc_row  = -1;
  }
  else {
    init_row = dest.start_row;
    test_row = dest.end_row;
    inc_row  = +1;
  }

  for(int row = init_row; row != test_row; row += inc_row)
    memmove(getcell(screen, row, dest.start_col),
            getcell(screen, row + downward, src.start_col),
            cols * sizeof(VTermScreenCell));

  return 1;
}

static int moverect_user(VTermRect dest, VTermRect src, void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->moverect) {
    if(screen->damage_merge != VTERM_DAMAGE_SCROLL)
      // Avoid an infinite loop
      vterm_screen_flush_damage(screen);

    if((*screen->callbacks->moverect)(dest, src, screen->cbdata))
      return 1;
  }

  damagerect(screen, dest);

  return 1;
}

/* yetty: modified for new cell format */
static int erase_internal(VTermRect rect, int selective, void *user)
{
  VTermScreen *screen = user;
  (void)selective; /* yetty: protected_cell not tracked in new format */

  VTermColor fg = screen->pen.fg;
  VTermColor bg = screen->pen.bg;
  if(screen->pen.reverse ^ screen->global_reverse) {
    VTermColor tmp = fg; fg = bg; bg = tmp;
  }

  for(int row = rect.start_row; row < screen->state->rows && row < rect.end_row; row++) {
    const VTermLineInfo *info = vterm_state_get_lineinfo(screen->state, row);
    /* yetty: TODO encode info->doublewidth, info->doubleheight if needed */
    (void)info;

    for(int col = rect.start_col; col < rect.end_col; col++) {
      VTermScreenCell *cell = getcell(screen, row, col);
      if(!cell) continue;

      cell->glyph_index = 0;
      cell->fg = fg;
      cell->bg = bg;
      cell->attrs.bold = screen->pen.bold;
      cell->attrs.underline = screen->pen.underline;
      cell->attrs.italic = screen->pen.italic;
      cell->attrs.blink = screen->pen.blink;
      cell->attrs.conceal = screen->pen.conceal;
      cell->attrs.strike = screen->pen.strike;
      cell->attrs.small = screen->pen.small;
      cell->attrs.baseline = screen->pen.baseline;
      cell->attrs.default_fg = 0;
      cell->attrs.default_bg = 0;
      cell->attrs.font_type = 0;
    }
  }

  return 1;
}

static int erase_user(VTermRect rect, int selective, void *user)
{
  VTermScreen *screen = user;

  damagerect(screen, rect);

  return 1;
}

static int erase(VTermRect rect, int selective, void *user)
{
  VTermScreen *screen = user;

  erase_internal(rect, selective, user);
  erase_user(rect, 0, user);

  /* yetty: surface erase to screen-level consumers (text-layer fans
   * this out to ydraw/ypaint, which can't infer erase from damage). */
  if(screen->callbacks && screen->callbacks->erase)
    (*screen->callbacks->erase)(rect, selective, screen->cbdata);

  return 1;
}

static int scrollrect(VTermRect rect, int downward, int rightward, void *user)
{
  VTermScreen *screen = user;

  if(screen->damage_merge != VTERM_DAMAGE_SCROLL) {
    vterm_scroll_rect(rect, downward, rightward,
        moverect_internal, erase_internal, screen);

    vterm_screen_flush_damage(screen);

    vterm_scroll_rect(rect, downward, rightward,
        moverect_user, erase_user, screen);

    return 1;
  }

  if(screen->damaged.start_row != -1 &&
     !rect_intersects(&rect, &screen->damaged)) {
    vterm_screen_flush_damage(screen);
  }

  if(screen->pending_scrollrect.start_row == -1) {
    screen->pending_scrollrect = rect;
    screen->pending_scroll_downward  = downward;
    screen->pending_scroll_rightward = rightward;
  }
  else if(rect_equal(&screen->pending_scrollrect, &rect) &&
     ((screen->pending_scroll_downward  == 0 && downward  == 0) ||
      (screen->pending_scroll_rightward == 0 && rightward == 0))) {
    screen->pending_scroll_downward  += downward;
    screen->pending_scroll_rightward += rightward;
  }
  else {
    vterm_screen_flush_damage(screen);

    screen->pending_scrollrect = rect;
    screen->pending_scroll_downward  = downward;
    screen->pending_scroll_rightward = rightward;
  }

  vterm_scroll_rect(rect, downward, rightward,
      moverect_internal, erase_internal, screen);

  if(screen->damaged.start_row == -1)
    return 1;

  if(rect_contains(&rect, &screen->damaged)) {
    /* Scroll region entirely contains the damage; just move it */
    vterm_rect_move(&screen->damaged, -downward, -rightward);
    rect_clip(&screen->damaged, &rect);
  }
  /* There are a number of possible cases here, but lets restrict this to only
   * the common case where we might actually gain some performance by
   * optimising it. Namely, a vertical scroll that neatly cuts the damage
   * region in half.
   */
  else if(rect.start_col <= screen->damaged.start_col &&
          rect.end_col   >= screen->damaged.end_col &&
          rightward == 0) {
    if(screen->damaged.start_row >= rect.start_row &&
       screen->damaged.start_row  < rect.end_row) {
      screen->damaged.start_row -= downward;
      if(screen->damaged.start_row < rect.start_row)
        screen->damaged.start_row = rect.start_row;
      if(screen->damaged.start_row > rect.end_row)
        screen->damaged.start_row = rect.end_row;
    }
    if(screen->damaged.end_row >= rect.start_row &&
       screen->damaged.end_row  < rect.end_row) {
      screen->damaged.end_row -= downward;
      if(screen->damaged.end_row < rect.start_row)
        screen->damaged.end_row = rect.start_row;
      if(screen->damaged.end_row > rect.end_row)
        screen->damaged.end_row = rect.end_row;
    }
  }
  else {
    DEBUG_LOG("TODO: Just flush and redo damaged=" STRFrect " rect=" STRFrect "\n",
        ARGSrect(screen->damaged), ARGSrect(rect));
  }

  return 1;
}

static int movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->movecursor)
    return (*screen->callbacks->movecursor)(pos, oldpos, visible, screen->cbdata);

  return 0;
}

static int setpenattr(VTermAttr attr, VTermValue *val, void *user)
{
  VTermScreen *screen = user;

  switch(attr) {
  case VTERM_ATTR_BOLD:
    screen->pen.bold = val->boolean;
    return 1;
  case VTERM_ATTR_UNDERLINE:
    screen->pen.underline = val->number;
    return 1;
  case VTERM_ATTR_ITALIC:
    screen->pen.italic = val->boolean;
    return 1;
  case VTERM_ATTR_BLINK:
    screen->pen.blink = val->boolean;
    return 1;
  case VTERM_ATTR_REVERSE:
    screen->pen.reverse = val->boolean;
    return 1;
  case VTERM_ATTR_CONCEAL:
    screen->pen.conceal = val->boolean;
    return 1;
  case VTERM_ATTR_STRIKE:
    screen->pen.strike = val->boolean;
    return 1;
  case VTERM_ATTR_FONT:
    screen->pen.font = val->number;
    return 1;
  case VTERM_ATTR_FOREGROUND:
    screen->pen.fg = val->color;
    return 1;
  case VTERM_ATTR_BACKGROUND:
    screen->pen.bg = val->color;
    return 1;
  case VTERM_ATTR_SMALL:
    screen->pen.small = val->boolean;
    return 1;
  case VTERM_ATTR_BASELINE:
    screen->pen.baseline = val->number;
    return 1;

  case VTERM_N_ATTRS:
    return 0;
  }

  return 0;
}

static int settermprop(VTermProp prop, VTermValue *val, void *user)
{
  VTermScreen *screen = user;

  switch(prop) {
  case VTERM_PROP_ALTSCREEN:
    if(val->boolean && !screen->buffers[BUFIDX_ALTSCREEN])
      return 0;

    screen->buffer = val->boolean ? screen->buffers[BUFIDX_ALTSCREEN] : screen->buffers[BUFIDX_PRIMARY];
    /* only send a damage event on disable; because during enable there's an
     * erase that sends a damage anyway
     */
    if(!val->boolean)
      damagescreen(screen);
    break;
  case VTERM_PROP_REVERSE:
    screen->global_reverse = val->boolean;
    damagescreen(screen);
    break;
  default:
    ; /* ignore */
  }

  if(screen->callbacks && screen->callbacks->settermprop)
    return (*screen->callbacks->settermprop)(prop, val, screen->cbdata);

  return 1;
}

static int bell(void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->bell)
    return (*screen->callbacks->bell)(screen->cbdata);

  return 0;
}

/* How many cells are non-blank
 * Returns the position of the first blank cell in the trailing blank end */
static int line_popcount(VTermScreenCell *buffer, int row, int rows, int cols)
{
  int col = cols - 1;
  while(col >= 0 && buffer[row * cols + col].glyph_index == 0)
    col--;
  return col + 1;
}

#define REFLOW (screen->reflow)

static void resize_buffer(VTermScreen *screen, int bufidx, int new_rows, int new_cols, bool active, VTermStateFields *statefields)
{
  int old_rows = screen->rows;
  int old_cols = screen->cols;

  VTermScreenCell *old_buffer = screen->buffers[bufidx];
  VTermLineInfo *old_lineinfo = statefields->lineinfos[bufidx];

  /* yetty: linearise the live rows down to offset 0 before the reflow
   * walks them. The reflow code uses flat row*cols indexing on old_buffer,
   * which only matches the live screen when root_row == 0. After the
   * linearise, root_row is reset to 0 for this bufidx; the new_buffer is
   * also allocated with root_row 0 (set after the buffer swap below). */
  if(old_buffer && screen->root_row[bufidx] != 0) {
    memmove(old_buffer,
            old_buffer + (size_t)screen->root_row[bufidx] * old_cols,
            (size_t)old_rows * old_cols * sizeof(VTermScreenCell));
    screen->root_row[bufidx] = 0;
  }

  /* Allocate the new buffer 2*rows tall to maintain the double-allocation
   * circular buffer invariant. The reflow code below only writes the first
   * new_rows rows; the second half is cleared explicitly so reads of it
   * (e.g. the GPU upload, which uploads the full 2*rows) see clean cells. */
  VTermScreenCell *new_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * new_rows * 2 * new_cols);
  VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);

  int old_row = old_rows - 1;
  int new_row = new_rows - 1;

  VTermPos old_cursor = statefields->pos;
  VTermPos new_cursor = { -1, -1 };

#ifdef DEBUG_REFLOW
  fprintf(stderr, "Resizing from %dx%d to %dx%d; cursor was at (%d,%d)\n",
      old_cols, old_rows, new_cols, new_rows, old_cursor.col, old_cursor.row);
#endif

  /* Keep track of the final row that is knonw to be blank, so we know what
   * spare space we have for scrolling into
   */
  int final_blank_row = new_rows;

  while(old_row >= 0) {
    int old_row_end = old_row;
    /* TODO: Stop if dwl or dhl */
    while(REFLOW && old_lineinfo && old_row >= 0 && old_lineinfo[old_row].continuation)
      old_row--;
    int old_row_start = old_row;

    int width = 0;
    for(int row = old_row_start; row <= old_row_end; row++) {
      if(REFLOW && row < (old_rows - 1) && old_lineinfo[row + 1].continuation)
        width += old_cols;
      else
        width += line_popcount(old_buffer, row, old_rows, old_cols);
    }

    if(final_blank_row == (new_row + 1) && width == 0)
      final_blank_row = new_row;

    int new_height = REFLOW
      ? width ? (width + new_cols - 1) / new_cols : 1
      : 1;

    int new_row_end = new_row;
    int new_row_start = new_row - new_height + 1;

    old_row = old_row_start;
    int old_col = 0;

    int spare_rows = new_rows - final_blank_row;

    if(new_row_start < 0 && /* we'd fall off the top */
        spare_rows >= 0 && /* we actually have spare rows */
        (!active || new_cursor.row == -1 || (new_cursor.row - new_row_start) < new_rows))
    {
      /* Attempt to scroll content down into the blank rows at the bottom to
       * make it fit
       */
      int downwards = -new_row_start;
      if(downwards > spare_rows)
        downwards = spare_rows;
      int rowcount = new_rows - downwards;

#ifdef DEBUG_REFLOW
      fprintf(stderr, "  scroll %d rows +%d downwards\n", rowcount, downwards);
#endif

      memmove(&new_buffer[downwards * new_cols], &new_buffer[0],   rowcount * new_cols * sizeof(VTermScreenCell));
      memmove(&new_lineinfo[downwards],          &new_lineinfo[0], rowcount            * sizeof(new_lineinfo[0]));

      new_row += downwards;
      new_row_start += downwards;
      new_row_end += downwards;

      if(new_cursor.row >= 0)
        new_cursor.row += downwards;

      final_blank_row += downwards;
    }

#ifdef DEBUG_REFLOW
    fprintf(stderr, "  rows [%d..%d] <- [%d..%d] width=%d\n",
        new_row_start, new_row_end, old_row_start, old_row_end, width);
#endif

    if(new_row_start < 0) {
      if(old_row_start <= old_cursor.row && old_cursor.row < old_row_end) {
        new_cursor.row = 0;
        new_cursor.col = old_cursor.col;
        if(new_cursor.col >= new_cols)
          new_cursor.col = new_cols-1;
      }
      break;
    }

    for(new_row = new_row_start, old_row = old_row_start; new_row <= new_row_end; new_row++) {
      int count = width >= new_cols ? new_cols : width;
      width -= count;

      int new_col = 0;

      while(count) {
        /* TODO: This could surely be done a lot faster by memcpy()'ing the entire range */
        new_buffer[new_row * new_cols + new_col] = old_buffer[old_row * old_cols + old_col];

        if(old_cursor.row == old_row && old_cursor.col == old_col)
          new_cursor.row = new_row, new_cursor.col = new_col;

        old_col++;
        if(old_col == old_cols) {
          old_row++;

          if(!REFLOW) {
            new_col++;
            break;
          }
          old_col = 0;
        }

        new_col++;
        count--;
      }

      if(old_cursor.row == old_row && old_cursor.col >= old_col) {
        new_cursor.row = new_row, new_cursor.col = (old_cursor.col - old_col + new_col);
        if(new_cursor.col >= new_cols)
          new_cursor.col = new_cols-1;
      }

      while(new_col < new_cols) {
        clearcell(screen, &new_buffer[new_row * new_cols + new_col]);
        new_col++;
      }

      new_lineinfo[new_row].continuation = (new_row > new_row_start);
    }

    old_row = old_row_start - 1;
    new_row = new_row_start - 1;
  }

  if(old_cursor.row <= old_row) {
    /* cursor would have moved entirely off the top of the screen; lets just
     * bring it within range */
    new_cursor.row = 0, new_cursor.col = old_cursor.col;
    if(new_cursor.col >= new_cols)
      new_cursor.col = new_cols-1;
  }

  /* We really expect the cursor position to be set by now */
  if(active && (new_cursor.row == -1 || new_cursor.col == -1)) {
    /* YETTY PATCH: Instead of aborting, fallback to a safe cursor position.
     * This can happen during rapid resize operations where the reflow logic
     * doesn't properly track the cursor through all edge cases.
     */
    fprintf(stderr, "screen_resize: cursor position not set, falling back to (0,0)\n");
    new_cursor.row = 0;
    new_cursor.col = 0;
  }

  if(old_row >= 0 && bufidx == BUFIDX_PRIMARY) {
    /* Push spare lines to scrollback buffer */
    if(screen->callbacks && screen->callbacks->sb_pushline)
      for(int row = 0; row <= old_row; row++)
        sb_pushline_from_row(screen, row);
    if(active)
      statefields->pos.row -= (old_row + 1);
  }
  if(new_row >= 0 && bufidx == BUFIDX_PRIMARY &&
      screen->callbacks && screen->callbacks->sb_popline) {
    /* Try to backfill rows by popping scrollback buffer */
    while(new_row >= 0) {
      if(!(screen->callbacks->sb_popline(old_cols, screen->sb_buffer, screen->cbdata)))
        break;

      VTermPos pos = { .row = new_row };
      for(pos.col = 0; pos.col < old_cols && pos.col < new_cols; pos.col++) {
        VTermScreenCell *src = &screen->sb_buffer[pos.col];
        VTermScreenCell *dst = &new_buffer[pos.row * new_cols + pos.col];
        *dst = *src;
      }
      for( ; pos.col < new_cols; pos.col++)
        clearcell(screen, &new_buffer[pos.row * new_cols + pos.col]);
      new_row--;

      if(active)
        statefields->pos.row++;
    }
  }
  if(new_row >= 0) {
    /* Scroll new rows back up to the top and fill in blanks at the bottom */
    int moverows = new_rows - new_row - 1;
    memmove(&new_buffer[0], &new_buffer[(new_row + 1) * new_cols], moverows * new_cols * sizeof(VTermScreenCell));
    memmove(&new_lineinfo[0], &new_lineinfo[new_row + 1], moverows * sizeof(new_lineinfo[0]));

    new_cursor.row -= (new_row + 1);

    for(new_row = moverows; new_row < new_rows; new_row++) {
      for(int col = 0; col < new_cols; col++)
        clearcell(screen, &new_buffer[new_row * new_cols + col]);
      new_lineinfo[new_row] = (VTermLineInfo){ 0 };
    }
  }

  /* yetty: clear the second half of the 2*rows allocation so reads (the
   * GPU upload uploads the full 2*new_rows) see clean cells. The first
   * new_rows are populated by the reflow code above. */
  for(int row = new_rows; row < new_rows * 2; row++)
    for(int col = 0; col < new_cols; col++)
      clearcell(screen, &new_buffer[(size_t)row * new_cols + col]);
  screen->root_row[bufidx] = 0;

  vterm_allocator_free(screen->vt, old_buffer);
  screen->buffers[bufidx] = new_buffer;

  vterm_allocator_free(screen->vt, old_lineinfo);
  statefields->lineinfos[bufidx] = new_lineinfo;

  if(active)
    statefields->pos = new_cursor;

  return;
}

static int resize(int new_rows, int new_cols, VTermStateFields *fields, void *user)
{
  VTermScreen *screen = user;

  int altscreen_active = (screen->buffers[BUFIDX_ALTSCREEN] && screen->buffer == screen->buffers[BUFIDX_ALTSCREEN]);

  int old_rows = screen->rows;
  int old_cols = screen->cols;

  if(new_cols > old_cols) {
    /* Ensure that ->sb_buffer is large enough for a new or and old row */
    if(screen->sb_buffer)
      vterm_allocator_free(screen->vt, screen->sb_buffer);

    screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * new_cols);
  }

  resize_buffer(screen, 0, new_rows, new_cols, !altscreen_active, fields);
  if(screen->buffers[BUFIDX_ALTSCREEN])
    resize_buffer(screen, 1, new_rows, new_cols, altscreen_active, fields);
  else if(new_rows != old_rows) {
    /* We don't need a full resize of the altscreen because it isn't enabled
     * but we should at least keep the lineinfo the right size */
    vterm_allocator_free(screen->vt, fields->lineinfos[BUFIDX_ALTSCREEN]);

    VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);
    for(int row = 0; row < new_rows; row++)
      new_lineinfo[row] = (VTermLineInfo){ 0 };

    fields->lineinfos[BUFIDX_ALTSCREEN] = new_lineinfo;
  }

  screen->buffer = altscreen_active ? screen->buffers[BUFIDX_ALTSCREEN] : screen->buffers[BUFIDX_PRIMARY];

  screen->rows = new_rows;
  screen->cols = new_cols;

  if(new_cols <= old_cols) {
    if(screen->sb_buffer)
      vterm_allocator_free(screen->vt, screen->sb_buffer);

    screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * new_cols);
  }

  /* TODO: Maaaaybe we can optimise this if there's no reflow happening */
  damagescreen(screen);

  if(screen->callbacks && screen->callbacks->resize)
    return (*screen->callbacks->resize)(new_rows, new_cols, screen->cbdata);

  return 1;
}

static int setlineinfo(int row, const VTermLineInfo *newinfo, const VTermLineInfo *oldinfo, void *user)
{
  VTermScreen *screen = user;

  /* yetty: dwl/dhl attrs removed, just handle damage and erase */
  if(newinfo->doublewidth != oldinfo->doublewidth ||
     newinfo->doubleheight != oldinfo->doubleheight) {
    VTermRect rect = {
      .start_row = row,
      .end_row   = row + 1,
      .start_col = 0,
      .end_col   = newinfo->doublewidth ? screen->cols / 2 : screen->cols,
    };
    damagerect(screen, rect);

    if(newinfo->doublewidth) {
      rect.start_col = screen->cols / 2;
      rect.end_col   = screen->cols;

      erase_internal(rect, 0, user);
    }
  }

  return 1;
}

static int sb_clear(void *user) {
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->sb_clear)
    if((*screen->callbacks->sb_clear)(screen->cbdata))
      return 1;

  return 0;
}

static VTermStateCallbacks state_cbs = {
  .putglyph    = &putglyph,
  .movecursor  = &movecursor,
  .scrollrect  = &scrollrect,
  .erase       = &erase,
  .setpenattr  = &setpenattr,
  .settermprop = &settermprop,
  .bell        = &bell,
  .resize      = &resize,
  .setlineinfo = &setlineinfo,
  .sb_clear    = &sb_clear,
};

static VTermScreen *screen_new(VTerm *vt)
{
  VTermState *state = vterm_obtain_state(vt);
  if(!state)
    return NULL;

  VTermScreen *screen = vterm_allocator_malloc(vt, sizeof(VTermScreen));
  int rows, cols;

  vterm_get_size(vt, &rows, &cols);

  screen->vt = vt;
  screen->state = state;

  screen->damage_merge = VTERM_DAMAGE_CELL;
  screen->damaged.start_row = -1;
  screen->pending_scrollrect.start_row = -1;

  screen->rows = rows;
  screen->cols = cols;

  screen->global_reverse = false;
  screen->reflow = false;

  screen->callbacks = NULL;
  screen->cbdata    = NULL;

  screen->buffers[BUFIDX_PRIMARY] = alloc_buffer(screen, rows, cols);
  screen->buffers[BUFIDX_ALTSCREEN] = NULL;
  screen->root_row[BUFIDX_PRIMARY] = 0;
  screen->root_row[BUFIDX_ALTSCREEN] = 0;

  screen->buffer = screen->buffers[BUFIDX_PRIMARY];

  screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * cols);

  vterm_state_set_callbacks(screen->state, &state_cbs, screen);

  return screen;
}

INTERNAL void vterm_screen_free(VTermScreen *screen)
{
  vterm_allocator_free(screen->vt, screen->buffers[BUFIDX_PRIMARY]);
  if(screen->buffers[BUFIDX_ALTSCREEN])
    vterm_allocator_free(screen->vt, screen->buffers[BUFIDX_ALTSCREEN]);

  vterm_allocator_free(screen->vt, screen->sb_buffer);

  vterm_allocator_free(screen->vt, screen);
}

void vterm_screen_reset(VTermScreen *screen, int hard)
{
  screen->damaged.start_row = -1;
  screen->pending_scrollrect.start_row = -1;
  vterm_state_reset(screen->state, hard);
  vterm_screen_flush_damage(screen);
}

static size_t _get_chars(const VTermScreen *screen, const int utf8, void *buffer, size_t len, const VTermRect rect)
{
  size_t outpos = 0;
  int padding = 0;

#define PUT(c)                                             \
  if(utf8) {                                               \
    size_t thislen = utf8_seqlen(c);                       \
    if(buffer && outpos + thislen <= len)                  \
      outpos += fill_utf8((c), (char *)buffer + outpos);   \
    else                                                   \
      outpos += thislen;                                   \
  }                                                        \
  else {                                                   \
    if(buffer && outpos + 1 <= len)                        \
      ((uint32_t*)buffer)[outpos++] = (c);                 \
    else                                                   \
      outpos++;                                            \
  }

  for(int row = rect.start_row; row < rect.end_row; row++) {
    for(int col = rect.start_col; col < rect.end_col; col++) {
      VTermScreenCell *cell = getcell(screen, row, col);

      if(cell->glyph_index == 0)
        padding++;
      else if(cell->glyph_index == GLYPH_WIDE_CONT)
        ;
      else {
        while(padding) {
          PUT(UNICODE_SPACE);
          padding--;
        }
        PUT(cell->glyph_index);
      }
    }

    if(row < rect.end_row - 1) {
      PUT(UNICODE_LINEFEED);
      padding = 0;
    }
  }

  return outpos;
}

size_t vterm_screen_get_chars(const VTermScreen *screen, uint32_t *chars, size_t len, const VTermRect rect)
{
  return _get_chars(screen, 0, chars, len, rect);
}

size_t vterm_screen_get_text(const VTermScreen *screen, char *str, size_t len, const VTermRect rect)
{
  return _get_chars(screen, 1, str, len, rect);
}

/* Copy internal to external representation of a screen cell */
/* yetty: buffer now stores VTermScreenCell directly, just copy */
int vterm_screen_get_cell(const VTermScreen *screen, VTermPos pos, VTermScreenCell *cell)
{
  VTermScreenCell *intcell = getcell(screen, pos.row, pos.col);
  if(!intcell)
    return 0;

  *cell = *intcell;
  return 1;
}

int vterm_screen_is_eol(const VTermScreen *screen, VTermPos pos)
{
  /* This cell is EOL if this and every cell to the right is empty */
  for(; pos.col < screen->cols; pos.col++) {
    VTermScreenCell *cell = getcell(screen, pos.row, pos.col);
    if(cell->glyph_index != 0)
      return 0;
  }

  return 1;
}

VTermScreen *vterm_obtain_screen(VTerm *vt, VTermGlyphResolver resolver, void *resolver_user)
{
  if(vt->screen)
    return vt->screen;

  VTermScreen *screen = screen_new(vt);
  screen->glyph_resolver = resolver;
  screen->resolver_user = resolver_user;
  vt->screen = screen;

  return screen;
}

void vterm_screen_enable_reflow(VTermScreen *screen, bool reflow)
{
  screen->reflow = reflow;
}

#undef vterm_screen_set_reflow
void vterm_screen_set_reflow(VTermScreen *screen, bool reflow)
{
  vterm_screen_enable_reflow(screen, reflow);
}

void vterm_screen_enable_altscreen(VTermScreen *screen, int altscreen)
{
  if(!screen->buffers[BUFIDX_ALTSCREEN] && altscreen) {
    int rows, cols;
    vterm_get_size(screen->vt, &rows, &cols);

    screen->buffers[BUFIDX_ALTSCREEN] = alloc_buffer(screen, rows, cols);
  }
}

void vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user)
{
  screen->callbacks = callbacks;
  screen->cbdata = user;
}

void *vterm_screen_get_cbdata(VTermScreen *screen)
{
  return screen->cbdata;
}

void vterm_screen_set_unrecognised_fallbacks(VTermScreen *screen, const VTermStateFallbacks *fallbacks, void *user)
{
  vterm_state_set_unrecognised_fallbacks(screen->state, fallbacks, user);
}

void *vterm_screen_get_unrecognised_fbdata(VTermScreen *screen)
{
  return vterm_state_get_unrecognised_fbdata(screen->state);
}

void vterm_screen_flush_damage(VTermScreen *screen)
{
  if(screen->pending_scrollrect.start_row != -1) {
    vterm_scroll_rect(screen->pending_scrollrect, screen->pending_scroll_downward, screen->pending_scroll_rightward,
        moverect_user, erase_user, screen);

    screen->pending_scrollrect.start_row = -1;
  }

  if(screen->damaged.start_row != -1) {
    if(screen->callbacks && screen->callbacks->damage)
      (*screen->callbacks->damage)(screen->damaged, screen->cbdata);

    screen->damaged.start_row = -1;
  }
}

void vterm_screen_set_damage_merge(VTermScreen *screen, VTermDamageSize size)
{
  vterm_screen_flush_damage(screen);
  screen->damage_merge = size;
}

/* yetty: use attrs field instead of pen; removed reverse and font */
static int attrs_differ(VTermAttrMask attrs, VTermScreenCell *a, VTermScreenCell *b)
{
  if((attrs & VTERM_ATTR_BOLD_MASK)       && (a->attrs.bold != b->attrs.bold))
    return 1;
  if((attrs & VTERM_ATTR_UNDERLINE_MASK)  && (a->attrs.underline != b->attrs.underline))
    return 1;
  if((attrs & VTERM_ATTR_ITALIC_MASK)     && (a->attrs.italic != b->attrs.italic))
    return 1;
  if((attrs & VTERM_ATTR_BLINK_MASK)      && (a->attrs.blink != b->attrs.blink))
    return 1;
  if((attrs & VTERM_ATTR_CONCEAL_MASK)    && (a->attrs.conceal != b->attrs.conceal))
    return 1;
  if((attrs & VTERM_ATTR_STRIKE_MASK)     && (a->attrs.strike != b->attrs.strike))
    return 1;
  if((attrs & VTERM_ATTR_SMALL_MASK)      && (a->attrs.small != b->attrs.small))
    return 1;
  if((attrs & VTERM_ATTR_BASELINE_MASK)   && (a->attrs.baseline != b->attrs.baseline))
    return 1;
  if((attrs & VTERM_ATTR_DEFAULT_FG_MASK) && (a->attrs.default_fg != b->attrs.default_fg))
    return 1;
  if((attrs & VTERM_ATTR_DEFAULT_BG_MASK) && (a->attrs.default_bg != b->attrs.default_bg))
    return 1;
  if((attrs & VTERM_ATTR_FONT_TYPE_MASK)  && (a->attrs.font_type != b->attrs.font_type))
    return 1;
  if((attrs & VTERM_ATTR_FOREGROUND_MASK) && !vterm_color_is_equal(&a->fg, &b->fg))
    return 1;
  if((attrs & VTERM_ATTR_BACKGROUND_MASK) && !vterm_color_is_equal(&a->bg, &b->bg))
    return 1;

  return 0;
}

int vterm_screen_get_attrs_extent(const VTermScreen *screen, VTermRect *extent, VTermPos pos, VTermAttrMask attrs)
{
  VTermScreenCell *target = getcell(screen, pos.row, pos.col);

  // TODO: bounds check
  extent->start_row = pos.row;
  extent->end_row   = pos.row + 1;

  if(extent->start_col < 0)
    extent->start_col = 0;
  if(extent->end_col < 0)
    extent->end_col = screen->cols;

  int col;

  for(col = pos.col - 1; col >= extent->start_col; col--)
    if(attrs_differ(attrs, target, getcell(screen, pos.row, col)))
      break;
  extent->start_col = col + 1;

  for(col = pos.col + 1; col < extent->end_col; col++)
    if(attrs_differ(attrs, target, getcell(screen, pos.row, col)))
      break;
  extent->end_col = col - 1;

  return 1;
}

/* yetty: removed vterm_screen_convert_color_to_rgb - colors are always RGB */

/* yetty: use attrs.default_fg/bg flags instead of VTermColor type field.
 * Walks the entire 2*rows allocation so the offscreen half stays consistent
 * if the live window later wraps into it. */
static void reset_default_colours(VTermScreen *screen, VTermScreenCell *buffer)
{
  for(int row = 0; row < screen->rows * 2; row++)
    for(int col = 0; col < screen->cols; col++) {
      VTermScreenCell *cell = &buffer[row * screen->cols + col];
      if(cell->attrs.default_fg)
        cell->fg = screen->pen.fg;
      if(cell->attrs.default_bg)
        cell->bg = screen->pen.bg;
    }
}

void vterm_screen_set_default_colors(VTermScreen *screen, const VTermColor *default_fg, const VTermColor *default_bg)
{
  vterm_state_set_default_colors(screen->state, default_fg, default_bg);

  if(default_fg)
    screen->pen.fg = *default_fg;

  if(default_bg)
    screen->pen.bg = *default_bg;

  reset_default_colours(screen, screen->buffers[0]);
  if(screen->buffers[1])
    reset_default_colours(screen, screen->buffers[1]);
}

const VTermScreenCell *vterm_screen_get_buffer(const VTermScreen *screen)
{
  return screen->buffer;
}

/* yetty: buffer is allocated 2*rows tall (double-allocation circular buffer).
 * Consumers that upload it to the GPU must use this size. The shader applies
 * the root_row offset (see vterm_screen_get_buffer_root_row) to find row 0
 * of the visible screen within this 2*rows allocation. */
size_t vterm_screen_get_buffer_size(const VTermScreen *screen)
{
  return (size_t)screen->rows * 2 * (size_t)screen->cols * sizeof(VTermScreenCell);
}

int vterm_screen_get_buffer_root_row(const VTermScreen *screen)
{
  return active_root_row(screen);
}

/* yetty: set cursor position directly and notify via callback */
void vterm_screen_set_cursorpos(VTermScreen *screen, VTermPos pos)
{
  if(!screen || !screen->state)
    return;

  VTermPos oldpos;
  vterm_state_get_cursorpos(screen->state, &oldpos);

  /* Clamp to screen bounds */
  if(pos.row < 0) pos.row = 0;
  if(pos.row >= screen->rows) pos.row = screen->rows - 1;
  if(pos.col < 0) pos.col = 0;
  if(pos.col >= screen->cols) pos.col = screen->cols - 1;

  /* Set position in state */
  screen->state->pos = pos;

  /* Notify via callback if position changed */
  if(pos.row != oldpos.row || pos.col != oldpos.col) {
    if(screen->callbacks && screen->callbacks->movecursor)
      (*screen->callbacks->movecursor)(pos, oldpos, 1, screen->cbdata);
  }
}

/* yetty: scroll screen content by N lines (positive = scroll up/content moves up) */
void vterm_screen_scroll_lines(VTermScreen *screen, int lines)
{
  if(!screen || lines == 0)
    return;

  VTermRect rect = {
    .start_row = 0,
    .end_row = screen->rows,
    .start_col = 0,
    .end_col = screen->cols,
  };

  scrollrect(rect, lines, 0, screen);
  vterm_screen_flush_damage(screen);
}
