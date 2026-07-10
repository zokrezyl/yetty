/*
 * env.ydev_* wasm bridges.
 *
 * Each ydev_* function is exposed to wasm guests as an env.<name> import.
 * The bridge wrapper:
 *   - pops wasm-ABI args (all i32 offsets / scalars)
 *   - translates pointer args from wasm offset to host pointer
 *   - calls the libydev function
 *   - returns the result (host pointers become integer handles)
 *
 * Opaque ydev handles (ydev_camera_t* etc.) live in per-kind handle
 * tables on the host. Wasm sees a small int; the bridge maps it back.
 *
 * The camera frame data path is reformulated for wasm: instead of
 * acquire+release with a host pointer, we expose a single
 * yos_ydev_camera_acquire_wasm() that copies up to `cap` bytes into a
 * wasm-side buffer, writes a fixed-layout info struct, and releases
 * the platform buffer in the same call. Loses zero-copy compared to
 * native use, but the alternative is exposing host pointers to wasm,
 * which we don't.
 */

#include "yos/types.h"
#include "wasm3.h"
#include "m3_api_defs.h"
#include "m3_env.h"

#include <yos/ydev/ydev.h>
#include <yos/ydev/perm.h>
#include <yos/ydev/camera.h>
#include <yos/ydev/audio.h>
#include <yos/ydev/sensor.h>
#include <yos/ydev/location.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* ── handle table ────────────────────────────────────────────────────── */

#define YDEV_BR_HMAX 128

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ydev_camera_t    *g_cam[YDEV_BR_HMAX];
static ydev_audio_in_t  *g_ain[YDEV_BR_HMAX];
static ydev_audio_out_t *g_aout[YDEV_BR_HMAX];
static ydev_sensor_t    *g_sens[YDEV_BR_HMAX];
static ydev_loc_t       *g_loc[YDEV_BR_HMAX];

#define ALLOC_HANDLE_FN(name, type, table)                          \
    static int32_t name(type *ptr) {                                 \
        if (!ptr) return -1;                                          \
        pthread_mutex_lock(&g_lock);                                  \
        for (int i = 1; i < YDEV_BR_HMAX; i++) {                      \
            if (table[i] == NULL) { table[i] = ptr;                    \
                pthread_mutex_unlock(&g_lock); return i; }              \
        }                                                              \
        pthread_mutex_unlock(&g_lock); return -1;                     \
    }
ALLOC_HANDLE_FN(alloc_cam,  ydev_camera_t,    g_cam)
ALLOC_HANDLE_FN(alloc_ain,  ydev_audio_in_t,  g_ain)
ALLOC_HANDLE_FN(alloc_aout, ydev_audio_out_t, g_aout)
ALLOC_HANDLE_FN(alloc_sens, ydev_sensor_t,    g_sens)
ALLOC_HANDLE_FN(alloc_loc,  ydev_loc_t,       g_loc)

#define GET_HANDLE(table, idx) \
    ((idx) > 0 && (idx) < YDEV_BR_HMAX ? (table)[(idx)] : NULL)

#define FREE_HANDLE(table, idx) do { \
    if ((idx) > 0 && (idx) < YDEV_BR_HMAX) (table)[(idx)] = NULL; \
} while (0)

/* ── helper: ctx + memory ────────────────────────────────────────────── */

static void refresh_memory(IM3Runtime rt, struct yos_exec_ctx *ctx)
{
    uint32_t sz = 0;
    ctx->memory = m3_GetMemory(rt, &sz, 0);
    ctx->memory_size = sz;
}

#define YDEV_BR_PROLOGUE(ctx_var)                                     \
    struct yos_exec_ctx *ctx_var =                                     \
        (struct yos_exec_ctx *)m3_GetUserData(runtime);                \
    refresh_memory(runtime, ctx_var)

