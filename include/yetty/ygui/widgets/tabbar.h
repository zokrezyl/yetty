/*
 * ygui-tabbar.h — strip of tab buttons + active-index state.
 *
 * Chrome widget (figure_kind == 0). The widget is a flex-row container
 * whose children are tab "headers" — one per tab. Each header is a
 * subclass of button with an extra `active` field; the tabbar manages
 * the mutually-exclusive active state.
 *
 * Click on a header → tabbar switches active index, fires the
 * VALUE_CHANGED event with i0 = active_index. The app's per-tab body
 * pane subscribes and swaps content.
 *
 * Minimal feature set vs ygui-old's tabbar:
 *   - No close buttons on tabs.
 *   - No reordering.
 *   - No overflow handling (long tab strips just clip).
 * Each can land as a follow-up.
 */
#ifndef YETTY_YGUI_WIDGETS_TABBAR_H
#define YETTY_YGUI_WIDGETS_TABBAR_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_tabbar_class_get(void);

/* Append a tab header to the tabbar. Returns the new header widget,
 * which the app can pass to layout setters or hide / show. The header
 * itself is a chrome widget so its rect / layout fields work the
 * normal way. */
struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_ygui_object *tabbar,
                                                              const char *label);

/* Remove a tab header. Returns OK_VOID if `index` is out of range. */
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_ygui_object *tabbar,
                                                            int index);

int yetty_ygui_tabbar_count(const struct yetty_ygui_object *tabbar);

int yetty_ygui_tabbar_active(const struct yetty_ygui_object *tabbar);

/* Set the active tab. Out-of-range values are clamped. Emits a
 * VALUE_CHANGED event with i0 = new_index. */
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_ygui_object *tabbar,
                                                            int index);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_TABBAR_H */
