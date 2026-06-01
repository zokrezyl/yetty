/* filepicker.c — directory browser. Lists the entries of a directory;
 * click a folder (trailing "/") to descend, ".." to go up, a file to
 * select it. */
#include "../internal.h"
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/filepicker.h>
#include <yetty/yplatform/fs.h>
#include <stdlib.h>
#include <string.h>

#define FP_HEADER_H 24.0f
#define FP_ROW_H 22.0f
#define FP_PAD 8.0f

/* Brand palette, packed 0xAABBGGRR. */
#define FP_BG 0xFF14100Bu       /* BRAND_BG            (11,16,20)  */
#define FP_HEADER_BG 0xFF2C261Eu /* BRAND_BG_ROW       (30,38,44)  */
#define FP_BORDER 0xFF474A36u   /* BRAND_BORDER        (54,74,71)  */
#define FP_TEXT 0xFFE4E5E0u     /* BRAND_TEXT_PRIMARY  (224,229,228) */
#define FP_DIR 0xFF92A86Bu      /* BRAND_ACCENT        (107,168,146) */
#define FP_SEL_BG 0xFF1F1A14u   /* BRAND_BG_LIFTED     (20,26,31)  */

struct [[clang::annotate("class@ygui:filepicker")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] fp_data {
    char *cwd;
    char **entries;
    int entry_count;
    int selected;
    int scroll; /* first visible row */
    /* Drag-to-scroll: captured at press, consumed by on_motion. */
    float press_y;
    int press_scroll;
};

static const struct yetty_yclass *fp_class(void)
{
    return yetty_ygui_class_expect(yetty_ygui_filepicker_class_get(),
                                   "yetty_ygui_filepicker_class_get");
}

static char *fp_strdup(const char *s)
{
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n + 1);
    }
    return p;
}

static void fp_free_entries(struct fp_data *d)
{
    for (int i = 0; i < d->entry_count; i++) {
        free(d->entries[i]);
    }
    free(d->entries);
    d->entries = NULL;
    d->entry_count = 0;
}

static int fp_entry_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Reload `cwd`'s entries into the sorted `entries` array. Directories
 * keep a trailing "/" so the user (and the click handler) can tell them
 * apart. "." is dropped; ".." is kept for navigation. */
/* Build the listing for `d->cwd`. Failure-safe: the new list is built
 * fully before the old one is replaced, so a directory that can't be
 * opened (permission denied, deleted, …) leaves the current listing
 * intact and returns a chained error instead of silently blanking. */
