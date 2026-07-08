/*
 * ybrowser-loader-test — exercises the shared network loader: the
 * request/response fetch API and the loader's resource cache.
 *
 * Needs the network (labelled `network` in CMake); self-skips with exit
 * code 77 when the first fetch fails so an offline CI lane doesn't go
 * red. The cache assertion is deterministic once the first fetch lands:
 * the second fetch of the same URL must be served from the cache, which
 * the load-profiler reports as a "cache-hit" line on stderr.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ybrowser/ybrowser.h>

#define TEST_URL "https://en.wikipedia.org/static/favicon/wikipedia.ico"

int main(void)
{
    /* Isolate the DISK cache tier: without this, an entry persisted by a
	 * previous run serves the FIRST fetch from disk and the hit count
	 * (and network expectations) drift run-over-run. */
    char disk_dir[] = "/tmp/ybrowser-loader-test-XXXXXX";
    if (mkdtemp(disk_dir)) {
        setenv("XDG_CACHE_HOME", disk_dir, 1);
    }

    /* Route the load profiler to a file so the cache-hit line is
	 * observable. Must happen before any fetch. */
    setenv("YBROWSER_PROFILE", "1", 1);
    const char *profile_path = "ybrowser-loader-test-profile.log";
    if (freopen(profile_path, "w", stderr) == NULL) {
        printf("SKIP: cannot redirect stderr\n");
        return 77;
    }

    struct yetty_ybrowser_loader_ptr_result loader_res = yetty_ybrowser_loader_create();
    if (YETTY_IS_ERR(loader_res)) {
        printf("FAIL: loader_create: %s\n", loader_res.error.msg);
        yetty_ycore_error_destroy(loader_res.error);
        return 1;
    }
    struct yetty_ybrowser_loader *loader = loader_res.value;

    struct yetty_ybrowser_request request = {
        .url = TEST_URL,
        .kind = YETTY_YBROWSER_REQUEST_IMAGE,
        .referer = "https://en.wikipedia.org/",
    };

    struct yetty_ybrowser_response first = {0};
    struct yetty_ycore_void_result fetch_res = yetty_ybrowser_fetch(loader, &request, &first);
    if (YETTY_IS_ERR(fetch_res)) {
        printf("FAIL: fetch: %s\n", fetch_res.error.msg);
        yetty_ycore_error_destroy(fetch_res.error);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 1;
    }
    if (!first.body || first.status < 200 || first.status >= 300) {
        printf("SKIP: network fetch failed (status=%ld) — offline?\n", first.status);
        yetty_ybrowser_response_dispose(&first);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 77;
    }

    struct yetty_ybrowser_response second = {0};
    fetch_res = yetty_ybrowser_fetch(loader, &request, &second);
    if (YETTY_IS_ERR(fetch_res)) {
        printf("FAIL: second fetch: %s\n", fetch_res.error.msg);
        yetty_ycore_error_destroy(fetch_res.error);
        yetty_ybrowser_response_dispose(&first);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 1;
    }

    int failures = 0;
    if (second.body == NULL || second.body_len != first.body_len ||
        memcmp(second.body, first.body, first.body_len) != 0) {
        printf("FAIL: cached body differs from the original (%zu vs %zu bytes)\n", second.body_len,
               first.body_len);
        failures++;
    }
    if (second.status != first.status) {
        printf("FAIL: cached status %ld differs from original %ld\n", second.status, first.status);
        failures++;
    }

    /* The profiler logs "HTTP cache-hit" for a cache-served response —
	 * exactly one is expected (the second fetch). */
    fflush(stderr);
    FILE *profile = fopen(profile_path, "r");
    int cache_hits = 0;
    if (profile) {
        char line[512];
        while (fgets(line, sizeof(line), profile)) {
            if (strstr(line, "cache-hit")) {
                cache_hits++;
            }
        }
        fclose(profile);
    }
    if (cache_hits != 1) {
        printf("FAIL: expected exactly 1 cache-hit, saw %d\n", cache_hits);
        failures++;
    }

    yetty_ybrowser_response_dispose(&first);
    yetty_ybrowser_response_dispose(&second);
    struct yetty_ycore_void_result destroy_res = yetty_ybrowser_loader_destroy(loader);
    if (YETTY_IS_ERR(destroy_res)) {
        printf("FAIL: loader_destroy: %s\n", destroy_res.error.msg);
        yetty_ycore_error_destroy(destroy_res.error);
        failures++;
    }
    remove(profile_path);

    if (failures == 0) {
        printf("PASS: fetch + cache round-trip (%zu bytes, 1 cache hit)\n", first.body_len);
        return 0;
    }
    return 1;
}
