#include <yetty/yfigure/figure.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_yfigure_group {
    struct yetty_yfigure_figure base;
    /* Insertion-ordered (back-to-front). The group owns each child
     * pointer until removed. */
    struct yetty_yfigure_figure **children;
    size_t children_count;
    size_t children_cap;
};

/*===========================================================================
 * Group ops
 *=========================================================================*/

static struct yetty_ycore_void_result group_destroy(struct yetty_yfigure_figure *self)
{
    struct yetty_yfigure_group *g = (struct yetty_yfigure_group *)self;
    /* Cleanup path: keep running so every child is released; surface
     * the first error at the end via the chained cause. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    for (size_t i = 0; i < g->children_count; ++i) {
        struct yetty_yfigure_figure *c = g->children[i];
        struct yetty_ycore_void_result r = c->ops->destroy(c);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) {
                first_err = r;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    free(g->children);
    free(g);
    if (have_err)
        return YETTY_ERR(yetty_ycore_void, "yfigure group_destroy: child destroy failed",
                         first_err);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result group_render(
    struct yetty_yfigure_figure *self,
    struct yetty_ydraw_target *target)
{
    struct yetty_yfigure_group *g = (struct yetty_yfigure_group *)self;
    ydebug("yfigure group_render: rect=(%.1f,%.1f)-(%.1f,%.1f) children=%zu",
           self->rect.min.x, self->rect.min.y, self->rect.max.x, self->rect.max.y,
           g->children_count);
    /* Children already know their own absolute positions via their
     * own rect — no translation here. */
    for (size_t i = 0; i < g->children_count; ++i) {
        struct yetty_yfigure_figure *c = g->children[i];
        struct yetty_ycore_void_result r = c->ops->render(c, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yfigure group_render: child render failed");
        c->dirty = 0;
    }
    return YETTY_OK_VOID();
}

static const struct yetty_yfigure_figure_ops *group_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = group_destroy,
        .render = group_render,
    };
    return &ops;
}

/*===========================================================================
 * Internal helpers
 *=========================================================================*/

static ssize_t find_child_index(const struct yetty_yfigure_group *g,
                                const struct yetty_yfigure_figure *child)
{
    for (size_t i = 0; i < g->children_count; ++i) {
        if (g->children[i] == child)
            return (ssize_t)i;
    }
    return -1;
}

static struct yetty_ycore_void_result grow_children(struct yetty_yfigure_group *g)
{
    if (g->children_count < g->children_cap)
        return YETTY_OK_VOID();
    size_t cap = g->children_cap ? g->children_cap * 2u : 4u;
    struct yetty_yfigure_figure **grown =
        (struct yetty_yfigure_figure **)realloc(g->children, cap * sizeof(*grown));
    if (!grown)
        return YETTY_ERR(yetty_ycore_void, "yfigure group: children table oom");
    g->children = grown;
    g->children_cap = cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_yfigure_group_ptr_result yetty_yfigure_group_create(
    struct yetty_ycore_rectangle rect)
{
    struct yetty_yfigure_group *g =
        (struct yetty_yfigure_group *)calloc(1, sizeof(struct yetty_yfigure_group));
    if (!g)
        return YETTY_ERR(yetty_yfigure_group_ptr, "yfigure_group_create: oom");
    g->base.ops = group_ops();
    g->base.rect = rect;
    g->base.dirty = 0;
    return YETTY_OK(yetty_yfigure_group_ptr, g);
}

struct yetty_yfigure_figure *yetty_yfigure_group_as_figure(struct yetty_yfigure_group *group)
{
    return group ? &group->base : NULL;
}

struct yetty_ycore_void_result yetty_yfigure_group_add_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child)
{
    if (!group || !child)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: NULL arg");
    /* Boundary check — verify the concrete figure was fully constructed
     * before it enters the group. After this point internal code may
     * assume child->ops + ops->destroy + ops->render are non-NULL. */
    if (!child->ops || !child->ops->destroy || !child->ops->render)
        return YETTY_ERR(yetty_ycore_void,
                         "yfigure_group_add_child: child ops vtable incomplete");
    if (find_child_index(group, child) >= 0)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: child already present");
    struct yetty_ycore_void_result gr = grow_children(group);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "yfigure_group_add_child: grow_children");
    group->children[group->children_count++] = child;
    child->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_group_remove_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child)
{
    if (!group || !child)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_remove_child: NULL arg");
    ssize_t idx = find_child_index(group, child);
    if (idx < 0)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_remove_child: child not in group");
    for (size_t i = (size_t)idx + 1; i < group->children_count; ++i)
        group->children[i - 1] = group->children[i];
    group->children_count--;
    group->children[group->children_count] = NULL;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_group_raise_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child)
{
    if (!group || !child)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_raise_child: NULL arg");
    ssize_t idx = find_child_index(group, child);
    if (idx < 0)
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_raise_child: child not in group");
    if ((size_t)idx == group->children_count - 1)
        return YETTY_OK_VOID();
    for (size_t i = (size_t)idx + 1; i < group->children_count; ++i)
        group->children[i - 1] = group->children[i];
    group->children[group->children_count - 1] = child;
    return YETTY_OK_VOID();
}
