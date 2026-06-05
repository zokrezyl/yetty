/*
 * yplatform/compat.h - Cross-platform compatibility for common POSIX functions
 */

#ifndef YETTY_YPLATFORM_COMPAT_H
#define YETTY_YPLATFORM_COMPAT_H

//TODO: extract functions that need platform specific implementation into the right header
#ifdef _MSC_VER
#include <stdlib.h>
#include <string.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/* POSIX strtok_r -> MSVC strtok_s. Identical signature and semantics:
 * char *(char *str, const char *delim, char **context). */
#define strtok_r strtok_s

/* POSIX setenv -> Windows _putenv_s. _putenv_s always overwrites, so the
 * `overwrite` flag is ignored. Returns 0 on success. */
static __inline int setenv(const char *name, const char *value, int overwrite)
{
    (void)overwrite;
    return _putenv_s(name, value);
}

/* clock_gettime / CLOCK_MONOTONIC — MSVC ships C11 struct timespec but not
 * the POSIX clock_gettime API. Back the monotonic clock with QPC; the
 * realtime clock with GetSystemTimeAsFileTime translated to UNIX epoch. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

static __inline int clock_gettime(int clk_id, struct timespec *ts)
{
    if (!ts) {
        return -1;
    }
    if (clk_id == CLOCK_MONOTONIC) {
        static LARGE_INTEGER s_freq;
        LARGE_INTEGER now;
        if (s_freq.QuadPart == 0) {
            QueryPerformanceFrequency(&s_freq);
        }
        QueryPerformanceCounter(&now);
        ts->tv_sec = (time_t)(now.QuadPart / s_freq.QuadPart);
        ts->tv_nsec = (long)(((now.QuadPart % s_freq.QuadPart) * 1000000000LL) / s_freq.QuadPart);
        return 0;
    }
    /* CLOCK_REALTIME */
    {
        FILETIME ft;
        ULARGE_INTEGER ui;
        const ULONGLONG EPOCH_DIFF_100NS = 116444736000000000ULL; /* 1601 -> 1970 */
        GetSystemTimeAsFileTime(&ft);
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        ui.QuadPart -= EPOCH_DIFF_100NS;
        ts->tv_sec = (time_t)(ui.QuadPart / 10000000ULL);
        ts->tv_nsec = (long)((ui.QuadPart % 10000000ULL) * 100);
        return 0;
    }
}

/* Minimal POSIX dirent shim — just enough for opendir / readdir / closedir
 * with d_name. Used by code that walks plain directories (no follow-symlink
 * semantics, no d_type, single-threaded iteration). Backed by FindFirstFile
 * / FindNextFile. */

#define _COMPAT_DIRENT_NAME_MAX 260

struct dirent {
    char d_name[_COMPAT_DIRENT_NAME_MAX];
};

typedef struct DIR {
    HANDLE find;
    int started; /* 0 = haven't returned the result of FindFirstFile yet */
    WIN32_FIND_DATAA fd;
    struct dirent ent;
} DIR;

static __inline DIR *opendir(const char *path)
{
    char glob[_COMPAT_DIRENT_NAME_MAX + 4];
    DIR *d;
    if (!path) {
        return NULL;
    }
    if (_snprintf_s(glob, sizeof(glob), _TRUNCATE, "%s\\*", path) < 0) {
        return NULL;
    }
    d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) {
        return NULL;
    }
    d->find = FindFirstFileA(glob, &d->fd);
    if (d->find == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->started = 0;
    return d;
}

static __inline struct dirent *readdir(DIR *d)
{
    if (!d) {
        return NULL;
    }
    if (d->started) {
        if (!FindNextFileA(d->find, &d->fd)) {
            return NULL;
        }
    } else {
        d->started = 1;
    }
    strncpy(d->ent.d_name, d->fd.cFileName, sizeof(d->ent.d_name) - 1);
    d->ent.d_name[sizeof(d->ent.d_name) - 1] = '\0';
    return &d->ent;
}

static __inline int closedir(DIR *d)
{
    if (!d) {
        return -1;
    }
    if (d->find != INVALID_HANDLE_VALUE) {
        FindClose(d->find);
    }
    free(d);
    return 0;
}

#else
#include <dirent.h>
#endif

#endif /* YETTY_YPLATFORM_COMPAT_H */
