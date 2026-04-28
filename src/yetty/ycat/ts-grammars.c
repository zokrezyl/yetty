/*
 * ts-grammars.c - tree-sitter grammar registry.
 *
 * Pairs each grammar name with (a) its language constructor (linked from the
 * corresponding ts-grammar-<name>.a static lib) and (b) the highlights.scm
 * query contents embedded at configure time via ts_queries_generated.h.
 * The latter is written by tools/ycat/CMakeLists.txt reading the
 * TS_QUERIES_DIR_<name> variables exported by tree-sitter.cmake.
 */

#include "ts-grammars.h"

#include <string.h>
#ifdef _WIN32
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#ifdef YETTY_YCAT_HAS_TREESITTER

extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_rust(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_java(void);
extern const TSLanguage *tree_sitter_bash(void);
extern const TSLanguage *tree_sitter_json(void);
extern const TSLanguage *tree_sitter_yaml(void);
extern const TSLanguage *tree_sitter_toml(void);
extern const TSLanguage *tree_sitter_html(void);
extern const TSLanguage *tree_sitter_xml(void);
extern const TSLanguage *tree_sitter_markdown(void);

/* Auto-generated: defines TS_QUERY_<name>[] for each grammar. */
#include "ts_queries_generated.h"

static const struct yetty_ycat_grammar grammars[] = {
    { "c",          tree_sitter_c,          TS_QUERY_c,          sizeof(TS_QUERY_c) - 1 },
    { "cpp",        tree_sitter_cpp,        TS_QUERY_cpp,        sizeof(TS_QUERY_cpp) - 1 },
    { "python",     tree_sitter_python,     TS_QUERY_python,     sizeof(TS_QUERY_python) - 1 },
    { "javascript", tree_sitter_javascript, TS_QUERY_javascript, sizeof(TS_QUERY_javascript) - 1 },
    { "typescript", tree_sitter_typescript, TS_QUERY_typescript, sizeof(TS_QUERY_typescript) - 1 },
    { "rust",       tree_sitter_rust,       TS_QUERY_rust,       sizeof(TS_QUERY_rust) - 1 },
    { "go",         tree_sitter_go,         TS_QUERY_go,         sizeof(TS_QUERY_go) - 1 },
    { "java",       tree_sitter_java,       TS_QUERY_java,       sizeof(TS_QUERY_java) - 1 },
    { "bash",       tree_sitter_bash,       TS_QUERY_bash,       sizeof(TS_QUERY_bash) - 1 },
    { "json",       tree_sitter_json,       TS_QUERY_json,       sizeof(TS_QUERY_json) - 1 },
    { "yaml",       tree_sitter_yaml,       TS_QUERY_yaml,       sizeof(TS_QUERY_yaml) - 1 },
    { "toml",       tree_sitter_toml,       TS_QUERY_toml,       sizeof(TS_QUERY_toml) - 1 },
    { "html",       tree_sitter_html,       TS_QUERY_html,       sizeof(TS_QUERY_html) - 1 },
    { "xml",        tree_sitter_xml,        TS_QUERY_xml,        sizeof(TS_QUERY_xml) - 1 },
    { "markdown",   tree_sitter_markdown,   TS_QUERY_markdown,   sizeof(TS_QUERY_markdown) - 1 },
};

const struct yetty_ycat_grammar *yetty_ycat_grammar_get(const char *name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof(grammars) / sizeof(grammars[0]); i++) {
        if (strcmp(grammars[i].name, name) == 0)
            return &grammars[i];
    }
    return NULL;
}

#else /* !YETTY_YCAT_HAS_TREESITTER */

const struct yetty_ycat_grammar *yetty_ycat_grammar_get(const char *name)
{
    (void)name;
    return NULL;
}

#endif

/*=============================================================================
 * MIME / extension → grammar name
 *===========================================================================*/

static int str_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

