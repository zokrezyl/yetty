# yetty terminal defaults — sourced by /etc/profile for every login shell.
#
# yetty has no terminfo entry of its own, so guests see the bare TERM the
# kernel sets (often "linux"), which lacks smcup/rmcup → nvim/less/htop
# can't enter the alternate screen. xterm-256color is the closest stock
# entry that has the full feature set yetty's libvterm implements.
export TERM=xterm-256color
export TERM_PROGRAM=yetty
