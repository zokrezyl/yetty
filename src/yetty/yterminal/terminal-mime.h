#ifndef YETTY_YTERMINAL_TERMINAL_MIME_H
#define YETTY_YTERMINAL_TERMINAL_MIME_H

/*
 * Private bridge between terminal.c (which owns struct
 * yetty_yterminal_terminal) and terminal-mime.c (the YETTY_DCS_MIME_FILE
 * handler — raw-file envelopes rendered terminal-side). Both files live in
 * yetty_yterminal; this header is the narrow surface between them so the
 * mime handler never touches the terminal struct directly. Not installed.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yterminal_terminal;
struct yetty_ywire_wire_statemachine;
struct yetty_yconfig_config;

/* Snapshot of the terminal state the mime handler needs. Implemented in
 * terminal.c. */
struct yetty_yterminal_mime_env {
    struct yetty_yconfig_config *config; /* borrowed; may be NULL */
    uint32_t cell_width;                 /* px; 0 when unavailable */
    uint32_t cell_height;
    uint32_t cols;
    uint32_t rows;
};

struct yetty_yterminal_mime_env yetty_yterminal_mime_env_get(
    struct yetty_yterminal_terminal *terminal);

/* Ingest one serialized drawable-list blob (the exact byte layout
 * yetty_ydraw_drawable_list_serialize produces) into the terminal's rich
 * content model — same line anchoring and vertical space reservation as an
 * inbound YDRAW_BIN envelope. Implemented in terminal.c. */
struct yetty_ycore_void_result yetty_yterminal_mime_ingest_serialized(
    struct yetty_yterminal_terminal *terminal, const uint8_t *bytes, size_t len);

/* Request a redraw after ingest. Implemented in terminal.c. */
void yetty_yterminal_mime_request_render(struct yetty_yterminal_terminal *terminal);

/* The YETTY_DCS_MIME_FILE process_input coroutine. userdata is the
 * terminal's mime_handler_self back-pointer. Implemented in
 * terminal-mime.c. */
struct yetty_ycore_void_result yetty_yterminal_mime_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERMINAL_TERMINAL_MIME_H */
