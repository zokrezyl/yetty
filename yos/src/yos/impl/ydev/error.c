/*
 * ydev error reporting — thread-local last-error string and the
 * static-table strerror.
 */

#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define YDEV_ERRMSG_MAX 256

static __thread char tls_errmsg[YDEV_ERRMSG_MAX];

const char *ydev_strerror(ydev_result_t r)
{
    switch (r) {
    case YDEV_OK:           return "ok";
    case YDEV_AGAIN:        return "no data ready";
    case YDEV_DENIED:       return "permission denied";
    case YDEV_RESTRICTED:   return "capability restricted by policy";
    case YDEV_UNSUPPORTED:  return "unsupported";
    case YDEV_IO:           return "device i/o error";
    case YDEV_INVALID_ARG:  return "invalid argument";
    case YDEV_NO_MEM:       return "out of memory";
    case YDEV_BUSY:         return "device busy";
    case YDEV_INTERNAL:     return "internal error";
    }
    return "unknown";
}

const char *ydev_last_error(void) { return tls_errmsg; }

void ydev_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tls_errmsg, sizeof tls_errmsg, fmt, ap);
    va_end(ap);
}

void ydev_clear_error(void) { tls_errmsg[0] = '\0'; }
