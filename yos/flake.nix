{
  description = "YOS — Linux syscall runtime for wasm3, with package builders";

  inputs = {
    # 24.11+ exposes apple-sdk_<N> packages and switches the darwin
    # default deployment target away from the ancient 10.12 we were
    # getting from 24.05 (which hides preadv/pwritev/mknodat etc.).
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        yos = import ./nixpkgs { inherit pkgs; src = self; };
      in {
        # `nix build .#<name>` — derivations.
        # Foundation:
        #   .#yos          host runtime binary (wasm3-based loader, meson)
        #   .#sysroot      wasm32 sysroot built from FreeBSD-i386 headers
        #   .#toolchain    wasm-clang shim + clang-unwrapped + lld + wasm-opt
        # Wasm ports:
        #   .#nvim         neovim 0.10.4 wasm (with all 9 deps)
        #   .#zsh          zsh 5.9 wasm
        #   .#cpython      DISABLED — see nixpkgs/default.nix
        #   .#freebsd-tools FreeBSD-base userland (cat/echo/ls/…)
        # nvim's libraries (each is its own derivation, also exposed):
        #   .#lua .#libuv .#msgpack-c .#unibilium .#libvterm
        #   .#tree-sitter .#lpeg .#lua-mpack .#luv
        # Convenience umbrella + sandbox entry point:
        #   .#all          yos + every wasm port merged into one tree.
        #                  `nix shell .#all` puts every runner on PATH
        #                  (host shell, host PATH alongside).
        #   nix run .#     enters `$out/bin/yos-shell` — a wasm zsh
        #                  under yos with PATH scoped to $out/libexec
        #                  only. Nothing reaches host /usr/bin from
        #                  inside; that's the sandbox boundary.
        packages = {
          default = yos.yos;
        } // builtins.removeAttrs yos [ "buildRecipe" "buildFreebsdTool" ];

        # `nix run .#` enters the wasm-zsh sandbox under yos: pristine
        # PATH (only $out/libexec wasm modules), no host /usr/bin reach.
        # See nixpkgs/default.nix `all.postBuild` for the wrapper details.
        apps.default = {
          type    = "app";
          program = "${yos.all}/bin/yos-shell";
        };

        # Re-export the recipe builder so downstream flakes can `yos.lib.<sys>.buildRecipe { … }`.
        lib = {
          inherit (yos) buildRecipe;
        };

        # `nix develop` — interactive shell with the wasm toolchain wired up.
        # The package builders above use Nix's sandboxed paths; the dev shell
        # is for running `meson compile` and `tools/wasm-pkg.sh` against the
        # in-tree build directly.
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            meson
            ninja
            pkg-config
            # Emscripten for the browser-convergence host (epic #33): the yos
            # C runtime + wasm3 compiled to a browser-targeted wasm module.
            # `make browser-host-phase0` runs its build under this dev shell.
            emscripten
            # Host libarchive for the env.archive_* bridges
            # (src/yos/impl/libc/libarchive.c) + the libc unit test.
            # Without it meson's with_libarchive=auto resolves to off
            # and the archive_* surface is absent from a local build.
            libarchive
            # Need wasm-ld to cross-link wasm32 test binaries. The system's
            # /usr/local/bin/wasm-ld (homebrew) is the one picked up by
            # default and on this machine its lld+llvm versions don't
            # match — every wasm link aborts. Putting nix's lld first on
            # PATH dodges the homebrew skew.
            llvmPackages.lld
          ]
            ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
              # Use a modern Apple SDK so libSystem stub libs expose
              # POSIX-2017 symbols (preadv/pwritev/mknodat ship in 11+).
              pkgs.apple-sdk_13
            ];
          # nix's clang-wrapper auto-injects -fzero-call-used-regs=used-gpr
          # (the "zerocallusedregs" hardening flag), which wasm32-unknown-unknown
          # doesn't support — every wasm cross-compile in tests/ aborts. Disable
          # that hardening at the shell level; it's an x86-only mitigation
          # anyway and the host yos binary doesn't materially benefit.
          hardeningDisable = [ "zerocallusedregs" ];
          # stdenv's setup-hook hardcodes SDKROOT to the 10.12 SDK; need
          # to override AFTER stdenv runs. shellHook fires last.
          shellHook = pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
            export MACOSX_DEPLOYMENT_TARGET=13.0
            export SDKROOT=${pkgs.apple-sdk_13}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
            # Some tools also read these:
            export NIX_LDFLAGS="$NIX_LDFLAGS -L$SDKROOT/usr/lib"
            export NIX_CFLAGS_COMPILE="$NIX_CFLAGS_COMPILE -isysroot $SDKROOT"
            # nix's clang-wrapper on darwin unconditionally injects
            # `-arch x86_64` and `-mmacos-version-min=...`. wasm-ld
            # rejects `-arch`, killing every wasm cross-link. The
            # build-tools/wasm-clang shim runs unwrapped clang with
            # those flags filtered. Pass it the resource-dir that
            # holds the freestanding headers (stdarg.h, stddef.h, …)
            # which the unwrapped clang doesn't ship by itself.
            export YOS_WASM_CLANG=${pkgs.llvmPackages.clang-unwrapped}/bin/clang
            export YOS_WASM_CLANG_RESOURCE_DIR=${pkgs.llvmPackages.clang}/resource-root
          '';
        };
      });
}
