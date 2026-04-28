#include <yetty/yimage/yimage.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Internal struct
 *===========================================================================*/

struct yetty_yimage {
    /* Pixel data (RGBA8, owned) */
    uint8_t *pixels;
    uint32_t pixel_width;
    uint32_t pixel_height;

    /* Display bounds in ypaint coordinates */
    float x, y, w, h;

    /* View properties */
    float zoom;
    float center_x;
    float center_y;
    uint32_t filter;

    /* GPU resource set */
    struct yetty_yrender_gpu_resource_set rs;
    bool dirty;
};

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_yimage_result yetty_yimage_create(void)
{
    struct yetty_yimage *img = calloc(1, sizeof(struct yetty_yimage));
    if (!img) {
        return YETTY_ERR(yetty_yimage, "allocation failed");
    }

    img->zoom = 1.0f;
    img->center_x = 0.5f;
    img->center_y = 0.5f;
    img->filter = YETTY_YIMAGE_FILTER_BILINEAR;
    img->dirty = true;

    /* Initialize gpu_resource_set */
    strncpy(img->rs.namespace, "yimage", YETTY_YRENDER_NAME_MAX - 1);

    /* Texture slot 0: image pixels */
    struct yetty_yrender_texture *tex = &img->rs.textures[0];
    strncpy(tex->name, "image_tex", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(tex->wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    strncpy(tex->sampler_name, "image_sampler", YETTY_YRENDER_NAME_MAX - 1);
    tex->format = 0x12;      /* WGPUTextureFormat_RGBA8Unorm */
    tex->sampler_filter = 1; /* WGPUFilterMode_Linear */
    img->rs.texture_count = 1;

    /* Uniforms */
    struct yetty_yrender_uniform *u;

    u = &img->rs.uniforms[0];
    strncpy(u->name, "image_bounds", YETTY_YRENDER_NAME_MAX - 1);
    u->type = YETTY_YRENDER_UNIFORM_VEC4;

    u = &img->rs.uniforms[1];
    strncpy(u->name, "image_size", YETTY_YRENDER_NAME_MAX - 1);
    u->type = YETTY_YRENDER_UNIFORM_VEC2;

    u = &img->rs.uniforms[2];
    strncpy(u->name, "image_zoom", YETTY_YRENDER_NAME_MAX - 1);
    u->type = YETTY_YRENDER_UNIFORM_F32;
    u->f32 = 1.0f;

    u = &img->rs.uniforms[3];
    strncpy(u->name, "image_center", YETTY_YRENDER_NAME_MAX - 1);
    u->type = YETTY_YRENDER_UNIFORM_VEC2;
    u->vec2[0] = 0.5f;
    u->vec2[1] = 0.5f;

    img->rs.uniform_count = 4;

    return YETTY_OK(yetty_yimage, img);
}

void yetty_yimage_destroy(struct yetty_yimage *img)
{
    if (!img) {
        return;
    }
    free(img->pixels);
    free(img);
}

/*=============================================================================
 * Pixel data
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yimage_set_pixels(struct yetty_yimage *img,
                                                       const uint8_t *pixels, uint32_t width,
                                                       uint32_t height)
{
    if (!img) {
        return YETTY_ERR(yetty_ycore_void, "img is NULL");
    }
    if (!pixels || width == 0 || height == 0) {
        return YETTY_ERR(yetty_ycore_void, "invalid pixel data");
    }

    size_t size = (size_t)width * height * 4;
    uint8_t *copy = realloc(img->pixels, size);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "allocation failed");
    }

    memcpy(copy, pixels, size);
    img->pixels = copy;
    img->pixel_width = width;
    img->pixel_height = height;
    img->dirty = true;

    return YETTY_OK_VOID();
}

uint32_t yetty_yimage_pixel_width(const struct yetty_yimage *img)
{
    return img ? img->pixel_width : 0;
}

uint32_t yetty_yimage_pixel_height(const struct yetty_yimage *img)
{
    return img ? img->pixel_height : 0;
}

/*=============================================================================
 * Spatial
 *===========================================================================*/

void yetty_yimage_set_bounds(struct yetty_yimage *img, float x, float y, float width, float height)
{
    if (!img) {
        return;
    }
    img->x = x;
    img->y = y;
    img->w = width;
    img->h = height;
    img->dirty = true;
}

void yetty_yimage_get_aabb(const struct yetty_yimage *img, float *min_x, float *min_y, float *max_x,
                           float *max_y)
{
    if (!img) {
        *min_x = *min_y = *max_x = *max_y = 0;
        return;
    }
    *min_x = img->x;
    *min_y = img->y;
    *max_x = img->x + img->w;
    *max_y = img->y + img->h;
}

/*=============================================================================
 * Display properties
 *===========================================================================*/

void yetty_yimage_set_zoom(struct yetty_yimage *img, float zoom)
{
    if (!img) {
        return;
    }
    img->zoom = zoom;
    img->dirty = true;
}

float yetty_yimage_get_zoom(const struct yetty_yimage *img)
{
    return img ? img->zoom : 1.0f;
}

void yetty_yimage_set_pan(struct yetty_yimage *img, float center_x, float center_y)
{
    if (!img) {
        return;
    }
    img->center_x = center_x;
    img->center_y = center_y;
    img->dirty = true;
}

void yetty_yimage_set_filter(struct yetty_yimage *img, uint32_t filter)
{
    if (!img) {
        return;
    }
    img->filter = filter;
    img->rs.textures[0].sampler_filter = (filter == YETTY_YIMAGE_FILTER_BILINEAR) ? 1 : 0;
    img->dirty = true;
}

/*=============================================================================
 * GPU Resource Set
 *===========================================================================*/

struct yetty_yrender_gpu_resource_set_result yetty_yimage_get_gpu_resource_set(
    struct yetty_yimage *img)
{
    if (!img) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set, "img is NULL");
    }

    if (img->dirty) {
        /* Update texture */
        struct yetty_yrender_texture *tex = &img->rs.textures[0];
        tex->data = img->pixels;
        tex->width = img->pixel_width;
        tex->height = img->pixel_height;
        tex->dirty = 1;

        /* Update uniforms */
        img->rs.uniforms[0].vec4[0] = img->x;
        img->rs.uniforms[0].vec4[1] = img->y;
        img->rs.uniforms[0].vec4[2] = img->w;
        img->rs.uniforms[0].vec4[3] = img->h;

        img->rs.uniforms[1].vec2[0] = (float)img->pixel_width;
        img->rs.uniforms[1].vec2[1] = (float)img->pixel_height;

        img->rs.uniforms[2].f32 = img->zoom;

        img->rs.uniforms[3].vec2[0] = img->center_x;
        img->rs.uniforms[3].vec2[1] = img->center_y;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &img->rs);
}

