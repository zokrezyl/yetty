/* FFI binding annotations.
 *
 * In a normal build these expand to nothing. The libclang extractor in
 * tools/ffi-gen/extract/ surfaces them as CXCursor_AnnotateAttr children of
 * the annotated declaration, and emits the matching role/ownership/lifetime
 * fact into build/ffi/metadata.yaml.
 *
 * Day-one shipping set (per docs/ffi-gen.md): YETTY_OUT, YETTY_ARRAY(len),
 * YETTY_RETURNS_OWNED, YETTY_CONSUMES. The remaining annotations document
 * intent today and become load-bearing once the relevant binding target
 * lands.
 */
#ifndef YETTY_YCORE_FFI_ANNOTATIONS_H
#define YETTY_YCORE_FFI_ANNOTATIONS_H

#if defined(__clang__) || defined(__GNUC__)
#  define YETTY_ANNOTATE(s) __attribute__((annotate(s)))
#else
#  define YETTY_ANNOTATE(s)
#endif

/* Parameter roles */
#define YETTY_OUT            YETTY_ANNOTATE("yetty:out")
#define YETTY_INOUT          YETTY_ANNOTATE("yetty:inout")
#define YETTY_ARRAY(len)     YETTY_ANNOTATE("yetty:array:" #len)

/* Ownership */
#define YETTY_OWNED          YETTY_ANNOTATE("yetty:owned")
#define YETTY_BORROWED       YETTY_ANNOTATE("yetty:borrowed")
#define YETTY_CONSUMES       YETTY_ANNOTATE("yetty:consumes")
#define YETTY_RETURNS_OWNED  YETTY_ANNOTATE("yetty:returns_owned")

/* Nullability and strings */
#define YETTY_NULLABLE       YETTY_ANNOTATE("yetty:nullable")
#define YETTY_NONNULL        YETTY_ANNOTATE("yetty:nonnull")
#define YETTY_CSTRING        YETTY_ANNOTATE("yetty:cstring")

/* Callback lifetime */
#define YETTY_CB_CALL_ONLY   YETTY_ANNOTATE("yetty:cb_call_only")
#define YETTY_CB_RETAINED    YETTY_ANNOTATE("yetty:cb_retained")

#endif /* YETTY_YCORE_FFI_ANNOTATIONS_H */
