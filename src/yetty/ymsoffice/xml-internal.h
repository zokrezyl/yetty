#ifndef YETTY_YMSOFFICE_XML_INTERNAL_H
#define YETTY_YMSOFFICE_XML_INTERNAL_H

/*
 * xml-internal.h - shared yxml SAX walker for the OOXML part parsers.
 *
 * OOXML producers bind namespaces to stable prefixes (w:, a:, p:, r:) or use
 * a default namespace (SpreadsheetML). The walker therefore tracks LOCAL
 * element names (prefix stripped) on an ancestor stack — parsers match on
 * local names plus ancestor context. Attribute callbacks receive the raw
 * attribute name as written (prefix included) so relationship attributes
 * ("r:id") stay distinguishable from plain ones ("id");
 * yetty_ymsoffice_xml_local_name() strips the prefix when a parser wants
 * the local part.
 *
 * Callbacks return 0 to continue, -1 to abort the walk.
 */

#include <stddef.h>
#include <stdint.h>

enum {
    YETTY_YMSOFFICE_XML_MAX_DEPTH = 64,
    YETTY_YMSOFFICE_XML_NAME_MAX = 48, /* per-level stored local name, incl NUL */
};

struct yetty_ymsoffice_xml_walker {
    char stack[YETTY_YMSOFFICE_XML_MAX_DEPTH][YETTY_YMSOFFICE_XML_NAME_MAX];
    size_t depth; /* number of open elements */
};

struct yetty_ymsoffice_xml_callbacks {
    int (*element_start)(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                         const char *local_name);
    int (*element_end)(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                       const char *local_name);
    /* Fires once per attribute with the accumulated value; element_local is
     * the local name of the element carrying the attribute. */
    int (*attribute)(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                     const char *element_local, const char *attribute_name, const char *value);
    /* Character-data chunk inside the current element (1..4 bytes per call,
     * entities already decoded by yxml). */
    int (*text)(void *ctx, const struct yetty_ymsoffice_xml_walker *walker, const char *chunk,
                size_t chunk_len);
};

/* The local name of the element `levels_up` above the current one
 * (0 = current). Returns "" past the document root or beyond the stored
 * depth. */
const char *yetty_ymsoffice_xml_ancestor(const struct yetty_ymsoffice_xml_walker *walker,
                                         size_t levels_up);

/* 1 when `local_name` matches any open element. */
int yetty_ymsoffice_xml_has_ancestor(const struct yetty_ymsoffice_xml_walker *walker,
                                     const char *local_name);

/* Strip a namespace prefix: "w:val" → "val", "val" → "val". */
const char *yetty_ymsoffice_xml_local_name(const char *name);

/* Run the SAX walk over a whole XML part. Returns 0 on success, -1 on a
 * syntax error or when a callback aborted. */
int yetty_ymsoffice_xml_walk(const uint8_t *xml, size_t len,
                             const struct yetty_ymsoffice_xml_callbacks *callbacks, void *ctx);

/*=============================================================================
 * Growable text accumulator — for gathering character data / attribute
 * values across SAX chunks. Zero-init; free with the free function.
 *===========================================================================*/

struct yetty_ymsoffice_text_accum {
    char *data;
    size_t len;
    size_t cap;
};

/* Append a chunk. Returns 0 / -1 on allocation failure. */
int yetty_ymsoffice_text_accum_append(struct yetty_ymsoffice_text_accum *accum, const char *chunk,
                                      size_t chunk_len);
void yetty_ymsoffice_text_accum_reset(struct yetty_ymsoffice_text_accum *accum);
/* Always NUL-terminated; "" when empty. */
const char *yetty_ymsoffice_text_accum_cstr(const struct yetty_ymsoffice_text_accum *accum);
void yetty_ymsoffice_text_accum_free(struct yetty_ymsoffice_text_accum *accum);

#endif /* YETTY_YMSOFFICE_XML_INTERNAL_H */
