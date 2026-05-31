/*
 * wav.c - RIFF/WAVE PCM reader, mmap-backed.
 *
 * The whole file is mapped read-only so we never copy sample bytes into
 * userspace until a caller asks for a decoded float slice. This is what
 * makes the reader usable on multi-gigabyte recordings: open() is O(1)
 * in I/O regardless of file size.
 */

#include <yetty/yaudio/wav.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* WAVE_FORMAT tag values (from MS RIFF spec) */
enum {
    WAVE_FORMAT_PCM = 0x0001,
    WAVE_FORMAT_IEEE_FLOAT = 0x0003,
    WAVE_FORMAT_EXTENSIBLE = 0xFFFE,
};

/* Little-endian readers. The WAV container is little-endian on every
 * platform the spec covers; we don't byteswap host-side. */
static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool tag_eq(const uint8_t *p, const char *tag)
{
    return memcmp(p, tag, 4) == 0;
}

static struct yetty_yaudio_wav_ptr_result parse_fmt_chunk(struct yetty_yaudio_wav *w,
                                                          const uint8_t *p, uint32_t size)
{
    if (size < 16) {
        return YETTY_ERR(yetty_yaudio_wav_ptr, "fmt chunk smaller than 16 bytes");
    }
    uint16_t fmt_tag = rd_u16le(p + 0);
    uint16_t channels = rd_u16le(p + 2);
    uint32_t sample_rate = rd_u32le(p + 4);
    uint16_t bits = rd_u16le(p + 14);

    /* WAVE_FORMAT_EXTENSIBLE: real format tag lives in the SubFormat
     * GUID (first 2 bytes of the 16-byte GUID at offset 24). */
    if (fmt_tag == WAVE_FORMAT_EXTENSIBLE) {
        if (size < 40) {
            return YETTY_ERR(yetty_yaudio_wav_ptr, "extensible fmt chunk truncated");
        }
        fmt_tag = rd_u16le(p + 24);
    }

    enum yetty_yaudio_wav_sample_format sfmt;
    switch (fmt_tag) {
    case WAVE_FORMAT_PCM:
        switch (bits) {
        case 16:
            sfmt = YETTY_YAUDIO_WAV_SAMPLE_FMT_S16;
            break;
        case 24:
            sfmt = YETTY_YAUDIO_WAV_SAMPLE_FMT_S24;
            break;
        case 32:
            sfmt = YETTY_YAUDIO_WAV_SAMPLE_FMT_S32;
            break;
        default:
            return YETTY_ERR(yetty_yaudio_wav_ptr,
                             "unsupported PCM bits-per-sample (need 16/24/32)");
        }
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        if (bits != 32) {
            return YETTY_ERR(yetty_yaudio_wav_ptr, "only float32 IEEE_FLOAT is supported");
        }
        sfmt = YETTY_YAUDIO_WAV_SAMPLE_FMT_F32;
        break;
    default:
        return YETTY_ERR(yetty_yaudio_wav_ptr,
                         "unsupported WAVE format tag (need PCM or IEEE_FLOAT)");
    }

    if (channels == 0 || sample_rate == 0) {
        return YETTY_ERR(yetty_yaudio_wav_ptr, "fmt chunk has zero channels or sample rate");
    }

    w->fmt = sfmt;
    w->channels = channels;
    w->sample_rate = sample_rate;
    w->bits_per_sample = bits;
    w->bytes_per_sample = (uint16_t)(bits / 8);
    w->frame_stride = (uint16_t)(channels * w->bytes_per_sample);
    return YETTY_OK(yetty_yaudio_wav_ptr, w);
}

struct yetty_yaudio_wav_ptr_result yetty_yaudio_wav_open(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: NULL path");
    }

