/*
 * imgui_impl_yetty.h — Dear ImGui renderer + platform backend for yetty.
 *
 * Card model (v2)
 *
 *   A "card" is a placed sub-region of the terminal grid that the app
 *   draws ImGui content into. The client may own multiple cards;
 *   each carries its own ImGuiContext (own DisplaySize, own font atlas,
 *   own focus state).
 *
 *   Lifecycle:
 *
 *     ImGui_ImplYetty_Init();                 // backend state
 *     ImGui_ImplYetty_PlatformInit();         // raw stdin + DEC subscribe
 *     uint32_t card = ImGui_ImplYetty_CreateCard(card_id, col, row, w, h);
 *
 *     // each frame:
 *     ImGui_ImplYetty_BeginCardFrame(card);   // context current + NewFrame
 *     ImGui::NewFrame();
 *     // … draw …
 *     ImGui::Render();
 *     ImGui_ImplYetty_RenderCardDrawData(card, ImGui::GetDrawData());
 *
 *   At shutdown the app should ImGui_ImplYetty_Clear() with keep_visible
 *   if it wants its last frames to remain on screen as scrollback.
 *
 * Input
 *
 *   The terminal forwards all input as OSC envelopes carrying a card_id
 *   and card-local coordinates. Three ways to consume them:
 *
 *     (1) PollInput() once per frame (sync drain).
 *     (2) AttachEventLoop(loop) (async, libuv) — preferred.
 *     (3) Push events yourself via the ImGui_ImplYetty_OnCard* hooks.
 *
 *   In every case the backend dispatches the event to the right card's
 *   ImGuiContext automatically — the app code only needs a frame_cb.
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
 * each live card is archived to the static scrollback layer. Call at
 * app shutdown after the last RenderCardDrawData. */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_Clear(bool keep_visible);

/*=============================================================================
 * Platform — raw mode + DEC ?1500/?1501 mouse subscription
 *===========================================================================*/

IMGUI_IMPL_API bool yetty_ymgui_ImGui_ImplYetty_PlatformInit(void);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_PlatformShutdown(void);

/*=============================================================================
 * Card lifecycle
 *
 * Card IDs are client-allocated u32 (per wire.h). Pass 0 to let the
 * backend pick the next free id (returned). w_cells == 0 means "until
 * right edge of the pane" — the card auto-resizes with the terminal.
 *===========================================================================*/

IMGUI_IMPL_API uint32_t yetty_ymgui_ImGui_ImplYetty_CreateCard(uint32_t card_id, int col, int row,
                                                   uint32_t w_cells, uint32_t h_cells);

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_MoveCard(uint32_t card_id, int col, int row, uint32_t w_cells,
                                             uint32_t h_cells);

/* Remove `card_id`. If keep_visible, the server archives the last
 * frame to the static scrollback layer; otherwise it's just dropped. */
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_RemoveCard(uint32_t card_id, bool keep_visible);

/* Returns the ImGuiContext bound to a card, or NULL if not found. */
IMGUI_IMPL_API ImGuiContext *yetty_ymgui_ImGui_ImplYetty_GetCardContext(uint32_t card_id);

/* Currently focused card per the latest server-side click-focus event,
 * or 0 if none. */
IMGUI_IMPL_API uint32_t yetty_ymgui_ImGui_ImplYetty_FocusedCard(void);

/*=============================================================================
 * Per-card frame
 *
 * BeginCardFrame: makes the card's ImGuiContext current and uploads
 * the font atlas if needed. Caller still calls ImGui::NewFrame()
 * (and ImGui::Render() / RenderCardDrawData) explicitly.
 *
 * RenderCardDrawData: ships the ImDrawData over the wire as the named
 * card's frame.
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_BeginCardFrame(uint32_t card_id);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_RenderCardDrawData(uint32_t card_id, ImDrawData *draw_data);

/*=============================================================================
 * Sync input drain (option 1)
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_PollInput(void);
IMGUI_IMPL_API bool yetty_ymgui_ImGui_ImplYetty_WaitInput(int timeout_ms);

/*=============================================================================
 * Push input (option 3) — feed ImGuiIO of the named card directly
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardMousePos(uint32_t card_id, double x, double y,
                                                   uint32_t buttons_held);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardMouseButton(uint32_t card_id, int button, int pressed,
                                                      double x, double y);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardMouseWheel(uint32_t card_id, double dy, double x,
                                                     double y);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardResize(uint32_t card_id, double width, double height);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardFocus(uint32_t card_id, int gained);
IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_OnCardKey(uint32_t card_id, int kind, int key, int mods,
                                              uint32_t codepoint);

/*=============================================================================
 * Async with libuv (option 2) — preferred
 *===========================================================================*/

IMGUI_IMPL_API void yetty_ymgui_ImGui_ImplYetty_AttachEventLoop(struct yetty_yclient_event_loop *loop);

#endif /* YETTY_YMGUI_IMGUI_IMPL_YETTY_H */
