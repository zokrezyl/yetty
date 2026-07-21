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

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* True if `s` begins with http:// or https://. Defined in main.c. */
int ybrowser_looks_like_url(const char *s);

/* Read HTML from a file path, "-"/NULL for stdin, or an http(s):// URL
 * (fetched via curl). Returns a malloc'd buffer (caller frees) and writes
 * its length to *out_len; returns NULL on failure. When out_effective_url
 * is non-NULL it receives the post-redirect URL for a fetched URL (caller
 * frees), or NULL otherwise. When out_content_type is non-NULL it receives
 * the response Content-Type for a fetched URL (caller frees), or NULL for
 * local files / stdin. Defined in main.c. */
struct yetty_ybrowser_loader;
/* `loader` supplies the shared libcurl caches for http(s) URLs (see
 * yetty_ybrowser_loader_create); NULL still works, just without
 * connection reuse. */
char *ybrowser_slurp_file(struct yetty_ybrowser_loader *loader, const char *path, size_t *out_len,
                          char **out_effective_url, char **out_content_type);

/* ybrowser_slurp_file plus transfer cancellation for document navigations:
 * when cancel_generation is non-NULL, the transfer aborts as soon as the
 * pointed-to counter no longer equals `generation` (Stop / superseding
 * navigation). Local files read synchronously without cancellation. */
char *ybrowser_slurp_document(struct yetty_ybrowser_loader *loader, const char *path,
                              uint64_t generation, const _Atomic uint64_t *cancel_generation,
                              size_t *out_len, char **out_effective_url, char **out_content_type);

/* Fetch a document with an explicit HTTP method + body (form submission).
 * `method` is "POST"/etc. and `body`/`body_len` the form-encoded request body;
 * the request carries application/x-www-form-urlencoded and follows redirects.
 * Returns the response body (caller frees), NULL on failure. Defined in main.c. */
char *ybrowser_slurp_request(struct yetty_ybrowser_loader *loader, const char *url,
                             const char *method, const char *body, size_t body_len, size_t *out_len,
                             char **out_effective_url, char **out_content_type);

/* Canonicalize a local file path into a file:// URL for use as a document
 * base (relative hrefs then resolve against the file's directory). Returns
 * a malloc'd URL (caller frees), or NULL for "-", NULL, http(s) inputs and
 * unresolvable paths. Defined in main.c. */
char *ybrowser_local_file_url(const char *path);

/* True when the input is a standalone SVG document: Content-Type wins when
 * present, else the document element must be <svg> (an inline SVG icon
 * inside HTML does NOT count). Defined in browser-ui.c — one shared sniff
 * for the interactive shell and the one-shot path. */
int ybrowser_content_is_svg(const char *content_type, const unsigned char *data, size_t len);

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
