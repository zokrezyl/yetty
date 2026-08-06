/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_layer_h_
#define css_parse_layer_h_

#include <stdbool.h>
#include <stdint.h>

#include <libwapcaplet/libwapcaplet.h>

#include <libcss/errors.h>
#include <libcss/stylesheet.h>

/**
 * One node in the cascade-layer tree. Each distinct layer (a name path such
 * as `framework` or `framework.base`, or an anonymous @layer{}) is a node.
 * Sibling order is declaration order; the packed key encodes the full path.
 */
struct css_layer_node {
    lwc_string *name; /**< segment name; NULL for an anonymous layer */
    struct css_layer_node *parent;
    struct css_layer_node *first_child;
    struct css_layer_node *last_child;
    struct css_layer_node *next_sibling;
    uint32_t next_child_index; /**< next sibling index to hand a child (1-based) */
    uint32_t depth;            /**< 0 == a root-level layer */
    uint64_t key;              /**< packed hierarchical sort key */
};

/**
 * The document-wide layer tree. Shared across all author sheets of a document
 * (ref-counted) so a layer name maps to one key everywhere.
 */
struct css_layer_registry {
    struct css_layer_node *first_root;
    struct css_layer_node *last_root;
    uint32_t next_root_index;
    int refcount;
};

css_error css__layer_registry_create(struct css_layer_registry **registry);
void css__layer_registry_ref(struct css_layer_registry *registry);
void css__layer_registry_unref(struct css_layer_registry *registry);

/**
 * Intern a single named (or anonymous) child of \a parent (NULL == a
 * root-level layer). Named layers are deduplicated among their siblings so a
 * re-opened layer keeps its key; anonymous layers always create a fresh node.
 * Returns NULL only on allocation failure.
 */
struct css_layer_node *css__layer_intern_child(struct css_layer_registry *registry,
                                               struct css_layer_node *parent, lwc_string *name);

/**
 * Find the node with the given packed key (as stamped on a rule / carried as a
 * sheet's base layer), or NULL. Lets a sheet imported via
 * `@import ... layer(x)` recover x's node so nested @layer blocks resolve
 * under it rather than at the root.
 */
struct css_layer_node *css__layer_find_by_key(struct css_layer_registry *registry, uint64_t key);

#endif
