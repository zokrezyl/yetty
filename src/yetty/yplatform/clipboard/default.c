/* GLFW clipboard manager implementation */

#include <yetty/yplatform/clipboard-manager.h>
#include <yetty/ycore/types.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

/* GLFW clipboard manager - embeds base as first member */
struct yetty_yplatform_glfw_clipboard_manager {
    struct yetty_platform_clipboard_manager base;
};

/* Forward declarations */
static void glfw_clipboard_destroy(struct yetty_platform_clipboard_manager *self);
static const char *glfw_clipboard_get_text(struct yetty_platform_clipboard_manager *self);
static void glfw_clipboard_set_text(struct yetty_platform_clipboard_manager *self, const char *text,
                                    size_t len);

/* Ops table */
static const struct yetty_platform_clipboard_manager_ops glfw_clipboard_ops = {
    .destroy = glfw_clipboard_destroy,
    .get_text = glfw_clipboard_get_text,
    .set_text = glfw_clipboard_set_text,
};

/* Implementation */

static void glfw_clipboard_destroy(struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *manager;

    manager = container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);
    free(manager);
}

static const char *glfw_clipboard_get_text(struct yetty_platform_clipboard_manager *self)
{
    (void)self;
    return glfwGetClipboardString(NULL);
}

static void glfw_clipboard_set_text(struct yetty_platform_clipboard_manager *self, const char *text,
                                    size_t len)
{
    char *buf;

    (void)self;

    /* GLFW expects null-terminated string */
    if (text[len] == '\0') {
        glfwSetClipboardString(NULL, text);
    } else {
        buf = malloc(len + 1);
        if (buf) {
            memcpy(buf, text, len);
            buf[len] = '\0';
            glfwSetClipboardString(NULL, buf);
            free(buf);
        }
    }
}

/* Create function */

struct yetty_yplatform_clipboard_manager_result yetty_platform_clipboard_manager_create(void)
{
    struct yetty_yplatform_glfw_clipboard_manager *manager;

    manager = malloc(sizeof(struct yetty_yplatform_glfw_clipboard_manager));
    if (!manager) {
        return YETTY_ERR(yetty_yplatform_clipboard_manager, "failed to allocate clipboard manager");
    }

    manager->base.ops = &glfw_clipboard_ops;

    return YETTY_OK(yetty_yplatform_clipboard_manager, &manager->base);
}
