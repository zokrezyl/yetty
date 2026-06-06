/*
 * yless — a `less`-semantics pager that renders into a server-side scrollable
 * figure (yview) instead of the scrolling buffer.
 *
 * Contrast with ycat: ycat dumps rendered content into the scrollback (DCS
 * YDRAW_BIN); it ages out of history like any text. yless ships the content
 * ONCE as a positioned, viewport-anchored figure (DCS YCOMPOSITOR_BIN) and
 * then stays in the foreground translating keystrokes into scroll commands.
 * Scrolling is server-side state: each key sends a tiny SET_CHILD_SCROLL
 * record, never re-shipping the content.
 *
 * Content comes from a file argument or stdin; keystrokes are read from
 * /dev/tty (so a piped stdin still leaves a usable keyboard). Envelopes are
 * written to stdout — the PTY connected to the parent yetty.
 *
 * Unix/tty only: a pager is inherently an interactive terminal program.
 */
#include <yetty/ycat/ycat.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yview/yview.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*=============================================================================
 * Input read helpers
 *===========================================================================*/

struct byte_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static int byte_buf_append(struct byte_buf *buffer, const uint8_t *src, size_t count)
{
    if (buffer->len + count > buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 65536;
        while (new_cap < buffer->len + count) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(buffer->data, new_cap);
        if (!grown) {
            return -1;
        }
        buffer->data = grown;
        buffer->cap = new_cap;
    }
    memcpy(buffer->data + buffer->len, src, count);
    buffer->len += count;
    return 0;
}

static int read_all_fd(int fd, struct byte_buf *out)
{
    uint8_t chunk[65536];
    for (;;) {
        ssize_t count = read(fd, chunk, sizeof(chunk));
        if (count < 0) {
            return -1;
        }
        if (count == 0) {
            return 0;
        }
        if (byte_buf_append(out, chunk, (size_t)count) < 0) {
            return -1;
        }
    }
}

/*=============================================================================
 * Geometry — viewport rect in target pixels from the tty winsize
 *===========================================================================*/

struct viewport {
    float pixel_w;
    float pixel_h;
    uint32_t cell_w;
    uint32_t cell_h;
    uint32_t cols;
};

static struct viewport probe_viewport(int tty_fd)
{
    struct viewport vp = {.pixel_w = 80 * 8, .pixel_h = 24 * 16, .cell_w = 8, .cell_h = 16,
                          .cols = 80};
    struct winsize ws = {0};
    if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        vp.cols = ws.ws_col;
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            vp.pixel_w = (float)ws.ws_xpixel;
            vp.pixel_h = (float)ws.ws_ypixel;
            vp.cell_w = ws.ws_xpixel / ws.ws_col;
            vp.cell_h = ws.ws_ypixel / ws.ws_row;
        } else {
            vp.pixel_w = (float)(ws.ws_col * vp.cell_w);
            vp.pixel_h = (float)(ws.ws_row * vp.cell_h);
        }
    }
    if (vp.cell_w == 0) {
        vp.cell_w = 8;
    }
    if (vp.cell_h == 0) {
        vp.cell_h = 16;
    }
    return vp;
}

/*=============================================================================
 * Raw-mode keyboard
 *===========================================================================*/

enum key {
    KEY_NONE = 0,
    KEY_QUIT,
    KEY_LINE_DOWN,
    KEY_LINE_UP,
    KEY_PAGE_DOWN,
    KEY_PAGE_UP,
    KEY_TOP,
    KEY_BOTTOM,
};

/* Read one logical key, decoding the common CSI arrow / page sequences. */
static enum key read_key(int tty_fd)
{
    uint8_t c;
    ssize_t n = read(tty_fd, &c, 1);
    if (n <= 0) {
        return KEY_QUIT; /* tty closed → exit */
    }
    switch (c) {
    case 'q':
    case 'Q':
        return KEY_QUIT;
    case 'j':
    case '\r':
    case '\n':
        return KEY_LINE_DOWN;
    case 'k':
        return KEY_LINE_UP;
    case ' ':
    case 'f':
        return KEY_PAGE_DOWN;
    case 'b':
        return KEY_PAGE_UP;
    case 'g':
        return KEY_TOP;
    case 'G':
        return KEY_BOTTOM;
    case 0x1b:
        break; /* ESC — maybe a CSI sequence */
    default:
        return KEY_NONE;
    }

    /* ESC: peek the next two bytes for a CSI arrow / page key. A bare ESC
     * (no follow-on within the buffered read) quits, matching less. */
    uint8_t seq[2];
    if (read(tty_fd, &seq[0], 1) <= 0) {
        return KEY_QUIT;
    }
    if (seq[0] != '[' && seq[0] != 'O') {
        return KEY_NONE;
    }
    if (read(tty_fd, &seq[1], 1) <= 0) {
        return KEY_NONE;
    }
    switch (seq[1]) {
    case 'A':
        return KEY_LINE_UP;
    case 'B':
        return KEY_LINE_DOWN;
    case 'H':
        return KEY_TOP;
    case 'F':
        return KEY_BOTTOM;
    case '5':
        return KEY_PAGE_UP; /* CSI 5 ~ (PageUp); trailing '~' consumed below */
    case '6':
        return KEY_PAGE_DOWN; /* CSI 6 ~ (PageDown) */
    default:
        return KEY_NONE;
    }
}