bool yetty_yimage_is_dirty(const struct yetty_yimage *img)
{
    return img ? img->dirty : false;
}

void yetty_yimage_clear_dirty(struct yetty_yimage *img)
{
    if (img) {
        img->dirty = false;
    }
}

/*=============================================================================
 * Primitive serialization
 *===========================================================================*/

uint32_t yetty_yimage_serialize_prim_header(struct yetty_yimage *img, uint32_t image_index,
                                            uint32_t col, uint32_t rolling_row, uint32_t z_order,
                                            uint32_t *buffer, uint32_t buffer_capacity)
{
    if (!img || !buffer || buffer_capacity < YETTY_YIMAGE_PRIM_HEADER_WORDS) {
        return 0;
    }

    uint32_t packed_offset = col | (rolling_row << 16);

    buffer[0] = packed_offset;

    uint32_t type_id = YETTY_YIMAGE_PRIM_TYPE_ID;
    memcpy(&buffer[1], &type_id, sizeof(type_id));

    buffer[2] = z_order;

    memcpy(&buffer[3], &img->x, sizeof(float));
    memcpy(&buffer[4], &img->y, sizeof(float));
    memcpy(&buffer[5], &img->w, sizeof(float));
    memcpy(&buffer[6], &img->h, sizeof(float));

    buffer[7] = image_index;

    return YETTY_YIMAGE_PRIM_HEADER_WORDS;
}
