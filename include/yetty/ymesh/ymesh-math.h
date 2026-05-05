#ifndef YETTY_YMESH_MATH_H
#define YETTY_YMESH_MATH_H

/*
 * Tiny inline vec3/mat4 helpers — just enough for the ymesh MVP.
 * Matrices are column-major (WebGPU/WGSL convention): m[col*4 + row].
 */

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymesh_vec3 {
    float x, y, z;
};

struct yetty_ymesh_mat4 {
    float m[16]; /* column-major */
};

static inline struct yetty_ymesh_vec3 yetty_ymesh_vec3_sub(
    struct yetty_ymesh_vec3 a, struct yetty_ymesh_vec3 b)
{
    return (struct yetty_ymesh_vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline struct yetty_ymesh_vec3 yetty_ymesh_vec3_cross(
    struct yetty_ymesh_vec3 a, struct yetty_ymesh_vec3 b)
{
    return (struct yetty_ymesh_vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

static inline float yetty_ymesh_vec3_dot(struct yetty_ymesh_vec3 a, struct yetty_ymesh_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline struct yetty_ymesh_vec3 yetty_ymesh_vec3_normalize(struct yetty_ymesh_vec3 v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f)
        return (struct yetty_ymesh_vec3){0.0f, 0.0f, 0.0f};
    return (struct yetty_ymesh_vec3){v.x / len, v.y / len, v.z / len};
}

static inline struct yetty_ymesh_mat4 yetty_ymesh_mat4_identity(void)
{
    struct yetty_ymesh_mat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

/* Right-handed perspective, NDC z in [0, 1] (WebGPU). */
static inline struct yetty_ymesh_mat4 yetty_ymesh_mat4_perspective(
    float fov_y_rad, float aspect, float z_near, float z_far)
{
    struct yetty_ymesh_mat4 r = {{0}};
    float f = 1.0f / tanf(fov_y_rad * 0.5f);
    float nf = 1.0f / (z_near - z_far);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = z_far * nf;
    r.m[11] = -1.0f;
    r.m[14] = z_far * z_near * nf;
    return r;
}

/* Right-handed look-at, world → view. */
static inline struct yetty_ymesh_mat4 yetty_ymesh_mat4_lookat(
    struct yetty_ymesh_vec3 eye, struct yetty_ymesh_vec3 center, struct yetty_ymesh_vec3 up)
{
    struct yetty_ymesh_vec3 f = yetty_ymesh_vec3_normalize(yetty_ymesh_vec3_sub(center, eye));
    struct yetty_ymesh_vec3 s = yetty_ymesh_vec3_normalize(yetty_ymesh_vec3_cross(f, up));
    struct yetty_ymesh_vec3 u = yetty_ymesh_vec3_cross(s, f);

    struct yetty_ymesh_mat4 r = yetty_ymesh_mat4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -yetty_ymesh_vec3_dot(s, eye);
    r.m[13] = -yetty_ymesh_vec3_dot(u, eye);
    r.m[14] =  yetty_ymesh_vec3_dot(f, eye);
    return r;
}

static inline struct yetty_ymesh_mat4 yetty_ymesh_mat4_mul(
    struct yetty_ymesh_mat4 a, struct yetty_ymesh_mat4 b)
{
    struct yetty_ymesh_mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int rr = 0; rr < 4; rr++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    }
    return r;
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_MATH_H */