/* ── init / shutdown ─────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_init)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, init_off);
    (void)init_off;
    m3ApiReturn(ydev_init(NULL));
}

m3ApiRawFunction(m3_ydev_shutdown)
{
    ydev_shutdown();
    m3ApiSuccess();
}

m3ApiRawFunction(m3_ydev_strerror_copy)
{
    /* Wasm-friendly variant: copy error string into a wasm buffer.
     * Returns bytes written excluding NUL. */
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  code);
    m3ApiGetArg(uint32_t, dst);
    m3ApiGetArg(uint32_t, cap);
    YDEV_BR_PROLOGUE(ctx);
    if (cap == 0 || dst + cap > ctx->memory_size) m3ApiReturn(0);
    const char *s = ydev_strerror((ydev_result_t)code);
    size_t n = strlen(s);
    if (n + 1 > cap) n = cap - 1;
    memcpy(ctx->memory + dst, s, n);
    ctx->memory[dst + n] = '\0';
    m3ApiReturn((int32_t)n);
}

/* ── perm ────────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_perm_status)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, cap);
    m3ApiReturn((int32_t)ydev_perm_status((ydev_capability_t)cap));
}

m3ApiRawFunction(m3_ydev_perm_request)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, cap);
    m3ApiReturn((int32_t)ydev_perm_request((ydev_capability_t)cap));
}

m3ApiRawFunction(m3_ydev_perm_fd)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn((int32_t)ydev_perm_fd());
}

/* ── camera ──────────────────────────────────────────────────────────── */

/* Wasm-layout mirrors of the host structs. uint32_t-only so they're
 * binary-compatible across wasm32 and host64.
 *
 * sizeof(ydev_camera_info_t) on the host is 64+64+4+4 = ~204 bytes; we
 * keep the same layout but pad to a multiple of 8. */
struct ydev_cam_info_wasm {
    char     id[64];
    char     display_name[128];
    uint32_t facing;
    uint32_t supported_formats_count;
};

m3ApiRawFunction(m3_ydev_camera_list)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, out_off);
    m3ApiGetArg(uint32_t, cap);
    m3ApiGetArg(uint32_t, count_off);
    YDEV_BR_PROLOGUE(ctx);

    ydev_camera_info_t tmp[16];
    size_t n = 0;
    size_t want = cap;
    if (want > 16) want = 16;
    ydev_result_t r = ydev_camera_list(tmp, want, &n);
    if (out_off && cap > 0 && r == YDEV_OK) {
        struct ydev_cam_info_wasm *out =
            (struct ydev_cam_info_wasm *)(ctx->memory + out_off);
        size_t emit = n > cap ? cap : n;
        for (size_t i = 0; i < emit; i++) {
            memset(&out[i], 0, sizeof out[i]);
            memcpy(out[i].id,           tmp[i].id,           sizeof out[i].id);
            memcpy(out[i].display_name, tmp[i].display_name, sizeof out[i].display_name);
            out[i].facing                  = (uint32_t)tmp[i].facing;
            out[i].supported_formats_count = tmp[i].supported_formats_count;
        }
    }
    if (count_off) *(uint32_t *)(ctx->memory + count_off) = (uint32_t)n;
    m3ApiReturn((int32_t)r);
}

m3ApiRawFunction(m3_ydev_camera_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, id_off);
    YDEV_BR_PROLOGUE(ctx);
    const char *id = (const char *)(ctx->memory + id_off);
    ydev_camera_t *c = ydev_camera_open(id);
    if (!c) m3ApiReturn(-1);
    int32_t h = alloc_cam(c);
    if (h < 0) { ydev_camera_close(c); m3ApiReturn(-1); }
    m3ApiReturn(h);
}

m3ApiRawFunction(m3_ydev_camera_set_format)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  handle);
    m3ApiGetArg(uint32_t, fmt_off);
    YDEV_BR_PROLOGUE(ctx);
    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    if (!c) m3ApiReturn(YDEV_INVALID_ARG);
    ydev_camera_format_t f;
    memcpy(&f, ctx->memory + fmt_off, sizeof f);
    m3ApiReturn((int32_t)ydev_camera_set_format(c, &f));
}

m3ApiRawFunction(m3_ydev_camera_start)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    if (!c) m3ApiReturn(YDEV_INVALID_ARG);
    m3ApiReturn((int32_t)ydev_camera_start(c));
}

m3ApiRawFunction(m3_ydev_camera_stop)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    if (!c) m3ApiReturn(YDEV_INVALID_ARG);
    m3ApiReturn((int32_t)ydev_camera_stop(c));
}

