{ pkgs, src }:

# Top of the yos Nix tree. Foundation is layered:
#
#   1. yos        — host runtime binary (the wasm3-based loader, meson
#                   build). Lowest layer; depends on nothing yos-specific.
#   2. sysroot    — wasm32 sysroot built from the FreeBSD i386 headers
#                   (downloaded + extracted by build-tools/freebsd/) plus
#                   the yos-flavoured crt1 from build-tools/sysroot/skel.sh.
#   3. toolchain  — clang-unwrapped + wasm-ld + wasm-opt + the wasm-clang
#                   shim that filters darwin-only flags.
#   4. lib.buildRecipe — generic builder. Drives a recipe under
#                   build-tools/wasm-pkg/configs/<name>/build.sh with the
#                   same env (ROOT/PREFIX/WORK/WASM_CC/WASM_SYSROOT/
#                   WASM_CFLAGS/WASM_LDFLAGS/DEP_PREFIXES) that
#                   tools/wasm-pkg.sh provides.
#   5. wasm packages — nvim and its 9 dep libraries (lua, libuv,
#                   msgpack-c, unibilium, libvterm, tree-sitter, lpeg,
#                   lua-mpack, luv).
#
# Each layer's source is the yos repo itself (`src` arg) — no
# vendoring, no separate fetch. Move/rename a source file in the repo
# and these derivations pick it up.

