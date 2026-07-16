/*
 * shaping-script.c - Codepoint -> shaping-script classification
 *
 * HarfBuzz-free run detection: groups codepoints into maximal runs of one
 * shaping class so the pack/expand path can decide which runs to hand to the
 * shaper. Kept independent of the HarfBuzz build gate so run detection works
 * (and degrades to the per-codepoint path) even when the shaper is absent.
 *
 * Ranges follow the Unicode block assignments for the scripts that need
 * OpenType shaping to render correctly. Scripts that render acceptably with
 * per-codepoint lookup (Latin, Greek, Cyrillic, Hebrew without points, CJK,
 * Hangul, emoji) are deliberately left as YETTY_YFONT_SHAPING_NONE.
 */

#include <yetty/yfont/font.h>

enum yetty_yfont_shaping_script yetty_yfont_shaping_script_for_codepoint(uint32_t codepoint)
{
    struct shaping_range {
        uint32_t first;
        uint32_t last;
        enum yetty_yfont_shaping_script script;
    };

    /* Sorted by first codepoint. Program-lifetime constant table lives as a
     * static const local (no file-scope symbol). */
    static const struct shaping_range ranges[] = {
        {0x0600, 0x06FF, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic */
        {0x0700, 0x074F, YETTY_YFONT_SHAPING_ARABIC},  /* Syriac */
        {0x0750, 0x077F, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic Supplement */
        {0x0780, 0x07BF, YETTY_YFONT_SHAPING_ARABIC},  /* Thaana */
        {0x07C0, 0x07FF, YETTY_YFONT_SHAPING_ARABIC},  /* N'Ko */
        {0x0870, 0x089F, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic Extended-B */
        {0x08A0, 0x08FF, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic Extended-A */
        {0x0900, 0x097F, YETTY_YFONT_SHAPING_INDIC},   /* Devanagari */
        {0x0980, 0x09FF, YETTY_YFONT_SHAPING_INDIC},   /* Bengali */
        {0x0A00, 0x0A7F, YETTY_YFONT_SHAPING_INDIC},   /* Gurmukhi */
        {0x0A80, 0x0AFF, YETTY_YFONT_SHAPING_INDIC},   /* Gujarati */
        {0x0B00, 0x0B7F, YETTY_YFONT_SHAPING_INDIC},   /* Oriya */
        {0x0B80, 0x0BFF, YETTY_YFONT_SHAPING_INDIC},   /* Tamil */
        {0x0C00, 0x0C7F, YETTY_YFONT_SHAPING_INDIC},   /* Telugu */
        {0x0C80, 0x0CFF, YETTY_YFONT_SHAPING_INDIC},   /* Kannada */
        {0x0D00, 0x0D7F, YETTY_YFONT_SHAPING_INDIC},   /* Malayalam */
        {0x0D80, 0x0DFF, YETTY_YFONT_SHAPING_INDIC},   /* Sinhala */
        {0x0E00, 0x0E7F, YETTY_YFONT_SHAPING_BRAHMIC}, /* Thai */
        {0x0E80, 0x0EFF, YETTY_YFONT_SHAPING_BRAHMIC}, /* Lao */
        {0x0F00, 0x0FFF, YETTY_YFONT_SHAPING_BRAHMIC}, /* Tibetan */
        {0x1000, 0x109F, YETTY_YFONT_SHAPING_BRAHMIC}, /* Myanmar */
        {0x1780, 0x17FF, YETTY_YFONT_SHAPING_BRAHMIC}, /* Khmer */
        {0xFB50, 0xFDFF, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic Presentation Forms-A */
        {0xFE70, 0xFEFF, YETTY_YFONT_SHAPING_ARABIC},  /* Arabic Presentation Forms-B */
    };

    size_t count = sizeof(ranges) / sizeof(ranges[0]);
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (codepoint < ranges[mid].first) {
            high = mid;
        } else if (codepoint > ranges[mid].last) {
            low = mid + 1;
        } else {
            return ranges[mid].script;
        }
    }
    return YETTY_YFONT_SHAPING_NONE;
}
