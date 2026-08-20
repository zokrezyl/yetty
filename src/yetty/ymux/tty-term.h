/*
 * ymux/tty-term.h — the tmux tty-term.c/tty-features.c analog (#699,
 * review cycle 21): a REAL terminal capability model.
 *
 *   1. LOADER: reads the compiled terminfo(5) entry for a TERM name from the
 *      standard database ($TERMINFO, ~/.terminfo, /etc/terminfo,
 *      /lib/terminfo, /usr/share/terminfo), including 32-bit-number entries
 *      and the extended-capability section (Smulx/Setulc/smxx/Sync live
 *      there). When the database has no entry, a compiled-in fallback covers
 *      the xterm-256color family and a minimal vt100-class profile, so
 *      resolution is deterministic on database-less systems.
 *   2. EXPANDER: a tparm-style parameterized-string interpreter (%p %d %i
 *      %{} %'' arithmetic/logic %? %t %e %; conditionals, %c, %s) — emitted
 *      sequences come from CAPABILITY STRINGS, not hard-coded ANSI.
 *   3. FEATURES: tmux tty-features.c port — feature tokens ADD capability
 *      strings (RGB -> setrgbf/setrgbb, usstyle -> Smulx/Setulc, …) on top
 *      of the loaded entry.
 *   4. OVERRIDES: tmux terminal-overrides semantics inside the features
 *      string — `cap=value` replaces a capability (with \E/\\ escapes),
 *      `cap@` CANCELS it (distinct from `feature@`, which cancels a feature
 *      token; a token is treated as a capability override when it names a
 *      known capability slot).
 *
 * Plain C leaf helpers (no yclass object) — owned by the renderer state.
 */
#ifndef YETTY_YMUX_TTY_TERM_H
#define YETTY_YMUX_TTY_TERM_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

/* The capability STRING slots the ymux renderer consumes — the subset of
 * tmux's tty_term_code_table it emits today, by terminfo name. Standard
 * capabilities carry their classic strfnames index; extended capabilities
 * (negative index) resolve by name from the extended section. */
enum yetty_ymux_tty_term_slot {
    YMUX_TTY_TERM_CUP = 0, /* cursor_address */
    YMUX_TTY_TERM_HPA,     /* column_address */
    YMUX_TTY_TERM_VPA,     /* row_address */
    YMUX_TTY_TERM_CUU,     /* parm_up_cursor */
    YMUX_TTY_TERM_CUD,     /* parm_down_cursor */
    YMUX_TTY_TERM_CUB,     /* parm_left_cursor */
    YMUX_TTY_TERM_CUF,     /* parm_right_cursor */
    YMUX_TTY_TERM_CUU1,    /* cursor_up */
    YMUX_TTY_TERM_CUD1,    /* cursor_down */
    YMUX_TTY_TERM_CUB1,    /* cursor_left */
    YMUX_TTY_TERM_CUF1,    /* cursor_right */
    YMUX_TTY_TERM_HOME,    /* cursor_home */
    YMUX_TTY_TERM_CR,      /* carriage_return */
    YMUX_TTY_TERM_CSR,     /* change_scroll_region */
    YMUX_TTY_TERM_EL,      /* clr_eol */
    YMUX_TTY_TERM_ED,      /* clr_eos */
    YMUX_TTY_TERM_ECH,     /* erase_chars */
    YMUX_TTY_TERM_IL,      /* parm_insert_line */
    YMUX_TTY_TERM_DL,      /* parm_delete_line */
    YMUX_TTY_TERM_IL1,     /* insert_line */
    YMUX_TTY_TERM_DL1,     /* delete_line */
    YMUX_TTY_TERM_ICH,     /* parm_ich */
    YMUX_TTY_TERM_DCH,     /* parm_dch */
    YMUX_TTY_TERM_INDN,    /* parm_index — \e[nS (margin scroll-up) */
    YMUX_TTY_TERM_CLEAR,   /* clear_screen */
    YMUX_TTY_TERM_SGR0,    /* exit_attribute_mode */
    YMUX_TTY_TERM_BOLD,    /* enter_bold_mode */
    YMUX_TTY_TERM_DIM,     /* enter_dim_mode */
    YMUX_TTY_TERM_SITM,    /* enter_italics_mode */
    YMUX_TTY_TERM_SMUL,    /* enter_underline_mode */
    YMUX_TTY_TERM_BLINK,   /* enter_blink_mode */
    YMUX_TTY_TERM_REV,     /* enter_reverse_mode */
    YMUX_TTY_TERM_INVIS,   /* enter_secure_mode */
    YMUX_TTY_TERM_SETAF,   /* set_a_foreground */
    YMUX_TTY_TERM_SETAB,   /* set_a_background */
    YMUX_TTY_TERM_SMACS,   /* enter_alt_charset_mode */
    YMUX_TTY_TERM_RMACS,   /* exit_alt_charset_mode */
    /* Extended (user-defined) capabilities, resolved by NAME. */
    YMUX_TTY_TERM_SMXX,    /* smxx  — strikethrough on */
    YMUX_TTY_TERM_RMXX,    /* rmxx  — strikethrough off */
    YMUX_TTY_TERM_SMULX,   /* Smulx — styled underline \E[4:%p1%dm */
    YMUX_TTY_TERM_SETULC,  /* Setulc — underline colour */
    YMUX_TTY_TERM_SMOL,    /* Smol  — overline on */
    YMUX_TTY_TERM_RMOL,    /* Rmol  — overline off */
    YMUX_TTY_TERM_SYNC,    /* Sync  — DEC 2026 synchronized output */
    YMUX_TTY_TERM_SETRGBF, /* setrgbf — truecolour foreground */
    YMUX_TTY_TERM_SETRGBB, /* setrgbb — truecolour background */

