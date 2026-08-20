/*
 * ymux content-addressed resource store (#695) — headless, no GPU, no yvterm.
 * Dedup identical payloads by content hash (one stored copy), distinct payloads
 * get distinct hashes, refcount eviction frees only when the last reference
 * goes, and a hash collision can never alias distinct content (hash + full byte
 * compare).
 */
#include <string.h>

#include "ytest.h"

#include "../../../src/yetty/ymux/resource.h"

static void test_resource_dedup(struct ytest *test)
{
    struct yetty_ymux_resource_store store;
    yetty_ymux_resource_store_init(&store);

    const uint8_t payload_a[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t payload_b[] = {9, 9, 9, 9};

    uint64_t hash_a = 0, hash_a2 = 0, hash_b = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_resource_add(&store, payload_a, sizeof(payload_a), &hash_a));
    YTEST_REQUIRE_OK(test, yetty_ymux_resource_add(&store, payload_b, sizeof(payload_b), &hash_b));
    YTEST_CHECK(test, hash_a != hash_b); /* distinct content -> distinct hash */
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_resource_count(&store), 2);

    /* Re-adding identical content dedups: same hash, no new copy, refcount++. */
    YTEST_REQUIRE_OK(test, yetty_ymux_resource_add(&store, payload_a, sizeof(payload_a), &hash_a2));
    YTEST_CHECK(test, hash_a2 == hash_a);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_resource_count(&store), 2); /* still 2, deduped */

    /* Retrieval returns the exact bytes. */
    size_t got_len = 0;
    const uint8_t *got = yetty_ymux_resource_get(&store, hash_a, &got_len);
    YTEST_REQUIRE_NOT_NULL(test, got);
    YTEST_CHECK_EQ_SIZE(test, got_len, sizeof(payload_a));
    YTEST_CHECK(test, memcmp(got, payload_a, sizeof(payload_a)) == 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_resource_has(&store, hash_b), 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_resource_has(&store, 0xdeadbeefu), 0);

    /* Refcount eviction: payload_a had 2 refs, so one release keeps it. */
    yetty_ymux_resource_release(&store, hash_a);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_resource_count(&store), 2);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_resource_has(&store, hash_a), 1);
    /* The last reference releases it; payload_b is untouched. */
    yetty_ymux_resource_release(&store, hash_a);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_resource_count(&store), 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_resource_has(&store, hash_a), 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_resource_has(&store, hash_b), 1);

    yetty_ymux_resource_store_free(&store);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_resource");
    YTEST_RUN(&test, test_resource_dedup);
    return ytest_end(&test);
}
