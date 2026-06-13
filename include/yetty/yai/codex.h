/* GENERATED — do not edit. */
/* Public interface for regular class(es) `codex` (module: yai).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YAI_CODEX_H
#define YETTY_YCLASSGEN_YAI_CODEX_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yai_codex;

struct yetty_yai_codex_ptr_result {
    int ok;
    union {
        struct yetty_yai_codex *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yai_codex_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yai_codex_ptr_result yetty_yai_codex_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yai_codex_to(struct yetty_yai_codex *data);

struct yetty_yclass_object_ptr_result yetty_yai_codex_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yai_register(void);

#endif
