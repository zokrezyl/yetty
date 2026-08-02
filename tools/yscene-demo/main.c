/*
 * yscene-demo — minimal DIRECT producer for the `yscene` retained-scene
 * figure kind (#691). Run INSIDE a yetty terminal.
 *
 * No yview: this tool speaks the same path the ychromium paint bridge
 * uses — connect over yclass RPC (DCS on the PTY), navigate to the
 * host's root figure container, CREATE_CHILD(kind="yscene"), and drive
 * content through the typed container stubs. The scene's process_bytes
 * adapter maps the flat wire body (plain records + CMD_GROUP bodies)
 * onto its retained tree.
 *
 * It exercises the retained model end to end through three phases:
 *   1. create: page background + a panel group + a nested accent chip
 *      + a trailing run (the run/child/run interleave);
 *   2. update: RE-EMIT the chip group with moved content — the retained
 *      tree replaces the node's content in place, keeping its paint
 *      depth (the flicker class ygrid needed a band-aid for);
 *   3. scroll: content taller than the figure + set_child_scroll — the
 *      scene scrolls on the GPU, nothing is re-emitted.
 *
 * On a tty the demo is interactive: the Right/Left arrow keys navigate
 * the phases forward/back and Ctrl-C exits (stdin is raw for the RPC
 * transport, so the 0x03 byte is handled here — no SIGINT). On a
 * non-tty stdin it keeps the original scripted behaviour: ship all
 * three phases and exit.
 *
 *   usage: yscene-demo [x y w h]     (default 40 40 680 520)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#define YSCENE_DEMO_GETPID() ((uint32_t)getpid())
#else
#include <process.h>
#define YSCENE_DEMO_GETPID() ((uint32_t)_getpid())
#endif

#include <yetty/api/yfigure/container.h>
#include <yetty/api/yterminal/terminal.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/kind.h>
#include <yetty/yplatform/tty.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/* Brand palette (docs/logo-2.jpeg roles), 0xAARRGGBB. */
#define DEMO_COLOR_BG 0xFF0B1014u
#define DEMO_COLOR_SURFACE 0xFF1E262Cu
#define DEMO_COLOR_ACCENT 0xFF6BA892u
#define DEMO_COLOR_ACCENT_BRIGHT 0xFF74C5A5u
#define DEMO_COLOR_TEXT 0xFFE0E5E4u

#define DEMO_CHIP_GROUP_ID 2u

static int add_box(struct yetty_ydraw_drawable_list *list, float x, float y, float w, float h,
                   uint32_t fill, float corner_radius)
{
    struct yetty_ysdf_rounded_box geometry = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = corner_radius,
        .radius_bottom_right = corner_radius,
        .radius_top_left = corner_radius,
        .radius_bottom_left = corner_radius,
    };
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_add_rounded_box(
        list, /*id=*/0, /*z_order=*/0, fill, /*stroke=*/0, /*stroke_w=*/0.0f, &geometry);
    if (YETTY_IS_ERR(add_res)) {
        fprintf(stderr, "yscene-demo: add box failed: %s\n", add_res.error.msg);
        yetty_ycore_error_destroy(add_res.error);
        return -1;
    }
    return 0;
}

static struct yetty_ydraw_drawable_list *make_list(void)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        fprintf(stderr, "yscene-demo: list create failed\n");
        yetty_ycore_error_destroy(list_res.error);
        return NULL;
    }
    return list_res.value;
}

static struct yetty_ycore_buffer list_buffer(const struct yetty_ydraw_drawable_list *list)
{
    return (struct yetty_ycore_buffer){
        .data = (uint8_t *)yetty_ydraw_drawable_list_data(list),
        .size = yetty_ydraw_drawable_list_size(list),
        .capacity = yetty_ydraw_drawable_list_size(list),
    };
}

/* The chip group body: re-used by create and by the phase-2 re-emission
 * (with a different offset — proving in-place content replacement keeps
 * the node's paint depth). */
