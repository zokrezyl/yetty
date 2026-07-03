/*
 * yvnc capture protocol contract test (#425) — pure, headless.
 *
 * The VNC server's framebuffer capture is GPU/socket-bound, but the tile-grid
 * math and the packed wire-format headers a client parses are pure. This pins:
 *   - vnc_tiles_x/y: how a framebuffer (incl. HiDPI-doubled sizes) divides into
 *     64px capture tiles, rounding up,
 *   - the byte size of each packed wire header (clients parse by fixed offset,
 *     so any field drift must fail here),
 *   - the tile-size and frame-magic constants.
 * No GPU, no socket.
 */

#include <yetty/yvnc/protocol.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------
 * Tile grid: a WxH framebuffer divides into ceil(W/64) x ceil(H/64) tiles.
 *-------------------------------------------------------------------------*/
static void test_tile_grid(struct ytest *test)
{
    /* Exact multiples of the 64px tile. */
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(64), 1);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(128), 2);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_y(64), 1);

    /* Partial tiles round up. */
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(65), 2);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(1), 1);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(0), 0);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_y(1080), 17); /* 1080/64 = 16.875 → 17 */

    /* Common + HiDPI-doubled resolutions. */
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(1920), 30);
    YTEST_CHECK_EQ_INT(test, vnc_tiles_x(2560), 40); /* 2x of 1280 */
    YTEST_CHECK_EQ_INT(test, vnc_tiles_y(1600), 25); /* 2x of 800 */
}

/*---------------------------------------------------------------------------
 * Packed wire headers keep a fixed byte layout (clients parse by offset).
 *-------------------------------------------------------------------------*/
static void test_wire_format(struct ytest *test)
{
    /* magic(4)+w(2)+h(2)+tile_size(2)+num_tiles(2)+seq(4) = 16 */
    YTEST_CHECK_EQ_SIZE(test, sizeof(struct yetty_yvnc_vnc_frame_header), 16u);
    /* tile_x(2)+tile_y(2)+encoding(1)+data_size(4) = 9 */
    YTEST_CHECK_EQ_SIZE(test, sizeof(struct yetty_yvnc_vnc_tile_header), 9u);
    /* px_x(2)+px_y(2)+w(2)+h(2)+encoding(1)+reserved(1)+data_size(4) = 14 */
    YTEST_CHECK_EQ_SIZE(test, sizeof(struct yetty_yvnc_vnc_rect_header), 14u);

    YTEST_CHECK_EQ_INT(test, VNC_TILE_SIZE, 64);
    YTEST_CHECK_EQ_INT(test, VNC_FRAME_MAGIC, 0x594E4346); /* "YNCF" */

    /* Encoding enum is a stable wire contract. */
    YTEST_CHECK_EQ_INT(test, YETTY_YVNC_VNC_ENCODING_RAW, 0);
    YTEST_CHECK_EQ_INT(test, YETTY_YVNC_VNC_ENCODING_JPEG, 2);
    YTEST_CHECK_EQ_INT(test, YETTY_YVNC_VNC_ENCODING_H264, 6);
}

int main(void)
{
    struct ytest test = ytest_begin("yvnc_protocol");
    YTEST_RUN(&test, test_tile_grid);
    YTEST_RUN(&test, test_wire_format);
    return ytest_end(&test);
}
