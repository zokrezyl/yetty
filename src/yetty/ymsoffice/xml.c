/*
 * xml.c - shared yxml SAX walker (see xml-internal.h).
 */

#include "xml-internal.h"

#include <stdlib.h>
#include <string.h>

#include <yxml.h>

/* yxml spills element/attribute names into this workspace; OOXML nesting is
 * shallow but names stack up, so keep a comfortable margin. */
enum {
    XML_YXML_BUFSIZE = 16384,
    XML_ATTR_VALUE_INITIAL_CAP = 64,
};

const char *yetty_ymsoffice_xml_local_name(const char *name)
{
    const char *colon = strchr(name, ':');
    return colon ? colon + 1 : name;
}

const char *yetty_ymsoffice_xml_ancestor(const struct yetty_ymsoffice_xml_walker *walker,
                                         size_t levels_up)
{
    if (levels_up >= walker->depth || walker->depth - levels_up > YETTY_YMSOFFICE_XML_MAX_DEPTH) {
        return "";
    }
    return walker->stack[walker->depth - 1 - levels_up];
}

int yetty_ymsoffice_xml_has_ancestor(const struct yetty_ymsoffice_xml_walker *walker,
                                     const char *local_name)
{
    size_t stored = walker->depth < YETTY_YMSOFFICE_XML_MAX_DEPTH ? walker->depth
                                                                  : YETTY_YMSOFFICE_XML_MAX_DEPTH;
    for (size_t i = 0; i < stored; i++) {
        if (strcmp(walker->stack[i], local_name) == 0) {
            return 1;
        }
    }
    return 0;
}

struct xml_walk_state {
    struct yetty_ymsoffice_xml_walker walker;
    const struct yetty_ymsoffice_xml_callbacks *callbacks;
    void *ctx;
    /* attribute value accumulation */
    char attribute_name[YETTY_YMSOFFICE_XML_NAME_MAX];
    char *attribute_value;
    size_t attribute_value_len;
    size_t attribute_value_cap;
};

static void xml_stack_push(struct yetty_ymsoffice_xml_walker *walker, const char *full_name)
{
    if (walker->depth < YETTY_YMSOFFICE_XML_MAX_DEPTH) {
        const char *local = yetty_ymsoffice_xml_local_name(full_name);
        size_t len = strlen(local);
        if (len >= YETTY_YMSOFFICE_XML_NAME_MAX) {
            len = YETTY_YMSOFFICE_XML_NAME_MAX - 1;
        }
        memcpy(walker->stack[walker->depth], local, len);
        walker->stack[walker->depth][len] = '\0';
    }
    walker->depth++;
}

