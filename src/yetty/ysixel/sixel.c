/* ysixel — sixel DCS-payload decoder + yimage-prim packager. See sixel.h. */

#include <yetty/ysixel/sixel.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yimage/yimage-gen.h>

enum {
    YETTY_YSIXEL_MAX_WIDTH = 10000,
    YETTY_YSIXEL_MAX_HEIGHT = 10000,
    YETTY_YSIXEL_MAX_PIXELS = 16u * 1024u * 1024u, /* 16 Mpx cap */
    YETTY_YSIXEL_PARAM_CAP = 1000000,
};

/* Decode cursor. `rgba` is NULL in the measure pass (pass 1) and the real
 * buffer in the fill pass (pass 2); the two passes share this same walker. */
struct yetty_ysixel_decoder {
    uint8_t palette[256][3];
    int color;
    uint32_t x;
    uint32_t band_top;
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
    uint32_t cap_w;
    uint32_t cap_h;
};

/* VT340 default 16-colour palette (RGB 0..255). */
static void ysixel_default_palette(uint8_t palette[256][3])
{
    static const uint8_t base[16][3] = {
        {0, 0, 0},      {51, 51, 204},  {204, 33, 33},  {51, 204, 51},
        {204, 51, 204}, {51, 204, 204}, {204, 204, 51}, {135, 135, 135},
        {66, 66, 66},   {84, 84, 148},  {148, 66, 66},  {84, 148, 84},
        {148, 84, 148}, {84, 148, 148}, {148, 148, 84}, {204, 204, 204},
    };
    memset(palette, 0, 256 * 3);
    memcpy(palette, base, sizeof(base));
}

static uint8_t ysixel_scale_100(long value)
{
    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }
    return (uint8_t)((value * 255 + 50) / 100);
}

static double ysixel_absf(double value)
{
    return value < 0.0 ? -value : value;
}

/* Sixel HLS (hue 0..360 with 0 = blue) → RGB, via standard HSL with the hue
 * rotated +240 degrees onto the red-origin wheel (matches libsixel). */
static void ysixel_hls_to_rgb(long hue, long light, long sat, uint8_t out[3])
{
    double lightness = (light < 0 ? 0 : light > 100 ? 100 : light) / 100.0;
    double saturation = (sat < 0 ? 0 : sat > 100 ? 100 : sat) / 100.0;
    double sector = (double)((((hue + 240) % 360) + 360) % 360) / 60.0;
    double chroma = (1.0 - ysixel_absf(lightness * 2.0 - 1.0)) * saturation;
    double intra = sector - 2.0 * (double)((long)(sector / 2.0));
    double second = chroma * (1.0 - ysixel_absf(intra - 1.0));
    double red = 0.0, green = 0.0, blue = 0.0;
    if (sector < 1.0) {
        red = chroma;
        green = second;
    } else if (sector < 2.0) {
        red = second;
        green = chroma;
    } else if (sector < 3.0) {
        green = chroma;
        blue = second;
    } else if (sector < 4.0) {
        green = second;
        blue = chroma;
    } else if (sector < 5.0) {
        red = second;
        blue = chroma;
    } else {
        red = chroma;
        blue = second;
    }
    double match = lightness - chroma / 2.0;
    out[0] = (uint8_t)((red + match) * 255.0 + 0.5);
    out[1] = (uint8_t)((green + match) * 255.0 + 0.5);
    out[2] = (uint8_t)((blue + match) * 255.0 + 0.5);
}

/* Paint the six vertical pixels of the current column selected by `bits`
 * (bit 0 = top). Extends width/height; writes only in the fill pass. */
