/*
 * yapp/app.h — the application entry the platform calls.
 *
 * The platform framework (yplatform:platform and its per-OS subclasses) is
 * app-agnostic: it brings up config / window / clipboard / GPU surface / the
 * cross-thread channels, then calls the two functions below. Each application —
 * the yetty terminal, a standalone tool — provides its own STRONG definition of
 * these symbols; the framework ships WEAK defaults (src/yetty/yapp/app.c) so it
 * links on its own and falls back to a clear error when no app is present.
 *
 * The linker selects the app: an executable that links the framework plus its
 * own strong yetty_app_run IS that app. The same framework sources are reused by
 * every app unchanged — no per-app object, subclass, or registration.
 *
 *   yetty_app_run(runtime)     — the app body. Runs on the worker thread the
 *                   platform spawns; the runtime stays valid until it returns.
 */

#ifndef YETTY_YAPP_APP_H
#define YETTY_YAPP_APP_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yinit_runtime;

struct yetty_ycore_void_result yetty_app_run(struct yetty_yinit_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YAPP_APP_H */
