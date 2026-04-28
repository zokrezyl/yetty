#include <yetty/yrender/types.h>
#include <yetty/ytrace.h>
#include <webgpu/webgpu.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint64_t yetty_yrender_hash(const void *data, size_t size)
{
    const uint8_t *p = data;
    uint64_t h = 0xcbf29ce484222325ULL; /* FNV-1a offset basis */
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL; /* FNV-1a prime */
    }
    return h;
}

void yetty_yrender_shader_code_set(struct yetty_yrender_shader_code *sc, const char *data,
                                   size_t size)
{
    sc->data = data;
    sc->size = size;
    sc->hash = (data && size > 0) ? yetty_yrender_hash(data, size) : 0;
}

int yetty_yrender_shader_code_load_file(struct yetty_yrender_shader_code *sc, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        yerror("shader_code_load_file: failed to open %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        yerror("shader_code_load_file: empty or invalid file %s", path);
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)size + 1);
    if (!data) {
        yerror("shader_code_load_file: failed to allocate %ld bytes", size);
        fclose(f);
        return -1;
    }

    size_t read = fread(data, 1, (size_t)size, f);
    fclose(f);

    if (read != (size_t)size) {
        yerror("shader_code_load_file: read %zu of %ld bytes", read, size);
        free(data);
        return -1;
    }

    data[size] = '\0'; /* null-terminate for safety */
    sc->data = data;
    sc->size = (size_t)size;
    sc->hash = yetty_yrender_hash(data, (size_t)size);
    ydebug("shader_code_load_file: loaded %s (%zu bytes)", path, sc->size);
    return 0;
}

size_t yetty_yrender_texture_get_size(const struct yetty_yrender_texture *texture)
{
    if (!texture || texture->width == 0 || texture->height == 0) {
        return 0;
    }

    size_t bpp;
    switch ((WGPUTextureFormat)texture->format) {
    case WGPUTextureFormat_R8Unorm:
        bpp = 1;
        break;
    case WGPUTextureFormat_RG8Unorm:
        bpp = 2;
        break;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
        bpp = 4;
        break;
    default:
        bpp = 4;
        break;
    }

    return (size_t)texture->width * (size_t)texture->height * bpp;
}

const char *yetty_yrender_uniform_type_wgsl(enum yetty_yrender_uniform_type type)
{
    switch (type) {
    case YETTY_YRENDER_UNIFORM_F32:
        return "f32";
    case YETTY_YRENDER_UNIFORM_VEC2:
        return "vec2<f32>";
    case YETTY_YRENDER_UNIFORM_VEC3:
        return "vec3<f32>";
    case YETTY_YRENDER_UNIFORM_VEC4:
        return "vec4<f32>";
    case YETTY_YRENDER_UNIFORM_MAT4:
        return "mat4x4<f32>";
    case YETTY_YRENDER_UNIFORM_U32:
        return "u32";
    case YETTY_YRENDER_UNIFORM_I32:
        return "i32";
    default:
        return "f32";
    }
}

size_t yetty_yrender_uniform_type_size(enum yetty_yrender_uniform_type type)
{
    switch (type) {
    case YETTY_YRENDER_UNIFORM_F32:
        return 4;
    case YETTY_YRENDER_UNIFORM_VEC2:
        return 8;
    case YETTY_YRENDER_UNIFORM_VEC3:
        return 12;
    case YETTY_YRENDER_UNIFORM_VEC4:
        return 16;
    case YETTY_YRENDER_UNIFORM_MAT4:
        return 64;
    case YETTY_YRENDER_UNIFORM_U32:
        return 4;
    case YETTY_YRENDER_UNIFORM_I32:
        return 4;
    default:
        return 4;
    }
}

/* WGSL alignment rules:
 *   f32/u32/i32 = 4, vec2 = 8, vec3 = 16 (not 12!), vec4 = 16, mat4 = 16 */
size_t yetty_yrender_uniform_type_align(enum yetty_yrender_uniform_type type)
{
    switch (type) {
    case YETTY_YRENDER_UNIFORM_F32:
        return 4;
    case YETTY_YRENDER_UNIFORM_VEC2:
        return 8;
    case YETTY_YRENDER_UNIFORM_VEC3:
        return 16;
    case YETTY_YRENDER_UNIFORM_VEC4:
        return 16;
    case YETTY_YRENDER_UNIFORM_MAT4:
        return 16;
    case YETTY_YRENDER_UNIFORM_U32:
        return 4;
    case YETTY_YRENDER_UNIFORM_I32:
        return 4;
    default:
        return 4;
    }
}
