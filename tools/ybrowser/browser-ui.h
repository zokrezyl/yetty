/*
 * browser-ui.h — entry point for ybrowser's interactive ygui mode, plus
 * the two HTML-acquisition helpers shared with the legacy one-shot path.
 *
 * The one-shot path and these helpers live in main.c; the interactive
 * Chrome-like UI (tabbar + address bar + nav + scrollable page) lives in
 * browser-ui.c and reuses the same curl/file slurp so navigation behaves
 * identically to a piped one-shot render.
 */
#ifndef YETTY_TOOLS_YBROWSER_BROWSER_UI_H
#define YETTY_TOOLS_YBROWSER_BROWSER_UI_H

#include <stddef.h>

/* True if `s` begins with http:// or https://. Defined in main.c. */
int ybrowser_looks_like_url(const char *s);

/* Read HTML from a file path, "-"/NULL for stdin, or an http(s):// URL
 * (fetched via curl). Returns a malloc'd buffer (caller frees) and writes
 * its length to *out_len; returns NULL on failure. When out_effective_url
 * is non-NULL it receives the post-redirect URL for a fetched URL (caller
 * frees), or NULL otherwise. Defined in main.c. */
char *ybrowser_slurp_file(const char *path, size_t *out_len, char **out_effective_url);

/* Run the interactive Chrome-like browser UI as a ygui client of the host
 * yetty (OSC over stdout, input over stdin). `initial_url` is the optional
 * page to open in the first tab (NULL → a built-in start page).
 * `viewport_w`/`viewport_h` seed the pane pixel size; `font_size` is the
 * default page font. Returns a process exit code.
 *
 * NOTE: in this mode the host consumes the mouse wheel for its own
 * scrollback, so the page does not scroll — run standalone for scrolling. */
int ybrowser_ui_run(const char *initial_url, int viewport_w, int viewport_h, float font_size);

/* Run the interactive browser UI standalone — its own GPU window, no host
 * yetty. Local mouse/keyboard (including the wheel) drive the UI, so
 * scrolling, clicking and link navigation all work. Only available when
 * built with YETTY_YBROWSER_HAS_STANDALONE (Linux + WebGPU). */
/* `no_ui` hides the tab strip + address toolbar so the window is just the
 * page content (clean recordings / screenshots). */
int ybrowser_ui_run_standalone(const char *initial_url, float font_size, int no_ui, int argc,
                               char **argv);

#endif /* YETTY_TOOLS_YBROWSER_BROWSER_UI_H */
