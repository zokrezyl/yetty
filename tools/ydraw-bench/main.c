// ydraw-bench: Generate random SDF primitives as binary OSC sequence
// Outputs raw binary format (not YAML) for performance benchmarking

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <yetty/yface/yface.h>
#include <yetty/yterminal/dcs-codes.h> /* YETTY_DCS_YDRAW_BIN */

// Composite type for yplot
#define YETTY_YDRAW_TYPE_YPLOT 0x80000003u

// yfsvm constants
#define YFSVM_MAGIC 0x5946534Du
#define YFSVM_VERSION 1u
#define YFSVM_MAX_FUNCTIONS 16u

// yfsvm opcodes
#define YFSVM_OP_RET 0x01
#define YFSVM_OP_LOAD_X 0x03
#define YFSVM_OP_MUL 0x12
#define YFSVM_OP_SIN 0x20
#define YFSVM_OP_COS 0x21

// Encode yfsvm instruction
static inline uint32_t yfsvm_encode(uint8_t op, uint8_t dst, uint8_t src1, uint8_t src2,
                                    uint16_t imm12)
{
    return ((uint32_t)op << 24) | ((dst & 0xF) << 20) | ((src1 & 0xF) << 16) |
           ((src2 & 0xF) << 12) | (imm12 & 0xFFF);
}

// SDF primitive types (from ydraw-sdf-types.gen.h)
enum ydraw_sdf_type {
    YDRAW_SDF_CIRCLE = 0,
    YDRAW_SDF_BOX = 1,
    YDRAW_SDF_SEGMENT = 2,
    YDRAW_SDF_TRIANGLE = 3,
    YDRAW_SDF_ELLIPSE = 6,
    YDRAW_SDF_RHOMBUS = 9,
    YDRAW_SDF_PENTAGON = 10,
    YDRAW_SDF_HEXAGON = 11,
    YDRAW_SDF_STAR = 12,
    YDRAW_SDF_HEART = 15,
    YDRAW_SDF_HEXAGRAM = 29,
};

// Word counts per primitive type
static uint32_t drawable_word_count(enum ydraw_sdf_type type)
{
    switch (type) {
    case YDRAW_SDF_CIRCLE:
        return 8;
    case YDRAW_SDF_BOX:
        return 10;
    case YDRAW_SDF_SEGMENT:
        return 9;
    case YDRAW_SDF_TRIANGLE:
        return 11;
    case YDRAW_SDF_ELLIPSE:
        return 9;
    case YDRAW_SDF_RHOMBUS:
        return 9;
    case YDRAW_SDF_PENTAGON:
        return 8;
    case YDRAW_SDF_HEXAGON:
        return 8;
    case YDRAW_SDF_STAR:
        return 10;
    case YDRAW_SDF_HEART:
        return 8;
    case YDRAW_SDF_HEXAGRAM:
        return 8;
    default:
        return 0;
    }
}

// Available primitive types for random selection
static const enum ydraw_sdf_type PRIM_TYPES[] = {
    YDRAW_SDF_CIRCLE,  YDRAW_SDF_BOX,     YDRAW_SDF_SEGMENT,  YDRAW_SDF_TRIANGLE,
    YDRAW_SDF_ELLIPSE, YDRAW_SDF_RHOMBUS, YDRAW_SDF_PENTAGON, YDRAW_SDF_HEXAGON,
    YDRAW_SDF_STAR,    YDRAW_SDF_HEART,   YDRAW_SDF_HEXAGRAM,
};
#define NUM_PRIM_TYPES (sizeof(PRIM_TYPES) / sizeof(PRIM_TYPES[0]))

// Fast xorshift64 PRNG
static uint64_t g_rng_state = 1;

static inline uint64_t xorshift64(void)
{
    uint64_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng_state = x;
    return x;
}

static inline float randf(float min, float max)
{
    return min + (float)(xorshift64() & 0xFFFFFF) / 16777215.0f * (max - min);
}

static inline uint32_t rand_color(void)
{
    uint64_t r = xorshift64();
    return (uint32_t)((0x80 + (r & 0x7F)) << 24) | (uint32_t)(r >> 8);
}

static inline uint32_t randu32(void)
{
    return (uint32_t)xorshift64();
}

// Write u32 as float bits
static void write_u32(float *buf, uint32_t val)
{
    memcpy(buf, &val, sizeof(uint32_t));
}

