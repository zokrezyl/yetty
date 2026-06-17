/* Embedded assets for the standalone yhello binary.
 *
 * Mirrors src/yetty/yncbin/incbin-assets.c but at a much smaller scale:
 * a single "data/" section, no yconfig/yemu/qemu, and a private marker
 * file so extraction doesn't collide with yetty's own asset marker when
 * the two binaries share a data dir.
 *
 * Compiled into yhello only when YETTY_ENABLE_LIB_INCBIN is on AND
 * the embed staging actually produced a manifest (HAS_DATA_MANIFEST). In
 * dev builds without incbin, this becomes a no-op so yhello still
 * works via argv0-relative lookup. */

#ifndef YHELLO_EMBEDDED_ASSETS_H
#define YHELLO_EMBEDDED_ASSETS_H

/* Extract every embedded asset under "data/" into <data_dir>/.
 * Idempotent: a per-build version marker at
 * <data_dir>/.yhello-assets/version short-circuits subsequent runs.
 *
 * data_dir is yetty_yplatform_get_data_dir(); pass it through so we
 * don't pull the paths header into the unit-test surface. Returns 0 on
 * success, non-zero on failure (already logged via ytrace). */
int yhello_embedded_assets_extract(const char *data_dir);

#endif