static struct yetty_ycore_void_result fp_load_dir(struct fp_data *d)
{
    if (!d->cwd) {
        return YETTY_ERR(yetty_ycore_void, "fp_load_dir: no directory set");
    }
    struct yetty_yplatform_dir *dir = yetty_yplatform_dir_open(d->cwd);
    if (!dir) {
        return YETTY_ERR(yetty_ycore_void, "fp_load_dir: cannot open directory");
    }
    int cap = 16, n = 0;
    char **list = malloc((size_t)cap * sizeof(char *));
    if (!list) {
        yetty_yplatform_dir_close(dir);
        return YETTY_ERR(yetty_ycore_void, "fp_load_dir: alloc entry list");
    }
    struct yetty_yplatform_dir_entry e;
    while (yetty_yplatform_dir_next(dir, &e)) {
        if (strcmp(e.name, ".") == 0) {
            continue;
        }
        if (n == cap) {
            int nc = cap * 2;
            char **grown = realloc(list, (size_t)nc * sizeof(char *));
            if (!grown) {
                for (int i = 0; i < n; i++) {
                    free(list[i]);
                }
                free(list);
                yetty_yplatform_dir_close(dir);
                return YETTY_ERR(yetty_ycore_void, "fp_load_dir: grow entry list");
            }
            list = grown;
            cap = nc;
        }
        size_t name_len = strlen(e.name);
        char *entry = malloc(name_len + 2);
        if (!entry) {
            for (int i = 0; i < n; i++) {
                free(list[i]);
            }
            free(list);
            yetty_yplatform_dir_close(dir);
            return YETTY_ERR(yetty_ycore_void, "fp_load_dir: alloc entry");
        }
        memcpy(entry, e.name, name_len);
        if (e.is_dir) {
            entry[name_len] = '/';
            entry[name_len + 1] = '\0';
        } else {
            entry[name_len] = '\0';
        }
        list[n++] = entry;
    }
    yetty_yplatform_dir_close(dir);
    qsort(list, (size_t)n, sizeof(char *), fp_entry_cmp);
    /* Commit — only now that the full listing succeeded. */
    fp_free_entries(d);
    d->selected = -1;
    d->scroll = 0;
    d->entries = list;
    d->entry_count = n;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:filepicker:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, fp_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "filepicker: super");
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    d->cwd = NULL;
    d->entries = NULL;
    d->entry_count = 0;
    d->selected = -1;
    d->scroll = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:filepicker:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    fp_free_entries(d);
    free(d->cwd);
    return yetty_ygui_super_void(obj, fp_class(),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:filepicker:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "filepicker paint: NULL ctx");
    }
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float fs = 13.0f;

    /* Frame + header bar with the current path. */
    struct yetty_ysdf_box frame = {.center_x = r.min.x + w * 0.5f,
                                   .center_y = r.min.y + h * 0.5f,
                                   .half_width = w * 0.5f,
                                   .half_height = h * 0.5f,
                                   .corner_radius = 0.0f};
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0, 0, FP_BG,
                                                              FP_BORDER, 1.0f, &frame),
                        "fp: frame");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y, w, FP_HEADER_H, FP_HEADER_BG, 0.0f),
                        "fp: header");
    if (d->cwd) {
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->cwd, r.min.x + FP_PAD,
                                       r.min.y + (FP_HEADER_H + fs) * 0.5f - 3.0f, fs, FP_TEXT),
                            "fp: cwd");
    }

    float list_y = r.min.y + FP_HEADER_H + 2.0f;
    float list_h = h - FP_HEADER_H - 4.0f;
    int visible = (int)(list_h / FP_ROW_H);
    if (visible < 1) {
        visible = 1;
    }
    int first = d->scroll;
    int last = first + visible;
    if (last > d->entry_count) {
        last = d->entry_count;
    }
    for (int i = first; i < last; i++) {
        float ry = list_y + (float)(i - first) * FP_ROW_H;
        if (i == d->selected) {
            YETTY_RETURN_IF_ERR(yetty_ycore_void,
                                yguix_box(ctx, r.min.x + 2.0f, ry, w - 4.0f, FP_ROW_H, FP_SEL_BG, 0.0f),
                                "fp: sel");
        }
        const char *e = d->entries[i];
        uint32_t col = (e && strchr(e, '/')) ? FP_DIR : FP_TEXT;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, e ? e : "", r.min.x + FP_PAD,
                                       ry + (FP_ROW_H + fs) * 0.5f - 3.0f, fs, col),
                            "fp: row");
    }

    /* Scrollbar — shown whenever the list overflows the visible rows. The
     * thumb height is the visible fraction; its position tracks d->scroll.
     * Drag the list (on_motion) to move it. */
    if (d->entry_count > visible) {
        float sb_w = 5.0f;
        float sb_x = r.max.x - sb_w - 2.0f;
        float track_h = (float)visible * FP_ROW_H;
        if (track_h > list_h) {
            track_h = list_h;
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_box(ctx, sb_x, list_y, sb_w, track_h, FP_SEL_BG, sb_w * 0.5f),
                            "fp: sb track");
        float thumb_h = track_h * (float)visible / (float)d->entry_count;
        if (thumb_h < 16.0f) {
            thumb_h = 16.0f;
        }
        if (thumb_h > track_h) {
            thumb_h = track_h;
        }
        int max_scroll = d->entry_count - visible;
        float frac = max_scroll > 0 ? (float)d->scroll / (float)max_scroll : 0.0f;
        float thumb_y = list_y + frac * (track_h - thumb_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_box(ctx, sb_x, thumb_y, sb_w, thumb_h, FP_DIR, sb_w * 0.5f),
                            "fp: sb thumb");
    }
    return YETTY_OK_VOID();
}

/* Drag-to-scroll: while this picker holds the pointer capture, translate
 * vertical drag into a row offset. The paint already windows + clips to
 * [scroll, scroll+visible), so changing scroll is all that's needed. */
