/* tests/freebsd-libc/run.c — main() for FreeBSD libc tests under yos.
 *
 * gen_driver.py emits a yos_atf_table[] containing a {name, fn} entry
 * per ATF_TC_BODY discovered in the test source. main() walks it,
 * runs each test, prints PASS/FAIL via write(2) (no fprintf — we
 * deliberately keep the harness independent of libc's printf, which
 * is exactly what some of the tests are exercising).
 */

#include <unistd.h>
#include <string.h>

extern struct yos_atf_entry { const char *name; void (*fn)(void *); }
    yos_atf_table[];

int yos_atf_failed;
const char *yos_atf_current_test;

/* atf-runner forks a fresh process per ATF_TC_BODY; we run them all
 * sequentially in one wasm instance, so a test that calls
 * clearenv() / setenv() leaves residual state for the next case.
 * yos exposes __yos_env_reload to drop the per-process env table
 * and re-pull from host environ — call it between cases. */
__attribute__((import_module("env"), import_name("__yos_env_reload")))
void yos_env_reload(void);

static void putw(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    write(2, s, n);
}

static void putn(int v)
{
    char b[16]; int n = 0;
    if (v == 0) { write(2, "0", 1); return; }
    if (v < 0)  { write(2, "-", 1); v = -v; }
    char tmp[16]; int ti = 0;
    while (v > 0 && ti < 15) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    while (ti > 0) b[n++] = tmp[--ti];
    write(2, b, n);
}

int main(void)
{
    int total = 0, failed_tests = 0;
    for (int i = 0; yos_atf_table[i].name != (void *)0; i++) {
        yos_atf_current_test = yos_atf_table[i].name;
        int before = yos_atf_failed;
        /* Re-pull the host env so each case starts with a clean
         * environ — atf-run's fork-per-case behaviour. */
        yos_env_reload();
        putw("  RUN  "); putw(yos_atf_current_test); putw("\n");
        yos_atf_table[i].fn(0);
        if (yos_atf_failed > before) {
            putw("  FAIL ");
            putw(yos_atf_current_test);
            putw("\n");
            failed_tests++;
        } else {
            putw("  PASS ");
            putw(yos_atf_current_test);
            putw("\n");
        }
        total++;
    }
    putw("==== ");      putn(total);
    putw(" test(s), "); putn(failed_tests); putw(" failed");
    putw(" (");         putn(yos_atf_failed); putw(" assertions) ====\n");
    return failed_tests == 0 ? 0 : 1;
}