static int xml_attr_value_append(struct xml_walk_state *state, const char *chunk)
{
    size_t chunk_len = strlen(chunk);
    if (state->attribute_value_len + chunk_len + 1 > state->attribute_value_cap) {
        size_t new_cap = state->attribute_value_cap ? state->attribute_value_cap * 2
                                                    : XML_ATTR_VALUE_INITIAL_CAP;
        while (new_cap < state->attribute_value_len + chunk_len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(state->attribute_value, new_cap);
        if (!grown) {
            return -1;
        }
        state->attribute_value = grown;
        state->attribute_value_cap = new_cap;
    }
    memcpy(state->attribute_value + state->attribute_value_len, chunk, chunk_len);
    state->attribute_value_len += chunk_len;
    state->attribute_value[state->attribute_value_len] = '\0';
    return 0;
}

static int xml_dispatch(struct xml_walk_state *state, yxml_t *parser, yxml_ret_t event)
{
    const struct yetty_ymsoffice_xml_callbacks *callbacks = state->callbacks;
    struct yetty_ymsoffice_xml_walker *walker = &state->walker;

    switch (event) {
    case YXML_ELEMSTART:
        xml_stack_push(walker, parser->elem);
        if (callbacks->element_start &&
            callbacks->element_start(state->ctx, walker, yetty_ymsoffice_xml_ancestor(walker, 0)) <
                0) {
            return -1;
        }
        break;
    case YXML_ELEMEND: {
        if (walker->depth == 0) {
            return -1; /* underflow — yxml would have flagged it, be safe */
        }
        const char *closing = yetty_ymsoffice_xml_ancestor(walker, 0);
        if (callbacks->element_end && callbacks->element_end(state->ctx, walker, closing) < 0) {
            return -1;
        }
        walker->depth--;
        break;
    }
    case YXML_ATTRSTART: {
        const char *name = parser->attr;
        size_t len = strlen(name);
        if (len >= sizeof(state->attribute_name)) {
            len = sizeof(state->attribute_name) - 1;
        }
        memcpy(state->attribute_name, name, len);
        state->attribute_name[len] = '\0';
        state->attribute_value_len = 0;
        if (state->attribute_value) {
            state->attribute_value[0] = '\0';
        }
        break;
    }
    case YXML_ATTRVAL:
        if (xml_attr_value_append(state, parser->data) < 0) {
            return -1;
        }
        break;
    case YXML_ATTREND:
        if (callbacks->attribute &&
            callbacks->attribute(state->ctx, walker, yetty_ymsoffice_xml_ancestor(walker, 0),
                                 state->attribute_name,
                                 state->attribute_value ? state->attribute_value : "") < 0) {
            return -1;
        }
        break;
    case YXML_CONTENT:
        if (callbacks->text &&
            callbacks->text(state->ctx, walker, parser->data, strlen(parser->data)) < 0) {
            return -1;
        }
        break;
    case YXML_OK:
    case YXML_PISTART:
    case YXML_PICONTENT:
    case YXML_PIEND:
    default:
        break;
    }
    return 0;
}

int yetty_ymsoffice_text_accum_append(struct yetty_ymsoffice_text_accum *accum, const char *chunk,
                                      size_t chunk_len)
{
    if (accum->len + chunk_len + 1 > accum->cap) {
        size_t new_cap = accum->cap ? accum->cap * 2 : XML_ATTR_VALUE_INITIAL_CAP;
        while (new_cap < accum->len + chunk_len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(accum->data, new_cap);
        if (!grown) {
            return -1;
        }
        accum->data = grown;
        accum->cap = new_cap;
    }
    memcpy(accum->data + accum->len, chunk, chunk_len);
    accum->len += chunk_len;
    accum->data[accum->len] = '\0';
    return 0;
}

void yetty_ymsoffice_text_accum_reset(struct yetty_ymsoffice_text_accum *accum)
{
    accum->len = 0;
    if (accum->data) {
        accum->data[0] = '\0';
    }
}

const char *yetty_ymsoffice_text_accum_cstr(const struct yetty_ymsoffice_text_accum *accum)
{
    return accum->data ? accum->data : "";
}

void yetty_ymsoffice_text_accum_free(struct yetty_ymsoffice_text_accum *accum)
{
    free(accum->data);
    accum->data = NULL;
    accum->len = 0;
    accum->cap = 0;
}

int yetty_ymsoffice_xml_walk(const uint8_t *xml, size_t len,
                             const struct yetty_ymsoffice_xml_callbacks *callbacks, void *ctx)
{
    if (!xml || !callbacks) {
        return -1;
    }

    void *yxml_workspace = malloc(XML_YXML_BUFSIZE);
    if (!yxml_workspace) {
        return -1;
    }
    yxml_t parser;
    yxml_init(&parser, yxml_workspace, XML_YXML_BUFSIZE);

    struct xml_walk_state state = {0};
    state.callbacks = callbacks;
    state.ctx = ctx;

    int rc = 0;
    for (size_t i = 0; i < len; i++) {
        yxml_ret_t event = yxml_parse(&parser, xml[i]);
        if (event < 0) {
            rc = -1;
            break;
        }
        if (xml_dispatch(&state, &parser, event) < 0) {
            rc = -1;
            break;
        }
    }
    if (rc == 0 && yxml_eof(&parser) < 0) {
        rc = -1;
    }

    free(state.attribute_value);
    free(yxml_workspace);
    return rc;
}
