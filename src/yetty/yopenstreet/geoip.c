/*
 * geoip.c — public-IP geolocation for the default map center.
 *
 * One keyless HTTPS GET to ipinfo.io; its JSON carries the coordinates as
 *   "loc": "47.4925,19.0514"
 * which a targeted string scan extracts without a JSON parser. The whole
 * lookup is best-effort by contract: IP geolocation is city-accurate at
 * most (ISP routing can be off by 100 km, VPN / CGNAT arbitrarily far),
 * the endpoint is third-party infrastructure, and the request necessarily
 * shows the service this machine's IP — so callers treat a failure as
 * "no default location" and fall back, never as a hard error. The short
 * timeout keeps a dead endpoint from stalling tool startup.
 */

#include <yetty/yopenstreet/openstreet.h>

#include "tile-fetch.h"

#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

#define GEOIP_URL "https://ipinfo.io/json"
#define GEOIP_TIMEOUT_SECONDS 4L
#define GEOIP_MAX_BODY 65536u

/* Extract the value of `"loc": "lat,lon"` from the response body. */
static struct yetty_ycore_void_result parse_loc_field(const char *body, double *out_latitude,
                                                      double *out_longitude)
{
    const char *key = strstr(body, "\"loc\"");
    if (!key) {
        return YETTY_ERR(yetty_ycore_void, "geoip: no \"loc\" field in response");
    }
    const char *colon = strchr(key + 5, ':');
    if (!colon) {
        return YETTY_ERR(yetty_ycore_void, "geoip: malformed \"loc\" field");
    }
    const char *quote = strchr(colon, '"');
    if (!quote) {
        return YETTY_ERR(yetty_ycore_void, "geoip: unquoted \"loc\" value");
    }
    char *end = NULL;
    double latitude = strtod(quote + 1, &end);
    if (!end || *end != ',') {
        return YETTY_ERR(yetty_ycore_void, "geoip: latitude parse failed");
    }
    double longitude = strtod(end + 1, &end);
    if (!end || *end != '"') {
        return YETTY_ERR(yetty_ycore_void, "geoip: longitude parse failed");
    }
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
        return YETTY_ERR(yetty_ycore_void, "geoip: coordinates out of range");
    }
    *out_latitude = latitude;
    *out_longitude = longitude;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yopenstreet_geolocate_public_ip(double *out_latitude,
                                                                     double *out_longitude)
{
    if (!out_latitude || !out_longitude) {
        return YETTY_ERR(yetty_ycore_void, "geoip: NULL output argument");
    }
    CURL *curl_handle = yetty_yopenstreet_curl_acquire();
    if (!curl_handle) {
        return YETTY_ERR(yetty_ycore_void, "geoip: curl init failed");
    }

    uint8_t *body_bytes = NULL;
    size_t body_len = 0;
    struct yetty_ycore_void_result get_res = yetty_yopenstreet_http_get(
        curl_handle, GEOIP_URL, GEOIP_TIMEOUT_SECONDS, &body_bytes, &body_len);
    yetty_yopenstreet_curl_release(curl_handle);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, get_res, "geoip: lookup request failed");

    if (body_len == 0 || body_len > GEOIP_MAX_BODY) {
        free(body_bytes);
        return YETTY_ERR(yetty_ycore_void, "geoip: response size out of range");
    }
    /* NUL-terminate for the string scan. */
    char *body_text = malloc(body_len + 1);
    if (!body_text) {
        free(body_bytes);
        return YETTY_ERR(yetty_ycore_void, "geoip: body copy alloc failed");
    }
    memcpy(body_text, body_bytes, body_len);
    body_text[body_len] = '\0';
    free(body_bytes);

    struct yetty_ycore_void_result parse_res =
        parse_loc_field(body_text, out_latitude, out_longitude);
    free(body_text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parse_res, "geoip: response parse failed");

    ydebug("geoip: public-IP location %.4f,%.4f", *out_latitude, *out_longitude);
    return YETTY_OK_VOID();
}
