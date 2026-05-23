/*
 * imgui_impl_yetty.h — Dear ImGui renderer + platform backend for yetty.
 *
 * Figure model
 *
 *   An ymgui figure is one placed sub-region of the terminal pane that
 *   the app draws ImGui content into — a yetty_ymgui_figure on the
 *   receiver side, hanging off the root container alongside the other
 *   yfigure kinds (ygrid, yplot, …). The client may own multiple
 *   figures; each carries its own ImGuiContext (own DisplaySize, own
 *   font atlas, own focus state).
 *
 *   Lifecycle:
 *
 *     ImGui_ImplYetty_Init();                   // backend state
 *     ImGui_ImplYetty_PlatformInit();           // raw stdin + DEC subscribe
 *     uint32_t fig = ImGui_ImplYetty_CreateFigure(figure_id, col, row, w, h);
 *
 *     // each frame:
 *     ImGui_ImplYetty_BeginFigureFrame(fig);    // context current + atlas check
 *     ImGui::NewFrame();
 *     // … draw …
 *     ImGui::Render();
 *     ImGui_ImplYetty_RenderFigureDrawData(fig, ImGui::GetDrawData());
 *
 *   At shutdown the app should ImGui_ImplYetty_Clear() with keep_visible
 *   if it wants its last frames to remain on screen as scrollback.
 *
 * Input
 *
 *   The terminal forwards input as OSC envelopes carrying a figure_id
 *   and figure-local coordinates. Three ways to consume them:
 *
 *     (1) PollInput() once per frame (sync drain).
 *     (2) AttachEventLoop(loop) (async, libuv) — preferred.
 *     (3) Push events yourself via the ImGui_ImplYetty_OnFigure* hooks.
 *
 *   In every case the backend dispatches the event to the right
 *   figure's ImGuiContext automatically — the app code only needs a
 *   frame_cb.
 */

#ifndef YETTY_YMGUI_IMGUI_IMPL_YETTY_H
#define YETTY_YMGUI_IMGUI_IMPL_YETTY_H

#include <stdint.h>
#include "imgui.h"

#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif

struct yetty_yclient_event_loop;

/*=============================================================================
 * Backend lifecycle
 *===========================================================================*/

IMGUI_IMPL_API bool yetty_ymgui_ImGui_ImplYetty_Init(void);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_Shutdown(void);

/* Optional — call before Init. Default is STDOUT_FILENO. */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_SetOutputFd(int fd);
/* Optional — input fd. Default STDIN_FILENO. */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_SetInputFd(int fd);

/* Drop ALL ymgui state at the server. If keep_visible, last frame of
 * each live figure is archived to the static scrollback layer (legacy
 * wire only). Call at app shutdown after the last RenderFigureDrawData. */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_Clear(bool keep_visible);

/*=============================================================================
 * Platform — raw mode + DEC ?1500/?1501 mouse subscription
 *===========================================================================*/

IMGUI_IMPL_API bool yetty_ymgui_ImGui_ImplYetty_PlatformInit(void);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_PlatformShutdown(void);

/*=============================================================================
 * Figure lifecycle
 *
 * Figure IDs are client-allocated u32 (per wire.h). Pass 0 to let the
 * backend pick the next free id (returned). w_cells == 0 means "until
 * right edge of the pane" — the figure auto-resizes with the terminal.
 *===========================================================================*/

IMGUI_IMPL_API uint32_t yetty_ymgui_ImGui_ImplYetty_CreateFigure(uint32_t figure_id, int col, int row,
                                                                 uint32_t w_cells, uint32_t h_cells);

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_MoveFigure(uint32_t figure_id, int col, int row,
                                                           uint32_t w_cells, uint32_t h_cells);

/* Remove `figure_id`. If keep_visible, the server archives the last
 * frame to the static scrollback layer (legacy wire only). */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_RemoveFigure(uint32_t figure_id, bool keep_visible);

/* Returns the ImGuiContext bound to a figure, or NULL if not found. */
IMGUI_IMPL_API ImGuiContext *yetty_ymgui_ImGui_ImplYetty_GetFigureContext(uint32_t figure_id);

/* Currently focused figure per the latest server-side click-focus
 * event, or 0 if none. */
IMGUI_IMPL_API uint32_t yetty_ymgui_ImGui_ImplYetty_FocusedFigure(void);

/*=============================================================================
 * Per-figure frame
 *
 * BeginFigureFrame: makes the figure's ImGuiContext current and uploads
 * the font atlas if needed. Caller still calls ImGui::NewFrame()
 * (and ImGui::Render() / RenderFigureDrawData) explicitly.
 *
 * RenderFigureDrawData: ships the ImDrawData over the wire as the named
 * figure's frame.
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_BeginFigureFrame(uint32_t figure_id);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_RenderFigureDrawData(uint32_t figure_id,
                                                                     ImDrawData *draw_data);

/*=============================================================================
 * Wire-format optimisations
 *
 * The renderer can dedup wire bytes against the previous frame in two
 * layered ways. Both default ON. Pass any combination of the flags to
 * SetOptimizations to override. Most apps never need to touch this — the
 * defaults give the best wire size for typical UIs. Disable selectively
 * when measuring performance differences.
 *
 *   DEDUP_CMDLIST (Stage 1): per-cmd_list REPEAT. If a whole cmd_list
 *     (one ImGui window in the default render) is byte-identical to last
 *     frame at the same slot, ship a 16-byte marker instead of the full
 *     vtx/idx/cmds.
 *
 *   DEDUP_CMD (Stage 2): per-cmd content dedup. When a cmd_list differs
 *     but most of its cmds are unchanged (button hover inside an
 *     otherwise static window), ship a draw-order hash list plus only
 *     the changed cmds.
 *===========================================================================*/

enum {
    YETTY_YMGUI_OPT_DEDUP_CMDLIST = 1u << 0, /* Stage 1 */
    YETTY_YMGUI_OPT_DEDUP_CMD = 1u << 1,     /* Stage 2 */
};

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_SetOptimizations(uint32_t flags);
IMGUI_IMPL_API uint32_t yetty_ymgui_ImGui_ImplYetty_GetOptimizations(void);

/*=============================================================================
 * Sync input drain (option 1)
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_PollInput(void);
IMGUI_IMPL_API bool yetty_ymgui_ImGui_ImplYetty_WaitInput(int timeout_ms);

/*=============================================================================
 * Push input (option 3) — feed ImGuiIO of the named figure directly
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureMousePos(uint32_t figure_id, double x, double y,
                                                                 uint32_t buttons_held);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureMouseButton(uint32_t figure_id, int button,
                                                                    int pressed, double x, double y);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureMouseWheel(uint32_t figure_id, double dy,
                                                                   double x, double y);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureResize(uint32_t figure_id, double width,
                                                               double height);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureFocus(uint32_t figure_id, int gained);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnFigureKey(uint32_t figure_id, int kind, int key,
                                                            int mods, uint32_t codepoint);

/*=============================================================================
 * Async with libuv (option 2) — preferred
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_AttachEventLoop(
    struct yetty_yclient_event_loop *loop);

#endif /* YETTY_YMGUI_IMGUI_IMPL_YETTY_H */
