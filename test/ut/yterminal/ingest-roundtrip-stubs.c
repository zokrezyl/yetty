/*
 * Link stubs for the headless ingest-roundtrip test.
 *
 * terminal.c is one translation unit: linking it pulls terminal_open's
 * references to the GPU figure factories and the yframework bootstrap,
 * none of which exist in a headless test link (yframework.c is compiled
 * into the application executable, and the GPU factory libs need Dawn
 * wiring the harness never performs). The harness path
 * (yetty_yterminal_ingest_harness_open + mime ingest) never calls
 * terminal_open, so these definitions exist purely to satisfy the
 * linker; reaching one is a test bug and fails loudly.
 */

#include <yetty/ycore/result.h>

#include <stddef.h>

struct yetty_ydraw_concrete_factory;
struct yetty_yframework;
struct yetty_yfigure_registry;
struct yetty_context;
struct yetty_yrdawn_factory_args;
YETTY_YRESULT_DECLARE(yetty_yrdawn_factory_args_ptr, struct yetty_yrdawn_factory_args *);

struct yetty_ydraw_concrete_factory *yetty_yplot_factory_create(void)
{
    return NULL;
}

void yetty_yplot_factory_destroy(struct yetty_ydraw_concrete_factory *factory)
{
    (void)factory;
}

struct yetty_ydraw_concrete_factory *yetty_yimage_factory_create(void)
{
    return NULL;
}

void yetty_yimage_factory_destroy(struct yetty_ydraw_concrete_factory *factory)
{
    (void)factory;
}

struct yetty_ydraw_concrete_factory *yetty_yshadertoy_prim_factory_create(void)
{
    return NULL;
}

void yetty_yshadertoy_prim_factory_destroy(struct yetty_ydraw_concrete_factory *factory)
{
    (void)factory;
}

struct yetty_ycore_void_result yetty_yframework_register_figure_factories(
    struct yetty_yframework *framework, struct yetty_yfigure_registry *registry,
    const struct yetty_context *context)
{
    (void)framework;
    (void)registry;
    (void)context;
    return YETTY_ERR(yetty_ycore_void, "ingest-roundtrip stub: register_figure_factories");
}

struct yetty_yrdawn_factory_args_ptr_result yetty_yframework_factory_args_yrdawn(
    struct yetty_yframework *framework)
{
    (void)framework;
    return YETTY_ERR(yetty_yrdawn_factory_args_ptr, "ingest-roundtrip stub: factory_args_yrdawn");
}