m3ApiRawFunction(m3_ydev_camera_close)
{
    m3ApiGetArg(int32_t, handle);
    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    if (c) {
        ydev_camera_close(c);
        FREE_HANDLE(g_cam, handle);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_ydev_camera_fd)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    m3ApiReturn(c ? ydev_camera_fd(c) : -1);
}

/* Wasm frame info layout. 64 bytes, all-u32 + u64 timestamps. */
struct ydev_frame_info_wasm {
    uint32_t width;
    uint32_t height;
    uint32_t stride[4];
    uint32_t plane_offset[4];
    uint32_t format;
    uint32_t size;
    uint64_t ts_ns;
    uint64_t seq;
};

m3ApiRawFunction(m3_ydev_camera_acquire_wasm)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  handle);
    m3ApiGetArg(uint32_t, dst);
    m3ApiGetArg(uint32_t, cap);
    m3ApiGetArg(uint32_t, info_off);
    m3ApiGetArg(int32_t,  timeout_ms);
    YDEV_BR_PROLOGUE(ctx);

    ydev_camera_t *c = GET_HANDLE(g_cam, handle);
    if (!c) m3ApiReturn(YDEV_INVALID_ARG);

    ydev_frame_t fr;
    ydev_result_t r = ydev_camera_acquire_frame(c, &fr, timeout_ms);
    if (r != YDEV_OK) m3ApiReturn((int32_t)r);

    size_t n = fr.size < cap ? fr.size : cap;
    if (dst && n) memcpy(ctx->memory + dst, fr.data, n);

    if (info_off) {
        struct ydev_frame_info_wasm *info =
            (struct ydev_frame_info_wasm *)(ctx->memory + info_off);
        info->width  = fr.width;
        info->height = fr.height;
        for (int i = 0; i < 4; i++) {
            info->stride[i]       = fr.stride[i];
            info->plane_offset[i] = (uint32_t)fr.plane_offset[i];
        }
        info->format = (uint32_t)fr.format;
        info->size   = (uint32_t)n;
        info->ts_ns  = fr.ts_ns;
        info->seq    = fr.seq;
    }

    ydev_camera_release_frame(c, &fr);
    m3ApiReturn(YDEV_OK);
}