/*=============================================================================
 * main
 *===========================================================================*/

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : NULL;

    /* Read the content bytes. */
    struct byte_buf input = {0};
    if (path) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "yless: %s: cannot open\n", path);
            return 1;
        }
        int rc = read_all_fd(fd, &input);
        close(fd);
        if (rc < 0) {
            fprintf(stderr, "yless: %s: read failed\n", path);
            free(input.data);
            return 1;
        }
    } else if (read_all_fd(STDIN_FILENO, &input) < 0) {
        fprintf(stderr, "yless: stdin: read failed\n");
        free(input.data);
        return 1;
    }

    /* Keyboard comes from the controlling tty so a piped stdin still works. */
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) {
        fprintf(stderr, "yless: no controlling tty\n");
        free(input.data);
        return 1;
    }

    struct viewport vp = probe_viewport(tty_fd);

    /* Render the content into a single drawable list. Mirror ycat's dispatch
     * for the single-shot paths: try a dedicated handler (image/svg/mermaid),
     * then fall back to the tree-sitter renderer for source files. (Streaming
     * formats — markdown/pdf — tile per envelope for the scrolling layer; a
     * single-figure pager needs them merged, which is a follow-up.) */
    struct yetty_ycat_config cfg = {
        .cell_width = vp.cell_w,
        .cell_height = vp.cell_h,
        .width_cells = vp.cols,
        .height_cells = 0,
    };
    struct yetty_ydraw_drawable_list *content = NULL;
    struct yetty_ydraw_drawable_list_result render_r =
        yetty_ycat_render(input.data, input.len, path, &cfg);
    if (YETTY_IS_OK(render_r)) {
        content = render_r.value;
    } else {
        yetty_ycore_error_destroy(render_r.error);
        const char *grammar = yetty_ycat_grammar_lookup(NULL, path);
        if (grammar) {
            struct yetty_ydraw_drawable_list_result ts_r =
                yetty_ycat_ts_render(input.data, input.len, grammar, &cfg);
            if (YETTY_IS_OK(ts_r)) {
                content = ts_r.value;
            } else {
                fprintf(stderr, "yless: render failed: %s\n", ts_r.error.msg);
                yetty_ycore_error_destroy(ts_r.error);
            }
        } else {
            fprintf(stderr,
                    "yless: no renderer for this content type "
                    "(supported: source files, images, svg, mermaid)\n");
        }
    }
    if (!content) {
        free(input.data);
        close(tty_fd);
        return 1;
    }

    /* Mint the scrollable view over the whole viewport. */
    struct yetty_yview_config view_cfg = {
        .fd = STDOUT_FILENO,
        .rect = {.min = {.x = 0.0f, .y = 0.0f}, .max = {.x = vp.pixel_w, .y = vp.pixel_h}},
        .kind = 0, /* default YGRID */
        .flags = YETTY_YVIEW_FLAG_NONE,
        .child_id = (uint32_t)getpid(),
    };
    struct yetty_yview_ptr_result view_r = yetty_yview_create(&view_cfg);
    if (YETTY_IS_ERR(view_r)) {
        fprintf(stderr, "yless: view create failed: %s\n", view_r.error.msg);
        yetty_ycore_error_destroy(view_r.error);
        yetty_ydraw_drawable_list_destroy(content);
        free(input.data);
        close(tty_fd);
        return 1;
    }
    struct yetty_yview *view = view_r.value;

    struct yetty_ycore_void_result content_r = yetty_yview_set_content(view, content);
    yetty_ydraw_drawable_list_destroy(content); /* envelope copied the bytes */
    if (YETTY_IS_ERR(content_r)) {
        fprintf(stderr, "yless: set_content failed: %s\n", content_r.error.msg);
        yetty_ycore_error_destroy(content_r.error);
        struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        free(input.data);
        close(tty_fd);
        return 1;
    }

    /* Raw mode on the tty for unbuffered, un-echoed key reads. */
    struct termios saved;
    bool raw_active = false;
    if (tcgetattr(tty_fd, &saved) == 0) {
        struct termios raw = saved;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(tty_fd, TCSANOW, &raw) == 0) {
            raw_active = true;
        }
    }

    const float line = (float)vp.cell_h;
    const float page = vp.pixel_h > line ? vp.pixel_h - line : line;
    const float to_bottom = 1.0e9f; /* clamped server-side and in yview */

    bool running = true;
    while (running) {
        enum key k = read_key(tty_fd);
        struct yetty_ycore_void_result sr = YETTY_OK_VOID();
        switch (k) {
        case KEY_QUIT:
            running = false;
            break;
        case KEY_LINE_DOWN:
            sr = yetty_yview_scroll_by(view, 0.0f, line);
            break;
        case KEY_LINE_UP:
            sr = yetty_yview_scroll_by(view, 0.0f, -line);
            break;
        case KEY_PAGE_DOWN:
            sr = yetty_yview_scroll_by(view, 0.0f, page);
            break;
        case KEY_PAGE_UP:
            sr = yetty_yview_scroll_by(view, 0.0f, -page);
            break;
        case KEY_TOP:
            sr = yetty_yview_scroll_to(view, 0.0f, 0.0f);
            break;
        case KEY_BOTTOM:
            sr = yetty_yview_scroll_to(view, 0.0f, to_bottom);
            break;
        case KEY_NONE:
        default:
            break;
        }
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
            running = false;
        }
    }

    if (raw_active) {
        tcsetattr(tty_fd, TCSANOW, &saved);
    }
    struct yetty_ycore_void_result dr = yetty_yview_destroy(view);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    close(tty_fd);
    free(input.data);
    return 0;
}
