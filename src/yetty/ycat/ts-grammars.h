/*
 * ts-grammars.h - internal tree-sitter grammar registry.
 */

#pragma once

#include <stddef.h>

#ifdef YETTY_YCAT_HAS_TREESITTER
#include <tree_sitter/api.h>
#else
/* Forward declare — ops are all no-op without the library. */
typedef struct TSLanguage TSLanguage;
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycat_grammar {
    const char *name; /* "c", "python", ... */
    const TSLanguage *(*language_fn)(void);
    const char *highlights_scm; /* embedded at configure time */
    size_t highlights_scm_len;
};

/* Look up a grammar by name. NULL if not found or tree-sitter disabled. */
const struct yetty_ycat_grammar *yetty_ycat_grammar_get(const char *name);

/* MIME / extension → grammar name (or NULL). */
const char *yetty_ycat_grammar_from_mime(const char *mime);
const char *yetty_ycat_grammar_from_extension(const char *ext);

#ifdef __cplusplus
}
#endif