/* ── audio in ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_audio_in_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, cfg_off);
    YDEV_BR_PROLOGUE(ctx);
    ydev_audio_config_t cfg;
    memcpy(&cfg, ctx->memory + cfg_off, sizeof cfg);
    ydev_audio_in_t *h = ydev_audio_in_open(&cfg);
    if (!h) m3ApiReturn(-1);
    int32_t id = alloc_ain(h);
    if (id < 0) { ydev_audio_in_close(h); m3ApiReturn(-1); }
    m3ApiReturn(id);
}

#define BR_LIFECYCLE_INT(name, table, ydev_fn)                            \
    m3ApiRawFunction(name) {                                              \
        m3ApiReturnType(int32_t);                                         \
        m3ApiGetArg(int32_t, h);                                          \
        void *p = GET_HANDLE(table, h);                                   \
        if (!p) m3ApiReturn(YDEV_INVALID_ARG);                            \
        m3ApiReturn((int32_t)ydev_fn(p));                                 \
    }
#define BR_LIFECYCLE_VOID(name, table, ydev_fn)                           \
    m3ApiRawFunction(name) {                                              \
        m3ApiGetArg(int32_t, h);                                          \
        void *p = GET_HANDLE(table, h);                                   \
        if (p) { ydev_fn(p); FREE_HANDLE(table, h); }                     \
        m3ApiSuccess();                                                   \
    }
#define BR_FD(name, table, ydev_fn)                                       \
    m3ApiRawFunction(name) {                                              \
        m3ApiReturnType(int32_t);                                         \
        m3ApiGetArg(int32_t, h);                                          \
        void *p = GET_HANDLE(table, h);                                   \
        m3ApiReturn(p ? ydev_fn(p) : -1);                                 \
    }

BR_LIFECYCLE_INT (m3_ydev_audio_in_start, g_ain, ydev_audio_in_start)
BR_LIFECYCLE_INT (m3_ydev_audio_in_stop,  g_ain, ydev_audio_in_stop)
BR_LIFECYCLE_VOID(m3_ydev_audio_in_close, g_ain, ydev_audio_in_close)
BR_FD            (m3_ydev_audio_in_fd,    g_ain, ydev_audio_in_fd)

m3ApiRawFunction(m3_ydev_audio_in_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  h);
    m3ApiGetArg(uint32_t, buf);
    m3ApiGetArg(uint32_t, bytes);
    m3ApiGetArg(uint32_t, ts_off);
    m3ApiGetArg(int32_t,  timeout_ms);
    YDEV_BR_PROLOGUE(ctx);
    ydev_audio_in_t *p = GET_HANDLE(g_ain, h);
    if (!p) m3ApiReturn(-1);
    uint64_t ts = 0;
    ssize_t r = ydev_audio_in_read(p, ctx->memory + buf, bytes,
                                   ts_off ? &ts : NULL, timeout_ms);
    if (ts_off && r > 0) *(uint64_t *)(ctx->memory + ts_off) = ts;
    m3ApiReturn((int32_t)r);
}

/* ── audio out ───────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_audio_out_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, cfg_off);
    YDEV_BR_PROLOGUE(ctx);
    ydev_audio_config_t cfg;
    memcpy(&cfg, ctx->memory + cfg_off, sizeof cfg);
    ydev_audio_out_t *h = ydev_audio_out_open(&cfg);
    if (!h) m3ApiReturn(-1);
    int32_t id = alloc_aout(h);
    if (id < 0) { ydev_audio_out_close(h); m3ApiReturn(-1); }
    m3ApiReturn(id);
}

BR_LIFECYCLE_INT (m3_ydev_audio_out_start, g_aout, ydev_audio_out_start)
BR_LIFECYCLE_INT (m3_ydev_audio_out_stop,  g_aout, ydev_audio_out_stop)
BR_LIFECYCLE_VOID(m3_ydev_audio_out_close, g_aout, ydev_audio_out_close)
BR_FD            (m3_ydev_audio_out_fd,    g_aout, ydev_audio_out_fd)

m3ApiRawFunction(m3_ydev_audio_out_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  h);
    m3ApiGetArg(uint32_t, buf);
    m3ApiGetArg(uint32_t, bytes);
    m3ApiGetArg(int32_t,  timeout_ms);
    YDEV_BR_PROLOGUE(ctx);
    ydev_audio_out_t *p = GET_HANDLE(g_aout, h);
    if (!p) m3ApiReturn(-1);
    ssize_t r = ydev_audio_out_write(p, ctx->memory + buf, bytes, timeout_ms);
    m3ApiReturn((int32_t)r);
}

/* ── sensor ──────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_sensor_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  kind);
    m3ApiGetArg(uint32_t, rate);
    ydev_sensor_t *h = ydev_sensor_open((ydev_sensor_kind_t)kind, rate);
    if (!h) m3ApiReturn(-1);
    int32_t id = alloc_sens(h);
    if (id < 0) { ydev_sensor_close(h); m3ApiReturn(-1); }
    m3ApiReturn(id);
}

BR_LIFECYCLE_INT (m3_ydev_sensor_start, g_sens, ydev_sensor_start)
BR_LIFECYCLE_INT (m3_ydev_sensor_stop,  g_sens, ydev_sensor_stop)
BR_LIFECYCLE_VOID(m3_ydev_sensor_close, g_sens, ydev_sensor_close)
BR_FD            (m3_ydev_sensor_fd,    g_sens, ydev_sensor_fd)

m3ApiRawFunction(m3_ydev_sensor_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  h);
    m3ApiGetArg(uint32_t, out);
    m3ApiGetArg(uint32_t, cap);
    m3ApiGetArg(int32_t,  timeout_ms);
    YDEV_BR_PROLOGUE(ctx);
    ydev_sensor_t *p = GET_HANDLE(g_sens, h);
    if (!p) m3ApiReturn(-1);
    ssize_t r = ydev_sensor_read(p,
        (ydev_sensor_record_t *)(ctx->memory + out), cap, timeout_ms);
    m3ApiReturn((int32_t)r);
}

/* ── location ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_ydev_loc_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, accuracy);
    ydev_loc_t *h = ydev_loc_open((ydev_loc_accuracy_t)accuracy);
    if (!h) m3ApiReturn(-1);
    int32_t id = alloc_loc(h);
    if (id < 0) { ydev_loc_close(h); m3ApiReturn(-1); }
    m3ApiReturn(id);
}

BR_LIFECYCLE_INT (m3_ydev_loc_start, g_loc, ydev_loc_start)
BR_LIFECYCLE_INT (m3_ydev_loc_stop,  g_loc, ydev_loc_stop)
BR_LIFECYCLE_VOID(m3_ydev_loc_close, g_loc, ydev_loc_close)
BR_FD            (m3_ydev_loc_fd,    g_loc, ydev_loc_fd)

m3ApiRawFunction(m3_ydev_loc_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  h);
    m3ApiGetArg(uint32_t, out);
    m3ApiGetArg(uint32_t, cap);
    m3ApiGetArg(int32_t,  timeout_ms);
    YDEV_BR_PROLOGUE(ctx);
    ydev_loc_t *p = GET_HANDLE(g_loc, h);
    if (!p) m3ApiReturn(-1);
    ssize_t r = ydev_loc_read(p,
        (ydev_loc_fix_t *)(ctx->memory + out), cap, timeout_ms);
    m3ApiReturn((int32_t)r);
}

/* ── link table ──────────────────────────────────────────────────────── */

