{ stdenv, lib, fetchurl, toolchain, sysroot }:

# zlib — wasm32 cross-compile. zlib's own configure is a hand-rolled
# shell script that tries to compile-and-run feature probes; under our
# nostdlib/freestanding wasm32 toolchain those probes fail with
# "Compiler error reporting is too harsh" and configure bails.
#
# Skip it entirely. zlib's static library is ~12 .c files — compile
# them directly, llvm-ar them into libz.a, and copy the two headers.
stdenv.mkDerivation rec {
  pname   = "zlib";
  version = "1.3.1";

  src = fetchurl {
    url    = "https://zlib.net/zlib-${version}.tar.gz";
    sha256 = "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23";
  };

  nativeBuildInputs = [ toolchain ];

  dontStrip    = true;
  dontPatchELF = true;
  dontFixup    = true;

  CFLAGS = lib.concatStringsSep " " [
    "-target wasm32-unknown-unknown"
    "-nostdlib" "-nostdinc"
    "--sysroot=${sysroot}"
    "-isystem ${sysroot}/usr/include"
    "-D__i386__=1" "-D__yos__=1" "-D__FreeBSD__=14" "-D_GNU_SOURCE"
    "-O2" "-fno-builtin" "-ffreestanding"
    # zlib's source uses these defines on platforms that have them;
    # we have stdarg / unistd / errno in the sysroot, so just declare
    # the standard set up front and skip configure's probing.
    "-DHAVE_UNISTD_H" "-DHAVE_STDARG_H"
  ];

  # zlib doesn't ship a useful raw Makefile; just compile the .c files
  # ourselves. Listing here so the recipe is self-documenting.
  ZLIB_SRCS = lib.concatStringsSep " " [
    "adler32.c" "compress.c" "crc32.c" "deflate.c" "gzclose.c"
    "gzlib.c" "gzread.c" "gzwrite.c" "infback.c" "inffast.c"
    "inflate.c" "inftrees.c" "trees.c" "uncompr.c" "zutil.c"
  ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    CC=${toolchain}/bin/wasm-clang
    for src in $ZLIB_SRCS; do
      $CC $CFLAGS -c "$src" -o "''${src%.c}.o"
    done
    llvm-ar rcs libz.a $(echo $ZLIB_SRCS | sed 's/\.c/.o/g')
    llvm-ranlib libz.a
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include
    install -m 644 libz.a $out/lib/
    install -m 644 zlib.h zconf.h $out/include/
    cat > $out/manifest.txt <<EOF
    name=zlib
    version=${version}
    prefix=$out
    libs=-lz
    cflags=-I$out/include
    deps=
    EOF
    runHook postInstall
  '';

  meta = with lib; {
    description = "zlib compression library, wasm32 static build for yos";
    homepage    = "https://zlib.net";
    license     = licenses.zlib;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
