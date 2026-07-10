/*
 * vterm_grid — render a terminal byte stream into a FAITHFUL fixed-width grid.
 *
 * libvterm's bundled `unterm` prints empty cells as zero-width (nothing), so a
 * glyph at column 42 with blanks before it collapses to look like column 4 —
 * useless for asserting layout/cursor positioning. This dumps a true rows x cols
 * grid: one character per cell, a space for every blank cell, every line padded
 * to exactly `cols` columns. That makes column positions (pane dividers, status
 * bar fields, cursor-addressed text) measurable.
 *
 *   vterm_grid [-c COLS] [-l ROWS] [SCRIPTFILE]   (stdin if no file)
 *
 * Built by vterm_render.mjs straight from the libvterm sources (no libtool).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "vterm.h"

static int encode_utf8(unsigned long codepoint, char *out)
{
  if (codepoint < 0x80) { out[0] = codepoint; return 1; }
  if (codepoint < 0x800) {
    out[0] = 0xC0 | (codepoint >> 6);
    out[1] = 0x80 | (codepoint & 0x3F);
    return 2;
  }
  if (codepoint < 0x10000) {
    out[0] = 0xE0 | (codepoint >> 12);
    out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
    out[2] = 0x80 | (codepoint & 0x3F);
    return 3;
  }
  out[0] = 0xF0 | (codepoint >> 18);
  out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
  out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
  out[3] = 0x80 | (codepoint & 0x3F);
  return 4;
}

int main(int argc, char *argv[])
{
  int rows = 24, cols = 80, opt;
  while ((opt = getopt(argc, argv, "l:c:")) != -1) {
    if (opt == 'l') rows = atoi(optarg);
    else if (opt == 'c') cols = atoi(optarg);
  }
  if (rows <= 0) rows = 24;
  if (cols <= 0) cols = 80;

  const char *file = optind < argc ? argv[optind] : NULL;
  int fd = file ? open(file, O_RDONLY) : 0;
  if (fd < 0) { fprintf(stderr, "vterm_grid: cannot open %s: %s\n", file, strerror(errno)); return 1; }

  VTerm *vt = vterm_new(rows, cols);
  vterm_set_utf8(vt, 1);
  VTermScreen *vts = vterm_obtain_screen(vt);
  vterm_screen_reset(vts, 1);

  char buf[4096];
  int len;
  while ((len = read(fd, buf, sizeof(buf))) > 0)
    vterm_input_write(vt, buf, len);

  for (int row = 0; row < rows; row++) {
    int col = 0;
    while (col < cols) {
      VTermPos pos = { .row = row, .col = col };
      VTermScreenCell cell;
      vterm_screen_get_cell(vts, pos, &cell);
      int width = cell.width < 1 ? 1 : cell.width;
      if (cell.chars[0] == 0) {
        fputc(' ', stdout);
        col += 1;
      } else {
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) {
          char bytes[4];
          int n = encode_utf8(cell.chars[i], bytes);
          fwrite(bytes, 1, n, stdout);
        }
        /* A wide glyph occupies `width` columns but is one (multi-byte) glyph;
         * pad the extra columns with spaces so column math stays 1:1 for the
         * common single-width UI (status bar, borders, latin text). */
        for (int k = 1; k < width; k++) fputc(' ', stdout);
        col += width;
      }
    }
    fputc('\n', stdout);
  }

  if (file) close(fd);
  vterm_free(vt);
  return 0;
}
