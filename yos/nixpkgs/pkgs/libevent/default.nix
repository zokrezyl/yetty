{ buildRecipe }:

# libevent 2.1.12 — event-notification library, tmux's core dependency.
# Cross-compiled to wasm32 against the yos sysroot. Static only; the
# select(2)/poll(2) backends are kept (yos bridges those), epoll/kqueue/
# evport/devpoll/eventfd/signalfd are disabled (Linux/BSD-specific).
buildRecipe {
  pname = "libevent";
  version = "2.1.12-stable";
  url = "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz";
  sha256 = "92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb";
}
