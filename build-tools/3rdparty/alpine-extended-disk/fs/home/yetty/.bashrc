# Auto-launch ygreeter as the first thing the yetty user sees.
# Runs (not exec) so pressing 'q' inside ygreeter returns to a normal
# bash prompt instead of disconnecting the session.
#
# Guard rails:
#   - interactive shells only ($PS1 set, $- contains 'i') — keeps scp,
#     non-login subshells, and the post-install bash invocations clean
#   - skip when YGREETER_SKIP=1 — escape hatch for debugging
#   - skip when the binary isn't there yet (image still building, or the
#     yetty-tools-riscv drive isn't attached) — drop straight to a shell
case $- in
    *i*) ;;
    *) return ;;
esac

if [ -z "${YGREETER_SKIP:-}" ] && [ -x /opt/yetty/yetty/bin/ygreeter ]; then
    /opt/yetty/yetty/bin/ygreeter
fi
