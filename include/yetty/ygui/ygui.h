/*
 * ygui.h — umbrella public header for the new ygui widget toolkit.
 *
 * Re-exports every component header so apps can include one file and
 * get the full API. Apps that care about compile times may include
 * individual headers (ygui-class.h, ygui-object.h, …) directly.
 */
#ifndef YETTY_YGUI_YGUI_H
#define YETTY_YGUI_YGUI_H

#include <yetty/ygui/class.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/event.h>
#include <yetty/ygui/object.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widget.h>

#include <yetty/ygui/mixins/clickable.h>

#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/checkbox.h>
#include <yetty/ygui/widgets/dialog.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/ygui/widgets/menubar.h>
#include <yetty/ygui/widgets/panel.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ygui/widgets/progress.h>
#include <yetty/ygui/widgets/separator.h>
#include <yetty/ygui/widgets/slider.h>
#include <yetty/ygui/widgets/statusbar.h>
#include <yetty/ygui/widgets/tabbar.h>
#include <yetty/ygui/widgets/toggle.h>
#include <yetty/ygui/widgets/tooltip.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/yimage.h>

#endif /* YETTY_YGUI_YGUI_H */
