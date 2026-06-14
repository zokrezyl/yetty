/* yetty_cpython — a libpython-free SUBSET of the CPython 3.14 parser.
 *
 * Turns Python 3.14 source text into an AST and emits an ast.dump()-style
 * rendering. The implementation links against libc only (no libpython). This
 * is a *subset* of Python: see src/cpython/README.md for the exact scope and
 * the Unicode-database limitations (\N{NAME} escapes, non-ASCII identifier
 * validation, non-Latin source encodings).
 *
 * This public header intentionally exposes no CPython internals (no PyObject,
 * no mod_ty) — only a plain C entry point.
 */
#ifndef YETTY_CPYTHON_H
#define YETTY_CPYTHON_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse UTF-8 `source` (a complete module) and write an ast.dump()-style tree
 * to `out` (defaults to stdout if NULL), followed by a newline.
 * `filename` is used only in error messages (may be NULL).
 * Returns 0 on success, 1 on syntax error (message via
 * yetty_cpython_last_error()), negative on out-of-memory. */
int yetty_cpython_parse_and_dump(const char *source, const char *filename, FILE *out);

/* Message for the most recent syntax error, or NULL if there was none. */
const char *yetty_cpython_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_CPYTHON_H */
