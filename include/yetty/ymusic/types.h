/* Module-owned result type declarations.
 * Add YETTY_YRESULT_DECLARE(<id>, <type>) lines here for any
 * struct/scalar return type used by ymusic's yclass methods.
 * Hand-written — codegen will NOT overwrite. */
#ifndef YETTY_YCLASSGEN_YMUSIC_TYPES_H
#define YETTY_YCLASSGEN_YMUSIC_TYPES_H

#include <yetty/ycore/result.h>

/* `render` returns a drawable list by value; its result type must be complete
 * (not just forward-declared) in the generated method stubs. */
#include <yetty/ydraw-core/drawable-list.h>

#endif /* YETTY_YCLASSGEN_YMUSIC_TYPES_H */
