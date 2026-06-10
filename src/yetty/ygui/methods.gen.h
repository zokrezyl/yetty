/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YGUI_METHODS_GEN_H
#define YETTY_YCLASSGEN_YGUI_METHODS_GEN_H

#include "yetty/ygui/mixins/clickable.h"
#include "yetty/ygui/mixins/draggable.h"
#include "yetty/ygui/primitive-widget.h"
#include "yetty/ygui/widget.h"
#include "yetty/ygui/widgets/breadcrumbs.h"
#include "yetty/ygui/widgets/button.h"
#include "yetty/ygui/widgets/checkbox.h"
#include "yetty/ygui/widgets/chip.h"
#include "yetty/ygui/widgets/choicebox.h"
#include "yetty/ygui/widgets/collapsing_header.h"
#include "yetty/ygui/widgets/colorpicker.h"
#include "yetty/ygui/widgets/combobox.h"
#include "yetty/ygui/widgets/datepicker.h"
#include "yetty/ygui/widgets/dialog.h"
#include "yetty/ygui/widgets/dropdown.h"
#include "yetty/ygui/widgets/filepicker.h"
#include "yetty/ygui/widgets/hbox.h"
#include "yetty/ygui/widgets/label.h"
#include "yetty/ygui/widgets/list.h"
#include "yetty/ygui/widgets/menubar.h"
#include "yetty/ygui/widgets/panel.h"
#include "yetty/ygui/widgets/popup_menu.h"
#include "yetty/ygui/widgets/progress.h"
#include "yetty/ygui/widgets/radio.h"
#include "yetty/ygui/widgets/rich.h"
#include "yetty/ygui/widgets/scrollarea.h"
#include "yetty/ygui/widgets/selectable.h"
#include "yetty/ygui/widgets/separator.h"
#include "yetty/ygui/widgets/slider.h"
#include "yetty/ygui/widgets/spinner.h"
#include "yetty/ygui/widgets/splitter.h"
#include "yetty/ygui/widgets/statusbar.h"
#include "yetty/ygui/widgets/stepper.h"
#include "yetty/ygui/widgets/tabbar.h"
#include "yetty/ygui/widgets/table.h"
#include "yetty/ygui/widgets/textarea.h"
#include "yetty/ygui/widgets/textinput.h"
#include "yetty/ygui/widgets/toggle.h"
#include "yetty/ygui/widgets/tooltip.h"
#include "yetty/ygui/widgets/tree_node.h"
#include "yetty/ygui/widgets/vbox.h"
#include "yetty/ygui/widgets/window.h"
#include "yetty/ygui/widgets/ybrowser.h"
#include "yetty/ygui/widgets/ydiagram.h"
#include "yetty/ygui/widgets/ydraw_embed.h"
#include "yetty/ygui/widgets/yimage.h"
#include "yetty/ygui/widgets/yjungle.h"
#include "yetty/ygui/widgets/ymarkdown.h"
#include "yetty/ygui/widgets/ymaze.h"
#include "yetty/ygui/widgets/ynode.h"
#include "yetty/ygui/widgets/ynodes.h"
#include "yetty/ygui/widgets/ypdf.h"
#include "yetty/ygui/widgets/yplot.h"
#include "yetty/ygui/widgets/yrich_view.h"
#include "yetty/ygui/widgets/yshadertoy.h"
#include "yetty/ygui/widgets/yvideo.h"
#include "yetty/ygui/widgets/yzoo.h"

typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_ctx *,
                                                                       struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_container_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);

#endif
