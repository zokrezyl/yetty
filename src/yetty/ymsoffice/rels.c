/*
 * rels.c - OPC relationship part parser (see rels-internal.h).
 */

#include "rels-internal.h"

#include "xml-internal.h"

#include <stdlib.h>
#include <string.h>

struct rels_parse_ctx {
    struct yetty_ymsoffice_rels *rels;
    char *pending_relationship_id;
    char *pending_target;
};

static int rels_attribute(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                          const char *element_local, const char *attribute_name, const char *value)
{
    (void)walker;
    struct rels_parse_ctx *ctx = ctx_ptr;
    if (strcmp(element_local, "Relationship") != 0) {
        return 0;
    }
    if (strcmp(attribute_name, "Id") == 0) {
        free(ctx->pending_relationship_id);
        ctx->pending_relationship_id = strdup(value);
        return ctx->pending_relationship_id ? 0 : -1;
    }
    if (strcmp(attribute_name, "Target") == 0) {
        free(ctx->pending_target);
        ctx->pending_target = strdup(value);
        return ctx->pending_target ? 0 : -1;
    }
    return 0;
}

static int rels_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                            const char *local_name)
{
    (void)walker;
    struct rels_parse_ctx *ctx = ctx_ptr;
    if (strcmp(local_name, "Relationship") != 0) {
        return 0;
    }
    struct yetty_ymsoffice_rels *rels = ctx->rels;
    if (ctx->pending_relationship_id && ctx->pending_target) {
        if (rels->count == rels->cap) {
            size_t new_cap = rels->cap ? rels->cap * 2 : 8;
            struct yetty_ymsoffice_rels_entry *grown =
                realloc(rels->entries, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            rels->entries = grown;
            rels->cap = new_cap;
        }
        rels->entries[rels->count].relationship_id = ctx->pending_relationship_id;
        rels->entries[rels->count].target = ctx->pending_target;
        rels->count++;
        ctx->pending_relationship_id = NULL;
        ctx->pending_target = NULL;
        return 0;
    }
    free(ctx->pending_relationship_id);
    free(ctx->pending_target);
    ctx->pending_relationship_id = NULL;
    ctx->pending_target = NULL;
    return 0;
}

int yetty_ymsoffice_rels_parse(const uint8_t *xml, size_t len, struct yetty_ymsoffice_rels *rels)
{
    struct rels_parse_ctx ctx = {0};
    ctx.rels = rels;
    struct yetty_ymsoffice_xml_callbacks callbacks = {
        .attribute = rels_attribute,
        .element_end = rels_element_end,
    };
    int rc = yetty_ymsoffice_xml_walk(xml, len, &callbacks, &ctx);
    free(ctx.pending_relationship_id);
    free(ctx.pending_target);
    if (rc < 0) {
        yetty_ymsoffice_rels_free(rels);
    }
    return rc;
}

void yetty_ymsoffice_rels_free(struct yetty_ymsoffice_rels *rels)
{
    for (size_t i = 0; i < rels->count; i++) {
        free(rels->entries[i].relationship_id);
        free(rels->entries[i].target);
    }
    free(rels->entries);
    memset(rels, 0, sizeof(*rels));
}

const char *yetty_ymsoffice_rels_target(const struct yetty_ymsoffice_rels *rels,
                                        const char *relationship_id)
{
    for (size_t i = 0; i < rels->count; i++) {
        if (strcmp(rels->entries[i].relationship_id, relationship_id) == 0) {
            return rels->entries[i].target;
        }
    }
    return NULL;
}