const char *yetty_ycat_grammar_from_mime(const char *mime)
{
    if (!mime)
        return NULL;
    if (str_eq(mime, "text/x-c") || str_eq(mime, "text/x-chdr"))
        return "c";
    if (str_eq(mime, "text/x-c++") || str_eq(mime, "text/x-c++hdr") ||
        str_eq(mime, "text/x-c++src"))
        return "cpp";
    if (str_eq(mime, "text/x-python") || str_eq(mime, "text/x-script.python"))
        return "python";
    if (str_eq(mime, "text/x-shellscript"))
        return "bash";
    if (str_eq(mime, "application/javascript") || str_eq(mime, "text/javascript"))
        return "javascript";
    if (str_eq(mime, "text/x-java") || str_eq(mime, "text/x-java-source"))
        return "java";
    if (str_eq(mime, "text/x-rustsrc"))
        return "rust";
    if (str_eq(mime, "text/x-go"))
        return "go";
    if (str_eq(mime, "application/json"))
        return "json";
    if (str_eq(mime, "text/x-yaml") || str_eq(mime, "text/yaml"))
        return "yaml";
    if (str_eq(mime, "text/x-toml"))
        return "toml";
    if (str_eq(mime, "application/typescript") || str_eq(mime, "text/x-typescript"))
        return "typescript";
    if (str_eq(mime, "text/html") || str_eq(mime, "application/xhtml+xml"))
        return "html";
    if (str_eq(mime, "text/xml") || str_eq(mime, "application/xml") ||
        str_eq(mime, "image/svg+xml"))
        return "xml";
    if (str_eq(mime, "text/markdown") || str_eq(mime, "text/x-markdown"))
        return "markdown";
    return NULL;
}

const char *yetty_ycat_grammar_from_extension(const char *ext)
{
    if (!ext)
        return NULL;
    if (*ext == '.')
        ext++;
    if (!*ext)
        return NULL;

    if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0)
        return "c";
    if (strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "cc") == 0 ||
        strcasecmp(ext, "cxx") == 0 || strcasecmp(ext, "hpp") == 0 ||
        strcasecmp(ext, "hxx") == 0 || strcasecmp(ext, "hh") == 0)
        return "cpp";
    if (strcasecmp(ext, "py") == 0 || strcasecmp(ext, "pyi") == 0)
        return "python";
    if (strcasecmp(ext, "js") == 0 || strcasecmp(ext, "mjs") == 0 ||
        strcasecmp(ext, "cjs") == 0 || strcasecmp(ext, "jsx") == 0)
        return "javascript";
    if (strcasecmp(ext, "ts") == 0 || strcasecmp(ext, "tsx") == 0)
        return "typescript";
    if (strcasecmp(ext, "rs") == 0)
        return "rust";
    if (strcasecmp(ext, "go") == 0)
        return "go";
    if (strcasecmp(ext, "java") == 0)
        return "java";
    if (strcasecmp(ext, "sh") == 0 || strcasecmp(ext, "bash") == 0 ||
        strcasecmp(ext, "zsh") == 0)
        return "bash";
    if (strcasecmp(ext, "json") == 0 || strcasecmp(ext, "jsonc") == 0)
        return "json";
    if (strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0)
        return "yaml";
    if (strcasecmp(ext, "toml") == 0)
        return "toml";
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0)
        return "html";
    if (strcasecmp(ext, "xml") == 0 || strcasecmp(ext, "svg") == 0)
        return "xml";
    if (strcasecmp(ext, "md") == 0 ||
	strcasecmp(ext, "markdown") == 0 ||
	strcasecmp(ext, "mdown") == 0 ||
	strcasecmp(ext, "mkd") == 0)
        return "markdown";
    return NULL;
}

/*=============================================================================
 * Public grammar-name resolver (for --ts)
 *===========================================================================*/

static const char *path_ext(const char *path)
{
    if (!path)
        return NULL;
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && slash > dot))
        return NULL;
    return dot;
}

const char *yetty_ycat_grammar_lookup(const char *mime, const char *path)
{
    const char *g = yetty_ycat_grammar_from_mime(mime);
    if (g)
        return g;
    return yetty_ycat_grammar_from_extension(path_ext(path));
}
