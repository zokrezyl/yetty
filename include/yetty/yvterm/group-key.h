/* Nested group-id path folding — shared by the grid (binding key) and the
 * terminal ingest (key composition) so both sides compute the SAME 64-bit key
 * for a given path. A group's identity is a path of local ids folded into 64
 * bits; there is no separate producer namespace — the top path segment is the
 * scope. */
#ifndef YETTY_YVTERM_GROUP_KEY_H
#define YETTY_YVTERM_GROUP_KEY_H

#include <stdint.h>

/* The root / empty path. */
#define YETTY_YVTERM_GROUP_KEY_ROOT 0ull

/* splitmix64 finalizer — spreads small sequential ids so a path like [1,2,3]
 * does not cluster in the low bits. */
static inline uint64_t yetty_yvterm_group_mix64(uint64_t value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

/* Extend a group path key by one child segment. Order/position/depth
 * sensitive (parent_key feeds every step), so [1,2] != [2,1], [1] != [1,2],
 * and [1] != [1,0]. Incremental: key(path + id) = fold(key(path), id), which
 * matches descend/ascend during ingest. */
static inline uint64_t yetty_yvterm_group_key_fold(uint64_t parent_key, uint32_t id)
{
    return yetty_yvterm_group_mix64(parent_key ^ yetty_yvterm_group_mix64((uint64_t)id));
}

#endif /* YETTY_YVTERM_GROUP_KEY_H */
