/*
 * list2-internal.h — module-private helpers for the ydrawlist2 classes.
 * Static-inline only (no file-scope data).
 */
#ifndef YETTY_YDRAWLIST2_INTERNAL_H
#define YETTY_YDRAWLIST2_INTERNAL_H

#include <yetty/ycore/result.h>

#include <stdint.h>

/* Parse "#RRGGBB" / "#RRGGBBAA" (leading '#' optional) into the wire's
 * 0xAARRGGBB word; alpha defaults to opaque for the 6-digit form. THE one
 * color-string parser of the v2 client interface — every language binding
 * goes through the C setters that call it, so no binding re-implements it. */
static inline struct yetty_ycore_uint32_result ydrawlist2_color_parse(const char *text)
{
    if (!text || !text[0]) {
        return YETTY_ERR(yetty_ycore_uint32, "color: empty string");
    }
    if (text[0] == '#') {
        text++;
    }
    uint32_t digits[8];
    int count = 0;
    for (; text[count] != '\0'; count++) {
        if (count >= 8) {
            return YETTY_ERR(yetty_ycore_uint32, "color: expected #RRGGBB or #RRGGBBAA");
        }
        char ch = text[count];
        if (ch >= '0' && ch <= '9') {
            digits[count] = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digits[count] = (uint32_t)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digits[count] = (uint32_t)(ch - 'A' + 10);
        } else {
            return YETTY_ERR(yetty_ycore_uint32, "color: non-hex digit");
        }
    }
    if (count != 6 && count != 8) {
        return YETTY_ERR(yetty_ycore_uint32, "color: expected #RRGGBB or #RRGGBBAA");
    }
    uint32_t rgb = 0;
    for (int i = 0; i < 6; i++) {
        rgb = (rgb << 4) | digits[i];
    }
    uint32_t alpha = 0xFFu;
    if (count == 8) {
        alpha = (digits[6] << 4) | digits[7];
    }
    return YETTY_OK(yetty_ycore_uint32, (alpha << 24) | rgb);
}

#endif /* YETTY_YDRAWLIST2_INTERNAL_H */
