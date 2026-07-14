# Shell dressing for the ephemeral yetty demo container.
# Sourced from /etc/profile.d for the login shell.

# yetty was installed system-wide under /usr/local at image build time. Its
# path resolver (yplatform/paths) locates the product's data and config at
# $XDG_DATA_HOME/yetty and $XDG_CONFIG_HOME/yetty. We must NOT point
# XDG_DATA_HOME at /usr/local/share here: XDG_DATA_HOME is by definition the
# user's *writable* data home, and overriding it would make every other
# XDG-aware program follow us into the read-only rootfs (e.g. nvim tries to
# create /usr/local/share/nvim and fails). So we keep the normal per-user XDG
# defaults and instead bridge the read-only yetty install into the writable
# per-session home with symlinks — the tools find their data, nvim & co. keep
# a writable data home.
export YETTY_DEMOS=/usr/local/share/yetty/demos

xdg_data="${XDG_DATA_HOME:-${HOME}/.local/share}"
xdg_config="${XDG_CONFIG_HOME:-${HOME}/.config}"
mkdir -p "${xdg_data}" "${xdg_config}" 2>/dev/null || true
ln -sfn /usr/local/share/yetty "${xdg_data}/yetty" 2>/dev/null || true
ln -sfn /usr/local/etc/xdg/yetty "${xdg_config}/yetty" 2>/dev/null || true

# Tell the yetty tools they're inside a yetty terminal so they emit rich-content
# envelopes (the browser-side yetty renders them) instead of raw fallbacks.
export TERM_PROGRAM=yetty

# Auto-logout idle sessions (seconds). Belt-and-braces with the outer
# `timeout` in yetty-demo-session.sh.
export TMOUT="${TMOUT:-1800}"

# Nothing persists across sessions anyway.
export HISTFILE=/dev/null

# `demos` — browse the bundled demo gallery.
demos() {
    if [ -n "$1" ]; then
        ls -1 "${YETTY_DEMOS}/scripts/$1" 2>/dev/null && return 0
    fi
    echo "yetty demo gallery — assets in ${YETTY_DEMOS}"
    echo
    echo "  ready-to-run scripts:"
    ls -1 "${YETTY_DEMOS}/scripts" 2>/dev/null | sed 's/^/    /'
    echo
    echo "  run one, e.g.:  bash ${YETTY_DEMOS}/scripts/all.sh"
}

# Brand mint accent (#6BA892 = rgb 107,168,146) for the prompt glyph.
PS1='\[\e[38;2;107;168;146m\]yetty-demo\[\e[0m\]:\w\$ '

# Show the welcome banner once at login.
if [ -f /etc/motd ]; then
    cat /etc/motd
fi
