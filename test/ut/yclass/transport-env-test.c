/*
 * yclass transport endpoint-selection contract test (#438).
 *
 * yetty_yclass_transport_pty_create_from_env picks the connection's byte
 * transport at attach time: a dedicated side-channel fd pair when the host
 * advertised one via YETTY_YWIRE_SIDE_CHANNEL, else the in-band fallback fds.
 * A set-but-broken variable must fail loudly (stale env inherited from an
 * unrelated parent), never silently degrade to in-band.
 */

#include <yetty/yclass/transport-pty.h>
#include <yetty/ycore/result.h>

#include "ytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void destroy_transport(struct yetty_yclass_transport_pty *transport)
{
    struct yetty_ycore_void_result res = yetty_yclass_transport_pty_destroy(transport);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
}

static void test_env_unset_falls_back(struct ytest *test)
{
    unsetenv(YETTY_YWIRE_SIDE_CHANNEL_ENV);
    int fallback[2];
    YTEST_REQUIRE(test, pipe(fallback) == 0);

    struct yetty_yclass_transport_pty_ptr_result res =
        yetty_yclass_transport_pty_create_from_env(fallback[0], fallback[1]);
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_INT(test, yetty_yclass_transport_pty_fd(res.value), fallback[0]);
    YTEST_CHECK_EQ_INT(test, yetty_yclass_transport_pty_out_fd(res.value), fallback[1]);
    destroy_transport(res.value);
    close(fallback[0]);
    close(fallback[1]);
}

static void test_env_selects_side_channel(struct ytest *test)
{
    int side[2];
    int fallback[2];
    YTEST_REQUIRE(test, pipe(side) == 0);
    YTEST_REQUIRE(test, pipe(fallback) == 0);

    char spec[32];
    snprintf(spec, sizeof(spec), "%d,%d", side[0], side[1]);
    YTEST_REQUIRE(test, setenv(YETTY_YWIRE_SIDE_CHANNEL_ENV, spec, 1) == 0);

    struct yetty_yclass_transport_pty_ptr_result res =
        yetty_yclass_transport_pty_create_from_env(fallback[0], fallback[1]);
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_INT(test, yetty_yclass_transport_pty_fd(res.value), side[0]);
    YTEST_CHECK_EQ_INT(test, yetty_yclass_transport_pty_out_fd(res.value), side[1]);
    destroy_transport(res.value);

    unsetenv(YETTY_YWIRE_SIDE_CHANNEL_ENV);
    close(side[0]);
    close(side[1]);
    close(fallback[0]);
    close(fallback[1]);
}

static void test_env_malformed_fails(struct ytest *test)
{
    static const char *bad_specs[] = {"nonsense", "3", "3,", ",4", "3,4,5", "3;4", "-1,2"};
    for (size_t i = 0; i < sizeof(bad_specs) / sizeof(bad_specs[0]); i++) {
        YTEST_REQUIRE(test, setenv(YETTY_YWIRE_SIDE_CHANNEL_ENV, bad_specs[i], 1) == 0);
        struct yetty_yclass_transport_pty_ptr_result res =
            yetty_yclass_transport_pty_create_from_env(STDIN_FILENO, STDOUT_FILENO);
        YTEST_CHECK(test, YETTY_IS_ERR(res));
        if (YETTY_IS_ERR(res)) {
            yetty_ycore_error_destroy(res.error);
        } else {
            destroy_transport(res.value);
        }
    }
    unsetenv(YETTY_YWIRE_SIDE_CHANNEL_ENV);
}

static void test_env_stale_fds_fail(struct ytest *test)
{
    /* Open a pipe, remember the numbers, close it — the spec then names fds
     * that are not open in this process (the stale-inheritance case). */
    int stale[2];
    YTEST_REQUIRE(test, pipe(stale) == 0);
    char spec[32];
    snprintf(spec, sizeof(spec), "%d,%d", stale[0], stale[1]);
    close(stale[0]);
    close(stale[1]);
    YTEST_REQUIRE(test, setenv(YETTY_YWIRE_SIDE_CHANNEL_ENV, spec, 1) == 0);

    struct yetty_yclass_transport_pty_ptr_result res =
        yetty_yclass_transport_pty_create_from_env(STDIN_FILENO, STDOUT_FILENO);
    YTEST_CHECK(test, YETTY_IS_ERR(res));
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    } else {
        destroy_transport(res.value);
    }
    unsetenv(YETTY_YWIRE_SIDE_CHANNEL_ENV);
}

int main(void)
{
    struct ytest test = ytest_begin("yclass_transport_env");
    YTEST_RUN(&test, test_env_unset_falls_back);
    YTEST_RUN(&test, test_env_selects_side_channel);
    YTEST_RUN(&test, test_env_malformed_fails);
    YTEST_RUN(&test, test_env_stale_fds_fail);
    return ytest_end(&test);
}
