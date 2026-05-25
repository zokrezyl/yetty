/*
 * ygui_flatten.h — internal helper: flatten a producer's command stream
 * into a buffer of paintable primitives suitable for the RICH widget.
 *
 * The RICH widget walks a draw_list and translates each record by its
 * resolved origin. It expects ONLY paint primitives (SDF, TEXT_SPAN,
 * complex, FONT). Producers like yjungle / yzoo emit control commands
 * (CMD_ZERO, CMD_DELETE) and wrap content in CMD_GROUP records that
 * the rich render path cannot interpret.
 *
 * yetty_ygui_old_flatten_draw_list copies every paint primitive from `src`
 * into `dst`, recursing into CMD_GROUP payloads and dropping CMD_ZERO
 * / CMD_DELETE control records. Bounds on `dst` are not modified.
 */

#ifndef YETTY_YGUI_OLD_YGUI_FLATTEN_H
#define YETTY_YGUI_OLD_YGUI_FLATTEN_H

#include <yetty/ycore/result.h>

struct yetty_ydraw_draw_list;

struct yetty_ycore_void_result yetty_ygui_old_flatten_draw_list(
    struct yetty_ydraw_draw_list *dst, const struct yetty_ydraw_draw_list *src);

#endif /* YETTY_YGUI_OLD_YGUI_FLATTEN_H */
