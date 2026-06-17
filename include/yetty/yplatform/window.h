#ifndef YETTY_YPLATFORM_WINDOW_H
#define YETTY_YPLATFORM_WINDOW_H

/*
 * Desktop (GLFW) window management. The implementation lives in
 * src/yetty/yplatform/window/default.c; the WebAssembly canvas backend
 * (window/webasm.c) exposes a separate yetty_yplatform_webasm_* API and
 * is not declared here.
 *
 * GLFWwindow is referenced only as an opaque pointer, so this header
 * forward-declares the struct rather than pulling in <GLFW/glfw3.h>.
 * Callers that need GLFW types include the GLFW header themselves; the
 * forward declaration is layout-compatible with GLFW's
 * `typedef struct GLFWwindow GLFWwindow`.
 */

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;

YETTY_YRESULT_DECLARE(yetty_yplatform_glfwindow_ptr, struct GLFWwindow *);

/* Create the application window (no OpenGL context — WebGPU draws into it;
 * undecorated, the tab strip is drawn by yetty). On failure the result error
 * names the GLFW reason (API unavailable, platform error, ...). */
struct yetty_yplatform_glfwindow_ptr_result yetty_yplatform_create_window(int width, int height,
                                                                          const char *title);

/* Best-effort teardown — NULL-tolerant, never fails. */
struct yetty_ycore_void_result yetty_yplatform_destroy_window(struct GLFWwindow *window);

/* Write the window (logical) size to the width and height out-params. Errors
 * on a NULL window. */
struct yetty_ycore_void_result yetty_yplatform_get_window_size(struct GLFWwindow *window,
                                                               int *width, int *height);

/* Write the framebuffer (physical pixel) size to the width and height
 * out-params. Errors on a NULL window. */
struct yetty_ycore_void_result yetty_yplatform_get_framebuffer_size(struct GLFWwindow *window,
                                                                    int *width, int *height);

/* Write the HiDPI content scale to the xscale and yscale out-params. Errors on
 * a NULL window. */
struct yetty_ycore_void_result yetty_yplatform_get_content_scale(struct GLFWwindow *window,
                                                                 float *xscale, float *yscale);

/* Result value is the should-close flag (1 = close). A NULL window reports 1. */
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct GLFWwindow *window);

/* Set the window title. Errors on a NULL window. */
struct yetty_ycore_void_result yetty_yplatform_set_window_title(struct GLFWwindow *window,
                                                                const char *title);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_WINDOW_H */