[[clang::annotate("override@ygui:filepicker:widget_on_motion")]]
static struct yetty_ycore_int_result fp_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                  struct yetty_yclass_object *yclass_obj, float x,
                                                  float y)
{
    (void)yclass_ctx;
    (void)x;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_framework *eng = yetty_ygui_object_framework(obj);
    if (!eng || eng->pressed_obj != obj) {
        return YETTY_OK(yetty_ycore_int, 0); /* only while we're the drag target */
    }
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float list_h = (r.max.y - r.min.y) - FP_HEADER_H - 4.0f;
    int visible = (int)(list_h / FP_ROW_H);
    if (visible < 1) {
        visible = 1;
    }
    int max_scroll = d->entry_count - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    /* Drag down scrolls down the list (later rows) — matches the thumb
     * drag and the gutter-jump direction. */
    int ns = d->press_scroll + (int)((y - d->press_y) / FP_ROW_H);
    if (ns < 0) {
        ns = 0;
    }
    if (ns > max_scroll) {
        ns = max_scroll;
    }
    if (ns != d->scroll) {
        d->scroll = ns;
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "fp: scroll dirty", dr);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Wheel / trackpad scroll over the list. dy>0 (wheel up) moves toward the
 * top. Consumed only when the list overflows, so it bubbles otherwise. */
[[clang::annotate("override@ygui:filepicker:widget_on_scroll")]]
static struct yetty_ycore_int_result fp_on_scroll(struct yetty_yclass_ctx *yclass_ctx,
                                                  struct yetty_yclass_object *yclass_obj, float x,
                                                  float y, float dx, float dy)
{
    (void)yclass_ctx;
    (void)x;
    (void)y;
    (void)dx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float list_h = (r.max.y - r.min.y) - FP_HEADER_H - 4.0f;
    int visible = (int)(list_h / FP_ROW_H);
    if (visible < 1) {
        visible = 1;
    }
    int max_scroll = d->entry_count - visible;
    if (max_scroll <= 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int ns = d->scroll - (int)dy;
    if (ns < 0) {
        ns = 0;
    }
    if (ns > max_scroll) {
        ns = max_scroll;
    }
    if (ns != d->scroll) {
        d->scroll = ns;
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "fp: scroll dirty", dr);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:filepicker:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)btn;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float ly = y - r.min.y;
    if (ly < FP_HEADER_H + 2.0f) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Anchor a possible drag-to-scroll; consuming the press captures the
     * pointer so on_motion gets the drag even past the widget's rect. */
    d->press_y = y;
    d->press_scroll = d->scroll;

    /* A press in the scrollbar gutter jumps the thumb to that position — it
     * is NOT a row click (clicking the gutter must never select/navigate
     * the row painted behind it). Geometry mirrors the paint. */
    float list_h = (r.max.y - r.min.y) - FP_HEADER_H - 4.0f;
    int visible = (int)(list_h / FP_ROW_H);
    if (visible < 1) {
        visible = 1;
    }
    int max_scroll = d->entry_count - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    float sb_x = r.max.x - 5.0f - 2.0f;
    if (x >= sb_x - 2.0f && max_scroll > 0) {
        float track_y = r.min.y + FP_HEADER_H + 2.0f;
        float track_h = (float)visible * FP_ROW_H;
        if (track_h > list_h) {
            track_h = list_h;
        }
        float frac = track_h > 0.0f ? (y - track_y) / track_h : 0.0f;
        if (frac < 0.0f) {
            frac = 0.0f;
        }
        if (frac > 1.0f) {
            frac = 1.0f;
        }
        int ns = (int)(frac * (float)max_scroll + 0.5f);
        if (ns > max_scroll) {
            ns = max_scroll;
        }
        d->scroll = ns;
        d->press_scroll = d->scroll;
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "fp: gutter scroll dirty", dr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    int row = (int)((ly - FP_HEADER_H - 2.0f) / FP_ROW_H) + d->scroll;
    if (row < 0 || row >= d->entry_count) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const char *name = d->entries[row];
    if (!name) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    if (strchr(name, '/')) {
        /* Directory — navigate. */
        char newcwd[4096];
        if (strcmp(name, "../") == 0) {
            strncpy(newcwd, d->cwd ? d->cwd : "/", sizeof(newcwd) - 1);
            newcwd[sizeof(newcwd) - 1] = '\0';
            char *slash = strrchr(newcwd, '/');
            if (slash && slash != newcwd) {
                *slash = '\0';
            } else if (slash == newcwd) {
                newcwd[1] = '\0'; /* keep "/" root */
            }
        } else {
            const char *base = d->cwd ? d->cwd : "/";
            if (strcmp(base, "/") == 0) {
                snprintf(newcwd, sizeof(newcwd), "/%s", name);
            } else {
                snprintf(newcwd, sizeof(newcwd), "%s/%s", base, name);
            }
            size_t len = strlen(newcwd);
            if (len > 1 && newcwd[len - 1] == '/') {
                newcwd[len - 1] = '\0';
            }
        }
        char *dup = fp_strdup(newcwd);
        if (!dup) {
            return YETTY_ERR(yetty_ycore_int, "fp: strdup cwd");
        }
        char *old = d->cwd;
        d->cwd = dup;
        struct yetty_ycore_void_result lr = fp_load_dir(d);
        if (YETTY_IS_ERR(lr)) {
            /* Open failed — stay in the current directory (don't blank). */
            free(d->cwd);
            d->cwd = old;
            return YETTY_ERR(yetty_ycore_int, "fp: open directory", lr);
        }
        free(old);
    } else {
        /* File — select. */
        d->selected = row;
    }
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "fp: dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ycore_void_result yetty_ygui_filepicker_set_dir(struct yetty_ygui_object *obj,
                                                             const char *path)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "filepicker_set_dir: NULL");
    }
    struct fp_data *d = yetty_ygui_data_get(obj, fp_class());
    char *dup = fp_strdup(path && *path ? path : "/");
    if (!dup) {
        return YETTY_ERR(yetty_ycore_void, "filepicker_set_dir: strdup");
    }
    char *old = d->cwd;
    d->cwd = dup;
    struct yetty_ycore_void_result lr = fp_load_dir(d);
    if (YETTY_IS_ERR(lr)) {
        free(d->cwd);
        d->cwd = old;
        return YETTY_ERR(yetty_ycore_void, "filepicker_set_dir: load", lr);
    }
    free(old);
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_filepicker_get_dir(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    return ((struct fp_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj, fp_class()))->cwd;
}

#include "filepicker.gen.c"
