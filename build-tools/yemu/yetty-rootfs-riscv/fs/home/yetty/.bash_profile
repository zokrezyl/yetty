# Login-shell init for the yetty user inside the unified yetty-rootfs-riscv
# image. Layered on top of the alpine-extended-disk pristine by the
# yetty-rootfs-riscv merge step (build-tools/yemu/yetty-rootfs-riscv/build.sh);
# overrides the .bash_profile that alpine-extended-disk ships.
#
# Bash reads this for login shells (telnetd → /bin/login -f yetty → bash -l)
# and does NOT source ~/.bashrc by default — that's a login-shell vs
# interactive-shell distinction. The auto-launch lives here so it fires
# exactly once per session: when the yetty user lands in the guest.
# Subshells the user spawns later (typing `bash` after ygreeter exits) are
# interactive non-login → read .bashrc → do NOT re-fire.
#
# Source .bashrc too so any future per-prompt config (PS1, aliases, …)
# in .bashrc applies to the login shell after ygreeter returns.
if [ -f ~/.bashrc ]; then
    . ~/.bashrc
fi

# Yetty tools live under XDG-conventional ~/.local/bin (per-user prefix);
# the same layout the user would get from a packaging step. Each tool
# carries its own assets via incbin and extracts them into
# ~/.local/share/yetty on first run, so the binary is standalone-
# redistributable.
case ":$PATH:" in
    *:"$HOME/.local/bin":*) ;;
    *) PATH="$HOME/.local/bin:$PATH" ;;
esac
export PATH

# Auto-launch ygreeter, but only for the FIRST login of this VM boot. Each
# yetty tab is its own login shell (telnetd → login -f yetty → bash -l), so
# without a guard every new tab would relaunch the greeter on top of the same
# running VM — the user wants it on the first tab only. Gate on a per-boot
# marker whose name carries the kernel boot_id (unique per boot): a fresh VM
# session greets again, while later tabs of the same session fall through to
# a plain shell. `set -C` (noclobber) makes the create atomic so two tabs
# racing at boot still launch only one greeter. The marker lives in /tmp
# (world-writable) because this login runs as the unprivileged yetty user,
# which can't write the root-owned fs root or /run.
YGREETER_MARKER="/tmp/ygreeter-launched.$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
if [ -z "${YGREETER_SKIP:-}" ] && [ -x "$HOME/.local/bin/ygreeter" ] &&
    (set -C; : > "$YGREETER_MARKER") 2>/dev/null; then
    "$HOME/.local/bin/ygreeter"
fi
