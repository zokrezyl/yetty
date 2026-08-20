/*
 * vtdiff-driver.c — emit the ymux projector's #699 VT redraw for stdin input.
 *
 * The differential-harness half that runs the YMUX implementation: read raw
 * application output from stdin, feed it to a real ymux pane/engine/projector at
 * a fixed geometry, and write the projected VT byte stream to stdout. The
 * companion harness (tmux-parity-harness.py) drives pinned tmux with the SAME
 * input and compares the two — both fed into a reference terminal — so the
 * comparison is of complete captured streams, not substring presence.
 *
 * Usage: vtdiff-driver <rows> <cols> < app-output > vt-bytes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>
#include <yetty/ycore/types.h>

/* Module-private capability enum (YMUX_TERM_CAP_*) — the driver must advertise
 * the SAME profile the real ymux attach tool does, or the differential compares
 * the wrong terminal: the tool is a truecolor client, so project_vt must pass
 * RGB through (\e[38;2;R;G;Bm) exactly as tmux does, not downgrade to 256. */
#include "../../../src/yetty/ymux/proto.h"

int main(int argc, char **argv)
{
    uint32_t rows = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 24;
    uint32_t cols = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 80;
    /* Optional 3rd arg: a byte split offset for the INCREMENTAL differential.
     * stdin[0,split) is the base (projected as a FULL redraw whose bytes are
     * DISCARDED — it only establishes the shadow), then stdin[split,end) is fed
     * and the resulting DELTA projection is what we emit. This is how the harness
     * measures the operation-driven delta path (ich/dch/il/dl/csr) against tmux's
     * incremental output. 0 (default) = single full redraw. */
    size_t split = argc > 3 ? (size_t)strtoul(argv[3], NULL, 10) : 0;
    /* Optional 4th arg: the capability profile. "256" = a client without
     * truecolor — the projector must downgrade RGB SGRs exactly as tmux does
     * for a non-RGB terminfo (colour_find_rgb). Default: truecolor. */
    const char *profile = argc > 4 ? argv[4] : "truecolor";
    if (rows == 0 || cols == 0) {
        fprintf(stderr, "vtdiff-driver: bad geometry\n");
        return 2;
    }

    struct yetty_yclass_object *pane = yetty_ymux_pane_make(rows, cols, 16, 0, NULL).value;
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, rows, cols).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    if (!pane || !attachment || !projector) {
        fprintf(stderr, "vtdiff-driver: rig create failed\n");
        return 2;
    }

    /* Advertise the real attach tool's terminal profile (truecolor VT-text), so
     * the projected bytes are those the actual client receives. */
    /* The PRODUCTION identity (proto.h): harness and real attach advertise
     * the same profile, so the oracle never hides a real-attach divergence. */
    uint32_t cap_mask = YMUX_TERM_CAPS_XTERM_256COLOR | YMUX_TERM_CAP_VT_TEXT;
    if (strcmp(profile, "256") == 0) {
        cap_mask &= ~(uint32_t)YMUX_TERM_CAP_TRUECOLOR;
    }
    if (strcmp(profile, "no-ech") == 0) {
        cap_mask &= ~(uint32_t)YMUX_TERM_CAP_ECH;
    }
    if (strcmp(profile, "no-bce") == 0) {
        cap_mask &= ~(uint32_t)YMUX_TERM_CAP_BCE;
    }
    if (strcmp(profile, "no-csr") == 0) {
        cap_mask &= ~(uint32_t)YMUX_TERM_CAP_DECSTBM;
    }
    if (strcmp(profile, "preamble") == 0 || strcmp(profile, "preamble-sync") == 0 ||
        strcmp(profile, "preamble-margins") == 0) {
        /* The attach-time emission comparator: production caps, and the
         * projector emits its attach preamble before the first redraw. */
        cap_mask |= YMUX_TERM_CAP_ATTACH_PREAMBLE;
    }
    if (strcmp(profile, "preamble-sync") == 0) {
        /* Sync-capable client: the full-redraw preamble must wrap in
         * synchronized-output markers (?2026h ... ?2026l), matching tmux. */
        cap_mask |= YMUX_TERM_CAP_SYNC;
    }
    if (strcmp(profile, "preamble-margins") == 0) {
        /* Margin-capable client: the preamble enables DECLRMM (?69h) and
         * establishes full-screen left/right margins, matching tmux. */
        cap_mask |= YMUX_TERM_CAP_MARGINS;
    }
    if (strcmp(profile, "exotic") == 0) {
        cap_mask |= YMUX_TERM_CAP_EXTENDED_UNDERLINE | YMUX_TERM_CAP_UNDERLINE_COLOUR |
                    YMUX_TERM_CAP_HYPERLINK;
    }
    struct yetty_ycore_void_result caps =
        yetty_ymux_projector_set_capabilities(projector, cap_mask);
    if (YETTY_IS_ERR(caps)) {
        yetty_ycore_error_destroy(caps.error);
        fprintf(stderr, "vtdiff-driver: set_capabilities failed\n");
        return 2;
    }
    /* tmux terminfo/features STATE-MODEL profile (review #17 item 8):
     * "term:<name>[:<features>]" resolves the capability set through the
     * ported tty-features pipeline instead of a hand-set bitmask — the
     * comparator then proves resolution equals tmux for that TERM. */
    if (strncmp(profile, "term:", 5) == 0) {
        char term_spec[128];
        snprintf(term_spec, sizeof(term_spec), "%s", profile + 5);
        char *features_spec = strchr(term_spec, ':');
        if (features_spec) {
            *features_spec = 0;
            ++features_spec;
        }
        struct yetty_ycore_void_result term_res =
            yetty_ymux_projector_set_terminal(projector, term_spec, features_spec);
        if (YETTY_IS_ERR(term_res)) {
            yetty_ycore_error_destroy(term_res.error);
            fprintf(stderr, "vtdiff-driver: set_terminal failed\n");
            return 2;
        }
    }

    /* Preamble mode mirrors the oracle's ORDER: the attach (preamble +
     * blank redraw, consuming the pane-init ops) projects BEFORE any input
     * feeds — the fed bytes then emit as ordinary deltas. */
    if (strcmp(profile, "preamble") == 0 || strcmp(profile, "preamble-sync") == 0 ||
        strcmp(profile, "preamble-margins") == 0) {
        struct yetty_ycore_buffer attach_out = yetty_ycore_buffer_create(65536).value;
        struct yetty_ycore_void_result attach_res =
            yetty_ymux_projector_project_vt(projector, &attach_out);
        if (YETTY_IS_ERR(attach_res)) {
            yetty_ycore_error_destroy(attach_res.error);
            fprintf(stderr, "vtdiff-driver: attach projection failed\n");
            return 2;
        }
        fwrite(attach_out.data, 1, attach_out.size, stdout);
        yetty_ycore_buffer_destroy(&attach_out);
    }

    /* Slurp all of stdin so a split can be applied. */
    size_t input_cap = 1u << 16, input_len = 0;
    char *input = malloc(input_cap);
    if (!input) {
        fprintf(stderr, "vtdiff-driver: alloc failed\n");
        return 2;
    }
    char chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (input_len + got > input_cap) {
            input_cap = (input_len + got) * 2;
            char *grown = realloc(input, input_cap);
            if (!grown) {
                free(input);
                fprintf(stderr, "vtdiff-driver: alloc failed\n");
                return 2;
            }
            input = grown;
        }
        memcpy(input + input_len, chunk, got);
        input_len += got;
    }
    if (split > input_len) {
        split = input_len;
    }

    /* Base (feed [0,split)) then a DISCARDED full projection to establish the
     * shadow, so the second projection is a genuine delta. split==0 skips this. */
    if (split > 0) {
        struct yetty_ycore_void_result feed = yetty_ymux_pane_feed(pane, input, split);
        if (YETTY_IS_ERR(feed)) {
            yetty_ycore_error_destroy(feed.error);
            fprintf(stderr, "vtdiff-driver: base feed failed\n");
            return 2;
        }
        struct yetty_ycore_buffer discard = yetty_ycore_buffer_create(1u << 20).value;
        struct yetty_ycore_void_result base = yetty_ymux_projector_project_vt(projector, &discard);
        if (YETTY_IS_ERR(base)) {
            yetty_ycore_error_destroy(base.error);
        }
        yetty_ycore_buffer_destroy(&discard);
    }

    /* Feed the remainder (or everything, if split==0). */
    struct yetty_ycore_void_result feed =
        yetty_ymux_pane_feed(pane, input + split, input_len - split);
    if (YETTY_IS_ERR(feed)) {
        yetty_ycore_error_destroy(feed.error);
        fprintf(stderr, "vtdiff-driver: feed failed\n");
        return 2;
    }
    free(input);

    /* Project the VT redraw (full when split==0, else the delta) to stdout. */
    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(1u << 20).value;
    struct yetty_ycore_void_result project = yetty_ymux_projector_project_vt(projector, &out);
    if (YETTY_IS_ERR(project)) {
        yetty_ycore_error_destroy(project.error);
        fprintf(stderr, "vtdiff-driver: project_vt failed\n");
        return 2;
    }
    if (out.size > 0) {
        fwrite(out.data, 1, out.size, stdout);
    }
    fflush(stdout);

    yetty_ycore_buffer_destroy(&out);
    struct yetty_ycore_void_result pd = yetty_ymux_projector_dispose(projector);
    if (YETTY_IS_ERR(pd)) {
        yetty_ycore_error_destroy(pd.error);
    }
    struct yetty_ycore_void_result ad = yetty_ymux_attachment_dispose(attachment);
    if (YETTY_IS_ERR(ad)) {
        yetty_ycore_error_destroy(ad.error);
    }
    struct yetty_ycore_void_result pnd = yetty_ymux_pane_dispose(pane);
    if (YETTY_IS_ERR(pnd)) {
        yetty_ycore_error_destroy(pnd.error);
    }
    return 0;
}
