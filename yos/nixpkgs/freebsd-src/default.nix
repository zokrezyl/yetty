{ stdenv, lib, fetchurl }:

# Layer 1.5 — pure-source derivation for the FreeBSD release tree.
#
# Both the sysroot and any FreeBSD-base tool port (sh, cat, ls, …) need
# the extracted FreeBSD-RELEASE source tree. Extracting the 200+ MiB
# src.txz once into its own /nix/store path lets every consumer share
# the cache instead of re-extracting per derivation.
#
# We extract only the parts our consumers actually read:
#   usr/src/include/         POSIX user-include trees
#   usr/src/sys/             kernel UAPI headers + a handful of UAPI
#                             utility .h files
#   usr/src/lib/             FreeBSD libc sources (Tier-2 candidates)
#   usr/src/bin/             core utilities (cat, ls, sh, …)
#   usr/src/usr.bin/         non-base utilities still in src
#   usr/src/sbin/, usr/src/usr.sbin/  admin/network tools
#   usr/src/contrib/         third-party imports (BSD-licensed)
#
# Output layout under $out:
#   $out/usr/src/include/...
#   $out/usr/src/bin/sh/...
#   …
#
# WARNING: the freebsdVersion / freebsdSha256 below MUST stay in sync
# with meson_options.txt and nixpkgs/yos/default.nix. There is no
# automatic check.

let
  freebsdVersion = "14.4";
  freebsdSrcUrl  = "https://download.freebsd.org/releases/amd64/${freebsdVersion}-RELEASE/src.txz";
  freebsdSha256  = "1b337069331dbf0791dc43129900377da256ba1ec0ef94e0d3f6e6232de84fe2";

  freebsdSrcTarball = fetchurl {
    url    = freebsdSrcUrl;
    sha256 = freebsdSha256;
    name   = "freebsd-src-${freebsdVersion}.txz";
  };
in stdenv.mkDerivation {
  pname = "freebsd-src";
  version = freebsdVersion;

  # We don't use a `src` attr — the tarball is the source.
  dontUnpack = true;
  dontConfigure = true;
  dontPatch = true;
  dontBuild = true;
  dontStrip = true;
  dontPatchELF = true;
  dontFixup = true;

  installPhase = ''
    runHook preInstall

    mkdir -p $out
    # Extract everything under usr/src/ — the consumers pick what they
    # need and ignore the rest. Extracting selectively here would mean
    # re-running this derivation every time we add a new tool.
    tar -xJf ${freebsdSrcTarball} -C $out usr/src

    runHook postInstall
  '';

  meta = with lib; {
    description = "FreeBSD ${freebsdVersion}-RELEASE source tree (extracted, shared)";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };

  passthru = {
    inherit freebsdVersion freebsdSha256 freebsdSrcUrl;
  };
}
