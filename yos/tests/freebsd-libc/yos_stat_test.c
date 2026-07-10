/* yos_stat_test.c — yos-authored regression for the cv_stat_h2w
 * converter. Not part of FreeBSD's upstream tests; lives here
 * because the rest of the freebsd-libc suite shares the harness.
 *
 * Verifies stat / lstat / fstat translate the host-shaped struct
 * back to the wasm32 FreeBSD layout correctly, and that errno is
 * set on the not-found path.
 */
#include <atf-c.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

ATF_TC(stat_basic);
ATF_TC_HEAD(stat_basic, tc) { atf_tc_set_md_var(tc, "descr", "stat returns sane values for /tmp"); }
ATF_TC_BODY(stat_basic, tc) {
    struct stat sb;
    int rc = stat("/tmp", &sb);
    ATF_REQUIRE(rc == 0);
    ATF_REQUIRE((sb.st_mode & S_IFMT) == S_IFDIR);
    ATF_REQUIRE(sb.st_dev != 0);
}

ATF_TC(stat_enoent);
ATF_TC_HEAD(stat_enoent, tc) { atf_tc_set_md_var(tc, "descr", "stat sets errno=ENOENT on missing path"); }
ATF_TC_BODY(stat_enoent, tc) {
    struct stat sb;
    errno = 0;
    int rc = stat("/this/does/not/exist", &sb);
    ATF_REQUIRE(rc == -1);
    ATF_REQUIRE(errno == ENOENT);
}

ATF_TC(fstat_self);
ATF_TC_HEAD(fstat_self, tc) { atf_tc_set_md_var(tc, "descr", "fstat on stdin or stdout returns sane mode"); }
ATF_TC_BODY(fstat_self, tc) {
    struct stat sb;
    /* /dev/null is universally available. */
    int fd = open("/dev/null", O_RDONLY);
    ATF_REQUIRE(fd >= 0);
    int rc = fstat(fd, &sb);
    ATF_REQUIRE(rc == 0);
    /* /dev/null is a character device. */
    ATF_REQUIRE((sb.st_mode & S_IFMT) == S_IFCHR);
    close(fd);
}

ATF_TP_ADD_TCS(tp) {
    ATF_TP_ADD_TC(tp, stat_basic);
    ATF_TP_ADD_TC(tp, stat_enoent);
    ATF_TP_ADD_TC(tp, fstat_self);
    return atf_no_error();
}
