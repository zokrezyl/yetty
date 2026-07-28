/*
 * ynet — network capture and observability.
 *
 * Thin frontend over the `yetty_ynet` yclass module. This entry implements the
 * headless "structured output" consumption mode: it loads a pcap / pcapng file
 * and prints a Wireshark-style packet table plus a flow (conversation) summary.
 * It is the correctness oracle for the reader + dissectors — no GPU, no root
 * (offline reading needs neither).
 *
 * The interactive pane (packet table + protocol tree + hex/ASCII) and the live
 * libpcap capture path build on this same capture object and land next.
 *
 *   ynet <file.pcap|file.pcapng>        # print the capture (default)
 *   ynet --dump <file>                  # explicit; same as above
 */
#include <yetty/ydraw-core/drawable-list.h>
#include "yetty/gen/impl/ynet/capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *program)
{
    fprintf(stderr,
            "ynet — network capture and observability\n"
            "\n"
            "usage:\n"
            "  %s <file.pcap|file.pcapng>   render the flow topology as a figure (default)\n"
            "  %s --dump <file>             print the packet table + flows as text\n"
            "  %s -h | --help               this help\n"
            "\n"
            "The default renders a GPU topology figure into the yetty pane (run inside\n"
            "yetty, or via `yetty -e`); emitting it to a plain terminal prints raw DCS\n"
            "bytes. --dump is the plain-text fallback. Offline reading only in this build\n"
            "(no root); live capture and the interactive pane land in later milestones.\n",
            program, program, program);
}

/* Borrowed-string accessor helper: returns the value or "" on error (and drops
 * the error — a dump row should never abort the whole print). */
static const char *dump_str(struct yetty_ycore_const_char_ptr_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
        return "";
    }
    return result.value ? result.value : "";
}

static int dump_capture(struct yetty_yclass_object *capture, const char *path)
{
    struct yetty_ycore_uint32_result count_result = yetty_ynet_packet_count(capture);
    if (YETTY_IS_ERR(count_result)) {
        fprintf(stderr, "ynet: packet_count failed: %s\n", count_result.error.msg);
        yetty_ycore_error_destroy(count_result.error);
        return 1;
    }
    uint32_t packet_count = count_result.value;

    printf("ynet — %s  (%u packets)\n\n", path, packet_count);
    printf("%7s  %11s  %-30s  %-30s  %-6s  %5s  %s\n", "No.", "Time", "Source", "Destination",
           "Proto", "Len", "Info");

    for (uint32_t index = 0; index < packet_count; index++) {
        struct yetty_ycore_float_result time_result = yetty_ynet_packet_time(capture, index);
        struct yetty_ycore_uint32_result length_result = yetty_ynet_packet_length(capture, index);
        float time_value = YETTY_IS_OK(time_result) ? time_result.value : 0.0f;
        if (YETTY_IS_ERR(time_result)) {
            yetty_ycore_error_destroy(time_result.error);
        }
        uint32_t length_value = 0;
        if (YETTY_IS_OK(length_result)) {
            length_value = length_result.value;
        } else {
            yetty_ycore_error_destroy(length_result.error);
        }

        printf("%7u  %11.6f  %-30s  %-30s  %-6s  %5u  %s\n", index + 1, (double)time_value,
               dump_str(yetty_ynet_packet_source(capture, index)),
               dump_str(yetty_ynet_packet_destination(capture, index)),
               dump_str(yetty_ynet_packet_protocol(capture, index)), length_value,
               dump_str(yetty_ynet_packet_info(capture, index)));
    }

    struct yetty_ycore_uint32_result flow_count_result = yetty_ynet_flow_count(capture);
    uint32_t flow_count = 0;
    if (YETTY_IS_OK(flow_count_result)) {
        flow_count = flow_count_result.value;
    } else {
        yetty_ycore_error_destroy(flow_count_result.error);
    }

    printf("\nFlows (%u):\n", flow_count);
    for (uint32_t index = 0; index < flow_count; index++) {
        printf("  %s\n", dump_str(yetty_ynet_flow_summary(capture, index)));
    }
    return 0;
}

/* Render the flow topology as a ydraw figure and ship it to stdout as a
 * YDRAW_BIN DCS envelope — renders inline in the yetty pane. */
static int render_figure(struct yetty_yclass_object *capture)
{
    struct yetty_ydraw_drawable_list_result render_result = yetty_ynet_render(capture, 0, 0);
    if (YETTY_IS_ERR(render_result)) {
        fprintf(stderr, "ynet: render failed: %s\n", render_result.error.msg);
        yetty_ycore_error_destroy(render_result.error);
        return 1;
    }
    struct yetty_ydraw_drawable_list *list = render_result.value;

    int status = 0;
    struct yetty_ycore_void_result emit_result = yetty_ynet_emit_osc(list, STDOUT_FILENO);
    if (YETTY_IS_ERR(emit_result)) {
        fprintf(stderr, "ynet: emit failed: %s\n", emit_result.error.msg);
        yetty_ycore_error_destroy(emit_result.error);
        status = 1;
    } else {
        /* The receiver anchors the figure to the rows it reserved and leaves the
         * cursor on the line just below the block. Emit one trailing newline so
         * the cursor moves fully clear of the figure's rows — otherwise the next
         * thing written (a returning shell prompt, the next command) can land on
         * the figure's top row, and writing over an anchored figure's cells
         * invalidates it, making it vanish. Matches yflame's trailing newline. */
        ssize_t written = write(STDOUT_FILENO, "\n", 1);
        (void)written;
    }
    yetty_ydraw_drawable_list_destroy(list);
    return status;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int text_dump = 0;
    for (int index = 1; index < argc; index++) {
        const char *arg = argv[index];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--dump") == 0) {
            text_dump = 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "ynet: unknown option '%s'\n", arg);
            print_usage(argv[0]);
            return 2;
        }
        path = arg;
    }

    if (!path) {
        print_usage(argv[0]);
        return 2;
    }

    struct yetty_ycore_void_result register_result = yetty_ynet_register();
    if (YETTY_IS_ERR(register_result)) {
        fprintf(stderr, "ynet: register failed: %s\n", register_result.error.msg);
        yetty_ycore_error_destroy(register_result.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result create_result = yetty_ynet_capture_create(NULL);
    if (YETTY_IS_ERR(create_result)) {
        fprintf(stderr, "ynet: capture_create failed: %s\n", create_result.error.msg);
        yetty_ycore_error_destroy(create_result.error);
        return 1;
    }
    struct yetty_yclass_object *capture = create_result.value;

    int status = 0;
    struct yetty_ycore_void_result load_result = yetty_ynet_load_file(capture, path);
    if (YETTY_IS_ERR(load_result)) {
        fprintf(stderr, "ynet: cannot load '%s': %s\n", path, load_result.error.msg);
        yetty_ycore_error_destroy(load_result.error);
        status = 1;
    } else {
        status = text_dump ? dump_capture(capture, path) : render_figure(capture);
    }

    struct yetty_ycore_void_result destroy_result = yetty_ynet_destroy(capture);
    if (YETTY_IS_ERR(destroy_result)) {
        yetty_ycore_error_destroy(destroy_result.error);
    }
    return status;
}
