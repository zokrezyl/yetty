{ buildRecipe }:

# CPython 3.12 — the reference Python interpreter, autoconf-driven
# port. Cross-compiled to wasm32 with most external-library extensions
# disabled (no ssl/sqlite/zlib/ctypes/curses/tkinter). What's left:
# the Python core + the pure-Python stdlib + the C extensions that
# don't need third-party libs (math, os, posix, sys, time, …).
buildRecipe {
  pname = "cpython";
  version = "3.12.7";
  url = "https://www.python.org/ftp/python/3.12.7/Python-3.12.7.tar.xz";
  sha256 = "24887b92e2afd4a2ac602419ad4b596372f67ac9b077190f459aba390faf5550";
}
