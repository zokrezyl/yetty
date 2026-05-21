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

if [ -z "${YGREETER_SKIP:-}" ] && [ -x "$HOME/.local/bin/ygreeter" ]; then
    "$HOME/.local/bin/ygreeter"
fi
