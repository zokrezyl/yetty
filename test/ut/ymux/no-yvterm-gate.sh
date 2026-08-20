#!/bin/sh
# #699 closure gate: the ymux implementation must not touch or link yvterm.
#
# The independent scene-terminal endpoint (libvterm + yscene vtermgrid) is the
# ONLY terminal engine ymux may use. This gate fails the build's test run if:
#   1. any ymux source (src/yetty/ymux, tools/ymux) includes a yvterm header, or
#   2. the built ymux tool binary carries any yetty_yvterm symbol
#      (defined or undefined — either means a link/source dependency crept in).
#
# Env: YMUX_BINARY   = path to the built tools/ymux/ymux
#      YETTY_ROOT    = repository root
set -eu

fail=0

if grep -rn "include.*yvterm" \
    "${YETTY_ROOT}/src/yetty/ymux" \
    "${YETTY_ROOT}/tools/ymux" 2>/dev/null; then
    echo "no-yvterm-gate: FAIL — ymux source includes a yvterm header (above)" >&2
    fail=1
fi

if [ ! -x "${YMUX_BINARY}" ]; then
    echo "no-yvterm-gate: FAIL — ymux binary not found at ${YMUX_BINARY}" >&2
    exit 1
fi

symbols=$(nm "${YMUX_BINARY}" 2>/dev/null | grep -c "yetty_yvterm" || true)
if [ "${symbols}" != "0" ]; then
    echo "no-yvterm-gate: FAIL — ${symbols} yetty_yvterm symbol(s) in ${YMUX_BINARY}:" >&2
    nm "${YMUX_BINARY}" | grep "yetty_yvterm" | head -10 >&2
    fail=1
fi

if [ "${fail}" != "0" ]; then
    exit 1
fi
echo "no-yvterm-gate: PASS — no yvterm includes, no yetty_yvterm symbols"
exit 0
