/*
 * yinstall/install.h — the yetty installer module.
 *
 * yinstall is a self-contained executable that installs the yetty product
 * (the terminal, the companion CLIs and demos, shaders, fonts, config, and
 * the RISC-V VM runtime) onto a machine. Its payload is baked into the
 * binary; on run it unpacks every piece to the right per-OS location and
 * prints a clear log of what landed where. The build produces three
 * variants of the same executable that differ only in the components they
 * carry (yinstall-min / yinstall / yinstall-max); the install loop is the
 * same for all — a component the variant does not carry is simply skipped.
 *
 * This module is the platform-independent core: the component model, the
 * install loop, decompression, and the log. The embedded bytes are read
 * through yetty_yplatform_install_foreach_asset (incbin symbols on
 * Linux/macOS, RT_RCDATA on Windows); destinations resolve through
 * yetty_yplatform_get_{bin,data,config}_dir. See src/yetty/yinstall/README.md.
 */

#ifndef YETTY_YINSTALL_INSTALL_H
#define YETTY_YINSTALL_INSTALL_H

#include <stddef.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which resolved root a component's files install under. */
enum yetty_yinstall_destination {
    YETTY_YINSTALL_DEST_BIN,    /* executables → platform paths bin_dir    */
    YETTY_YINSTALL_DEST_LIB,    /* shared libraries → platform paths lib_dir */
    YETTY_YINSTALL_DEST_DATA,   /* shaders, fonts, VM runtime → data_dir   */
    YETTY_YINSTALL_DEST_CONFIG, /* config files → config_dir               */
};

/*
 * One installable bundle. The installer holds a static table of these
 * (see the component table in install.c) and lays each one down in order.
 * Every embedded asset whose name starts with `prefix` belongs to this
 * component; the prefix is stripped and the remainder is rooted at
 * `destination` (+ optional `subdir`).
 */
struct yetty_yinstall_component {
    const char *name;                            /* log label, e.g. "Executables"        */
    const char *prefix;                          /* embed prefix, e.g. "bin/"            */
    enum yetty_yinstall_destination destination; /* which root the files land under      */
    const char *subdir;                          /* "" or e.g. "yemu" (under the root)   */
    int executable;                              /* set exec bit on every file extracted */
    const char *description;                     /* one-line summary for the log         */
};

/* Knobs for a single install run. */
struct yetty_yinstall_options {
    const char *name; /* installer variant for the banner, e.g. "yinstall-min"; NULL → "yinstall" */
    const char *version; /* build version for the banner + marker; NULL → "dev" */
    int verbose;         /* list every file as it lands, not just per-component totals */
    int force;           /* rewrite files even when an up-to-date copy is present */
};

/*
 * Run the installer: resolve destinations, unpack every embedded
 * component to disk, and print the install log. Idempotent — files already
 * present with the right size are skipped unless `force` is set. Returns
 * the first failure (the offending file is named in the printed log) or
 * success.
 */
struct yetty_ycore_void_result yetty_yinstall_run(const struct yetty_yinstall_options *options);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YINSTALL_INSTALL_H */