static void ysixel_put(struct yetty_ysixel_decoder *decoder, uint32_t bits)
{
    for (int row = 0; row < 6; row++) {
        if (!(bits & (1u << row))) {
            continue;
        }
        uint32_t pixel_y = decoder->band_top + (uint32_t)row;
        uint32_t pixel_x = decoder->x;
        if (pixel_x >= YETTY_YSIXEL_MAX_WIDTH || pixel_y >= YETTY_YSIXEL_MAX_HEIGHT) {
            continue;
        }
        if (pixel_x + 1 > decoder->width) {
            decoder->width = pixel_x + 1;
        }
        if (pixel_y + 1 > decoder->height) {
            decoder->height = pixel_y + 1;
        }
        if (decoder->rgba && pixel_x < decoder->cap_w && pixel_y < decoder->cap_h) {
            uint8_t *pixel = &decoder->rgba[((size_t)pixel_y * decoder->cap_w + pixel_x) * 4];
            pixel[0] = decoder->palette[decoder->color & 0xFF][0];
            pixel[1] = decoder->palette[decoder->color & 0xFF][1];
            pixel[2] = decoder->palette[decoder->color & 0xFF][2];
            pixel[3] = 255;
        }
    }
}

/* Parse a ';'-separated run of decimal parameters at *pos into out[]. A missing
 * field yields -1. Advances *pos past the run. */
static int ysixel_parse_params(const char *data, size_t len, size_t *pos, long *out, int max_params)
{
    int count = 0;
    while (count < max_params) {
        long value = 0;
        int have = 0;
        while (*pos < len && data[*pos] >= '0' && data[*pos] <= '9') {
            value = value * 10 + (data[*pos] - '0');
            if (value > YETTY_YSIXEL_PARAM_CAP) {
                value = YETTY_YSIXEL_PARAM_CAP;
            }
            have = 1;
            (*pos)++;
        }
        out[count++] = have ? value : -1;
        if (*pos < len && data[*pos] == ';') {
            (*pos)++;
            continue;
        }
        break;
    }
    return count;
}

/* Walk the sixel data once. Writes pixels when decoder->rgba is set, otherwise
 * only measures width/height. */
static void ysixel_run(struct yetty_ysixel_decoder *decoder, const char *data, size_t len)
{
    decoder->color = 0;
    decoder->x = 0;
    decoder->band_top = 0;
    size_t pos = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char)data[pos];
        if (ch >= 0x3F && ch <= 0x7E) {
            ysixel_put(decoder, (uint32_t)(ch - 0x3F));
            decoder->x++;
            pos++;
        } else if (ch == '!') {
            pos++;
            long params[1];
            ysixel_parse_params(data, len, &pos, params, 1);
            long repeat = params[0] < 0 ? 1 : params[0];
            if (pos < len && (unsigned char)data[pos] >= 0x3F && (unsigned char)data[pos] <= 0x7E) {
                uint32_t bits = (uint32_t)((unsigned char)data[pos] - 0x3F);
                for (long run = 0; run < repeat; run++) {
                    ysixel_put(decoder, bits);
                    decoder->x++;
                }
                pos++;
            }
        } else if (ch == '#') {
            pos++;
            long params[5];
            int count = ysixel_parse_params(data, len, &pos, params, 5);
            int index = params[0] < 0 ? 0 : (int)(params[0] & 0xFF);
            if (count >= 5) {
                uint8_t rgb[3];
                if (params[1] == 1) {
                    ysixel_hls_to_rgb(params[2], params[3], params[4], rgb);
                } else {
                    rgb[0] = ysixel_scale_100(params[2]);
                    rgb[1] = ysixel_scale_100(params[3]);
                    rgb[2] = ysixel_scale_100(params[4]);
                }
                decoder->palette[index][0] = rgb[0];
                decoder->palette[index][1] = rgb[1];
                decoder->palette[index][2] = rgb[2];
            }
            decoder->color = index;
        } else if (ch == '$') {
            decoder->x = 0; /* carriage return: back to left margin, same band */
            pos++;
        } else if (ch == '-') {
            decoder->x = 0; /* new line: next 6-pixel band */
            decoder->band_top += 6;
            pos++;
        } else if (ch == '"') {
            pos++;
            long params[4];
            ysixel_parse_params(data, len, &pos, params, 4); /* raster attrs — sizing hint */
        } else {
            pos++; /* whitespace / unknown byte */
        }
    }
}

