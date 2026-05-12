# Interactive non-login shell init. The ygreeter auto-launch is in
# ~/.bash_profile (login shells only) so subshells the user spawns after
# ygreeter exits get a plain prompt instead of relaunching the greeter.
#
# Keep this file empty-ish — anything that has to apply to every shell
# (login + subshells) belongs here; .bash_profile sources this on login.
case $- in
    *i*) ;;
    *) return ;;
esac
