/* FFI binding annotations.
 *
 * In a normal build these expand to nothing. The libclang extractor in
 * tools/ffi-gen/extract/ surfaces them as CXCursor_AnnotateAttr children of
 * the annotated declaration, and emits the matching role/ownership/lifetime
 * fact into build/ffi/metadata.yaml.
 *
 * All flavor macros are prefixed `YETTY_ANNOT_` so any annotation is visually
 * grouped at a glance and `grep YETTY_ANNOT_` finds the full surface.
 *
 * Day-one shipping set (per docs/ffi-gen.md): YETTY_ANNOT_OUT,
 * YETTY_ANNOT_ARRAY(len), YETTY_ANNOT_CALLER_OWNED, YETTY_ANNOT_CALLEE_OWNED.
 * The remaining annotations document intent today and become load-bearing
 * once the relevant binding target lands.
 */
#ifndef YETTY_YCORE_FFI_ANNOTATIONS_H
#define YETTY_YCORE_FFI_ANNOTATIONS_H

#if defined(__clang__) || defined(__GNUC__)
#  define YETTY_ANNOTATE(s) __attribute__((annotate(s)))
#else
#  define YETTY_ANNOTATE(s)
#endif

/* Parameter roles */
#define YETTY_ANNOT_OUT            YETTY_ANNOTATE("yetty:out")
#define YETTY_ANNOT_INOUT          YETTY_ANNOTATE("yetty:inout")
#define YETTY_ANNOT_ARRAY(len)     YETTY_ANNOTATE("yetty:array:" #len)

/* Ownership.
 *
 * Names who owns the resource at the call boundary the annotation tags:
 *
 *   on a return  — who owns the returned pointer after the call returns?
 *   on a param   — who owns the pointer after the call returns?
 *
 *   YETTY_ANNOT_CALLER_OWNED  on return  → caller must destroy.
 *   YETTY_ANNOT_CALLEE_OWNED  on return  → caller borrows; must NOT destroy.
 *   YETTY_ANNOT_CALLEE_OWNED  on param   → ownership transferred in;
 *                                          caller must not use the pointer
 *                                          afterwards (matches *_destroy).
 *   YETTY_ANNOT_CALLER_OWNED  on param   → just a borrow (= default).
 */
#define YETTY_ANNOT_CALLER_OWNED   YETTY_ANNOTATE("yetty:caller_owned")
#define YETTY_ANNOT_CALLEE_OWNED   YETTY_ANNOTATE("yetty:callee_owned")

/* Nullability and strings */
#define YETTY_ANNOT_NULLABLE       YETTY_ANNOTATE("yetty:nullable")
#define YETTY_ANNOT_NONNULL        YETTY_ANNOTATE("yetty:nonnull")
#define YETTY_ANNOT_CSTRING        YETTY_ANNOTATE("yetty:cstring")

/* Callback lifetime */
#define YETTY_ANNOT_CB_CALL_ONLY   YETTY_ANNOTATE("yetty:cb_call_only")
#define YETTY_ANNOT_CB_RETAINED    YETTY_ANNOTATE("yetty:cb_retained")

#endif /* YETTY_YCORE_FFI_ANNOTATIONS_H */
