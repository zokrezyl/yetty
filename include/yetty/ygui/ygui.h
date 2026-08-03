/*
 * ygui.h — umbrella public header for the new ygui widget toolkit.
 *
 * Re-exports every component header so apps can include one file and
 * get the full API. Apps that care about compile times may include
 * individual headers (ygui-object.h, ygui-framework.h, …) directly.
 */
#ifndef YETTY_YGUI_YGUI_H
#define YETTY_YGUI_YGUI_H

#include <yetty/api/ygui/framework.h>
#include <yetty/ygui/event.h>
#include <yetty/api/ygui/widget.h>
#include <yetty/api/ygui/primitive-widget.h>

#include <yetty/api/ygui/mixins/clickable.h>

#include <yetty/api/ygui/widgets/breadcrumbs.h>
#include <yetty/api/ygui/widgets/button.h>
#include <yetty/api/ygui/widgets/checkbox.h>
#include <yetty/api/ygui/widgets/chip.h>
#include <yetty/api/ygui/widgets/choicebox.h>
#include <yetty/api/ygui/widgets/collapsing_header.h>
#include <yetty/api/ygui/widgets/colorpicker.h>
#include <yetty/api/ygui/widgets/combobox.h>
#include <yetty/api/ygui/widgets/datepicker.h>
#include <yetty/api/ygui/widgets/dialog.h>
#include <yetty/api/ygui/widgets/dropdown.h>
#include <yetty/api/ygui/widgets/filepicker.h>
#include <yetty/api/ygui/widgets/hbox.h>
#include <yetty/api/ygui/widgets/label.h>
#include <yetty/api/ygui/widgets/list.h>
#include <yetty/api/ygui/widgets/menubar.h>
#include <yetty/api/ygui/widgets/panel.h>
#include <yetty/api/ygui/widgets/popup_menu.h>
#include <yetty/api/ygui/widgets/progress.h>
#include <yetty/api/ygui/widgets/radio.h>
#include <yetty/api/ygui/widgets/rich.h>
#include <yetty/api/ygui/widgets/scrollarea.h>
#include <yetty/api/ygui/widgets/selectable.h>
#include <yetty/api/ygui/widgets/separator.h>
#include <yetty/api/ygui/widgets/slider.h>
#include <yetty/api/ygui/widgets/spinner.h>
#include <yetty/api/ygui/widgets/splitter.h>
#include <yetty/api/ygui/widgets/statusbar.h>
#include <yetty/api/ygui/widgets/stepper.h>
#include <yetty/api/ygui/widgets/tabbar.h>
#include <yetty/api/ygui/widgets/table.h>
#include <yetty/api/ygui/widgets/textarea.h>
#include <yetty/api/ygui/widgets/textinput.h>
#include <yetty/api/ygui/widgets/toggle.h>
#include <yetty/api/ygui/widgets/tooltip.h>
#include <yetty/api/ygui/widgets/tree_node.h>
#include <yetty/api/ygui/widgets/vbox.h>
#include <yetty/api/ygui/widgets/window.h>
#include <yetty/api/ygui/widgets/ybrowser.h>
#include <yetty/api/ygui/widgets/ydiagram.h>
#include <yetty/api/ygui/widgets/ydraw_embed.h>
#include <yetty/api/ygui/widgets/yimage.h>
#include <yetty/api/ygui/widgets/yjungle.h>
#include <yetty/api/ygui/widgets/ymarkdown.h>
#include <yetty/api/ygui/widgets/ymaze.h>
#include <yetty/api/ygui/widgets/ynode.h>
#include <yetty/api/ygui/widgets/ynodes.h>
#include <yetty/api/ygui/widgets/ypdf.h>
#include <yetty/api/ygui/widgets/yplot.h>
#include <yetty/api/ygui/widgets/yshadertoy.h>
#include <yetty/api/ygui/widgets/yvideo.h>
#include <yetty/api/ygui/widgets/yzoo.h>

#endif /* YETTY_YGUI_YGUI_H */
