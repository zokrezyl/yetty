/*
 * editor.c — yclass base class `yai:editor`: the abstract line-editor
 * strategy. Two subclasses implement the keystroke interpretation:
 * yai:emacs (editor-emacs.c) and yai:vi (editor-vi.c).
 *
 * The line buffer itself lives in `struct yai_app` (so the renderer's
 * pinned prompt can borrow it); the editor object carries only the
 * mode's own scratch state. The single polymorphic slot is feed_byte:
 *
 *   feed_byte(obj, app, byte) -> yetty_ycore_int_result
 *
 * whose int value is one of the YAI_EDIT_* action codes (editor-ops.h).
 * main.c maps each action to a UI effect, so menu/history/submit policy
 * stays out of the editor.
 *
 * This is `local@` — the editor is an in-process strategy object, never
 * proxied over RPC; the model still records it for the binding
 * generators. The base default errors: the base is abstract, every
 * concrete mode overrides feed_byte. editor.gen.c (the accessor body)
 * is #included at the foot.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

struct yai_app;

/* The class@ annotation needs a struct to sit on; the base carries no
 * per-instance state (the line buffer is in struct yai_app, mode scratch
 * is in the subclass). */
struct [[clang::annotate("class@yai:editor")]] yetty_yai_editor {
    char unused;
};

YETTY_YRESULT_DECLARE(yetty_yai_editor_ptr, struct yetty_yai_editor *);

[[clang::annotate("virtual@yai:editor:feed_byte")]] [[clang::annotate("local@yai:feed_byte")]]
static struct yetty_ycore_int_result editor_feed_byte(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, int byte)
{
    (void)obj;
    (void)app;
    (void)byte;
    return YETTY_ERR(yetty_ycore_int, "yai editor: feed_byte not implemented (abstract base)");
}

#include "editor.gen.c"
