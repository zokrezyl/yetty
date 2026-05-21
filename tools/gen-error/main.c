/*
 * gen-error — emit malformed / abusive OSC sequences to stdout so we can
 * exercise the error paths in the wire state machine and, in particular,
 * the callback-boundary ynotify surfacing wired into post_fatal_error.
 *
 * No yetty deps on purpose: the whole point is to dump a chosen byte
 * pattern at the PTY. Run it inside a yetty terminal (./gen-error -m
 * oversize-body) or pipe its output into a captured stream — yetty's
 * wire SM sees it identically either way.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define ESC  "\033"
#define OSC  "\033]"
#define BEL  "\007"
#define ST   "\033\\"

struct mode_def {
    const char *name;
    const char *summary;
};

static const struct mode_def MODES[] = {
    {"list",
     "list all modes and exit"},
    {"trigger-error",
     "OSC 99099 — wire-SM test hook that synthesises a 3-frame error "
     "chain. Surfaces via post_fatal_error → ynotify card. THE MODE THAT "
     "ACTUALLY EXERCISES THE NOTIFY PATH."},
    {"unknown-code",
     "OSC with a code yetty has no handler for (drains body silently — "
     "control mode, no error)"},
    {"malformed-code",
     "non-digit byte inside the OSC code position (logs ywarn, no error)"},
    {"truncated",
     "open OSC with no terminator — SM stalls waiting for more bytes"},
    {"bare-esc",
     "ESC followed by a non-']' byte (ESC pair forwarded to default layer)"},
    {"nested-osc",
     "OSC opener appears inside another open OSC body"},
};

static const size_t MODES_COUNT = sizeof(MODES) / sizeof(MODES[0]);

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s -m MODE [-c N] [-o FILE]\n"
        "\n"
        "Emit malformed OSC sequences to test wire-statemachine error paths.\n"
        "\n"
        "Options:\n"
        "  -m, --mode=MODE     which bad sequence to emit (see --list)\n"
        "  -c, --count=N       repeat N times (default 1)\n"
        "  -o, --output=FILE   write to FILE instead of stdout\n"
        "  -l, --list          list available modes and exit\n"
        "  -h, --help          show this help\n"
        "\n"
        "Example — trigger the ynotify card path:\n"
        "  %s -m trigger-error\n",
        prog, prog);
}

static void list_modes(FILE *out)
{
    fprintf(out, "Available modes:\n");
    for (size_t i = 0; i < MODES_COUNT; i++) {
        fprintf(out, "  %-16s %s\n", MODES[i].name, MODES[i].summary);
    }
}

static void emit_trigger_error(FILE *out)
{
    fputs(OSC "99099;;trigger" BEL, out);
}

static void emit_unknown_code(FILE *out)
{
    fputs(OSC "999999;;ignored-body" BEL, out);
}

static void emit_malformed_code(FILE *out)
{
    fputs(OSC "12X3;;body" BEL, out);
}

static void emit_truncated(FILE *out)
{
    fputs(OSC "600001;", out);
}

static void emit_bare_esc(FILE *out)
{
    fputs(ESC "X", out);
}

static void emit_nested_osc(FILE *out)
{
    fputs(OSC "100;;before-" OSC "200;;-after" BEL BEL, out);
}

static int run_mode(const char *name, FILE *out)
{
    if (!strcmp(name, "trigger-error"))  { emit_trigger_error(out);  return 0; }
    if (!strcmp(name, "unknown-code"))   { emit_unknown_code(out);   return 0; }
    if (!strcmp(name, "malformed-code")) { emit_malformed_code(out); return 0; }
    if (!strcmp(name, "truncated"))      { emit_truncated(out);      return 0; }
    if (!strcmp(name, "bare-esc"))       { emit_bare_esc(out);       return 0; }
    if (!strcmp(name, "nested-osc"))     { emit_nested_osc(out);     return 0; }
    return -1;
}

static const char *arg_value(int argc, char **argv, int *i, const char *opt)
{
    char *a = argv[*i];
    size_t optlen = strlen(opt);
    if (!strncmp(a, opt, optlen) && a[optlen] == '=') {
        return a + optlen + 1;
    }
    if (!strcmp(a, opt)) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "gen-error: %s requires an argument\n", opt);
            exit(2);
        }
        (*i)++;
        return argv[*i];
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *mode = NULL;
    const char *output_path = NULL;
    long count = 1;
    bool list = false;

    for (int i = 1; i < argc; i++) {
        const char *v;
        if ((v = arg_value(argc, argv, &i, "-m")) ||
            (v = arg_value(argc, argv, &i, "--mode"))) {
            mode = v;
        } else if ((v = arg_value(argc, argv, &i, "-c")) ||
                   (v = arg_value(argc, argv, &i, "--count"))) {
            count = strtol(v, NULL, 10);
            if (count < 1) count = 1;
        } else if ((v = arg_value(argc, argv, &i, "-o")) ||
                   (v = arg_value(argc, argv, &i, "--output"))) {
            output_path = v;
        } else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--list")) {
            list = true;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            fprintf(stderr, "gen-error: unknown argument: %s\n", argv[i]);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (list || (mode && !strcmp(mode, "list"))) {
        list_modes(stdout);
        return 0;
    }

    if (!mode) {
        fprintf(stderr, "gen-error: -m MODE is required (try --list)\n");
        return 2;
    }

    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "wb");
        if (!out) {
            fprintf(stderr, "gen-error: cannot open '%s' for writing\n", output_path);
            return 1;
        }
    }
#ifdef _WIN32
    /* Stop the CRT from translating LF→CRLF on raw OSC bytes. */
    _setmode(_fileno(out), _O_BINARY);
#endif

    int rc = 0;
    for (long i = 0; i < count; i++) {
        if (run_mode(mode, out) != 0) {
            fprintf(stderr, "gen-error: unknown mode '%s' (try --list)\n", mode);
            rc = 2;
            break;
        }
    }

    fflush(out);
    if (out != stdout) {
        fclose(out);
    }
    return rc;
}
