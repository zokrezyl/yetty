/*
 * yinstall — the yetty installer.
 *
 * A self-contained executable that carries the whole yetty product as an
 * embedded payload and unpacks it to the right per-OS locations. All the
 * real work lives in the yetty_yinstall module; this is just argument
 * parsing and error reporting. See docs/yinstall.md.
 */

#include <stdio.h>
#include <string.h>

#include <yetty/yinstall/install.h>
#include <yetty/ycore/result.h>

#ifndef YETTY_BUILD_VERSION
#define YETTY_BUILD_VERSION "dev"
#endif

static void print_usage(const char *program)
{
    printf("Usage: %s [options]\n\n", program);
    printf("Installs yetty and its companion tools onto this machine.\n\n");
    printf("Options:\n");
    printf("  -v, --verbose   list every file as it is installed\n");
    printf("  -f, --force     reinstall even files already present\n");
    printf("  -V, --version   print the installer version and exit\n");
    printf("  -h, --help      show this help and exit\n");
}

int main(int argc, char **argv)
{
    struct yetty_yinstall_options options = {0};
    options.version = YETTY_BUILD_VERSION;

    for (int index = 1; index < argc; index++) {
        const char *arg = argv[index];
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            options.verbose = 1;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            options.force = 1;
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            printf("yinstall %s\n", YETTY_BUILD_VERSION);
            return 0;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "yinstall: unknown option '%s'\n\n", arg);
            print_usage(argv[0]);
            return 2;
        }
    }

    struct yetty_ycore_void_result result = yetty_yinstall_run(&options);
    if (YETTY_IS_ERR(result)) {
        fprintf(stderr, "\n");
        yetty_ycore_error_print(stderr, "install failed", result.error);
        yetty_ycore_error_destroy(result.error);
        return 1;
    }
    return 0;
}