static int add_chip_group(struct yetty_ydraw_drawable_list *list, float offset_x)
{
    struct yetty_ydraw_id_result chip_res =
        yetty_ydraw_drawable_list_begin_group(list, DEMO_CHIP_GROUP_ID);
    if (YETTY_IS_ERR(chip_res)) {
        yetty_ycore_error_destroy(chip_res.error);
        return -1;
    }
    if (add_box(list, 80 + offset_x, 80, 160, 48, DEMO_COLOR_ACCENT, 8.0f) != 0) {
        return -1;
    }
    if (add_box(list, 92 + offset_x, 92, 24, 24, DEMO_COLOR_ACCENT_BRIGHT, 12.0f) != 0) {
        return -1;
    }
    yetty_ydraw_drawable_list_end_group(list, chip_res.value);
    return 0;
}

/* Re-emit the chip group at `offset_x` — the retained in-place update
 * (phase 2 forward, and its inverse when navigating back). Returns 0 on
 * success, -1 on failure (error already reported). */
static int ship_chip(struct yetty_yclass_object *container, uint32_t child_id, float offset_x)
{
    struct yetty_ydraw_drawable_list *body = make_list();
    if (!body) {
        return -1;
    }
    int status = -1;
    if (add_chip_group(body, offset_x) == 0) {
        struct yetty_ycore_void_result body_res =
            yetty_yfigure_apply_child_body(container, child_id, list_buffer(body));
        if (YETTY_IS_ERR(body_res)) {
            fprintf(stderr, "yscene-demo: apply_child_body failed: %s\n", body_res.error.msg);
            yetty_ycore_error_destroy(body_res.error);
        } else {
            status = 0;
        }
    }
    yetty_ydraw_drawable_list_destroy(body);
    return status;
}

/* GPU-side scroll — nothing is re-emitted. Returns 0 on success. */
static int ship_scroll(struct yetty_yclass_object *container, uint32_t child_id, float scroll_y)
{
    struct yetty_ycore_void_result scroll_res =
        yetty_yfigure_set_child_scroll(container, child_id, 0.0f, scroll_y);
    if (YETTY_IS_ERR(scroll_res)) {
        fprintf(stderr, "yscene-demo: set_child_scroll failed: %s\n", scroll_res.error.msg);
        yetty_ycore_error_destroy(scroll_res.error);
        return -1;
    }
    return 0;
}

enum demo_key {
    DEMO_KEY_NONE,
    DEMO_KEY_QUIT,
    DEMO_KEY_BACK,    /* Left arrow */
    DEMO_KEY_FORWARD, /* Right arrow */
};

/* Feed one raw stdin byte through a minimal escape decoder. Recognizes
 * Ctrl-C (raw mode delivers it as the 0x03 byte, no SIGINT) and the
 * Left/Right arrows in both normal (ESC [ C/D) and application
 * (ESC O C/D) cursor modes. `escape_state`: 0 = ground, 1 = after ESC,
 * 2 = after ESC[ or ESCO. */
static enum demo_key demo_key_feed(int *escape_state, uint8_t input_byte)
{
    switch (*escape_state) {
    case 1:
        *escape_state = (input_byte == '[' || input_byte == 'O') ? 2 : 0;
        return DEMO_KEY_NONE;
    case 2:
        *escape_state = 0;
        if (input_byte == 'C') {
            return DEMO_KEY_FORWARD;
        }
        if (input_byte == 'D') {
            return DEMO_KEY_BACK;
        }
        return DEMO_KEY_NONE;
    default:
        if (input_byte == 0x03) {
            return DEMO_KEY_QUIT;
        }
        if (input_byte == 0x1b) {
            *escape_state = 1;
        }
        return DEMO_KEY_NONE;
    }
}

/* Status line per phase. Raw tty output: \r\n, not bare \n. */
static void demo_report_phase(int phase)
{
    static const char *const descriptions[] = {
        "phase 1/3: initial scene — chip at rest",
        "phase 2/3: chip group re-emitted in place (moved right)",
        "phase 3/3: scrolled to the bottom on the GPU",
    };
    fprintf(stderr, "yscene-demo: %s\r\n", descriptions[phase]);
}

