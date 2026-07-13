#ifndef YETTY_YMSOFFICE_RELS_INTERNAL_H
#define YETTY_YMSOFFICE_RELS_INTERNAL_H

/*
 * rels-internal.h - OPC relationship part (.rels) parser shared by the
 * format parsers: maps relationship ids ("rId1") to target part paths.
 */

#include <stddef.h>
#include <stdint.h>

struct yetty_ymsoffice_rels_entry {
    char *relationship_id;
    char *target;
};

struct yetty_ymsoffice_rels {
    struct yetty_ymsoffice_rels_entry *entries;
    size_t count;
    size_t cap;
};

/* Parse a .rels part into `rels` (zero-initialized by the caller).
 * Returns 0 / -1; on failure the partial content is already freed. */
int yetty_ymsoffice_rels_parse(const uint8_t *xml, size_t len, struct yetty_ymsoffice_rels *rels);

void yetty_ymsoffice_rels_free(struct yetty_ymsoffice_rels *rels);

/* NULL when the id is unknown. */
const char *yetty_ymsoffice_rels_target(const struct yetty_ymsoffice_rels *rels,
                                        const char *relationship_id);

#endif /* YETTY_YMSOFFICE_RELS_INTERNAL_H */
