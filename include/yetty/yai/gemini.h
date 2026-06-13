/* GENERATED — do not edit. */
/* Public interface for regular class(es) `gemini` (module: yai).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YAI_GEMINI_H
#define YETTY_YCLASSGEN_YAI_GEMINI_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yai_gemini;

struct yetty_yai_gemini_ptr_result {
    int ok;
    union {
        struct yetty_yai_gemini *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yai_gemini_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yai_gemini_ptr_result yetty_yai_gemini_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yai_gemini_to(struct yetty_yai_gemini *data);

struct yetty_yclass_object_ptr_result yetty_yai_gemini_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yai_register(void);

#endif
