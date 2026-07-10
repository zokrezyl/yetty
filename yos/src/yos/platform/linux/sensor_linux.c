/*
 * Linux sensor backend — iio (industrial I/O subsystem).
 *
 * Sensors live under /sys/bus/iio/devices/iio:device*. Each one has a
 * name file and per-channel raw / scale / offset files. We scan the
 * tree, match the requested sensor kind by name heuristics, and read
 * the raw values from sysfs on each ydev_sensor_read call. The fd we
 * return is a timerfd ticking at the requested rate so poll() works.
 *
 * This is a simple polling implementation. A higher-performance build
 * would use iio's char-device ring buffer and a worker thread; for
 * now the sysfs approach handles the common embedded sensor use case.
 */

#include "../../impl/ydev/internal.h"
#include <yos/ydev/sensor.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

struct ydev_sensor {
    ydev_sensor_kind_t kind;
    uint32_t           rate_hz;
    int                tfd;             /* timerfd, ticks at rate_hz     */
    char               dev_path[128];   /* /sys/bus/iio/devices/iio:deviceN */
    int                started;
};

static int read_text(const char *path, char *out, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, out, n - 1);
    close(fd);
    if (r < 0) return -1;
    while (r > 0 && (out[r-1] == '\n' || out[r-1] == ' ')) r--;
    out[r] = '\0';
    return 0;
}

static int matches_kind(const char *name, ydev_sensor_kind_t k)
{
    switch (k) {
    case YDEV_SENSOR_ACCEL: return strstr(name, "accel") != NULL;
    case YDEV_SENSOR_GYRO:  return strstr(name, "gyro")  != NULL ||
                                   strstr(name, "anglvel") != NULL;
    case YDEV_SENSOR_MAG:   return strstr(name, "magn")  != NULL;
    case YDEV_SENSOR_BARO:  return strstr(name, "press") != NULL ||
                                   strstr(name, "baro")  != NULL;
    case YDEV_SENSOR_LIGHT: return strstr(name, "light") != NULL ||
                                   strstr(name, "illum") != NULL;
    case YDEV_SENSOR_PROX:  return strstr(name, "prox")  != NULL;
    case YDEV_SENSOR_STEPS: return strstr(name, "step")  != NULL;
    case YDEV_SENSOR_ORIENT:return strstr(name, "rotat") != NULL ||
                                   strstr(name, "orient") != NULL;
    }
    return 0;
}

