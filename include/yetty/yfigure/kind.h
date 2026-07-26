/*
 * yetty_yfigure kind token — figure-kind name → wire token.
 *
 * This is the CLIENT-SAFE half of the figure registry: a producer needs a
 * kind token to build a CREATE_CHILD (it sends yetty_yfigure_kind_token("<name>")
 * and the host mints the matching factory), but it must NOT drag in the host
 * registry API — which pulls the GPU context (<yetty/yetty/yetty.h>) for its
 * factory signatures. Splitting the token out lets a pure wire client compute
 * it by including THIS header alone: no GPU includes, no link dependency on
 * yetty_yfigure.
 *
 * The host-side registry (create/register/mint + the factory typedef) lives in
 * <yetty/yfigure/registry.h>, which re-includes this header so its own
 * consumers keep seeing the token unchanged.
 */
#ifndef YETTY_YFIGURE_KIND_H
#define YETTY_YFIGURE_KIND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map a figure-kind name to its registry token. The token is the key the
 * registry stores factories under and the value carried (derived from the
 * self-describing name) on a CREATE_CHILD; there is no central enum of kinds.
 * A figure-kind module registers under yetty_yfigure_kind_token("<name>") and
 * the producer mints by sending the same "<name>" — both sides agree purely on
 * the string, so adding a kind touches no shared header.
 *
 * static inline (FNV-1a over the name bytes): a pure, dependency-free hash, so
 * any producer can compute a token by including this header alone — it needs no
 * link dependency on yetty_yfigure. */
static inline uint32_t yetty_yfigure_kind_token_n(const char *name, size_t name_len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        hash ^= (uint32_t)(uint8_t)name[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline uint32_t yetty_yfigure_kind_token(const char *name)
{
    size_t name_len = 0;
    if (name) {
        while (name[name_len] != '\0') {
            name_len++;
        }
    }
    return yetty_yfigure_kind_token_n(name, name_len);
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_KIND_H */
