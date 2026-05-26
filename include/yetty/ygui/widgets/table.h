/* ygui-table.h — rows × columns of text. Headers in the top row. */
#ifndef YETTY_YGUI_WIDGETS_TABLE_H
#define YETTY_YGUI_WIDGETS_TABLE_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_table_class_get(void);
struct yetty_ycore_void_result yetty_ygui_table_set_columns(struct yetty_ygui_object *obj,
                                                            int n_cols,
                                                            const char *const *headers);
struct yetty_ycore_void_result yetty_ygui_table_add_row(struct yetty_ygui_object *obj,
                                                        const char *const *cells, int n_cells);
#ifdef __cplusplus
}
#endif
#endif