void yetty_ysixel_image_destroy(struct yetty_ysixel_image *image)
{
    if (!image) {
        return;
    }
    free(image->rgba);
    image->rgba = NULL;
    image->width = 0;
    image->height = 0;
}

struct yetty_ysixel_image_result yetty_ysixel_decode(const char *data, size_t len)
{
    if (!data || len == 0) {
        return YETTY_ERR(yetty_ysixel_image, "ysixel: empty payload");
    }

    struct yetty_ysixel_decoder measure;
    memset(&measure, 0, sizeof(measure));
    ysixel_default_palette(measure.palette);
    ysixel_run(&measure, data, len);

    uint32_t width = measure.width;
    uint32_t height = measure.height;
    if (width == 0 || height == 0) {
        return YETTY_ERR(yetty_ysixel_image, "ysixel: no pixels decoded");
    }
    if ((uint64_t)width * height > YETTY_YSIXEL_MAX_PIXELS) {
        return YETTY_ERR(yetty_ysixel_image, "ysixel: image exceeds pixel cap");
    }

    uint8_t *rgba = calloc((size_t)width * height, 4);
    if (!rgba) {
        return YETTY_ERR(yetty_ysixel_image, "ysixel: pixel alloc failed");
    }

    struct yetty_ysixel_decoder fill;
    memset(&fill, 0, sizeof(fill));
    ysixel_default_palette(fill.palette);
    fill.rgba = rgba;
    fill.cap_w = width;
    fill.cap_h = height;
    ysixel_run(&fill, data, len);

    struct yetty_ysixel_image image = {.width = width, .height = height, .rgba = rgba};
    return YETTY_OK(yetty_ysixel_image, image);
}

struct yetty_ydraw_drawable_list_result yetty_ysixel_render(
    const char *data, size_t len, const struct yetty_ysixel_render_config *config)
{
    struct yetty_ysixel_image_result decode_res = yetty_ysixel_decode(data, len);
    if (YETTY_IS_ERR(decode_res)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ysixel render: decode", decode_res);
    }
    struct yetty_ysixel_image image = decode_res.value;

    /* Display bounds: native size, or aspect-preserving downscale when wider
     * than the pane (identical policy to the mime image renderer). */
    float display_w = (float)image.width;
    float display_h = (float)image.height;
    if (config && config->max_width_px > 0.0f && display_w > config->max_width_px) {
        display_h = config->max_width_px * display_h / display_w;
        display_w = config->max_width_px;
    }

    /* One yimage prim from the raw RGBA — the yimage factory renders type
     * 0x80000004, so no receiver-side registration is needed. */
    struct yetty_yimage_uniforms uniforms = {0};
    uniforms.bounds_w = display_w;
    uniforms.bounds_h = display_h;
    uniforms.image_w = image.width;
    uniforms.image_h = image.height;
    struct yetty_yimage_buffers buffers = {
        .pixels = (const uint32_t *)(const void *)image.rgba,
        .pixels_len = (size_t)image.width * image.height,
    };

    size_t required = yetty_yimage_uniforms_serialized_size(&uniforms, &buffers);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        yetty_ysixel_image_destroy(&image);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ysixel render: prim alloc");
    }
    struct yetty_ycore_size_result serialize_res =
        yetty_yimage_uniforms_serialize(&uniforms, &buffers, prim_buf, required);
    yetty_ysixel_image_destroy(&image);
    if (YETTY_IS_ERR(serialize_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ysixel render: serialize", serialize_res);
    }

    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = display_w,
        .scene_max_y = display_h,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    if (YETTY_IS_ERR(list_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ysixel render: list create", list_res);
    }
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(list_res.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ysixel render: add_prim", add_res);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list_res.value);
}
