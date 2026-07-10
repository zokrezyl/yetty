/* Phase 1b bridge support (epic #33, issue #36).
 *
 * The generated yos_bridge.c calls a handful of tracing/crash-dump helpers
 * that the desktop host defines in main.c: yos_brg_last_call (a data symbol),
 * yos_brg_record, yos_brg_record_args, yos_brg_retstr, yos_brg_strarg. The
 * browser host does not compile main.c (it has its own tiny entry point), so
 * it provides minimal definitions here.
 *
 * These are pure instrumentation — they record the last bridge call for crash
 * dumps and format return values for ytrace. Trace output is gated off by
 * default (YTRACE_DEFAULT_ON), so no-op / empty-string versions are correct;
 * behaviour of the actual libc bridges is unaffected.
 */

#include <stdint.h>
#include <stdio.h>

#include "yos/types.h"

/* Name of the most recent bridge entered — read by crash dumps. */
const char *yos_brg_last_call = "<none>";

void yos_brg_record(const char *name)
{
	yos_brg_last_call = name;
}

void yos_brg_record_args(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* Return-value formatter for ytrace lines. Shows the numeric return + errno so
 * converged-host traces are useful (the desktop main.c version is richer). */
const char *yos_brg_retstr(long long ret, int host_errno)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%lld (errno=%d)", ret, host_errno);
	return buf;
}

/* String-argument formatter for ytrace lines. */
const char *yos_brg_strarg(struct yos_exec_ctx *ctx, uint32_t off)
{
	(void)ctx;
	(void)off;
	return "";
}
