/*
 * ligature.c - Programming-ligature detection for the terminal grid
 *
 * HarfBuzz-free, deterministic detection of the well-known monospace
 * programming ligatures (=>, !=, ->, ===, ...). The terminal renders a
 * ligature by suppressing the covered cells on the grid (which cannot shape,
 * see vterm_pack_line) and re-drawing the shaped glyph through the SDF
 * free-position path against a ligature-capable face (Fira Code). Both the
 * suppression pass (vterm.c) and the shaping pass (sdf-layer.c) consult this
 * table, so they agree — without communicating — on exactly which cells a
 * ligature covers, the same contract the shaping-script classifier gives the
 * complex-script path.
 *
 * The set is PUNCTUATION-ONLY: every entry is a run of ASCII operator
 * characters, so detection never fires on ordinary words, identifiers or
 * numbers (no alphabetic ligatures like "www"/"0x"). Kept independent of the
 * HarfBuzz build gate so callers compile either way; a build without the
 * shaper simply never suppresses the cells and the grid renders the
 * characters individually as before.
 */

#include <yetty/yfont/font.h>

size_t yetty_yfont_ligature_length_at(const uint32_t *codepoints, size_t count, size_t pos)
{
    if (!codepoints || pos >= count) {
        return 0;
    }

    /* Longest-first within each length class doesn't matter — we scan all and
     * keep the longest match — but every entry is <= YETTY_YFONT_LIGATURE_MAX_LEN
     * so the window the callers gather stays small. Program-lifetime constant
     * table lives as a static const local (no file-scope symbol). */
    static const char *const ligatures[] = {
        /* 4-char */
        "<==>",
        "<-->",
        /* 3-char */
        "===",
        "!==",
        "=/=",
        "=>>",
        "<<=",
        "->>",
        "<<-",
        "-->",
        "<--",
        "<=>",
        "<->",
        "<~>",
        ">=>",
        "<=<",
        ">->",
        "<-<",
        "|->",
        "<-|",
        "://",
        "...",
        "..<",
        ":::",
        "///",
        "***",
        "+++",
        "---",
        "###",
        ">>>",
        "<<<",
        "|||",
        "</>",
        "==>",
        "<==",
        /* 2-char */
        "->",
        "<-",
        "=>",
        "==",
        "!=",
        ">=",
        "<=",
        "<>",
        "::",
        "//",
        "**",
        "++",
        "--",
        "<<",
        ">>",
        "..",
        "|>",
        "<|",
        "&&",
        "||",
        "/*",
        "*/",
        "~>",
        "<~",
        "|=",
        "^=",
        "##",
        ":=",
        "=~",
        "!~",
        "</",
        "/>",
        "<:",
        ":>",
    };

    size_t best = 0;
    for (size_t i = 0; i < sizeof(ligatures) / sizeof(ligatures[0]); i++) {
        const char *lig = ligatures[i];
        size_t len = 0;
        int matched = 1;
        for (const char *cursor = lig; *cursor; cursor++, len++) {
            if (pos + len >= count || codepoints[pos + len] != (uint32_t)(unsigned char)*cursor) {
                matched = 0;
                break;
            }
        }
        if (matched && len > best) {
            best = len;
        }
    }
    return best;
}