// Generate single random primitive into buffer, returns words written
static uint32_t gen_primitive(float *buf, float scene_w, float scene_h)
{
    (void)scene_h; // unused - we use cursor-relative coords
    enum ydraw_sdf_type type = PRIM_TYPES[randu32() % NUM_PRIM_TYPES];
    uint32_t word_count = drawable_word_count(type);

    // Common header: type, z_order, fill, stroke, stroke_width
    write_u32(&buf[0], type);
    write_u32(&buf[1], randu32() % 100); // z_order
    write_u32(&buf[2], rand_color());    // fill
    write_u32(&buf[3], rand_color());    // stroke
    buf[4] = randf(0.5f, 3.0f);          // stroke_width

    // Cursor-relative coordinates - small Y values near cursor
    float cx = randf(0, scene_w);
    float cy = randf(10.0f, 150.0f); // relative to cursor, not absolute
    float size = randf(10.0f, 50.0f);

    switch (type) {
    case YDRAW_SDF_CIRCLE:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;
        break;
    case YDRAW_SDF_BOX:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;            // half_width
        buf[8] = size * 0.7f;     // half_height
        buf[9] = randf(0, 10.0f); // corner_radius
        break;
    case YDRAW_SDF_SEGMENT:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = cx + randf(-100, 100);
        buf[8] = cy + randf(-100, 100);
        break;
    case YDRAW_SDF_TRIANGLE:
        buf[5] = cx;
        buf[6] = cy - size;
        buf[7] = cx - size;
        buf[8] = cy + size;
        buf[9] = cx + size;
        buf[10] = cy + size;
        break;
    case YDRAW_SDF_ELLIPSE:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;
        buf[8] = size * 0.6f;
        break;
    case YDRAW_SDF_RHOMBUS:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;
        buf[8] = size * 0.8f;
        break;
    case YDRAW_SDF_PENTAGON:
    case YDRAW_SDF_HEXAGON:
    case YDRAW_SDF_HEXAGRAM:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;
        break;
    case YDRAW_SDF_STAR:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size;
        buf[8] = 5.0f + (float)(randu32() % 4); // num_points: 5-8
        buf[9] = randf(0.3f, 0.6f);             // inner_ratio
        break;
    case YDRAW_SDF_HEART:
        buf[5] = cx;
        buf[6] = cy;
        buf[7] = size * 0.5f;
        break;
    default:
        break;
    }

    return word_count;
}

// Default colors for plot functions
static const uint32_t PLOT_COLORS[] = {
    0xFF0000FF, // Red
    0xFF00FF00, // Green
    0xFFFF0000, // Blue
    0xFF00FFFF, // Cyan
    0xFFFF00FF, // Magenta
    0xFFFFFF00, // Yellow
    0xFFFF8000, // Orange
    0xFF8000FF, // Purple
};

// Generate yplot primitive into buffer, returns bytes written
static uint32_t gen_yplot(uint8_t *buf, float scene_w, float scene_h, int func_count)
{
    (void)scene_w;
    (void)scene_h;
    uint32_t *u32buf = (uint32_t *)buf;
    float *fbuf = (float *)buf;
    uint32_t pos = 0;

    // FAM header: type, payload_size (filled in later)
    u32buf[pos++] = YETTY_YDRAW_TYPE_YPLOT;
    uint32_t payload_size_pos = pos++;

    // Wire format starts here
    uint32_t wire_start = pos;

    // Bounds (float)
    fbuf[pos++] = 50.0f;  // bounds_x
    fbuf[pos++] = 50.0f;  // bounds_y
    fbuf[pos++] = 400.0f; // bounds_w
    fbuf[pos++] = 200.0f; // bounds_h

    // Flags
    u32buf[pos++] = 0x07; // grid | axes | functions

    // Function count
    if (func_count < 1) {
        func_count = 1;
    }
    if (func_count > 8) {
        func_count = 8;
    }
    u32buf[pos++] = (uint32_t)func_count;

    // Colors[8]
    for (int i = 0; i < 8; i++) {
        u32buf[pos++] = (i < func_count) ? PLOT_COLORS[i] : 0;
    }

    // Bytecode
    // Header: magic, version, func_count, const_count
    uint32_t bc_pos = 0;
    uint32_t bytecode[256];

    bytecode[bc_pos++] = YFSVM_MAGIC;
    bytecode[bc_pos++] = YFSVM_VERSION;
    bytecode[bc_pos++] = (uint32_t)func_count;
    bytecode[bc_pos++] = 0; // const_count = 0

    // Function table: packed (length:16 | offset:16) for each function
    for (int i = 0; i < (int)YFSVM_MAX_FUNCTIONS; i++) {
        if (i < func_count) {
            // Each function has 3 instructions: LOAD_X, op, RET
            uint32_t offset = i * 3;
            uint32_t length = 3;
            bytecode[bc_pos++] = (length << 16) | offset;
        } else {
            bytecode[bc_pos++] = 0;
        }
    }

    // Code starts after header (4) + function table (YFSVM_MAX_FUNCTIONS)
    // bc_pos is already at the right position after function table
    for (int i = 0; i < func_count; i++) {
        // r1 = x
        bytecode[bc_pos++] = yfsvm_encode(YFSVM_OP_LOAD_X, 1, 0, 0, 0);
        // r0 = sin(r1) or cos(r1) depending on function index
        if (i % 2 == 0) {
            bytecode[bc_pos++] = yfsvm_encode(YFSVM_OP_SIN, 0, 1, 0, 0);
        } else {
            bytecode[bc_pos++] = yfsvm_encode(YFSVM_OP_COS, 0, 1, 0, 0);
        }
        // ret
        bytecode[bc_pos++] = yfsvm_encode(YFSVM_OP_RET, 0, 0, 0, 0);
    }

    // Write bytecode_word_count and bytecode
    u32buf[pos++] = bc_pos; // bytecode_word_count
    for (uint32_t i = 0; i < bc_pos; i++) {
        u32buf[pos++] = bytecode[i];
    }

    // Fill in payload_size (in bytes, not words)
    uint32_t payload_words = pos - wire_start;
    u32buf[payload_size_pos] = payload_words * 4;

    return pos * 4; // return bytes
}

