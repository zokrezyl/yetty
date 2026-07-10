/*
 * Linux location backend — gpsd over loopback TCP.
 *
 * gpsd is the de-facto Linux GPS daemon; it listens on TCP 2947 and
 * speaks line-delimited JSON. We connect, send "?WATCH=...", then
 * read JSON lines on a worker thread, parsing TPV records into
 * ydev_loc_fix_t and pushing into the vfd ring.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/location.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct ydev_loc {
    struct ydev_vfd vfd;
    int             sock;
    pthread_t       thr;
    int             started;
    int             stop_req;
};

static double j_num(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return atof(p);
}

static void *worker(void *arg)
{
    ydev_loc_t *h = arg;
    char buf[4096];
    size_t used = 0;
    while (!h->stop_req) {
        ssize_t r = recv(h->sock, buf + used, sizeof(buf) - 1 - used, 0);
        if (r <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        used += r;
        buf[used] = '\0';

        char *nl;
        char *start = buf;
        while ((nl = memchr(start, '\n', used - (start - buf))) != NULL) {
            *nl = '\0';
            if (strstr(start, "\"class\":\"TPV\"")) {
                ydev_loc_fix_t fix = {0};
                fix.ts_ns       = ydev_now_ns();
                fix.lat         = j_num(start, "\"lat\"");
                fix.lon         = j_num(start, "\"lon\"");
                fix.alt_m       = j_num(start, "\"alt\"");
                fix.horiz_acc_m = (float)j_num(start, "\"eph\"");
                fix.vert_acc_m  = (float)j_num(start, "\"epv\"");
                fix.speed_mps   = (float)j_num(start, "\"speed\"");
                fix.bearing_deg = (float)j_num(start, "\"track\"");
                ydev_vfd_push(&h->vfd, &fix);
            }
            start = nl + 1;
        }
        size_t left = used - (start - buf);
        memmove(buf, start, left);
        used = left;
    }
    return NULL;
}

ydev_loc_t *ydev_loc_open(ydev_loc_accuracy_t a)
{
    (void)a;
    ydev_loc_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if (ydev_vfd_init(&h->vfd, sizeof(ydev_loc_fix_t), 32, NULL) != 0) {
        free(h); return NULL;
    }
    h->sock = -1;
    return h;
}

ydev_result_t ydev_loc_start(ydev_loc_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    h->sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (h->sock < 0) return YDEV_IO;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(2947);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(h->sock, (struct sockaddr *)&sa, sizeof sa) != 0) {
        ydev_set_error("loc_start: gpsd connect: %s", strerror(errno));
        close(h->sock); h->sock = -1;
        return YDEV_IO;
    }
    const char *enable = "?WATCH={\"enable\":true,\"json\":true};\r\n";
    if (send(h->sock, enable, strlen(enable), 0) < 0) {
        close(h->sock); h->sock = -1;
        return YDEV_IO;
    }
    h->stop_req = 0;
    if (pthread_create(&h->thr, NULL, worker, h) != 0) {
        close(h->sock); h->sock = -1;
        return YDEV_IO;
    }
    h->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_loc_stop(ydev_loc_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    h->stop_req = 1;
    if (h->sock >= 0) shutdown(h->sock, SHUT_RDWR);
    if (h->started) pthread_join(h->thr, NULL);
    if (h->sock >= 0) { close(h->sock); h->sock = -1; }
    h->started = 0;
    ydev_vfd_close(&h->vfd);
    return YDEV_OK;
}

void ydev_loc_close(ydev_loc_t *h)
{
    if (!h) return;
    if (h->started) ydev_loc_stop(h);
    ydev_vfd_destroy(&h->vfd);
    free(h);
}

int ydev_loc_fd(ydev_loc_t *h) { return h ? ydev_vfd_fd(&h->vfd) : -1; }

ssize_t ydev_loc_read(ydev_loc_t *h, ydev_loc_fix_t *out, size_t cap, int timeout_ms)
{
    if (!h || !out || cap == 0) { errno = EINVAL; return -1; }
    size_t got = 0;
    while (got < cap) {
        ydev_result_t r = ydev_vfd_pop(&h->vfd, &out[got],
                                       got == 0 ? timeout_ms : 0);
        if (r == YDEV_AGAIN) break;
        if (r != YDEV_OK)    { errno = EIO; return got ? (ssize_t)got : -1; }
        got++;
    }
    return (ssize_t)got;
}
