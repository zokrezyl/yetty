{ buildRecipe, libevent, bison }:

# tmux 3.4 — terminal multiplexer, autoconf cross-build to wasm32.
# Depends on our wasm libevent (event loop). The recipe also compiles a
# minimal terminfo/curses stub in-tree (yos has no ncurses), the same
# tactic zsh uses for termcap — enough symbols for tmux to link and run;
# real terminfo rendering is iterative.
buildRecipe {
  pname = "tmux";
  version = "3.4";
  url = "https://github.com/tmux/tmux/releases/download/3.4/tmux-3.4.tar.gz";
  sha256 = "551ab8dea0bf505c0ad6b7bb35ef567cdde0ccb84357df142c254f35a23e19aa";
  deps = [ libevent ];
  # tmux's cmd-parse.y grammar needs a yacc; bison runs on the host.
  extraNativeBuildInputs = [ bison ];
}