// Buffer for primitives
#define MAX_BUFFER_SIZE (1024 * 1024) // 1MB
static float g_buffer[MAX_BUFFER_SIZE / sizeof(float)];

int main(int argc, char **argv)
{
    uint32_t count = 100;
    uint32_t seed = 0;
    float scene_w = 800.0f;
    float scene_h = 600.0f;
    int loop_count = 1; // 1 = single run, -1 = infinite, >1 = N iterations
    int delay_ms = 16;  // ~60fps
    int plot_count = 0; // Number of plots to generate (0 = SDF mode)
    int plot_funcs = 2; // Number of functions per plot

    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            scene_w = atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            scene_h = atof(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--plot") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                plot_count = atoi(argv[++i]);
            } else {
                plot_count = 1;
            }
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            plot_funcs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--loop") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                loop_count = atoi(argv[++i]);
                if (loop_count <= 0) {
                    loop_count = -1; // infinite
                }
            } else {
                loop_count = -1; // infinite
            }
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delay_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "Usage: %s [options]\n"
                    "  -n <count>  Number of SDF primitives (default: 100)\n"
                    "  -p [count]  Generate plots instead of SDF (default: 1 if no count)\n"
                    "  -f <funcs>  Functions per plot (default: 2, max: 8)\n"
                    "  -s <seed>   Random seed (default: time-based)\n"
                    "  -w <width>  Scene width (default: 800)\n"
                    "  -h <height> Scene height (default: 600)\n"
                    "  -l [N]      Loop N times (default: infinite if no N)\n"
                    "  -d <ms>     Delay between frames in loop mode (default: 16)\n",
                    argv[0]);
            return 0;
        }
    }

    g_rng_state = seed ? seed : (uint64_t)time(NULL);

    do {
        size_t raw_bytes;

        if (plot_count > 0) {
            // Generate plots
            uint8_t *bytebuf = (uint8_t *)g_buffer;
            uint32_t total_bytes = 0;
            for (int i = 0; i < plot_count; i++) {
                if (total_bytes >= MAX_BUFFER_SIZE - 1024) {
                    break;
                }
                total_bytes += gen_yplot(&bytebuf[total_bytes], scene_w, scene_h, plot_funcs);
            }
            raw_bytes = total_bytes;
        } else {
            // Generate SDF primitives
            uint32_t total_words = 0;
            for (uint32_t i = 0; i < count; i++) {
                if (total_words >= MAX_BUFFER_SIZE / sizeof(float) - 20) {
                    break;
                }
                total_words += gen_primitive(&g_buffer[total_words], scene_w, scene_h);
            }
            raw_bytes = total_words * sizeof(float);
        }

        // OSC sequence via yface: ESC ] 600001 ; <b64(meta)> ; <b64(LZ4F(bytes))> ST
        struct yetty_yface_bin_meta meta = {
            .magic = YETTY_YFACE_BIN_MAGIC,
            .version = YETTY_YFACE_BIN_VERSION,
            .compressed = YETTY_YFACE_COMP_LZ4F,
            .compression_algo = 0,
            .raw_size = raw_bytes,
            .reserved = {0, 0},
        };
        struct yetty_ycore_void_result rr =
            yetty_yface_emit_to_fd(fileno(stdout), YETTY_DCS_YDRAW_BIN,
                                   /*compressed=*/1, &meta, sizeof(meta), g_buffer, raw_bytes);
        if (YETTY_IS_ERR(rr)) {
            fprintf(stderr, "yface_emit: %s\n", rr.error.msg);
            return 1;
        }
        fflush(stdout);

        if (loop_count != 1) {
#ifdef _WIN32
            Sleep((DWORD)delay_ms);
#else
            struct timespec ts = {.tv_sec = delay_ms / 1000,
                                  .tv_nsec = (delay_ms % 1000) * 1000000L};
            nanosleep(&ts, NULL);
#endif
            g_rng_state++;
            if (loop_count > 1) {
                loop_count--;
            }
        }
    } while (loop_count != 1);

    return 0;
}