int main(int argc, char **argv)
{
    float origin_x = 40.0f;
    float origin_y = 40.0f;
    float width = 680.0f;
    float height = 520.0f;
    if (argc == 5) {
        origin_x = (float)atof(argv[1]);
        origin_y = (float)atof(argv[2]);
        width = (float)atof(argv[3]);
        height = (float)atof(argv[4]);
    }
    uint32_t child_id = YSCENE_DEMO_GETPID();
    /* Content is taller than the figure so phase 3 has something to
     * scroll to. */
    float content_h = height * 2.0f;

    /* The RPC handshake reads binary replies from stdin: raw/binary
     * mode, or the line discipline stalls the connect. */
    int stdin_tty = yetty_yplatform_tty_stdin_is_tty();
    if (stdin_tty) {
        yetty_yplatform_tty_binary_io();
        if (yetty_yplatform_tty_set_raw() < 0) {
            fprintf(stderr, "yscene-demo: cannot put stdin into raw mode\n");
            return 1;
        }
    }

    int exit_code = 1;
    struct yetty_yclass_object *rpc_root = NULL;
    struct yetty_ydraw_drawable_list *create_body = NULL;

    /* Connect: PTY fds → session → typed terminal root → the host's
     * root figure container (the same navigation the ychromium bridge
     * performs). */
    struct yetty_yclass_object_ptr_result root_res =
        yetty_yclass_rpc_connect_fds(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(root_res)) {
        fprintf(stderr, "yscene-demo: rpc connect failed: %s\n", root_res.error.msg);
        yetty_ycore_error_destroy(root_res.error);
        goto out;
    }
    rpc_root = root_res.value;
    struct yetty_yclass_object_ptr_result container_res =
        yetty_yterminal_figure_root_container(rpc_root);
    if (YETTY_IS_ERR(container_res)) {
        fprintf(stderr, "yscene-demo: figure_root_container failed: %s\n", container_res.error.msg);
        yetty_ycore_error_destroy(container_res.error);
        goto out;
    }
    struct yetty_yclass_object *container = container_res.value;
    /* Prime the container class's method-slot translation while the
     * pipeline is empty — the typed stubs below resolve remote slot ids
     * through this table (same step yview performs). */
    {
        struct yetty_ycore_void_result prime_res =
            yetty_yclass_rpc_session_translate_class(rpc_root->session, "yetty_yfigure_container");
        if (YETTY_IS_ERR(prime_res)) {
            fprintf(stderr, "yscene-demo: translate_class failed: %s\n", prime_res.error.msg);
            yetty_ycore_error_destroy(prime_res.error);
            goto out;
        }
    }

    /* Phase 1 — CREATE_CHILD(kind="yscene") with the initial scene as
     * the init body: background run, panel group, nested chip group, a
     * trailing run (run/child/run interleave, ordered by declaration
     * seq — no z juggling). */
    create_body = make_list();
    if (!create_body) {
        goto out;
    }
    if (add_box(create_body, 0, 0, width, content_h, DEMO_COLOR_BG, 0.0f) != 0) {
        goto out;
    }
    {
        struct yetty_ydraw_id_result panel_res =
            yetty_ydraw_drawable_list_begin_group(create_body, 1);
        if (YETTY_IS_ERR(panel_res)) {
            yetty_ycore_error_destroy(panel_res.error);
            goto out;
        }
        if (add_box(create_body, 40, 40, width - 80.0f, content_h - 80.0f, DEMO_COLOR_SURFACE,
                    12.0f) != 0) {
            goto out;
        }
        if (add_chip_group(create_body, /*offset_x=*/0.0f) != 0) {
            goto out;
        }
        /* Trailing run AFTER the nested group — paints above the chip. */
        if (add_box(create_body, 80, 160, width - 240.0f, 8, DEMO_COLOR_TEXT, 4.0f) != 0) {
            goto out;
        }
        /* A marker near the content bottom: visible only after phase-3
         * scrolling. */
        if (add_box(create_body, 80, content_h - 120.0f, 220, 48, DEMO_COLOR_ACCENT_BRIGHT, 8.0f) !=
            0) {
            goto out;
        }
        yetty_ydraw_drawable_list_end_group(create_body, panel_res.value);
    }
    {
        struct yetty_ycore_rectangle rect = {
            .min = {.x = origin_x, .y = origin_y},
            .max = {.x = origin_x + width, .y = origin_y + height},
        };
        struct yetty_ycore_void_result create_res =
            yetty_yfigure_create_child(container, yetty_yfigure_kind_token("yscene"), child_id,
                                       rect, list_buffer(create_body));
        if (YETTY_IS_ERR(create_res)) {
            fprintf(stderr, "yscene-demo: create_child failed: %s\n", create_res.error.msg);
            yetty_ycore_error_destroy(create_res.error);
            goto out;
        }
    }

    /* Declare the taller content extent up front so scrolling works
     * whenever a navigation step asks for it. View state only — the
     * initial appearance is unchanged (scroll stays at 0). */
    {
        struct yetty_ycore_void_result size_res =
            yetty_yfigure_set_child_content_size(container, child_id, width, content_h);
        if (YETTY_IS_ERR(size_res)) {
            fprintf(stderr, "yscene-demo: set_child_content_size failed: %s\n", size_res.error.msg);
            yetty_ycore_error_destroy(size_res.error);
            goto out;
        }
    }

    if (!stdin_tty) {
        /* Scripted (non-tty) mode — ship phases 2 and 3 back to back and
         * exit, the original end-to-end smoke behaviour. */
        if (ship_chip(container, child_id, /*offset_x=*/240.0f) != 0) {
            goto out;
        }
        if (ship_scroll(container, child_id, content_h - height) != 0) {
            goto out;
        }
        fprintf(stderr,
                "yscene-demo: scene figure %u shipped (%.0fx%.0f at %.0f,%.0f), chip re-emitted, "
                "scrolled to bottom — the bright marker should be visible\n",
                child_id, width, height, origin_x, origin_y);
        exit_code = 0;
        goto out;
    }

    /* Interactive mode — Right/Left arrows navigate the phases forward/
     * back, Ctrl-C exits. Keys are drained into our own buffer before a
     * transition issues its RPC call, so queued keystrokes never mix
     * into the lockstep response read. */
    fprintf(stderr,
            "yscene-demo: scene figure %u shipped (%.0fx%.0f at %.0f,%.0f) — "
            "Right/Left arrows navigate the phases forward/back, Ctrl-C exits\r\n",
            child_id, width, height, origin_x, origin_y);
    demo_report_phase(0);
    {
        int phase = 0; /* 0 = created, 1 = chip re-emitted, 2 = scrolled */
        int escape_state = 0;
        int quit = 0;
        int step_failed = 0;
        while (!quit && !step_failed) {
            int wait_status = yetty_yplatform_tty_stdin_wait(200);
            if (wait_status < 0) {
                break;
            }
            if (wait_status == 0) {
                continue;
            }
            uint8_t input_bytes[64];
            int bytes_read = yetty_yplatform_tty_stdin_read(input_bytes, sizeof(input_bytes));
            if (bytes_read <= 0) {
                /* Readable with nothing to read on a VMIN=0 tty means the
                 * peer hung up; negative is a hard error. Either way the
                 * link is gone. */
                break;
            }
            for (int i = 0; i < bytes_read && !quit && !step_failed; i++) {
                enum demo_key key = demo_key_feed(&escape_state, input_bytes[i]);
                if (key == DEMO_KEY_QUIT) {
                    quit = 1;
                    break;
                }
                int next_phase = phase;
                if (key == DEMO_KEY_FORWARD && phase < 2) {
                    next_phase = phase + 1;
                } else if (key == DEMO_KEY_BACK && phase > 0) {
                    next_phase = phase - 1;
                }
                if (next_phase == phase) {
                    continue;
                }
                if (next_phase == 0) {
                    step_failed = ship_chip(container, child_id, /*offset_x=*/0.0f) != 0;
                } else if (next_phase == 1 && phase == 0) {
                    step_failed = ship_chip(container, child_id, /*offset_x=*/240.0f) != 0;
                } else if (next_phase == 1) {
                    step_failed = ship_scroll(container, child_id, 0.0f) != 0;
                } else {
                    step_failed = ship_scroll(container, child_id, content_h - height) != 0;
                }
                if (step_failed) {
                    break;
                }
                phase = next_phase;
                demo_report_phase(phase);
            }
        }
        if (!step_failed) {
            exit_code = 0;
        }
    }

out:
    if (create_body) {
        yetty_ydraw_drawable_list_destroy(create_body);
    }
    if (rpc_root) {
        /* Tears down the session + transport; the figure stays on screen
         * (host-owned) after this producer exits. */
        struct yetty_ycore_void_result disconnect_res = yetty_yclass_rpc_disconnect(rpc_root);
        if (YETTY_IS_ERR(disconnect_res)) {
            yetty_ycore_error_destroy(disconnect_res.error);
        }
    }
    if (stdin_tty) {
        yetty_yplatform_tty_restore();
    }
    return exit_code;
}