static int find_device(ydev_sensor_kind_t k, char *out, size_t n)
{
    DIR *d = opendir("/sys/bus/iio/devices");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "iio:device", 10) != 0) continue;
        char name_path[160];
        snprintf(name_path, sizeof name_path, "/sys/bus/iio/devices/%s/name", de->d_name);
        char name[64];
        if (read_text(name_path, name, sizeof name) != 0) continue;
        if (matches_kind(name, k)) {
            snprintf(out, n, "/sys/bus/iio/devices/%s", de->d_name);
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static double read_axis(const char *base, const char *axis_name)
{
    char p[200];
    snprintf(p, sizeof p, "%s/in_%s_raw", base, axis_name);
    char buf[64];
    double raw = 0;
    if (read_text(p, buf, sizeof buf) == 0) raw = atof(buf);

    double scale = 1.0;
    snprintf(p, sizeof p, "%s/in_%s_scale", base, axis_name);
    if (read_text(p, buf, sizeof buf) == 0) scale = atof(buf);
    else {
        /* per-channel scale absent: try the generic in_<type>_scale */
        const char *u = strchr(axis_name, '_');
        if (u) {
            snprintf(p, sizeof p, "%s/in_%.*s_scale", base,
                     (int)(u - axis_name), axis_name);
            if (read_text(p, buf, sizeof buf) == 0) scale = atof(buf);
        }
    }
    return raw * scale;
}

ydev_sensor_t *ydev_sensor_open(ydev_sensor_kind_t kind, uint32_t rate_hz)
{
    if (rate_hz == 0 || rate_hz > 1000) {
        ydev_set_error("sensor_open: rate %u out of range", rate_hz);
        return NULL;
    }
    char path[128];
    if (find_device(kind, path, sizeof path) != 0) {
        ydev_set_error("sensor_open: no iio device for kind=%d", (int)kind);
        return NULL;
    }
    ydev_sensor_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->kind    = kind;
    s->rate_hz = rate_hz;
    strncpy(s->dev_path, path, sizeof(s->dev_path) - 1);

    s->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (s->tfd < 0) { ydev_set_error("timerfd_create: %s", strerror(errno));
                      free(s); return NULL; }
    return s;
}

ydev_result_t ydev_sensor_start(ydev_sensor_t *s)
{
    if (!s) return YDEV_INVALID_ARG;
    struct itimerspec it = {0};
    uint64_t period_ns = 1000000000ull / s->rate_hz;
    it.it_interval.tv_sec  = period_ns / 1000000000ull;
    it.it_interval.tv_nsec = period_ns % 1000000000ull;
    it.it_value            = it.it_interval;
    if (timerfd_settime(s->tfd, 0, &it, NULL) != 0) return YDEV_IO;
    s->started = 1;
    return YDEV_OK;
}

ydev_result_t ydev_sensor_stop(ydev_sensor_t *s)
{
    if (!s) return YDEV_INVALID_ARG;
    struct itimerspec it = {0};
    timerfd_settime(s->tfd, 0, &it, NULL);
    s->started = 0;
    return YDEV_OK;
}

void ydev_sensor_close(ydev_sensor_t *s)
{
    if (!s) return;
    if (s->tfd >= 0) close(s->tfd);
    free(s);
}

int ydev_sensor_fd(ydev_sensor_t *s) { return s ? s->tfd : -1; }

ssize_t ydev_sensor_read(ydev_sensor_t *s, ydev_sensor_record_t *out,
                         size_t cap, int timeout_ms)
{
    (void)timeout_ms;
    if (!s || !out || cap == 0) { errno = EINVAL; return -1; }

    uint64_t ticks = 0;
    ssize_t r = read(s->tfd, &ticks, sizeof ticks);
    if (r < 0 && errno == EAGAIN) return 0;
    if (ticks == 0) return 0;
    if (ticks > cap) ticks = cap;

    /* Sysfs sensors give one *current* value, not historical samples.
     * We synthesise N identical records when more than one tick is
     * pending so the client's pacing isn't disrupted. */
    ydev_sensor_record_t rec = {0};
    rec.ts_ns = ydev_now_ns();
    switch (s->kind) {
    case YDEV_SENSOR_ACCEL:
        rec.u.v3[0] = (float)read_axis(s->dev_path, "accel_x");
        rec.u.v3[1] = (float)read_axis(s->dev_path, "accel_y");
        rec.u.v3[2] = (float)read_axis(s->dev_path, "accel_z");
        break;
    case YDEV_SENSOR_GYRO:
        rec.u.v3[0] = (float)read_axis(s->dev_path, "anglvel_x");
        rec.u.v3[1] = (float)read_axis(s->dev_path, "anglvel_y");
        rec.u.v3[2] = (float)read_axis(s->dev_path, "anglvel_z");
        break;
    case YDEV_SENSOR_MAG:
        rec.u.v3[0] = (float)read_axis(s->dev_path, "magn_x");
        rec.u.v3[1] = (float)read_axis(s->dev_path, "magn_y");
        rec.u.v3[2] = (float)read_axis(s->dev_path, "magn_z");
        break;
    case YDEV_SENSOR_BARO:
        rec.u.v1 = (float)read_axis(s->dev_path, "pressure");
        break;
    case YDEV_SENSOR_LIGHT:
        rec.u.v1 = (float)read_axis(s->dev_path, "illuminance");
        break;
    case YDEV_SENSOR_PROX:
        rec.u.v1 = (float)read_axis(s->dev_path, "proximity");
        break;
    case YDEV_SENSOR_STEPS:
        rec.u.counter = (uint64_t)read_axis(s->dev_path, "steps");
        break;
    case YDEV_SENSOR_ORIENT:
        rec.u.quat[0] = (float)read_axis(s->dev_path, "rot_quaternion_w");
        rec.u.quat[1] = (float)read_axis(s->dev_path, "rot_quaternion_x");
        rec.u.quat[2] = (float)read_axis(s->dev_path, "rot_quaternion_y");
        rec.u.quat[3] = (float)read_axis(s->dev_path, "rot_quaternion_z");
        break;
    }
    for (uint64_t i = 0; i < ticks; i++) out[i] = rec;
    return (ssize_t)ticks;
}
