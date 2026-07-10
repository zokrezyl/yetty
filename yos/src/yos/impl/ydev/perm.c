/*
 * ydev permission state.
 *
 * The cross-platform half keeps a small cache of status values per
 * capability and a self-pipe so that ydev_perm_fd() is pollable. The
 * per-platform half (apple/perm_av.m, android/perm_jni.c, linux/perm.c,
 * …) is responsible for actually asking the OS — when it gets an
 * answer it calls ydev_perm_set() to update the cache and signal the
 * pipe.
 */

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_once_t g_perm_once = PTHREAD_ONCE_INIT;

static void perm_init_impl(void)
{
    int p[2];
    if (pipe(p) != 0) {
        g_ydev_perm.pipe_r = g_ydev_perm.pipe_w = -1;
        return;
    }
    int fl;
    fl = fcntl(p[0], F_GETFL, 0); fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
    fl = fcntl(p[1], F_GETFL, 0); fcntl(p[1], F_SETFL, fl | O_NONBLOCK);
    g_ydev_perm.pipe_r = p[0];
    g_ydev_perm.pipe_w = p[1];
    pthread_mutex_init(&g_ydev_perm.lock, NULL);
    for (size_t i = 0; i < sizeof g_ydev_perm.status / sizeof g_ydev_perm.status[0]; i++)
        g_ydev_perm.status[i] = YDEV_PERM_UNKNOWN;
}

void ydev_perm_init_once(void)
{
    pthread_once(&g_perm_once, perm_init_impl);
}

static int cap_index(ydev_capability_t cap)
{
    switch (cap) {
    case YDEV_CAP_CAMERA:   return 1;
    case YDEV_CAP_MIC:      return 2;
    case YDEV_CAP_LOCATION: return 3;
    case YDEV_CAP_MOTION:   return 4;
    }
    return 0;
}

void ydev_perm_set(ydev_capability_t cap, ydev_perm_status_t st)
{
    ydev_perm_init_once();
    int idx = cap_index(cap);
    if (idx == 0) return;

    pthread_mutex_lock(&g_ydev_perm.lock);
    ydev_perm_status_t prev = g_ydev_perm.status[idx];
    g_ydev_perm.status[idx] = st;
    pthread_mutex_unlock(&g_ydev_perm.lock);

    if (prev != st && g_ydev_perm.pipe_w >= 0) {
        char x = 1;
        ssize_t w = write(g_ydev_perm.pipe_w, &x, 1);
        (void)w;
    }
}

ydev_perm_status_t ydev_perm_status(ydev_capability_t cap)
{
    ydev_perm_init_once();
    int idx = cap_index(cap);
    if (idx == 0) return YDEV_PERM_UNKNOWN;

    /* Each query also drains the perm pipe so a subsequent poll() blocks
     * until the next transition. The platform half is what writes a
     * fresh byte on the next change. */
    if (g_ydev_perm.pipe_r >= 0) {
        char buf[8];
        while (read(g_ydev_perm.pipe_r, buf, sizeof buf) > 0) {}
    }

    /* Refresh from the platform on every call: on iOS / macOS the user
     * can flip permission state in Settings while the app is running. */
    ydev_perm_status_t platform = ydev_perm_query_platform(cap);
    if (platform != YDEV_PERM_UNKNOWN) {
        pthread_mutex_lock(&g_ydev_perm.lock);
        g_ydev_perm.status[idx] = platform;
        pthread_mutex_unlock(&g_ydev_perm.lock);
        return platform;
    }

    pthread_mutex_lock(&g_ydev_perm.lock);
    ydev_perm_status_t out = g_ydev_perm.status[idx];
    pthread_mutex_unlock(&g_ydev_perm.lock);
    return out;
}

ydev_result_t ydev_perm_request(ydev_capability_t cap)
{
    ydev_perm_init_once();
    if (cap_index(cap) == 0) return YDEV_INVALID_ARG;
    ydev_perm_set(cap, YDEV_PERM_PENDING);
    return ydev_perm_request_platform(cap);
}

int ydev_perm_fd(void)
{
    ydev_perm_init_once();
    return g_ydev_perm.pipe_r;
}