    YMUX_TTY_TERM_SLOT_COUNT
};

/* Terminfo BOOLEAN capabilities the renderer's strategy depends on (tmux reads
 * the compiled boolean section; a `bce@`/`am@`/`xenl@` override cancels them). */
enum yetty_ymux_tty_term_bool {
    YMUX_TTY_TERM_BOOL_BCE = 0, /* back_color_erase — standard index 27 */
    YMUX_TTY_TERM_BOOL_AM,      /* auto_right_margin — standard index 1 */
    YMUX_TTY_TERM_BOOL_XENL,    /* eat_newline_glitch — standard index 13 */
    YMUX_TTY_TERM_BOOL_COUNT
};

/* A resolved terminal: owned capability strings per slot (NULL = the
 * terminal does not have the capability) + the colours number + the loaded
 * boolean capabilities. */
struct yetty_ymux_tty_term {
    char name[64];
    char *strings[YMUX_TTY_TERM_SLOT_COUNT];
    int colors;
    uint8_t bools[YMUX_TTY_TERM_BOOL_COUNT]; /* 0/1, resolved (terminfo + overrides) */
    unsigned loaded_from_db : 1;             /* 0 = compiled-in fallback entry */
    unsigned bools_loaded : 1;               /* 1 = the boolean section was parsed */
};

/* Query a resolved boolean capability (0/1). */
int yetty_ymux_tty_term_bool(const struct yetty_ymux_tty_term *term,
                             enum yetty_ymux_tty_term_bool which);

/* Load the compiled terminfo entry for `term_name` and populate `out`.
 * `terminfo_path` overrides the database search path (tests use a private
 * directory); NULL = standard search. Falls back to the compiled-in family
 * entries when no database entry exists — `out->loaded_from_db` records
 * which happened. Always initializes `out`; the void result reports only
 * allocation-level failures. Free with yetty_ymux_tty_term_free. */
struct yetty_ycore_void_result yetty_ymux_tty_term_load(struct yetty_ymux_tty_term *out,
                                                        const char *term_name,
                                                        const char *terminfo_path);

void yetty_ymux_tty_term_free(struct yetty_ymux_tty_term *term);

/* Apply one features/overrides string (comma-separated) on top of a loaded
 * entry, tmux-style:
 *   feature token   — adds that feature's capability strings (tty-features.c
 *                     tables: 256, RGB, usstyle, overline, strikethrough,
 *                     sync, …).
 *   feature@        — cancels a previously-implied feature token.
 *   cap=value       — override: replace the capability string (supports \E,
 *                     \\, \a, \n, \r, \t and ^X control escapes).
 *   cap@            — cancel: REMOVE the capability from the terminal.
 * Unknown tokens are ignored (forward compatibility), matching tmux. */
struct yetty_ycore_void_result yetty_ymux_tty_term_apply_features(struct yetty_ymux_tty_term *term,
                                                                  const char *features);

/* tparm-style expansion of `cap` with `params`. Returns bytes written into
 * `out` (NUL-terminated), 0 on overflow/parse failure or NULL cap.
 *
 * SUPPORTED SUBSET (the operators the capabilities this renderer emits
 * actually use — deliberately NOT full terminfo tparm): literal bytes;
 * %% ; %p<n> push param; %'c' / %{n} push char/int literal; %d (decimal) /
 * %c (byte) / %s (string, popped-and-ignored — no emitted cap uses it) output;
 * %i one-based increment of the first two params; %P<a>/%g<a> set/get static
 * vars; the arithmetic/logic/comparison ops %+ %- %* %/ %m %& %| %^ %= %> %<
 * %! %~ ; and conditionals %? %t %e %; . Printf-style width/precision
 * (`%2.2d`, `%:...`) and %l are NOT supported — no consumed capability needs
 * them; an unrecognized `%X` is passed through verbatim rather than
 * mis-evaluated. Callers must not feed caps outside this subset. */
size_t yetty_ymux_tty_term_expand(const char *cap, const long *params, size_t param_count,
                                  char *out, size_t out_cap);

/* Convenience: expand slot `slot` of `term`. Returns 0 when the terminal
 * lacks the capability. */
size_t yetty_ymux_tty_term_emit(const struct yetty_ymux_tty_term *term,
                                enum yetty_ymux_tty_term_slot slot, const long *params,
                                size_t param_count, char *out, size_t out_cap);

static inline int yetty_ymux_tty_term_has(const struct yetty_ymux_tty_term *term,
                                          enum yetty_ymux_tty_term_slot slot)
{
    return term && term->strings[slot] != NULL;
}

#endif /* YETTY_YMUX_TTY_TERM_H */
