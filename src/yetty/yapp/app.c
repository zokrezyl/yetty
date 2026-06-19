/*
 * yapp/app.c — WEAK default application entry points.
 *
 * The platform framework calls yetty_app_run / yetty_app_extract_assets but does
 * not know which application it is hosting. These weak definitions let the
 * framework link and run on its own; any executable that also links a STRONG
 * definition (the yetty terminal in src/yetty/yetty/app.c, a standalone tool in
 * its own main) overrides the weak one at link time. That is how one framework
 * is reused by every app — the app is whichever strong yetty_app_run the
 * executable links.
 *
 * Compiled into yetty_yplatform_core so the weak fallback is always available.
 */

#include <yetty/yapp/app.h>
#include <yetty/ycore/result.h>
#include <yetty/yinit/yinit.h>

__attribute__((weak)) struct yetty_ycore_void_result yetty_app_run(struct yetty_yinit_runtime *runtime)
{
    (void)runtime;
    return YETTY_ERR(yetty_ycore_void,
                     "yetty_app_run: no application linked — the executable must define a strong "
                     "yetty_app_run");
}