let
  # Per-derivation src scoping. The whole yos repo is the flake src,
  # but most derivations only read a small subtree under build-tools/.
  # Filter to just what each one needs so edits in src/yos/impl/ don't
  # invalidate the wasm-pkg cache. See ./lib/scope-src.nix.
  scopeSrc = pkgs.callPackage ./lib/scope-src.nix { };

  toolchainSrc = scopeSrc {
    inherit src;
    name = "yos-toolchain-src";
    keep = [ "build-tools/wasm-clang" ];
  };

  sysrootSrc = scopeSrc {
    inherit src;
    name = "yos-sysroot-src";
    keep = [
      "build-tools/freebsd"
      "build-tools/sysroot"
      # sysroot compiles yos_libc_init.c into the sysroot; this file
      # is also read by nvim's recipe.
      "build-tools/wasm-pkg/configs/nvim/yos_libc_init.c"
    ];
  };

  freebsd-src = pkgs.callPackage ./freebsd-src { };
  # Force the yos host build to use clang on every platform. nix's
  # default stdenv on linux is gcc-based, which (a) emits a different
  # diagnostic surface than the darwin/iOS/tvOS slices the same code
  # has to compile under, so a change that builds clean on macOS keeps
  # tripping on linux for trivia clang accepts; (b) makes the auto-
  # generated bridge's pointer-to-int casts a hard error on newer gcc.
  # Pinning clang means every host uses the same compiler frontend.
  yos = pkgs.callPackage ./yos {
    inherit src;
    stdenv = if pkgs.stdenv.isLinux
             then pkgs.llvmPackages_18.stdenv
             else pkgs.stdenv;
  };
  sysroot     = pkgs.callPackage ./sysroot    { inherit freebsd-src; src = sysrootSrc; };
  toolchain   = pkgs.callPackage ./toolchain  { src = toolchainSrc; };

  buildRecipe = pkgs.callPackage ./lib/build-recipe.nix {
    inherit toolchain sysroot src;
    # Don't bake yos's store path into the wasm-pkg runner scripts —
    # doing so would re-introduce a full umbrella rebuild every time
    # any yos host source changes (since the runner string is part of
    # the derivation hash). The runner falls back to `yos` on PATH; the
    # umbrella .#all derivation supplies an absolute-path runner for
    # `nix run .#` consumers via its own wrapper.
    yos = null;
  };

  buildFreebsdTool = pkgs.callPackage ./lib/build-freebsd-tool.nix {
    inherit toolchain sysroot freebsd-src;
    # yos = yos;   # uncomment once .#yos builds reliably; for now
    # the runner script falls back to `yos` on PATH so the tool
    # ports can land independently of the host derivation work.
    yos = null;
  };

  # Leaf wasm libraries (no inter-package deps).
  lua          = pkgs.callPackage ./pkgs/lua          { inherit buildRecipe; };
  libuv        = pkgs.callPackage ./pkgs/libuv        { inherit buildRecipe; };
  msgpack-c    = pkgs.callPackage ./pkgs/msgpack-c    { inherit buildRecipe; };
  unibilium    = pkgs.callPackage ./pkgs/unibilium    { inherit buildRecipe; };
  tree-sitter  = pkgs.callPackage ./pkgs/tree-sitter  { inherit buildRecipe; };
  libvterm     = pkgs.callPackage ./pkgs/libvterm     { inherit buildRecipe; };
  libevent     = pkgs.callPackage ./pkgs/libevent     { inherit buildRecipe; };

  # Lua-side bindings (depend on lua / libuv).
  lpeg         = pkgs.callPackage ./pkgs/lpeg         { inherit buildRecipe lua; };
  lua-mpack    = pkgs.callPackage ./pkgs/lua-mpack    { inherit buildRecipe lua; };
  luv          = pkgs.callPackage ./pkgs/luv          { inherit buildRecipe lua libuv; };

  # The big consumer. callPackage's auto-resolution would otherwise pull
  # in nixpkgs's host-side `lua`/`libuv`/etc.; pass ours explicitly.
  nvim         = pkgs.callPackage ./pkgs/nvim {
    inherit buildRecipe sysroot
            lua libuv msgpack-c unibilium libvterm
            tree-sitter lpeg lua-mpack luv;
  };

  # FreeBSD-base userland — single derivation that compiles the whole
  # toolset (cat, echo, ls, sh, …) from the shared FreeBSD source tree
  # against our sysroot. Output: $out/bin/<name> + $out/libexec/<name>
  # for each tool. See pkgs/freebsd-tools/default.nix to add one.
  freebsd-tools = pkgs.callPackage ./pkgs/freebsd-tools {
    inherit toolchain sysroot freebsd-src;
    # Same reason as buildRecipe above — don't bake the yos store path
    # into runner scripts, or every yos-host edit invalidates this
    # derivation. Runner falls back to `yos` on PATH; the .#all umbrella
    # supplies an absolute-path runner for `nix run .#` consumers.
    yos = null;
  };

  # Upstream-tarball ports (autoconf, cmake, …) — wasm32 cross-builds
  # via the standard buildRecipe pattern. Each carries its own
  # build-tools/wasm-pkg/configs/<name>/build.sh recipe.
  zsh = pkgs.callPackage ./pkgs/zsh { inherit buildRecipe; };

  # tmux — terminal multiplexer. autoconf cross-build to wasm32; depends
  # on our wasm libevent (event loop) and a minimal terminfo/curses stub
  # the recipe compiles in (same approach zsh uses for termcap). Output:
  # $out/bin/tmux.wasm.
  tmux = pkgs.callPackage ./pkgs/tmux { inherit buildRecipe libevent; };

  # CPython 3.12 wasm — DISABLED. The compile-to-wasm path has expat
  # FASTCALL collisions vs our -D__i386__=1 (build-tools/wasm-pkg/
  # configs/cpython/build.sh) that we never finished smashing. We
  # abandoned this route in favour of linking host libpython into the
  # yos binary and bridging Py_* (impl/libpython.c) — much smaller,
  # native speed, no cross-compile pain, no per-platform yos surprises.
  # Re-enable here when the libpython-in-host approach is proven to
  # NOT need the standalone wasm interpreter as a fallback.
  # cpython = pkgs.callPackage ./pkgs/cpython { inherit buildRecipe; };

  # Network stack: zlib → openssl → openssh. Static-linked,
  # cross-compiled to wasm32 against the FreeBSD sysroot. Built as
  # plain stdenv.mkDerivation recipes (no shared shell script): each
  # package declares its own build-time tooling (perl for openssl,
  # python3 for openssh's config.sub patch), and the wasm toolchain
  # + sysroot are passed in explicitly.
  zlib    = pkgs.callPackage ./pkgs/zlib    { inherit toolchain sysroot; };
  openssl = pkgs.callPackage ./pkgs/openssl { inherit toolchain sysroot zlib; };
  openssh = pkgs.callPackage ./pkgs/openssh { inherit toolchain sysroot zlib openssl; };

  # In-tree stress test compiled to wasm and shipped as a runnable
  # tool inside the umbrella ($out/libexec/perf-stress). Lets users
  # invoke it from within `./tools/yos.sh`:
  #
  #     nixem% perf-stress &           # background
  #     nixem% wait                    # observe stdout/stderr
  #
  # The source narrows to just tests/ut/libc/test_perf_stress.c +
  # whatever the wasm toolchain needs; edits elsewhere in src/yos/
  # don't invalidate this derivation's cache.
  perf-stress-src = scopeSrc {
    inherit src;
    name = "yos-perf-stress-src";
    keep = [ "tests/ut/libc/test_perf_stress.c" ];
  };
  perf-stress = pkgs.callPackage ./pkgs/perf-stress {
    inherit toolchain sysroot;
    src = perf-stress-src;
  };

  # Gerrit Pape's runit, wasm32 port: runsv / runsvdir / sv / svlogd /
  # runsvchdir. Foundation for the "real multi-process test rig with
  # telnet sessions" workflow — drop a service dir under /tmp/run/<svc>
  # inside the yos sandbox, point `runsvdir /tmp/run` at it from the
  # zsh prompt, and let the supervisor restart-on-exit / signal-route
  # / log-fd-setup any wasm program you point its run script at.
  runit = pkgs.callPackage ./pkgs/runit { inherit toolchain sysroot; };

  # BSD telnetd from FreeBSD-13.4 libexec/, wasm32 port. NO auth /
  # encryption — strictly a test-rig server for piping a connecting
  # telnet client into a wasm program (e.g. zsh) under runit.
  telnetd = pkgs.callPackage ./pkgs/telnetd { inherit toolchain sysroot; };

  # fzy — the fuzzy finder: C port on the native yos ABI, fully
  # interactive on both runtimes. (fzf was evaluated and dropped: it is
  # Go, whose only wasm target is wasip1 — an ABI with no process
  # model, no termios, and nothing in yos that executes it. Not worth
  # a second import dialect for a gutted picker; revisit only if
  # genuinely-needed wasip1-only tools pile up.)
  fzy = pkgs.callPackage ./pkgs/fzy { inherit toolchain sysroot freebsd-src; };

  # Minimal TCP super-server. Each accept forks a wasm child + execve's
  # the configured program with the connection on stdin/stdout/stderr.
  # Pair with /libexec/zsh for "telnet → fresh shell per session"; pair
  # with /libexec/telnetd for the real telnet protocol (telnetd handles
  # PTY, IAC, login, etc.). Bypasses runit's cwd-race blocker entirely.
  yos-tcpserver = pkgs.callPackage ./pkgs/yos-tcpserver { inherit toolchain sysroot; };

  # ytrace — guest-side strace-style libc-surface tracer. Tiny wasm
  # program that sets YTRACE_DEFAULT_ON=yes in the env and execvps the
  # rest of argv. Installed as $out/libexec/ytrace so the umbrella's
  # PATH (the yos-shell sandbox) picks it up.
  ytrace-wasm = pkgs.callPackage ./pkgs/ytrace-wasm { inherit toolchain sysroot; };

  # yperf — guest-side per-app wrapper around the host's wasm-function
  # profile recorder. fork+exec+wait+stop pattern; needs asyncify
  # (see yos-tcpserver for the same rationale).
  yperf-wasm = pkgs.callPackage ./pkgs/yperf-wasm { inherit toolchain sysroot; };

  # yctl — client for the runtime introspection + control daemon. Same
  # .c builds twice: wasm32 guest binary in libexec/ (yctl-wasm) and a
  # host binary in bin/ (yctl-host). Both connect to the AF_UNIX socket
  # the yos host opens when launched with `--yctl-socket PATH`.
  yctl-wasm = pkgs.callPackage ./pkgs/yctl {
    inherit toolchain sysroot;
    msgpack-c = msgpack-c;   # wasm32 msgpack-c built by our buildRecipe
  };
  yctl-host = pkgs.callPackage ./pkgs/yctl-host {
    msgpack-c = pkgs.msgpack-c;
    yctlSrc   = ./pkgs/yctl;
  };

  # Umbrella package: every user-facing yos artefact merged into one
  # tree via symlinkJoin. Lets users do
  #   nix run .#                    # drops into wasm zsh under yos (sandbox)
  #   nix shell .#all              # host shell with yos's bin/ on PATH
  #   nix build .#all              # one $out with yos + zsh + nvim + tools
  # without listing individual derivations. New top-level packages
  # should be added to `paths` below to keep this a single landing zone.
  #
  # `bin/yos-shell` is the security-relevant entry point: it strips the
  # host's environment via `env -i`, sets PATH to `$out/libexec/` (raw
  # wasm modules — yos's exec bridge can load these directly), and execs
  # yos→zsh.wasm. From inside that shell every command resolves through
  # yos's wasm process model; nothing reaches host /usr/bin. That's the
  # sandbox boundary.
  all = pkgs.symlinkJoin {
    name = "yos-all";
    paths = [ yos zsh tmux nvim freebsd-tools openssh perf-stress runit telnetd fzy yos-tcpserver ytrace-wasm yperf-wasm yctl-wasm yctl-host ];  # cpython disabled — see above
    postBuild = ''
      # `sh` alias for zsh: guests exec "/bin/sh" constantly (tmux's
      # default-shell, $SHELL fallbacks, scripts). yos's execve resolves
      # a non-wasm absolute path by BASENAME on the guest $PATH, so this
      # symlink makes /bin/sh land on the wasm zsh — which sees
      # basename(argv[0]) == "sh" and enters sh-emulation.
      ln -sfn zsh $out/libexec/sh
'' + ''
      cat > $out/bin/yos-shell <<RUNNER_EOF
      #!/usr/bin/env bash
      # yos-shell — pristine wasm-zsh sandbox under yos.
      # PATH points at the umbrella's libexec/ where the bare .wasm
      # modules live. The runner shims in bin/ are host-bash scripts;
      # we don't put them on PATH because yos can't load shebang-bash
      # from inside the wasm guest. Keeping libexec on PATH means
      # lookups inside the wasm guest resolve to wasm modules yos can
      # exec directly. Anything not under $out/libexec is unreachable
      # from inside this shell — that's the sandbox boundary.
      # No args → interactive prompt. Args → forwarded to zsh, so
      # \`nix run .# -- -c 'echo hi'\` works as a one-shot.
      if [ "\$#" -eq 0 ]; then
          set -- -i
      fi
      exec env -i \\
          HOME="\$HOME" \\
          USER="\''${USER:-yos}" \\
          TERM="\''${TERM:-xterm-256color}" \\
          PATH="$out/libexec" \\
          "$out/bin/yos" "$out/libexec/zsh" "\$@"
      RUNNER_EOF
      chmod +x $out/bin/yos-shell

      # ytrace — strace-style libc-surface tracer.
      # Front for the per-call ytrace points emitted by every m3w_<name>
      # bridge wrapper. Flips YTRACE_DEFAULT_ON=yes so every trace point
      # fires from the first instruction; optionally redirects to a
      # file (-o) using the ytrace per-thread file-prefix routing.
      #
      # Usage mirrors strace:
      #   ytrace <wasm-prog> [args...]
      #   ytrace -o /tmp/trace <wasm-prog>     # writes /tmp/trace-<comm>-<tid>
      #   ytrace -e zsh                         # shorthand: trace a libexec/<name>
      #
      # Bare names (no leading /) are looked up in $out/libexec, so
      # \`ytrace zsh -c true\` runs the umbrella's wasm zsh under trace.
      cat > $out/bin/ytrace <<RUNNER_EOF
      #!/usr/bin/env bash
      out_prefix=""
      while [ "\$#" -gt 0 ]; do
        case "\$1" in
          -o) out_prefix="\$2"; shift 2 ;;
          -h|--help)
            echo "ytrace — strace-style libc-surface trace for wasm under yos."
            echo "Usage: ytrace [-o file] <wasm-prog|libexec-name> [args...]"
            echo "  -o file   write per-thread trace to <file>-<comm>-<tid>"
            echo "  bare name resolves under $out/libexec/"
            exit 0 ;;
          --) shift; break ;;
          -*) echo "ytrace: unknown flag \$1" >&2; exit 2 ;;
          *)  break ;;
        esac
      done
      if [ "\$#" -eq 0 ]; then
        echo "ytrace: missing program" >&2; exit 2
      fi
      prog="\$1"; shift
      case "\$prog" in
        /*|./*|../*) ;;                                  # absolute / relative — pass through
        *) [ -f "$out/libexec/\$prog" ] && prog="$out/libexec/\$prog" ;;
      esac
      env_args=( YTRACE_DEFAULT_ON=yes )
      [ -n "\$out_prefix" ] && env_args+=( YTRACE_FILE_PREFIX="\$out_prefix" )
      exec env "\''${env_args[@]}" "$out/bin/yos" "\$prog" "\$@"
      RUNNER_EOF
      chmod +x $out/bin/ytrace
    '';
    meta = with pkgs.lib; {
      description = "yos host runtime + every wasm32 port (zsh, nvim, freebsd-tools) merged into one bin/libexec tree, plus yos-shell sandbox wrapper";
      platforms   = platforms.linux ++ platforms.darwin;
    };
  };
in {
  inherit yos sysroot toolchain freebsd-src buildRecipe buildFreebsdTool
          lua libuv msgpack-c unibilium tree-sitter libvterm libevent
          lpeg lua-mpack luv nvim
          freebsd-tools
          zsh tmux  # cpython disabled — see above
          zlib openssl openssh
          perf-stress
          runit
          telnetd
          fzy
          yos-tcpserver
          ytrace-wasm
          yperf-wasm
          yctl-wasm
          yctl-host
          all;
}
