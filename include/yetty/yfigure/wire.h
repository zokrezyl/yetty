/*
 * yfigure wire format — record header + figure-kind tokens.
 *
 * A body buffer addressed to a single child figure is a sequence of
 * records. Each record:
 *
 *   u32 length     bytes of payload that follow this header
 *   u32 id         parent-scoped figure id
 *   bytes payload[length]
 *
 * Length-first so a pipeline linter / proxy can walk and validate
 * without knowing ids or figure semantics. The container's figure tree
 * is driven by the typed yclass mutation slots (create_child /
 * set_child_rect / apply_child_body / …), not by a record stream.
 */
#ifndef YETTY_YFIGURE_WIRE_H
#define YETTY_YFIGURE_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Top-level record header. */
struct yetty_yfigure_header {
    uint32_t length;
    uint32_t id;
};

/* Figure kinds are NOT a central enum. Each figure-kind module registers its
 * factory under yetty_yfigure_kind_token("<name>") (a hash of the kind name)
 * and the producer mints by passing the same token to create_child's `kind`
 * argument. Adding a kind touches no shared header — see
 * <yetty/yfigure/registry.h>. */

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_WIRE_H */