#ifdef _WIN32
    /* Win32 file mapping — same read-only whole-file view as the POSIX
     * mmap path, so the rest of the reader stays platform-agnostic. */
    HANDLE hfile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hfile == INVALID_HANDLE_VALUE) {
        yerror("wav_open: CreateFile(%s) failed (err=%lu)", path, (unsigned long)GetLastError());
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: open failed");
    }
    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(hfile, &fsz)) {
        CloseHandle(hfile);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: GetFileSizeEx failed");
    }
    if ((uint64_t)fsz.QuadPart < 44) {
        CloseHandle(hfile);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: file too small for RIFF header");
    }
    HANDLE hmap = CreateFileMappingA(hfile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hmap) {
        CloseHandle(hfile);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: CreateFileMapping failed");
    }
    void *map = MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, 0);
    if (!map) {
        CloseHandle(hmap);
        CloseHandle(hfile);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: MapViewOfFile failed");
    }
    size_t map_size = (size_t)fsz.QuadPart;

    struct yetty_yaudio_wav *w = calloc(1, sizeof(struct yetty_yaudio_wav));
    if (!w) {
        UnmapViewOfFile(map);
        CloseHandle(hmap);
        CloseHandle(hfile);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: calloc failed");
    }
    w->fd = -1;
    w->win_file = hfile;
    w->win_mapping = hmap;
    w->map_base = map;
    w->map_size = map_size;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        yerror("wav_open: open(%s) failed: %s", path, strerror(errno));
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: open failed");
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: fstat failed");
    }
    if ((size_t)st.st_size < 44) {
        close(fd);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: file too small for RIFF header");
    }

    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: mmap failed");
    }

    /* Hint the kernel: we'll read sequentially across the whole file. */
    madvise(map, (size_t)st.st_size, MADV_SEQUENTIAL);

    struct yetty_yaudio_wav *w = calloc(1, sizeof(struct yetty_yaudio_wav));
    if (!w) {
        munmap(map, (size_t)st.st_size);
        close(fd);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: calloc failed");
    }
    w->fd = fd;
    w->map_base = map;
    w->map_size = (size_t)st.st_size;
#endif

    const uint8_t *p = w->map_base;
    const uint8_t *end = p + w->map_size;

    /* RIFF header: "RIFF" <size> "WAVE" */
    if (!tag_eq(p, "RIFF") || !tag_eq(p + 8, "WAVE")) {
        yetty_yaudio_wav_close(w);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: not a RIFF/WAVE file");
    }
    p += 12;

    bool have_fmt = false;
    bool have_data = false;

    while (p + 8 <= end) {
        const uint8_t *tag = p;
        uint32_t csz_field = rd_u32le(p + 4);
        p += 8;
        size_t remaining = (size_t)(end - p);

        if (tag_eq(tag, "data")) {
            /* The RIFF `data` chunk size is a uint32 — so a single
             * chunk can legitimately address up to 4 GiB. Two real-
             * world cases where the on-disk value is unreliable:
             *
             *   (1) Recorder firmware caps the size field at INT32_MAX
             *       (0x7FFFFFFF) for size_t/ssize_t conservatism, even
             *       when the actual data is one frame larger. Observed
             *       on Zoom H2e 4-channel float32 captures: real data
             *       is exactly 2 GiB but csz reports 2 GiB - 1.
             *   (2) Files written in the "extends to EOF" style use
             *       0xFFFFFFFF as a sentinel (the recorder didn't know
             *       the final size at write time).
             *
             * Defensive policy for the data chunk: trust the file size.
             * If csz lands on either sentinel or overruns the file, use
             * the actual remaining bytes. Sub-frame discrepancies (e.g.
             * the Zoom 1-byte short) are absorbed by frame_stride
             * rounding when we compute w->frames. */
            size_t csz;
            if (csz_field == 0x7FFFFFFFu || csz_field == 0xFFFFFFFFu ||
                (size_t)csz_field > remaining) {
                csz = remaining;
                if ((size_t)csz_field != remaining) {
                    yinfo("wav_open: data csz=0x%08x %s file remaining=%zu — "
                          "using file size (%zu bytes)",
                          csz_field,
                          (size_t)csz_field > remaining ? "overruns"
                                                        : "= signed/unsigned max sentinel",
                          remaining, csz);
                }
            } else {
                csz = (size_t)csz_field;
            }
            w->data = p;
            w->data_size = csz;
            have_data = true;
            /* Typical layout is fmt-then-data; once we have both we're done. */
            if (have_fmt) {
                break;
            }
            /* Chunks are word-aligned: skip pad byte if size is odd. */
            p += csz + (csz & 1u);
            continue;
        }

        /* Non-data chunks: keep the strict overrun check — these are
         * small metadata blocks and a bogus size means real corruption. */
        if (remaining < (size_t)csz_field) {
            yetty_yaudio_wav_close(w);
            return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: chunk size overruns file");
        }
        uint32_t csz = csz_field;
        if (tag_eq(tag, "fmt ")) {
            struct yetty_yaudio_wav_ptr_result fr = parse_fmt_chunk(w, p, csz);
            if (YETTY_IS_ERR(fr)) {
                yetty_yaudio_wav_close(w);
                return fr;
            }
            have_fmt = true;
        }
        /* Word-aligned skip. */
        p += csz + (csz & 1u);
    }

    if (!have_fmt) {
        yetty_yaudio_wav_close(w);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: missing fmt chunk");
    }
    if (!have_data) {
        yetty_yaudio_wav_close(w);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: missing data chunk");
    }
    if (w->frame_stride == 0) {
        yetty_yaudio_wav_close(w);
        return YETTY_ERR(yetty_yaudio_wav_ptr, "wav_open: zero frame stride");
    }
    w->frames = w->data_size / w->frame_stride;

    yinfo("wav_open(%s): %u Hz, %u ch, %u-bit %s, %zu frames (%.3f s)", path, w->sample_rate,
          w->channels, w->bits_per_sample,
          w->fmt == YETTY_YAUDIO_WAV_SAMPLE_FMT_F32 ? "float" : "PCM", w->frames,
          (double)w->frames / (double)w->sample_rate);

    return YETTY_OK(yetty_yaudio_wav_ptr, w);
}

