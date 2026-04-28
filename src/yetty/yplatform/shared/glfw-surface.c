/* GLFW surface - WebGPU surface creation from GLFW window */

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

WGPUSurface yetty_yplatform_create_surface(WGPUInstance instance, GLFWwindow *window)
{
    if (!instance || !window) {
        return NULL;
    }

    return glfwCreateWindowWGPUSurface(instance, window);
}
