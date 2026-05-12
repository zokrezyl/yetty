# Login-shell init. Bash reads this for login shells (telnetd → /bin/login
# -f yetty → bash -l) and does NOT source ~/.bashrc by default — that's a
# login-shell vs interactive-shell distinction. The auto-launch lives here
# so it fires exactly once per session: when the yetty user lands in the
# guest. Subshells the user spawns later (typing `bash` after ygreeter
# exits) are interactive non-login → read .bashrc → do NOT re-fire.
#
# Source .bashrc too so any future per-prompt config (PS1, aliases, …)
# in .bashrc applies to the login shell after ygreeter returns.
if [ -f ~/.bashrc ]; then
    . ~/.bashrc
fi

if [ -z "${YGREETER_SKIP:-}" ] && [ -x /opt/yetty/yetty/bin/ygreeter ]; then
    /opt/yetty/yetty/bin/ygreeter
fi