void yetty_yaudio_wav_close(struct yetty_yaudio_wav *w)
{
    if (!w) {
        return;
    }
#ifdef _WIN32
    if (w->map_base) {
        UnmapViewOfFile((void *)w->map_base);
    }
    if (w->win_mapping) {
        CloseHandle((HANDLE)w->win_mapping);
    }
    if (w->win_file) {
        CloseHandle((HANDLE)w->win_file);
    }
#else
    if (w->map_base && w->map_size) {
        munmap((void *)w->map_base, w->map_size);
    }
    if (w->fd >= 0) {
        close(w->fd);
    }
#endif
    free(w);
}

/* Per-sample decode helpers. The 24-bit path packs three bytes into a
 * signed 32-bit value and divides by 2^31 — same scale convention as
 * S32 so the caller doesn't care about container width. */
static inline float decode_s16(const uint8_t *p)
{
    int16_t v = (int16_t)(p[0] | (p[1] << 8));
    return (float)v * (1.0f / 32768.0f);
}

static inline float decode_s24(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24);
    v >>= 8; /* arithmetic shift sign-extends from bit 23 */
    return (float)v * (1.0f / 8388608.0f);
}

static inline float decode_s32(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                          (uint32_t)p[3] << 24);
    return (float)v * (1.0f / 2147483648.0f);
}

static inline float decode_f32(const uint8_t *p)
{
    float v;
    memcpy(&v, p, sizeof(float));
    return v;
}

struct yetty_ycore_size_result yetty_yaudio_wav_read_channel_f32(const struct yetty_yaudio_wav *w,
                                                                 uint32_t channel, size_t frame_off,
                                                                 float *out, size_t n_frames)
{
    if (!w || !out) {
        return YETTY_ERR(yetty_ycore_size, "read_channel_f32: NULL arg");
    }
    if (channel >= w->channels) {
        return YETTY_ERR(yetty_ycore_size, "read_channel_f32: channel out of range");
    }
    if (frame_off >= w->frames) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    size_t avail = w->frames - frame_off;
    if (n_frames > avail) {
        n_frames = avail;
    }

    const uint8_t *base =
        w->data + frame_off * w->frame_stride + (size_t)channel * w->bytes_per_sample;
    const uint16_t stride = w->frame_stride;

    switch (w->fmt) {
    case YETTY_YAUDIO_WAV_SAMPLE_FMT_S16:
        for (size_t i = 0; i < n_frames; i++) {
            out[i] = decode_s16(base + i * stride);
        }
        break;
    case YETTY_YAUDIO_WAV_SAMPLE_FMT_S24:
        for (size_t i = 0; i < n_frames; i++) {
            out[i] = decode_s24(base + i * stride);
        }
        break;
    case YETTY_YAUDIO_WAV_SAMPLE_FMT_S32:
        for (size_t i = 0; i < n_frames; i++) {
            out[i] = decode_s32(base + i * stride);
        }
        break;
    case YETTY_YAUDIO_WAV_SAMPLE_FMT_F32:
        for (size_t i = 0; i < n_frames; i++) {
            out[i] = decode_f32(base + i * stride);
        }
        break;
    default:
        return YETTY_ERR(yetty_ycore_size, "read_channel_f32: unknown sample format");
    }

    return YETTY_OK(yetty_ycore_size, n_frames);
}
