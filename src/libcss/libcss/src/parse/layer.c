/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>

#include "parse/layer.h"

/**
 * Pack a layer's hierarchical position into a single comparable key.
 *
 * Byte 0 (most significant) holds the root layer's declaration index, byte 1
 * the next nesting level, and so on. Bytes past the layer's depth are padded
 * 0xFF so that a shorter path (rules directly in a layer) compares GREATER
 * than a longer one (its sub-layers) — i.e. direct rules outrank sub-layer
 * rules. A larger key means higher cascade priority. Unlayered rules use the
 * reserved key 0 and are treated as the maximum by the cascade.
 */
static uint64_t layer_compute_key(struct css_layer_node *parent, uint32_t depth, uint32_t index)
{
    uint64_t base = (parent != NULL) ? parent->key : 0xFFFFFFFFFFFFFFFFull;
    uint32_t shift;

    if (depth > 7) {
        /* Deeper than the key can encode: reuse the parent's key. Sub-layers
		 * this deep can no longer be told apart, which no real sheet hits. */
        return base;
    }

    if (index > 254) {
        /* 0xFF is the pad byte; cap real indices below it. */
        index = 254;
    }

    shift = 8u * (7u - depth);
    base &= ~((uint64_t)0xFFu << shift);
    base |= (uint64_t)index << shift;
    return base;
}

css_error css__layer_registry_create(struct css_layer_registry **registry)
{
    struct css_layer_registry *reg;

    if (registry == NULL) {
        return CSS_BADPARM;
    }

    reg = calloc(1, sizeof(*reg));
    if (reg == NULL) {
        return CSS_NOMEM;
    }

    reg->next_root_index = 1;
    reg->refcount = 1;

    *registry = reg;
    return CSS_OK;
}

void css__layer_registry_ref(struct css_layer_registry *registry)
{
    if (registry != NULL) {
        registry->refcount++;
    }
}

static void layer_node_destroy(struct css_layer_node *node)
{
    struct css_layer_node *child = node->first_child;

    while (child != NULL) {
        struct css_layer_node *next = child->next_sibling;
        layer_node_destroy(child);
        child = next;
    }

    if (node->name != NULL) {
        lwc_string_unref(node->name);
    }

    free(node);
}

void css__layer_registry_unref(struct css_layer_registry *registry)
{
    struct css_layer_node *root;

    if (registry == NULL) {
        return;
    }

    if (--registry->refcount > 0) {
        return;
    }

    root = registry->first_root;
    while (root != NULL) {
        struct css_layer_node *next = root->next_sibling;
        layer_node_destroy(root);
        root = next;
    }

    free(registry);
}

struct css_layer_node *css__layer_intern_child(struct css_layer_registry *registry,
                                               struct css_layer_node *parent, lwc_string *name)
{
    struct css_layer_node **head;
    struct css_layer_node **tail;
    uint32_t *counter;
    uint32_t depth;
    struct css_layer_node *node;

    if (registry == NULL) {
        return NULL;
    }

    if (parent == NULL) {
        head = &registry->first_root;
        tail = &registry->last_root;
        counter = &registry->next_root_index;
        depth = 0;
    } else {
        head = &parent->first_child;
        tail = &parent->last_child;
        counter = &parent->next_child_index;
        depth = parent->depth + 1;
    }

    /* Named layers deduplicate among siblings (re-opening keeps the key). */
    if (name != NULL) {
        for (node = *head; node != NULL; node = node->next_sibling) {
            bool match = false;
            if (node->name != NULL &&
                lwc_string_isequal(node->name, name, &match) == lwc_error_ok && match) {
                return node;
            }
        }
    }

    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }

    node->name = (name != NULL) ? lwc_string_ref(name) : NULL;
    node->parent = parent;
    node->depth = depth;
    node->next_child_index = 1;
    node->key = layer_compute_key(parent, depth, (*counter)++);

    if (*tail == NULL) {
        *head = *tail = node;
    } else {
        (*tail)->next_sibling = node;
        *tail = node;
    }

    return node;
}

static struct css_layer_node *layer_find_by_key(struct css_layer_node *node, uint64_t key)
{
    for (; node != NULL; node = node->next_sibling) {
        struct css_layer_node *found;

        if (node->key == key) {
            return node;
        }

        found = layer_find_by_key(node->first_child, key);
        if (found != NULL) {
            return found;
        }
    }

    return NULL;
}

struct css_layer_node *css__layer_find_by_key(struct css_layer_registry *registry, uint64_t key)
{
    if (registry == NULL || key == 0) {
        return NULL;
    }

    return layer_find_by_key(registry->first_root, key);
}

/* ---- public API (opaque css_layer_registry) ---------------------------- */

css_error css_layer_registry_create(css_layer_registry **registry)
{
    return css__layer_registry_create(registry);
}

css_error css_layer_registry_destroy(css_layer_registry *registry)
{
    css__layer_registry_unref(registry);
    return CSS_OK;
}

css_error css_layer_registry_resolve(css_layer_registry *registry, const char *names,
                                     uint64_t base_layer, uint64_t *key)
{
    struct css_layer_node *parent = NULL;
    const char *cursor;

    if (key == NULL) {
        return CSS_BADPARM;
    }

    /* base_layer would let an @import inside a layer resolve relative to it;
	 * top-of-file @import (the only place @import is valid) is always at root,
	 * so path resolution starts at the registry root. */
    (void)base_layer;

    if (registry == NULL) {
        *key = 0;
        return CSS_OK;
    }

    if (names == NULL || *names == '\0') {
        /* @import ... layer;  (anonymous layer) */
        struct css_layer_node *node = css__layer_intern_child(registry, NULL, NULL);
        if (node == NULL) {
            return CSS_NOMEM;
        }
        *key = node->key;
        return CSS_OK;
    }

    cursor = names;
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t seglen;
        lwc_string *segment = NULL;
        struct css_layer_node *node;

        while (*cursor != '\0' && *cursor != '.') {
            cursor++;
        }
        seglen = (size_t)(cursor - start);

        if (seglen > 0) {
            if (lwc_intern_string(start, seglen, &segment) != lwc_error_ok) {
                return CSS_NOMEM;
            }
        }

        node = css__layer_intern_child(registry, parent, segment);
        if (segment != NULL) {
            lwc_string_unref(segment);
        }
        if (node == NULL) {
            return CSS_NOMEM;
        }

        parent = node;
        if (*cursor == '.') {
            cursor++;
        }
    }

    *key = (parent != NULL) ? parent->key : 0;
    return CSS_OK;
}
