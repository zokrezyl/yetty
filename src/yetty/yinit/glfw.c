/*
 * yinit/glfw.c — TEMPORARY tombstone.
 *
 * The GLFW desktop bootstrap moved into the yplatform glfw_platform yclass
 * (src/yetty/yplatform/yplatform/glfw.c). Its run() is the real desktop
 * bootstrap and brings up the window + clipboard through the ywindow / yclipboard
 * abstractions. The yetty exec boots through that path; it does not use this
 * file.
 *
 * The standalone apps (ydoc / ysheet / yslide and the demo/diagnostic tools)
 * still call yetty_yinit_run and have not yet been migrated to the new bootstrap.
 * This stub keeps them linking until that migration happens; invoking it returns
 * an error. Delete this file once those apps no longer reference yetty_yinit_run.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/ycore/result.h>

struct yetty_ycore_int_result yetty_yinit_run(int argc, char **argv,
                                              yetty_yinit_worker_fn worker, void *user)
{
    (void)argc;
    (void)argv;
    (void)worker;
    (void)user;
    return YETTY_ERR(yetty_ycore_int,
                     "yetty_yinit_run was removed; this app must migrate to the yplatform "
                     "glfw_platform bootstrap");
}