void yos_ydev_link(IM3Module module, struct yos_exec_ctx *ctx)
{
    /* Common */
    m3_LinkRawFunctionEx(module, "env", "ydev_init",          "i(i)",  m3_ydev_init,          ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_shutdown",      "v()",   m3_ydev_shutdown,      ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_strerror_copy", "i(iii)",m3_ydev_strerror_copy, ctx);

    /* Perm */
    m3_LinkRawFunctionEx(module, "env", "ydev_perm_status",   "i(i)",  m3_ydev_perm_status,   ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_perm_request",  "i(i)",  m3_ydev_perm_request,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_perm_fd",       "i()",   m3_ydev_perm_fd,       ctx);

    /* Camera */
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_list",         "i(iii)",  m3_ydev_camera_list,         ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_open",         "i(i)",    m3_ydev_camera_open,         ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_set_format",   "i(ii)",   m3_ydev_camera_set_format,   ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_start",        "i(i)",    m3_ydev_camera_start,        ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_stop",         "i(i)",    m3_ydev_camera_stop,         ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_close",        "v(i)",    m3_ydev_camera_close,        ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_fd",           "i(i)",    m3_ydev_camera_fd,           ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_camera_acquire_wasm", "i(iiiii)",m3_ydev_camera_acquire_wasm, ctx);

    /* Audio in */
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_open",  "i(i)",   m3_ydev_audio_in_open,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_start", "i(i)",   m3_ydev_audio_in_start, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_stop",  "i(i)",   m3_ydev_audio_in_stop,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_close", "v(i)",   m3_ydev_audio_in_close, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_fd",    "i(i)",   m3_ydev_audio_in_fd,    ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_in_read",  "i(iiiii)",m3_ydev_audio_in_read, ctx);

    /* Audio out */
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_open",  "i(i)",   m3_ydev_audio_out_open,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_start", "i(i)",   m3_ydev_audio_out_start, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_stop",  "i(i)",   m3_ydev_audio_out_stop,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_close", "v(i)",   m3_ydev_audio_out_close, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_fd",    "i(i)",   m3_ydev_audio_out_fd,    ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_audio_out_write", "i(iiii)",m3_ydev_audio_out_write, ctx);

    /* Sensor */
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_open",  "i(ii)",  m3_ydev_sensor_open,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_start", "i(i)",   m3_ydev_sensor_start, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_stop",  "i(i)",   m3_ydev_sensor_stop,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_close", "v(i)",   m3_ydev_sensor_close, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_fd",    "i(i)",   m3_ydev_sensor_fd,    ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_sensor_read",  "i(iiii)",m3_ydev_sensor_read,  ctx);

    /* Location */
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_open",  "i(i)",   m3_ydev_loc_open,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_start", "i(i)",   m3_ydev_loc_start, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_stop",  "i(i)",   m3_ydev_loc_stop,  ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_close", "v(i)",   m3_ydev_loc_close, ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_fd",    "i(i)",   m3_ydev_loc_fd,    ctx);
    m3_LinkRawFunctionEx(module, "env", "ydev_loc_read",  "i(iiii)",m3_ydev_loc_read,  ctx);
}
