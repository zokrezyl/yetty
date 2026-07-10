{ stdenv, lib, binaryen, bison, buildPackages, toolchain, sysroot, freebsd-src
, yos ? null   # see runner-script note below
}:

# yos-freebsd-tools — FreeBSD-base userland ported to wasm32, built
# in ONE Nix derivation against the shared FreeBSD source tree +
# yos sysroot + wasm toolchain.
#
# Why one derivation instead of one-per-tool?
#   - All tools share inputs (freebsd-src, sysroot, toolchain).
#     Per-tool derivations meant 17× setup overhead for every
#     evaluation + 17 separate /nix/store paths to manage.
#   - The user-facing artifact is naturally a single `bin/` directory
#     with N executables — exactly what one $out gives us.
#   - Adding a new tool now = one entry in the `tools` attrset below,
#     no new file, no flake.nix wiring.
#
# Output layout:
#   $out/bin/<name>          shell runner (`exec yos $out/libexec/<name> "$@"`)
#   $out/libexec/<name>      wasm module (no suffix)
#   $out/manifest.txt        per-tool metadata (pname/srcDir/srcs)
#
# To run a tool:
#     result/bin/cat /etc/hostname            # via the runner
#     yos $(nix path-info .#freebsd-tools)/libexec/cat /etc/hostname
#
# Adding a tool:
#   1. Find its sources under build-tools/freebsd/src/usr/src/{bin,usr.bin}/
#   2. Add an entry to `tools` below: pname → { srcDir, srcs?, libcExtras?,
#      extraSrcDirs?, stageDirs?, extraCflags? }.
#   3. `nix build .#freebsd-tools` — the single derivation rebuilds and
#      adds your tool to its $out/bin and $out/libexec. No flake change.
#
# Schema notes (per-tool fields):
#   srcDir       — path under usr/src/ that holds the tool's sources    (required)
#   srcs         — .c basenames; default ["<pname>.c"]
#   libcExtras   — libc helper sources from libcExtraTable; default
#                  defaultLibcExtras (getopt/getopt_long/basename/strsignal)
#   extraSrcDirs — additional usr/src/<dir> paths searched for srcs
#                  (FreeBSD .PATH semantics)
#   stageDirs    — additional `$STAGE/<dir>` paths searched for srcs;
#                  used for files generated at build time (bison output,
#                  maketab output, libxo shim) staged before build_tool
#                  runs
#   extraCflags  — tool-specific -D / -I; may reference $STAGE
#
# ps, top: FreeBSD's versions need libkvm and a kernel-style proc
#   table; yos has a host-side proc table exposed through
#   sysctl(KERN_PROC_PROC) instead. Both ship as small yos-native
#   reimplementations (yos_ps.c / yos_top.c, staged in buildPhase)
#   that read that snapshot directly — see their entries in `tools`.
#
# Ports that are deliberately NOT in this derivation:
#   tar          — needs libarchive (~500 source files); belongs in its
#                  own derivation alongside libarchive itself.
#   gzip         — needs libz, liblzma, libzstd (and optionally libbz2);
#                  each is its own wasm port + derivation.

let
  # ── libc helpers ─────────────────────────────────────────────────────
  # Yos doesn't bridge every libc fn at the host surface; the simplest
  # answer for fns that are pure wasm-side (no kernel call) is to
  # compile FreeBSD's own libc source straight in. Tools opt in via
  # `libcExtras = [ "<name>" … ]` on their entry in `tools`.
  libcExtraTable = {
    getopt       = { sub = "lib/libc/stdlib"; file = "getopt.c"; };
    getopt_long  = { sub = "lib/libc/stdlib"; file = "getopt_long.c"; };
    err          = { sub = "lib/libc/gen";    file = "err.c"; };
    basename     = { sub = "lib/libc/gen";    file = "basename.c"; };
    strsignal    = { sub = "lib/libc/string"; file = "strsignal.c"; };
    # fts(3) — directory-tree walker used by ls, cp, find, du, rm -r,
    # chmod -R, etc. Pure userspace traversal sitting on top of
    # opendir/readdir/closedir/fstatfs/fchdir/lstat — all already
    # bridged in yos. Pulled verbatim from FreeBSD libc; the FreeBSD-
    # internal headers it needs (namespace.h, un-namespace.h,
    # gen-private.h, libc_private.h) come from -I lib/libc/include
    # and lib/libc/gen (added in libcCflags below).
    #
    # fts.c references the underscored symbols _open/_close/_fstat/
    # _fstatfs and __opendir2 — these are FreeBSD libc-internal aliases
    # for the public POSIX calls. The sysroot's libyos_stubs.a provides
    # thin wrappers so they resolve at link time.
    fts          = { sub = "lib/libc/gen";    file = "fts.c"; };
    # qsort(3) — fts(3) uses it to sort directory entries when the
    # user passes a comparator to fts_open(). Compiling FreeBSD's
    # qsort.c into the tool means the comparator callback stays a
    # wasm function pointer (no cross-runtime call) and we get
    # FreeBSD-faithful ordering semantics.
    qsort        = { sub = "lib/libc/stdlib"; file = "qsort.c"; };
    # reallocf(3) — realloc that frees the old pointer on failure.
    # FreeBSD-specific; pulled in for tools that link fts (fts.c uses
    # it during path-buffer growth).
    reallocf     = { sub = "lib/libc/stdlib"; file = "reallocf.c"; };
  };

  # Default libc helpers every tool gets unless it opts out by setting
  # `libcExtras = []` (e.g. `yes`, `sync` — no flag parsing, no signals).
  defaultLibcExtras = [ "getopt" "getopt_long" "basename" "strsignal" ];

  # Tools that touch ctype (isalpha, isalnum, tolower, …) or call
  # setlocale(LC_CTYPE, …) — i.e. anything that processes text — need
  # _CurrentRuneLocale set to a rune table. Without it every is*()
  # returns 0: awk reads only the first byte of any keyword, tr never
  # matches a-z, etc. We can't compile FreeBSD's lib/libc/locale/table.c
  # straight to wasm32 — it pulls in machine/atomic.h with x86 asm.
  # The `yos_locale_stub.c` in stage-dir gives us a static C-locale
  # rune table (ASCII flags + lower/upper maps) and the global decls.
  ctypeLibcExtras = defaultLibcExtras;
  # The locale stub is staged in $STAGE/locale-stub by buildPhase.

  # ── tool definitions ─────────────────────────────────────────────────
  # Each entry: pname → {
  #   srcDir       : path under usr/src/                        (required)
  #   srcs         : .c basenames in srcDir                     (default: ["<pname>.c"])
  #   libcExtras   : libc helpers from libcExtraTable           (default: defaultLibcExtras)
  #   extraSrcDirs : extra paths to search for srcs (FreeBSD .PATH style)
  #   extraCflags  : tool-specific -D / -I
  # }
  tools = {
    # ── FreeBSD bin/ ─────────────────────────────────────────────────
    # sh     = { srcDir = "bin/sh";     };
    #ps     = { srcDir = "bin/ps";     };
    cat      = { srcDir = "bin/cat";      libcExtras = [ "getopt" ]; extraCflags = [ "-DBOOTSTRAP_CAT" ]; };
    cp       = { srcDir = "bin/cp";       srcs = [ "cp.c" "utils.c" ];
                 libcExtras  = defaultLibcExtras ++ [ "fts" "qsort" "reallocf" ];
                 extraCflags = [ "-D_ACL_PRIVATE" ]; };
    chmod    = { srcDir = "bin/chmod";    };
    date     = { srcDir = "bin/date";     srcs = [ "date.c" "vary.c" ]; };
    dd       = { srcDir = "bin/dd";       srcs = [ "args.c" "conv.c" "conv_tab.c" "dd.c" "misc.c" "position.c" ]; };
    echo     = { srcDir = "bin/echo";     libcExtras = []; };
    domainname = { srcDir = "bin/domainname"; };
    hostname = { srcDir = "bin/hostname"; };
    kill     = { srcDir = "bin/kill";     };
    ln       = { srcDir = "bin/ln";       };
    ls       = { srcDir = "bin/ls";       srcs = [ "cmp.c" "ls.c" "print.c" "util.c" "yos_locale_stub.c" ];
                 libcExtras = defaultLibcExtras ++ [ "fts" "qsort" "reallocf" ];
                 # print.c filters every output byte through isprint().
                 # Without the locale stub, _CurrentRuneLocale is NULL
                 # so isprint() returns 0 for every byte and ls (in
                 # default `-q` mode under a TTY) substitutes '?' for
                 # every character of every filename.
                 stageDirs = [ "locale-stub" ]; };
    mkdir    = { srcDir = "bin/mkdir";    };
    mv       = { srcDir = "bin/mv";       };
    pwd      = { srcDir = "bin/pwd";      libcExtras = [ "getopt" ]; };
    realpath = { srcDir = "bin/realpath"; };
    rm       = { srcDir = "bin/rm";       };
    rmdir    = { srcDir = "bin/rmdir";    };
    sleep    = { srcDir = "bin/sleep";    };
    stty     = { srcDir = "bin/stty";     srcs = [ "cchar.c" "gfmt.c" "key.c" "modes.c" "print.c" "stty.c" "util.c" ]; };
    sync     = { srcDir = "bin/sync";     libcExtras = []; };
    test     = { srcDir = "bin/test";     };
    timeout  = { srcDir = "bin/timeout";  };
    uuidgen  = { srcDir = "bin/uuidgen";  extraCflags = [ "-include" "errno.h" ]; };

    # ── FreeBSD usr.bin/ ─────────────────────────────────────────────
    "true"   = { srcDir = "usr.bin/true";  libcExtras = []; };
    "false"  = { srcDir = "usr.bin/false"; libcExtras = []; };
    basename = { srcDir = "usr.bin/basename"; };
    dirname  = { srcDir = "usr.bin/dirname";  };
    head     = { srcDir = "usr.bin/head";     };
    uniq     = { srcDir = "usr.bin/uniq";     };
    yes      = { srcDir = "usr.bin/yes";      libcExtras = []; };
    id       = { srcDir = "usr.bin/id";       };
    mktemp   = { srcDir = "usr.bin/mktemp";   };
    touch    = { srcDir = "usr.bin/touch";    };

    # ── second batch: pure libc, multi-source ────────────────────────
    # All of these process text and so include yos_locale_stub.c, which
    # provides _CurrentRuneLocale + __mb_sb_limit so isalpha/isalnum
    # actually work in the wasm sandbox.
    cut      = { srcDir = "usr.bin/cut"; srcs = [ "cut.c" "yos_locale_stub.c" ];
                 stageDirs = [ "locale-stub" ]; };
    xargs    = { srcDir = "usr.bin/xargs";
                 srcs = [ "xargs.c" "strnsubst.c" "yos_locale_stub.c" ];
                 stageDirs = [ "locale-stub" ]; };
    tr       = { srcDir = "usr.bin/tr";
                 srcs = [ "cmap.c" "cset.c" "str.c" "tr.c" "yos_locale_stub.c" ];
                 stageDirs = [ "locale-stub" ]; };
    sed      = { srcDir = "usr.bin/sed";
                 srcs = [ "compile.c" "main.c" "misc.c" "process.c" "yos_locale_stub.c" ];
                 stageDirs = [ "locale-stub" ]; };
    du       = { srcDir = "usr.bin/du"; srcs = [ "du.c" "yos_locale_stub.c" ];
                 libcExtras = ctypeLibcExtras ++ [ "fts" "qsort" "reallocf" ];
                 stageDirs = [ "locale-stub" ]; };
    grep     = { srcDir = "usr.bin/grep";
                 srcs = [ "file.c" "grep.c" "queue.c" "util.c" "yos_locale_stub.c" ];
                 stageDirs = [ "locale-stub" ]; };

    # ── third batch: extra source dirs / generated files ─────────────
    # find: getdate.y → getdate.c via bison; the codegen step in
    # buildPhase writes it into $STAGE/find-gen so we point a stageDir
    # at that.
    find     = { srcDir = "usr.bin/find";
                 srcs = [ "find.c" "function.c" "ls.c" "main.c" "misc.c"
                          "operator.c" "option.c" "getdate.c"
                          "yos_locale_stub.c" ];
                 libcExtras = defaultLibcExtras ++ [ "fts" "qsort" "reallocf" ];
                 stageDirs   = [ "find-gen" "locale-stub" ];
                 extraCflags = [ "-Wno-incompatible-pointer-types" ]; };
    # sort: pulls md5c.c straight in from lib/libmd, then -Is the same
    # dir for <md5.h>.
    sort     = { srcDir = "usr.bin/sort";
                 srcs = [ "bwstring.c" "coll.c" "file.c" "mem.c" "radixsort.c"
                          "sort.c" "vsort.c" "md5c.c" "yos_locale_stub.c" ];
                 extraSrcDirs = [ "lib/libmd" ];
                 stageDirs    = [ "locale-stub" ];
                 extraCflags  = [ "-I${freebsd-src}/usr/src/lib/libmd"
                                  "-Wno-pointer-sign" ]; };
    # awk: bison generates awkgram.c + awkgram.tab.h; maketab compiled
    # for the host emits proctab.c. Both staged in $STAGE/awk-gen.
    awk      = { srcDir = "usr.bin/awk";
                 srcs = [ "awkgram.c" "b.c" "lex.c" "lib.c" "main.c"
                          "parse.c" "proctab.c" "run.c" "tran.c"
                          "yos_locale_stub.c" ];
                 extraSrcDirs = [ "contrib/one-true-awk" ];
                 stageDirs    = [ "awk-gen" "locale-stub" ];
                 extraCflags  = [ "-DHAS_ISBLANK" "-DFOPEN_MAX=64"
                                  "-I${freebsd-src}/usr/src/contrib/one-true-awk"
                                  "-I$STAGE/awk-gen"
                                  "-Wno-implicit-int" ]; };
    # wc, df: pull in our minimal text-only libxo shim staged at
    # $STAGE/libxo (yos_libxo_shim.c + libxo.h). Real libxo (8500+
    # lines, JSON / XML / HTML output) is overkill for wasm — we only
    # need text mode, which is ~150 lines of format parsing.
    wc       = { srcDir = "usr.bin/wc";
                 srcs = [ "wc.c" "yos_libxo_shim.c" "yos_locale_stub.c" ];
                 stageDirs   = [ "libxo" "locale-stub" ];
                 extraCflags = [ "-I$STAGE/libxo" "-Wno-pointer-sign" ]; };
    df       = { srcDir = "bin/df";
                 srcs = [ "df.c" "yos_libxo_shim.c" "yos_locale_stub.c" ];
                 stageDirs   = [ "libxo" "locale-stub" ];
                 extraCflags = [ "-I$STAGE/libxo" ]; };

    # ── real FreeBSD ps ──────────────────────────────────────────────
    # The real bin/ps, FreeBSD source verbatim (ps.c fmt.c keyword.c
    # nlist.c print.c). It gets the process list the STANDARD FreeBSD way:
    # kvm_getprocs(), which the ps-compat shim routes to
    # sysctl(CTL_KERN, KERN_PROC, KERN_PROC_*) — served by yos's libc
    # sysctl bridge (src/yos/impl/libc/sysctl.c). No /proc, no toy. The
    # ps-compat/ dir (copied verbatim, not a heredoc) is a self-contained
    # stand-in for the libs ps links that the wasm sysroot lacks:
    # libkvm/libxo/libutil/libjail + devname/strvis + a small sysctlbyname.
    ps       = { srcDir = "bin/ps";
                 srcs = [ "ps.c" "fmt.c" "keyword.c" "nlist.c" "print.c"
                          "yos_ps_compat.c" "yos_locale_stub.c" ];
                 libcExtras  = defaultLibcExtras ++ [ "qsort" ];
                 # keyword.c folds option keywords with tolower(); without the
                 # C-locale rune table that derefs a NULL locale and the lookup
                 # fails (the browser engine has no host ctype to fall back on).
                 stageDirs   = [ "ps-compat" "locale-stub" ];
                 extraCflags = [ "-I${freebsd-src}/usr/src/bin/ps"
                                 "-I$STAGE/ps-compat"
                                 "-Wno-incompatible-pointer-types"
                                 "-Wno-pointer-sign"
                                 "-Wno-implicit-function-declaration" ]; };

    # ── real FreeBSD top ─────────────────────────────────────────────
    # The interactive William-LeFebvre top, built from FreeBSD source
    # verbatim — commands.c, display.c, screen.c, top.c, username.c,
    # utils.c. Only the OS-specific machine.c is replaced: yos has no
    # libkvm, so yos_machine.c sources the process list from
    # sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PROC) + getloadavg instead.
    # Small shims stand in for the libs top normally links that aren't
    # in the wasm sysroot:
    #   yos_top_compat.c  — kvm shim (utils.c find_pid → sysctl
    #       KERN_PROC_PID), jail_getid/setpriority/sigblock stubs.
    #   $STAGE/top-compat/{kvm,jail,curses}.h — the missing headers.
    # termcap (tgetent/tgetstr/tgoto/tputs) comes from the sysroot's
    # libyos_stubs, which now advertises an 80x24 ANSI screen + cursor
    # addressing so screen.c keeps smart_terminal on (see
    # nixpkgs/sysroot/default.nix). commands.c/display.c call isdigit/
    # isprint, which deref a NULL rune locale unless yos_locale_stub.c
    # sets the C-locale table.
    top      = { srcDir = "usr.bin/top";
                 srcs = [ "commands.c" "display.c" "screen.c" "top.c"
                          "username.c" "utils.c" "humanize_number.c"
                          "yos_machine.c"
                          "yos_top_compat.c" "yos_locale_stub.c" ];
                 libcExtras   = defaultLibcExtras ++ [ "qsort" ];
                 # utils.c::format_k uses humanize_number(3) from libutil;
                 # pull its source in (pure C) rather than a shim.
                 extraSrcDirs = [ "lib/libutil" ];
                 stageDirs    = [ "top-compat" "locale-stub" ];
                 extraCflags = [ "-I${freebsd-src}/usr/src/usr.bin/top"
                                 "-I$STAGE/top-compat"
                                 "-Wno-incompatible-pointer-types"
                                 "-Wno-pointer-sign" ]; };

    # ── archive tools (on the libarchive surface) ────────────────────
    # bsdcat: FreeBSD's libarchive `cat` front-end, built from contrib
    # source verbatim (cat/bsdcat.c + cmdline.c) plus the small lafe
    # front-end helper (libarchive_fe/lafe_err.c). It imports env.archive_*
    # — resolved by the host libarchive bridge (src/yos/impl/libc/
    # libarchive.c) — so no libarchive bodies are compiled into the wasm.
    # A minimal guest archive.h / archive_entry.h / config.h (staged in
    # $STAGE/archive-compat) bolt the import attributes on and satisfy
    # the autotools config the contrib sources expect.
    bsdcat   = { srcDir = "contrib/libarchive/cat";
                 srcs = [ "bsdcat.c" "cmdline.c" "lafe_err.c" ];
                 extraSrcDirs = [ "contrib/libarchive/libarchive_fe" ];
                 stageDirs    = [ "archive-compat" ];
                 extraCflags  = [ "-I$STAGE/archive-compat"
                                  "-I${freebsd-src}/usr/src/contrib/libarchive/cat"
                                  "-I${freebsd-src}/usr/src/contrib/libarchive/libarchive_fe"
                                  "-include" "stdint.h" ]; };

    # ── networking tools (libc sockets, already bridged) ─────────────
    # whois needs no extra library (LIBADD is empty) — it's plain libc
    # + sockets (socket/connect/getaddrinfo), all of which yos already
    # bridges (the same surface openssh/telnetd use). A real query needs
    # outbound network; the tool itself builds and runs under yos.
    whois    = { srcDir = "usr.bin/whois"; };

    # nc (OpenBSD netcat, contrib/netcat). The FreeBSD Makefile adds
    # libipsec (-DIPSEC) and libsbuf+libstats (-DWITH_STATS), but both
    # are opt-in #ifdef blocks — by simply NOT defining those macros nc
    # drops to plain libc + sockets, which yos already bridges. So no
    # extra library is needed for the core connect/listen/proxy paths.
    nc       = { srcDir = "contrib/netcat";
                 srcs = [ "netcat.c" "atomicio.c" "socks.c" ];
                 # getopt + getopt_long BOTH: with only getopt_long the
                 # guest's getopt() resolves to yos's host env.getopt
                 # bridge (which tracks optind host-side and walks guest
                 # argv as host pointers) — infinite loop / OOB. Linking
                 # getopt.c keeps getopt() guest-side. (defaultLibcExtras
                 # already pairs them; nc just needs the non-default
                 # basename-only trim avoided.)
                 libcExtras  = [ "getopt" "getopt_long" "basename" ];
                 extraCflags = [ "-DINET6" "-Wno-incompatible-pointer-types" ]; };

    # telnet (contrib/telnet). Built WITHOUT OpenSSL (ENCRYPTION/
    # AUTHENTICATION), Kerberos (KRB5) or IPSEC — those are the only
    # things that pull mp/crypto/pam/krb5/roken/ipsec, and they're all
    # behind #ifdefs. What's left is the telnet client + the two
    # libtelnet helpers it uses unconditionally (genget.c, misc.c) +
    # termcap (the sysroot libyos_stubs stub provides tget*/tputs).
    telnet   = { srcDir = "contrib/telnet/telnet";
                 srcs = [ "commands.c" "main.c" "network.c" "ring.c"
                          "sys_bsd.c" "telnet.c" "terminal.c"
                          "utilities.c" "genget.c" "misc.c" ];
                 extraSrcDirs = [ "contrib/telnet/libtelnet" ];
                 extraCflags  = [ "-DKLUDGELINEMODE" "-DUSE_TERMIO" "-DENV_HACK"
                                  "-I${freebsd-src}/usr/src/contrib/telnet"
                                  "-I${freebsd-src}/usr/src/contrib/telnet/libtelnet"
                                  "-I$STAGE/telnet-compat"
                                  "-include" "sys/wait.h"
                                  "-Wno-incompatible-pointer-types"
                                  "-Wno-pointer-sign"
                                  "-Wno-implicit-function-declaration" ]; };
  };

  # ── helpers (Nix side) ───────────────────────────────────────────────
  toolSrcs       = t: t.srcs       or [ "${(t._name or "")}.c" ];
  toolExtras     = t: t.libcExtras or defaultLibcExtras;
  toolDirs       = t: t.extraSrcDirs or [];
  toolStageDirs  = t: t.stageDirs    or [];
  toolCflags     = t: t.extraCflags  or [];

  libcCflags = lib.optionals
    (lib.any (t: (toolExtras t) != []) (lib.attrValues tools))
    [ "-I${freebsd-src}/usr/src/lib/libc/include"
      # lib/libc/gen carries gen-private.h, which fts.c includes via
      # double-quote ("gen-private.h"). The compiler finds it in the
      # source file's own directory when fts.c is compiled, but the
      # consumer translation units (ls.c, cp.c, …) don't compile
      # anything from lib/libc/gen, so adding the dir to the search
      # path keeps the include resolvable from any TU that ends up
      # parsing it (none today, but cheap insurance).
      "-I${freebsd-src}/usr/src/lib/libc/gen" ];

  # Render the bash buildPhase loop. Each tool becomes a build_tool call
  # with its arguments folded into a heredoc-friendly form.
  #
  # Stage dirs are emitted as literal `$STAGE/<name>` strings — the
  # buildPhase pre-generates files there before any tool builds, so by
  # the time build_tool runs, $STAGE is set and the path resolves.
  toolBuildScript = pname: cfg:
    let
      e         = cfg // { _name = pname; };
      srcs      = toolSrcs e;
      extras    = toolExtras e;
      dirs      = toolDirs e;
      stageDirs = toolStageDirs e;
      cf        = toolCflags e;
      libcSrcs  = map (n:
        let lc = libcExtraTable.${n}; in
        "${freebsd-src}/usr/src/${lc.sub}/${lc.file}"
      ) extras;
      searchDirs = [ "${freebsd-src}/usr/src/${cfg.srcDir}" ]
                ++ map (d: "${freebsd-src}/usr/src/${d}") dirs
                ++ map (d: "$STAGE/${d}") stageDirs;
    in ''
      build_tool ${pname} \
        "${cfg.srcDir}" \
        "${lib.concatStringsSep " " srcs}" \
        "${lib.concatStringsSep " " searchDirs}" \
        "${lib.concatStringsSep " " libcSrcs}" \
        "${lib.concatStringsSep " " (cf ++ libcCflags)}"
    '';

  yosRunner = wasmPath: pname:
    if yos != null
    then ''exec ${yos}/bin/yos ${wasmPath} "$@"''
    else ''exec yos ${wasmPath} "$@"'';
in

stdenv.mkDerivation {
  pname   = "yos-freebsd-tools";
  version = freebsd-src.version;

  dontUnpack    = true;
  dontConfigure = true;
  dontPatch     = true;
  dontStrip     = true;
  dontPatchELF  = true;
  dontFixup     = true;

  nativeBuildInputs = [
    toolchain binaryen
    # bison: generates getdate.c (find), awkgram.c (awk).
    bison
    # host cc: builds awk's maketab tool that emits proctab.c.
    buildPackages.stdenv.cc
  ];

  buildPhase = ''
    runHook preBuild

    set -euo pipefail
    mkdir -p out/bin out/libexec

    # ── stub headers for compression libs we don't link ──────────────
    # FreeBSD's grep.h (and a few other ports) `#include` <bzlib.h> and
    # <zlib.h> for declarations they don't actually use — they're left
    # over from the original NetBSD zgrep wrapper. We don't ship any
    # of those compression libraries in the wasm sysroot, so provide
    # empty stubs in a tool-private include dir. Same trick for lzma /
    # zstd. Tools that actually need them (gzip) would never resolve
    # against these — they'd need a real port.
    STUBINC="$TMPDIR/stubinc"
    mkdir -p "$STUBINC"
    : > "$STUBINC/bzlib.h"
    : > "$STUBINC/zlib.h"
    : > "$STUBINC/lzma.h"
    : > "$STUBINC/zstd.h"

    # ── compile_c <found.c> <out.o> ───────────────────────────────────
    # Single per-source compile — same flags whether the .c is a tool
    # source or a libc helper. Tool-specific cflags get appended by the
    # caller when needed.
    compile_c () {
      local found="$1" obj="$2" extra_cflags="$3"
      wasm-clang \
        -target wasm32-unknown-unknown -nostdlib -nostdinc \
        --sysroot="${sysroot}" \
        -isystem "${sysroot}/usr/include" \
        -idirafter "$STUBINC" \
        -D__i386__=1 -D__yos__=1 \
        -O2 -fno-builtin -ffreestanding \
        -Wno-unused-parameter -Wno-unused-but-set-variable \
        $extra_cflags \
        -c "$found" -o "$obj"
    }

    # ── build_tool <pname> <srcDir> "<srcs>" "<searchDirs>" "<libcSrcs>" "<cflags>" ──
    build_tool () {
      local pname="$1" srcDir="$2" srcs="$3" searchDirs="$4" libcSrcs="$5" cflags="$6"
      local objs=()
      local td="$(mktemp -d)"

      # Tool srcs — search through searchDirs (mimics FreeBSD .PATH).
      for s in $srcs; do
        local found=""
        for d in $searchDirs; do
          if [ -f "$d/$s" ]; then found="$d/$s"; break; fi
        done
        if [ -z "$found" ]; then
          echo "freebsd-tools($pname): src '$s' not found under: $searchDirs" >&2
          exit 1
        fi
        local obj="$td/tool_$(basename "$s" .c).o"
        compile_c "$found" "$obj" "$cflags"
        objs+=("$obj")
      done

      # libc helpers — absolute paths, uniquified obj names so a tool
      # whose own source matches a libc helper (basename/strsignal/…)
      # doesn't get duplicate symbols at link time.
      for p in $libcSrcs; do
        local obj="$td/libc_$(basename "$p" .c).o"
        compile_c "$p" "$obj" "$cflags"
        objs+=("$obj")
      done

      # Link.
      wasm-clang \
        -target wasm32-unknown-unknown -nostdlib \
        --sysroot="${sysroot}" \
        -L"${sysroot}/usr/lib" \
        -Wl,--no-entry -Wl,--allow-undefined -Wl,--export=_start \
        -Wl,--export=main \
        -Wl,-z,stack-size=8388608 \
        -o "out/libexec/$pname" \
        "${sysroot}/usr/lib/crt1.o" \
        "''${objs[@]}" \
        -lc -lyos_stubs

      # Asyncify, like every other universal binary (zsh, tmux, nvim, the
      # openssh tools). The cooperative engines suspend a guest by unwinding
      # its stack — native yos for fork(), the browser engine additionally
      # for ANY blocking syscall. Without the instrumentation an interactive
      # tool traps the moment it blocks: browser `top` died on its first
      # select() with "asyncify_start_unwind is not a function", and
      # fork-using tools (find -exec, xargs) can't fork under native yos.
      wasm-opt --asyncify -O2 "out/libexec/$pname" -o "out/libexec/$pname.async"
      mv "out/libexec/$pname.async" "out/libexec/$pname"

      # Runner — `exec yos <libexec-path> "$@"`. yos store path is
      # baked in when the .#yos derivation is wired (yos != null);
      # otherwise we fall back to looking up `yos` on PATH.
      cat > "out/bin/$pname" <<RUNNER_EOF
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} "$out/libexec/$pname" "\$@"
    RUNNER_EOF
      chmod +x "out/bin/$pname"

      rm -rf "$td"
    }

    # ── codegen for tools that need yacc/maketab ─────────────────────
    # Done up-front, in one place, so the per-tool build_tool calls
    # below can find generated .c / .h via stageDirs (rendered as
    # $STAGE/<name> in the searchDirs list).
    STAGE="$TMPDIR/yos-stage"
    mkdir -p "$STAGE"

    # find: getdate.y → getdate.c (yacc grammar). Bison's `-y` mode is
    # POSIX-yacc-compatible; FreeBSD uses byacc but the grammar is
    # plain enough that bison-as-yacc handles it. The grammar emits
    # the function `get_date()` that find/function.c calls into.
    mkdir -p "$STAGE/find-gen"
    bison -y -o "$STAGE/find-gen/getdate.c" \
        "${freebsd-src}/usr/src/usr.bin/find/getdate.y"

    # awk: bison generates awkgram.{c,h}; we then build a host-side
    # `maketab` binary and run it on awkgram.tab.h to emit proctab.c.
    # FreeBSD's Makefile renames awkgram.h → awkgram.tab.h with a
    # symlink; we just produce the .tab.h directly.
    AWK_SRC="${freebsd-src}/usr/src/contrib/one-true-awk"
    mkdir -p "$STAGE/awk-gen"
    bison --header="$STAGE/awk-gen/awkgram.tab.h" \
          -o "$STAGE/awk-gen/awkgram.c" \
          "$AWK_SRC/awkgram.y"
    # maketab is host-side build-tool (it runs at build time, not in
    # the wasm guest), so use the build-platform cc, not wasm-clang.
    "${buildPackages.stdenv.cc}/bin/cc" \
        -O2 -I"$STAGE/awk-gen" \
        -o "$STAGE/awk-gen/maketab" \
        "$AWK_SRC/maketab.c"
    "$STAGE/awk-gen/maketab" "$STAGE/awk-gen/awkgram.tab.h" \
        > "$STAGE/awk-gen/proctab.c"

    # ── locale stub: minimal C-locale rune table + __mb_sb_limit ─────
    # Without _CurrentRuneLocale set, every isalpha/isalnum/isspace
    # returns 0 (the inline __sbmaskrune in <_ctype.h> deref's a NULL
    # pointer's runetype[] field). awk reads only "B" before bailing,
    # tr never matches a-z, sed/grep can't lex regex classes.
    #
    # Compiling FreeBSD's lib/libc/locale/table.c straight to wasm32
    # fails: it includes mblocal.h → xlocale_private.h → atomic.h
    # which is x86 inline asm. Strip those includes + the host-side
    # __runes_for_locale function (only the static initializer is
    # needed for the C-locale fast path) and write the result + the
    # __mb_sb_limit definition into the stage dir.
    mkdir -p "$STAGE/locale-stub"
    LOC="${freebsd-src}/usr/src/lib/libc/locale"
    {
        # Skip mblocal.h include → no xlocale_private.h → no atomic.h
        sed -e 's|#include "mblocal.h"||' "$LOC/table.c" \
            | sed -e '/^_RuneLocale \*$/,/^}$/d' \
                  -e '/^__runes_for_locale/,/^}$/d'
        echo
        echo "/* yos addition: __mb_sb_limit lives in none.c upstream;"
        echo " * pulled out separately so we don't have to compile in"
        echo " * none.c (which calls into setlocale machinery). 256 is"
        echo " * the upstream none.c value (every byte is single-byte)."
        echo " */"
        echo "int __mb_sb_limit = 256;"
        echo ""
        echo "/* yos addition: <runetype.h> declares _ThreadRuneLocale as"
        echo " * _Thread_local and the inline __getCurrentRuneLocale reads"
        echo " * it on every is*()/iswXXX() call. Without a definition the"
        echo " * wasm-ld link only resolves it through --allow-undefined,"
        echo " * which leaves the TLS slot uninitialised; reads return"
        echo " * garbage pointers and __maskrune dereferences them. Define"
        echo " * it explicitly as the zero TLS so __getCurrentRuneLocale"
        echo " * falls through to the global _CurrentRuneLocale set above."
        echo " */"
        echo "_Thread_local const _RuneLocale *_ThreadRuneLocale = 0;"
    } > "$STAGE/locale-stub/yos_locale_stub.c"

    # ── yos-native ps ────────────────────────────────────────────────
    # Output mirrors `ps -e` from BSD ps closely enough to be useful in
    # the yos shell without dragging in libkvm + 3 kloc of FreeBSD's
    # actual bin/ps.
    #
    # Reads the process list via FreeBSD's standard
    # ── real ps compat shim (copied verbatim from ps-compat/) ────────
    # libkvm/libxo/libutil/libjail stand-in routing the process list
    # through sysctl(CTL_KERN, KERN_PROC, KERN_PROC_*). FreeBSD does NOT
    # ship /proc; sysctl is the right contract.
    mkdir -p "$STAGE/ps-compat"
    cp -r ${./ps-compat}/. "$STAGE/ps-compat/"
    chmod -R u+w "$STAGE/ps-compat"

    # ── real FreeBSD top: shims + retargeted machine.c ────────────────
    # Only machine.c is replaced (yos has no libkvm); the rest of top
    # is compiled from FreeBSD source verbatim. See the `top` entry in
    # `tools` above for the full rationale.
    mkdir -p "$STAGE/top-compat"
    cat > "$STAGE/top-compat/curses.h" <<'YOS_CURSES_EOF'
    #ifndef _YOS_CURSES_H_
    #define _YOS_CURSES_H_
    /* top's screen.c includes <curses.h> for the termcap PC/UP/BC/ospeed
     * globals (declared by the sysroot <termcap.h>) and, implicitly, for
     * bool/true/false and putchar that the real curses.h drags in. yos
     * ships no curses, so this stub provides just those. */
    #include <stdbool.h>
    #include <stdio.h>
    #endif
    YOS_CURSES_EOF

    cat > "$STAGE/top-compat/kvm.h" <<'YOS_KVM_EOF'
    #ifndef _YOS_KVM_H_
    #define _YOS_KVM_H_
    /* Minimal kvm(3) surface for top's utils.c::find_pid. yos has no
     * libkvm; these route through sysctl(KERN_PROC_*) — see
     * yos_top_compat.c. */
    #include <sys/types.h>
    struct kinfo_proc;
    typedef struct yos_kvm kvm_t;
    kvm_t *kvm_open(const char *execfile, const char *corefile,
                    const char *swapfile, int flags, const char *errstr);
    int    kvm_close(kvm_t *kd);
    struct kinfo_proc *kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt);
    char **kvm_getargv(kvm_t *kd, const struct kinfo_proc *p, int nchr);
    #endif
    YOS_KVM_EOF

    cat > "$STAGE/top-compat/jail.h" <<'YOS_JAIL_EOF'
    #ifndef _YOS_JAIL_H_
    #define _YOS_JAIL_H_
    /* top references one libjail symbol; yos has no jails. */
    int jail_getid(const char *name);
    #endif
    YOS_JAIL_EOF

    cat > "$STAGE/top-compat/yos_top_compat.c" <<'YOS_COMPAT_EOF'
    /* yos top compat shims — the bits top links that aren't in the wasm
     * sysroot, kept tiny and host-portable (Linux/macOS/Windows hosts all
     * reach them through the same yos sysctl/libc import surface).
     *
     *  - kvm(3): utils.c::find_pid only needs kvm_open/getprocs/close; we
     *    serve them from sysctl(KERN_PROC_PID), which yos implements in
     *    src/yos/impl/libc/sysctl.c. (yos_machine.c does NOT use kvm.)
     *  - jail_getid / setpriority / getpriority / sigblock / sigsetmask:
     *    stubbed — yos has no jails, no priority knob, and the legacy BSD
     *    signal-mask helpers are only hit on the interactive SIGTSTP path.
     */
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <sys/user.h>
    #include <stdlib.h>
    #include <string.h>
    #include <kvm.h>
    
    struct yos_kvm { int unused; };
    
    kvm_t *
    kvm_open(const char *execfile, const char *corefile, const char *swapfile,
             int flags, const char *errstr)
    {
        static struct yos_kvm h;
        (void)execfile; (void)corefile; (void)swapfile; (void)flags; (void)errstr;
        return &h;
    }
    
    int
    kvm_close(kvm_t *kd)
    {
        (void)kd;
        return 0;
    }
    
    struct kinfo_proc *
    kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
    {
        static struct kinfo_proc *buf;
        static size_t cap;
        int mib[4], miblen;
        size_t len = 0;
    
        (void)kd;
        mib[0] = CTL_KERN;
        mib[1] = KERN_PROC;
        mib[2] = op;
        mib[3] = arg;
        miblen = (op == KERN_PROC_PID) ? 4 : 3;
    
        if (sysctl(mib, miblen, NULL, &len, NULL, 0) < 0 || len == 0) {
            *cnt = 0;
            return NULL;
        }
        if (len > cap) {
            free(buf);
            buf = malloc(len);
            cap = buf ? len : 0;
        }
        if (buf == NULL || sysctl(mib, miblen, buf, &len, NULL, 0) < 0) {
            *cnt = 0;
            return NULL;
        }
        *cnt = (int)(len / sizeof(struct kinfo_proc));
        return buf;
    }
    
    char **
    kvm_getargv(kvm_t *kd, const struct kinfo_proc *p, int nchr)
    {
        (void)kd; (void)p; (void)nchr;
        return NULL;   /* arguments display falls back to ki_comm */
    }
    
    int
    jail_getid(const char *name)
    {
        (void)name;
        return -1;
    }
    
    /* id_t is int32 on this ABI; int args match the call sites. */
    int
    setpriority(int which, int who, int prio)
    {
        (void)which; (void)who; (void)prio;
        return 0;
    }
    
    int
    getpriority(int which, int who)
    {
        (void)which; (void)who;
        return 0;
    }
    
    int
    sigblock(int mask)
    {
        (void)mask;
        return 0;
    }
    
    int
    sigsetmask(int mask)
    {
        (void)mask;
        return 0;
    }
    YOS_COMPAT_EOF

    cat > "$STAGE/top-compat/yos_machine.c" <<'YOS_MACHINE_EOF'
    /* yos machine.c — the OS-specific half of top(1), retargeted from
     * libkvm to yos's sysctl(KERN_PROC_PROC) surface.
     *
     * Everything else in top (top.c, display.c, screen.c, commands.c,
     * utils.c, username.c) is the unmodified FreeBSD source; only this
     * file is replaced, because it is the one that talks to the kernel.
     * yos has no libkvm and no /dev/kmem — it exposes its process table
     * through sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PROC) (served by
     * src/yos/impl/libc/sysctl.c) and load averages through getloadavg.
     *
     * yos currently fills pid/ppid/uid/stat/nice/comm/tdname/numthreads
     * in struct kinfo_proc; CPU%, RSS, SIZE, runtime and the system-wide
     * cpu/memory/swap counters are not tracked yet and read back as 0.
     * The columns are wired so they light up for free once the proc table
     * grows those fields.
     */
    #include <sys/param.h>
    #include <sys/proc.h>
    #include <sys/priority.h>
    #include <sys/resource.h>
    #include <sys/sysctl.h>
    #include <sys/time.h>
    #include <sys/user.h>
    
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>
    
    #include "top.h"
    #include "display.h"
    #include "machine.h"
    #include "loadavg.h"
    #include "screen.h"
    #include "utils.h"
    #include "layout.h"
    
    enum displaymodes displaymode;
    static const int namelength = 10;
    
    /* get_process_info hands back this opaque handle. */
    struct handle {
        struct kinfo_proc **next_proc;
        int remaining;
    };
    
    #define PROCSIZE(pp)   ((pp)->ki_size / 1024)
    #define PCTCPU(pp)     (pcpu[(pp) - pbase])
    #define pagetok(size)  ((size) << pageshift)
    #define ki_swap(kip) \
        ((kip)->ki_swrss > (kip)->ki_rssize ? (kip)->ki_swrss - (kip)->ki_rssize : 0)
    
    static const char *state_abbrev[] = {
        "", "START", "RUN\0\0\0", "SLEEP", "STOP", "ZOMB", "WAIT", "LOCK"
    };
    
    static int lastpid = -1;
    
    static const char *procstatenames[] = {
        "", " starting, ", " running, ", " sleeping, ", " stopped, ",
        " zombie, ", " waiting, ", " lock, ",
        NULL
    };
    static int process_states[nitems(procstatenames)];
    
    static int cpu_states[CPUSTATES];
    static const char *cpustatenames[] = {
        "user", "nice", "system", "interrupt", "idle", NULL
    };
    
    static const char *memorynames[] = {
        "K Active, ", "K Inact, ", "K Laundry, ", "K Wired, ", "K Buf, ",
        "K Free", NULL
    };
    static int memory_stats[nitems(memorynames)];
    
    static const char *swapnames[] = {
        "K Total, ", "K Used, ", "K Free, ", "% Inuse, ", "K In, ", "K Out", NULL
    };
    static int swap_stats[nitems(swapnames)];
    
    static const char *ordernames[] = {
        "cpu", "size", "res", "time", "pri", "threads",
        "total", "read", "write", "fault", "vcsw", "ivcsw",
        "jid", "swap", "pid", NULL
    };
    
    /* the proc array, (re)read every cycle from sysctl */
    static int nproc;
    static int onproc = -1;
    static int pref_len;
    static struct kinfo_proc *pbase;
    static size_t pbase_cap;
    static struct kinfo_proc **pref;
    static double *pcpu;
    static int pageshift;
    static int ncpus = 1;
    
    static const char *format_nice(const struct kinfo_proc *pp);
    static void update_layout(void);
    
    static int
    find_uid(uid_t needle, int *haystack)
    {
        for (size_t i = 0; i < TOP_MAX_UIDS; ++i)
            if ((uid_t)haystack[i] == needle)
                return 1;
        return 0;
    }
    
    void
    toggle_pcpustats(void)
    {
        update_layout();
    }
    
    static void
    update_layout(void)
    {
        /* yos reports no per-CPU, ARC or swap lines, so the layout is the
         * fixed no-ARC/no-swap variant of the upstream formula. */
        y_mem        = 3;
        y_swap       = 3;
        y_idlecursor = 4;
        y_message    = 4;
        y_header     = 5;
        y_procs      = 6;
        Header_lines = 6;
    }
    
    int
    machine_init(struct statics *statics)
    {
        int pagesize;
    
        pbase = NULL;
        pbase_cap = 0;
        pref = NULL;
        pcpu = NULL;
        nproc = 0;
        onproc = -1;
    
        pagesize = getpagesize();
        pageshift = 0;
        while (pagesize > 1) {
            pageshift++;
            pagesize >>= 1;
        }
        pageshift -= LOG1024;
        if (pageshift < 0)
            pageshift = 0;
    
        statics->procstate_names = procstatenames;
        statics->cpustate_names  = cpustatenames;
        statics->memory_names    = memorynames;
        statics->arc_names       = NULL;
        statics->carc_names      = NULL;
        statics->swap_names      = NULL;
        statics->order_names     = ordernames;
        statics->nbatteries      = 0;
        statics->ncpus           = 1;
        ncpus = 1;
    
        update_layout();
        return 0;
    }
    
    char *
    format_header(const char *uname_field)
    {
        static char hdr[256];
    
        if (displaymode == DISP_IO) {
            snprintf(hdr, sizeof hdr,
                "  PID %-*.*s   VCSW  IVCSW   READ  WRITE  FAULT  TOTAL PERCENT COMMAND",
                namelength, namelength, uname_field);
        } else {
            snprintf(hdr, sizeof hdr,
                "  PID %-*.*s %sPRI NICE   SIZE    RES STATE    TIME %6s COMMAND",
                namelength, namelength, uname_field,
                ps.thread ? "" : "THR ",
                ps.wcpu ? "WCPU" : "CPU");
        }
        return hdr;
    }
    
    void
    get_system_info(struct system_info *si)
    {
        double load[NUM_AVERAGES] = { 0 };
    
        getloadavg(load, NUM_AVERAGES);
        for (int i = 0; i < NUM_AVERAGES; i++)
            si->load_avg[i] = load[i];
    
        /* cpu / memory / swap counters: not tracked by yos yet -> zeros. */
        memset(cpu_states, 0, sizeof cpu_states);
        si->cpustates = cpu_states;
        si->ncpus = 1;
    
        memset(memory_stats, 0, sizeof memory_stats);
        memory_stats[nitems(memorynames) - 1] = -1;
        si->memory = memory_stats;
    
        memset(swap_stats, 0, sizeof swap_stats);
        swap_stats[nitems(swapnames) - 1] = -1;
        si->swap = swap_stats;
    
        /* last_pid must be valid from the VERY FIRST sample. The display
         * layer paints the "last pid: N;" header label only on the initial
         * screen (i_loadave); later refreshes write just the number at its
         * fixed column. When the first sample reported -1 ("unavailable")
         * and a later one reported a pid — this was seeded only by the proc
         * scan in get_process_info — the number landed on top of the
         * "load averages" text and the header rendered mangled. Seed with
         * our own pid: top is the most recently spawned process, so its pid
         * IS the last allocated one until a scan learns better. */
        if (lastpid < 0)
            lastpid = getpid();
        si->last_pid = lastpid;
        si->boottime.tv_sec = -1;
        si->battery = 0;
        /* si->procstates is filled in get_process_info */
    }
    
    static int
    cmd_matches(struct kinfo_proc *proc, const char *term)
    {
        if (!term)
            return 1;
        if (strstr(proc->ki_comm, term))
            return 1;
        return 0;
    }
    
    static struct handle handle;
    
    static void *
    empty_handle(struct system_info *si)
    {
        si->procstates = process_states;
        memset(process_states, 0, sizeof process_states);
        si->p_total = 0;
        si->p_pactive = 0;
        handle.next_proc = pref;
        handle.remaining = 0;
        return &handle;
    }
    
    void *
    get_process_info(struct system_info *si, struct process_select *sel,
        int (*compare)(const void *, const void *))
    {
        int i, total_procs, active_procs;
        int mib[4], miblen;
        size_t len;
        struct kinfo_proc *pp;
        struct kinfo_proc **prefp;
    
        mib[0] = CTL_KERN;
        mib[1] = KERN_PROC;
        mib[2] = sel->thread ? KERN_PROC_ALL : KERN_PROC_PROC;
        mib[3] = 0;
        miblen = 3;
    
        len = 0;
        if (sysctl(mib, miblen, NULL, &len, NULL, 0) < 0 || len == 0)
            return empty_handle(si);
    
        if (len > pbase_cap) {
            free(pbase);
            pbase = malloc(len);
            pbase_cap = pbase ? len : 0;
        }
        if (pbase == NULL || sysctl(mib, miblen, pbase, &len, NULL, 0) < 0)
            return empty_handle(si);
    
        nproc = (int)(len / sizeof(struct kinfo_proc));
    
        if (nproc > onproc) {
            pref = realloc(pref, sizeof(*pref) * nproc);
            pcpu = realloc(pcpu, sizeof(*pcpu) * nproc);
            onproc = nproc;
        }
        if (pref == NULL || pcpu == NULL) {
            fprintf(stderr, "top: Out of memory.\n");
            quit(TOP_EX_SYS_ERROR);
        }
        memset(pcpu, 0, sizeof(*pcpu) * nproc);
    
        si->procstates = process_states;
        total_procs = 0;
        active_procs = 0;
        memset(process_states, 0, sizeof process_states);
        prefp = pref;
        for (pp = pbase, i = 0; i < nproc; pp++, i++) {
            if (pp->ki_stat == 0)
                continue;
            if (!sel->self && pp->ki_pid == mypid && sel->pid == -1)
                continue;
            if (!sel->system && (pp->ki_flag & P_SYSTEM) && sel->pid == -1)
                continue;
            if (pp->ki_pid > lastpid)
                lastpid = pp->ki_pid;
            total_procs++;
            process_states[(unsigned char)pp->ki_stat]++;
            if (pp->ki_stat == SZOMB)
                continue;
            PCTCPU(pp) = pctdouble(pp->ki_pctcpu);
            if (displaymode == DISP_CPU && !sel->idle &&
                (pp->ki_stat == SSTOP || pp->ki_stat == SIDL))
                continue;
            if (sel->jid != -1 && pp->ki_jid != sel->jid)
                continue;
            if (sel->uid[0] != -1 && !find_uid(pp->ki_ruid, sel->uid))
                continue;
            if (sel->pid != -1 && pp->ki_pid != sel->pid)
                continue;
            if (!cmd_matches(pp, sel->command))
                continue;
            *prefp++ = pp;
            active_procs++;
        }
    
        if (compare != NULL)
            qsort(pref, active_procs, sizeof(*pref), compare);
    
        si->p_total = total_procs;
        si->p_pactive = pref_len = active_procs;
        handle.next_proc = pref;
        handle.remaining = active_procs;
        return &handle;
    }
    
    char *
    format_next_process(struct handle *xhandle, char *(*get_userid)(int), int flags)
    {
        static char procbuf[1024];
        struct kinfo_proc *pp;
        long cputime;
        char status[22];
        size_t state;
        char cmdbuf[512];
        int off = 0;
    
        (void)flags;   /* arg display unsupported: kvm_getargv stubbed */
    
        pp = *(xhandle->next_proc++);
        xhandle->remaining--;
    
        cputime = (pp->ki_runtime + 500000) / 1000000;
    
        switch (state = pp->ki_stat) {
        case SRUN:
            strcpy(status, "RUN");
            break;
        case SLOCK:
        case SSLEEP:
            if (pp->ki_wmesg[0])
                snprintf(status, sizeof status, "%.6s", pp->ki_wmesg);
            else
                strcpy(status, "-");
            break;
        default:
            if (state < nitems(state_abbrev))
                snprintf(status, sizeof status, "%.6s", state_abbrev[state]);
            else
                snprintf(status, sizeof status, "?%5zu", state);
            break;
        }
    
        if (ps.thread && pp->ki_tdname[0])
            snprintf(cmdbuf, sizeof cmdbuf, "%s{%s}", pp->ki_comm, pp->ki_tdname);
        else
            snprintf(cmdbuf, sizeof cmdbuf, "%s", pp->ki_comm);
    
        if (displaymode == DISP_IO) {
            snprintf(procbuf, sizeof procbuf,
                "%5d %-*.*s %6ld %6ld %6ld %6ld %6ld %6ld %6.2f%% %s",
                pp->ki_pid, namelength, namelength,
                (*get_userid)(pp->ki_ruid),
                0L, 0L, 0L, 0L, 0L, 0L, 0.0, cmdbuf);
            return procbuf;
        }
    
        off += snprintf(procbuf + off, sizeof procbuf - off, "%5d ",
            ps.thread_id ? pp->ki_tid : pp->ki_pid);
        off += snprintf(procbuf + off, sizeof procbuf - off, "%-*.*s ",
            namelength, namelength, (*get_userid)(pp->ki_ruid));
        if (!ps.thread)
            off += snprintf(procbuf + off, sizeof procbuf - off, "%4d ",
                pp->ki_numthreads);
        else
            off += snprintf(procbuf + off, sizeof procbuf - off, " ");
        off += snprintf(procbuf + off, sizeof procbuf - off, "%3d ",
            pp->ki_pri.pri_level - PUSER);
        off += snprintf(procbuf + off, sizeof procbuf - off, "%4s",
            format_nice(pp));
        off += snprintf(procbuf + off, sizeof procbuf - off, "%7s ",
            format_k(PROCSIZE(pp)));
        off += snprintf(procbuf + off, sizeof procbuf - off, "%6s ",
            format_k(pagetok(pp->ki_rssize)));
        off += snprintf(procbuf + off, sizeof procbuf - off, "%-6.6s ", status);
        off += snprintf(procbuf + off, sizeof procbuf - off, "%6s ",
            format_time(cputime));
        off += snprintf(procbuf + off, sizeof procbuf - off, "%6.2f%% ",
            100.0 * PCTCPU(pp));
        snprintf(procbuf + off, sizeof procbuf - off, "%s", cmdbuf);
        return procbuf;
    }
    
    static const char *
    format_nice(const struct kinfo_proc *pp)
    {
        const char *fifo, *kproc;
        int rtpri;
        static char nicebuf[4 + 1];
    
        fifo  = PRI_NEED_RR(pp->ki_pri.pri_class) ? "" : "F";
        kproc = (pp->ki_flag & P_KPROC) ? "k" : "";
        switch (PRI_BASE(pp->ki_pri.pri_class)) {
        case PRI_ITHD:
            return "-";
        case PRI_REALTIME:
            rtpri = ((pp->ki_flag & P_KPROC) ? pp->ki_pri.pri_native :
                pp->ki_pri.pri_user) - PRI_MIN_REALTIME;
            snprintf(nicebuf, sizeof nicebuf, "%sr%d%s", kproc, rtpri, fifo);
            break;
        case PRI_TIMESHARE:
            if (pp->ki_flag & P_KPROC)
                return "-";
            snprintf(nicebuf, sizeof nicebuf, "%d", pp->ki_nice - NZERO);
            break;
        case PRI_IDLE:
            rtpri = ((pp->ki_flag & P_KPROC) ? pp->ki_pri.pri_native :
                pp->ki_pri.pri_user) - PRI_MIN_IDLE;
            snprintf(nicebuf, sizeof nicebuf, "%si%d%s", kproc, rtpri, fifo);
            break;
        default:
            return "?";
        }
        return nicebuf;
    }
    
    /* qsort comparators. The i/o keys (total/read/write/fault/vcsw/ivcsw)
     * collapse to a stable no-op because yos doesn't track rusage yet. */
    static const int sorted_state[] = {
        [SIDL]   = 3,
        [SRUN]   = 1,
        [SSLEEP] = 6,
        [SSTOP]  = 5,
        [SZOMB]  = 2,
        [SWAIT]  = 4,
        [SLOCK]  = 7,
    };
    
    #define ORDERKEY_PCTCPU(a, b) do { \
        double diff = PCTCPU(b) - PCTCPU(a); \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_CPTICKS(a, b) do { \
        int64_t diff = (int64_t)(b)->ki_runtime - (int64_t)(a)->ki_runtime; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_STATE(a, b) do { \
        int diff = sorted_state[(unsigned char)(b)->ki_stat] - \
                   sorted_state[(unsigned char)(a)->ki_stat]; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_PRIO(a, b) do { \
        int diff = (int)(b)->ki_pri.pri_level - (int)(a)->ki_pri.pri_level; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_THREADS(a, b) do { \
        int diff = (int)(b)->ki_numthreads - (int)(a)->ki_numthreads; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_RSSIZE(a, b) do { \
        long diff = (long)(b)->ki_rssize - (long)(a)->ki_rssize; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_MEM(a, b) do { \
        long diff = (long)PROCSIZE(b) - (long)PROCSIZE(a); \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_JID(a, b) do { \
        int diff = (int)(b)->ki_jid - (int)(a)->ki_jid; \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    #define ORDERKEY_SWAP(a, b) do { \
        int diff = (int)ki_swap(b) - (int)ki_swap(a); \
        if (diff != 0) return (diff > 0 ? 1 : -1); \
    } while (0)
    
    #define CMP_PROLOGUE \
        const struct kinfo_proc *a = *(const struct kinfo_proc * const *)arg1; \
        const struct kinfo_proc *b = *(const struct kinfo_proc * const *)arg2
    
    static int compare_cpu(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_PCTCPU(a,b); ORDERKEY_CPTICKS(a,b); ORDERKEY_STATE(a,b);
      ORDERKEY_PRIO(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b); return 0; }
    static int compare_size(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_MEM(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_PCTCPU(a,b);
      ORDERKEY_CPTICKS(a,b); ORDERKEY_STATE(a,b); ORDERKEY_PRIO(a,b); return 0; }
    static int compare_res(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b); ORDERKEY_PCTCPU(a,b);
      ORDERKEY_CPTICKS(a,b); ORDERKEY_STATE(a,b); ORDERKEY_PRIO(a,b); return 0; }
    static int compare_time(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_CPTICKS(a,b); ORDERKEY_PCTCPU(a,b); ORDERKEY_STATE(a,b);
      ORDERKEY_PRIO(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b); return 0; }
    static int compare_prio(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_PRIO(a,b); ORDERKEY_CPTICKS(a,b); ORDERKEY_PCTCPU(a,b);
      ORDERKEY_STATE(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b); return 0; }
    static int compare_threads(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_THREADS(a,b); ORDERKEY_PCTCPU(a,b); ORDERKEY_CPTICKS(a,b);
      ORDERKEY_STATE(a,b); ORDERKEY_PRIO(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b);
      return 0; }
    static int compare_jid(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_JID(a,b); ORDERKEY_PCTCPU(a,b); ORDERKEY_CPTICKS(a,b);
      ORDERKEY_STATE(a,b); ORDERKEY_PRIO(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b);
      return 0; }
    static int compare_swap(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; ORDERKEY_SWAP(a,b); ORDERKEY_PCTCPU(a,b); ORDERKEY_CPTICKS(a,b);
      ORDERKEY_STATE(a,b); ORDERKEY_PRIO(a,b); ORDERKEY_RSSIZE(a,b); ORDERKEY_MEM(a,b);
      return 0; }
    static int compare_pid(const void *arg1, const void *arg2)
    { CMP_PROLOGUE; return a->ki_pid - b->ki_pid; }
    static int compare_zero(const void *arg1, const void *arg2)
    { (void)arg1; (void)arg2; return 0; }
    
    int (*compares[])(const void *, const void *) = {
        compare_cpu,
        compare_size,
        compare_res,
        compare_time,
        compare_prio,
        compare_threads,
        compare_zero,   /* total */
        compare_zero,   /* read  */
        compare_zero,   /* write */
        compare_zero,   /* fault */
        compare_zero,   /* vcsw  */
        compare_zero,   /* ivcsw */
        compare_jid,
        compare_swap,
        compare_pid,
        NULL
    };
    YOS_MACHINE_EOF


    # ── minimal text-only libxo shim ──────────────────────────────────
    # df, wc (and other FreeBSD utilities) drive output through
    # libxo, which can emit text / XML / JSON / HTML. We only need
    # text mode in the wasm sandbox. Real libxo is 8.5kloc + locale +
    # encoder plugins; instead we ship a 200-line shim that parses
    # the `{role:name/text-fmt/enc-fmt}` token grammar enough to feed
    # the text-fmt + remaining varargs to vprintf. Tools link this
    # alongside their own object files; the symbols never resolve as
    # imports.
    mkdir -p "$STAGE/libxo/libxo"
    cat > "$STAGE/libxo/libxo/xo.h" <<'XOH_EOF'
    #ifndef _YOS_LIBXO_H_
    #define _YOS_LIBXO_H_
    #include <stdarg.h>
    #include <stdio.h>
    typedef struct yos_xo_handle xo_handle_t;
    /* Style/flag enums consumers reference; values irrelevant here. */
    enum { XO_STYLE_TEXT = 0, XO_STYLE_XML, XO_STYLE_JSON, XO_STYLE_HTML };
    /* Flags. */
    #define XOF_WARN          0x00000001
    #define XOF_FLUSH         0x00000002
    #define XOF_PRETTY        0x00000004
    #define XOF_DTRT          0x00000008
    int          xo_parse_args(int argc, char **argv);
    xo_handle_t *xo_create_to_file(FILE *fp, unsigned style, unsigned flags);
    int          xo_finish(void);
    int          xo_emit(const char *fmt, ...);
    int          xo_emit_h(xo_handle_t *xop, const char *fmt, ...);
    void         xo_attr(const char *name, const char *fmt, ...);
    void         xo_open_container(const char *name);
    void         xo_close_container(const char *name);
    void         xo_open_list(const char *name);
    void         xo_close_list(const char *name);
    void         xo_open_instance(const char *name);
    void         xo_close_instance(const char *name);
    void         xo_warn(const char *fmt, ...);
    void         xo_warnx(const char *fmt, ...);
    void         xo_err(int eval, const char *fmt, ...);
    void         xo_errx(int eval, const char *fmt, ...);
    void         xo_error(const char *fmt, ...);
    #endif
    XOH_EOF
    cat > "$STAGE/libxo/yos_libxo_shim.c" <<'XOC_EOF'
    /* yos minimal libxo shim — text-only.
     *
     * Translates xo_emit("{role:name/text-fmt/enc-fmt}…", ...)
     * into a single vprintf() of the concatenated text-fmts plus
     * any literal characters between tokens. xo_attr / xo_open_*
     * etc. are no-ops in text mode (they only matter for XML / JSON
     * / HTML emit paths we don't ship). */
    #include <err.h>
    #include <errno.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <libxo/xo.h>

    int xo_parse_args(int argc, char **argv) { (void)argv; return argc; }
    xo_handle_t *xo_create_to_file(FILE *fp, unsigned s, unsigned f)
    { (void)fp; (void)s; (void)f; return (xo_handle_t *)1; }
    int xo_finish(void) { fflush(stdout); return 0; }
    void xo_attr(const char *n, const char *f, ...)
    { (void)n; (void)f; }
    void xo_open_container(const char *n)  { (void)n; }
    void xo_close_container(const char *n) { (void)n; }
    void xo_open_list(const char *n)       { (void)n; }
    void xo_close_list(const char *n)      { (void)n; }
    void xo_open_instance(const char *n)   { (void)n; }
    void xo_close_instance(const char *n)  { (void)n; }

    static void
    yos_xo_translate(const char *fmt, char *out, size_t outsz,
                     const char *defaults_for_T)
    {
        (void)defaults_for_T;
        size_t op = 0;
        while (*fmt && op + 1 < outsz) {
            if (*fmt != '{') {
                out[op++] = *fmt++;
                continue;
            }
            fmt++;
            /* role chars: zero or more letters before ':' */
            while (*fmt && *fmt != ':' && *fmt != '/' && *fmt != '}') fmt++;
            if (*fmt == ':') fmt++;
            /* name: chars up to '/' or '}' — emitted verbatim if no
             * format spec follows, otherwise discarded. */
            const char *name = fmt;
            while (*fmt && *fmt != '/' && *fmt != '}') fmt++;
            size_t nlen = (size_t)(fmt - name);
            if (*fmt == '/') {
                /* text-fmt — copy verbatim into out. */
                fmt++;
                while (*fmt && *fmt != '/' && *fmt != '}' && op + 1 < outsz) {
                    out[op++] = *fmt++;
                }
                /* enc-fmt — discard. */
                if (*fmt == '/') {
                    fmt++;
                    while (*fmt && *fmt != '}') fmt++;
                }
            } else if (nlen > 0) {
                size_t n = nlen < (outsz - op - 1) ? nlen : (outsz - op - 1);
                memcpy(out + op, name, n);
                op += n;
            }
            if (*fmt == '}') fmt++;
        }
        out[op] = 0;
    }

    int xo_emit(const char *fmt, ...)
    {
        char buf[8192];
        va_list ap;
        yos_xo_translate(fmt, buf, sizeof(buf), NULL);
        va_start(ap, fmt);
        int rc = vprintf(buf, ap);
        va_end(ap);
        return rc;
    }

    int xo_emit_h(xo_handle_t *xop, const char *fmt, ...)
    {
        (void)xop;
        char buf[8192];
        va_list ap;
        yos_xo_translate(fmt, buf, sizeof(buf), NULL);
        va_start(ap, fmt);
        int rc = vprintf(buf, ap);
        va_end(ap);
        return rc;
    }

    /* err/warn-style: forward to libyos_stubs's err/warn (linked
     * into every tool via -lyos_stubs). */
    void xo_warn(const char *fmt, ...)
    {
        int saved = errno;
        va_list ap; va_start(ap, fmt);
        vwarn(fmt, ap);
        va_end(ap);
        errno = saved;
    }
    void xo_warnx(const char *fmt, ...)
    { va_list ap; va_start(ap, fmt); vwarnx(fmt, ap); va_end(ap); }
    void xo_err(int eval, const char *fmt, ...)
    { va_list ap; va_start(ap, fmt); verr(eval, fmt, ap); va_end(ap); }
    void xo_errx(int eval, const char *fmt, ...)
    { va_list ap; va_start(ap, fmt); verrx(eval, fmt, ap); va_end(ap); }
    void xo_error(const char *fmt, ...)
    { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
    XOC_EOF

    # ── archive-compat: guest headers for the libarchive tools ────────
    # bsdcat #includes <archive.h>/<archive_entry.h> and (via
    # *_platform.h) "config.h". We provide minimal versions: archive.h
    # declares only the functions bsdcat uses, each carrying the
    # import_module("env")/import_name attribute so the wasm guest emits
    # env.archive_* imports (resolved by impl/libc/libarchive.c); the
    # config.h defines just the seven HAVE_* the contrib sources check.
    mkdir -p "$STAGE/archive-compat"
    cat > "$STAGE/archive-compat/config.h" <<'ARC_CONFIG_EOF'
    #ifndef YOS_BSDCAT_CONFIG_H
    #define YOS_BSDCAT_CONFIG_H
    /* Minimal autotools config for bsdcat/cmdline/lafe_err — only the
     * HAVE_* macros those three translation units actually test. */
    #define HAVE_ERRNO_H   1
    #define HAVE_SIGACTION 1
    #define HAVE_SIGNAL_H  1
    #define HAVE_STDARG_H  1
    #define HAVE_STDLIB_H  1
    #define HAVE_STRING_H  1
    #define HAVE_UNISTD_H  1
    /* bsdcat.c prints this in --version; the FreeBSD Makefile greps it
     * out of contrib archive.h (ARCHIVE_VERSION_ONLY_STRING). */
    #define BSDCAT_VERSION_STRING "3.8.5"
    #endif
    ARC_CONFIG_EOF
    cat > "$STAGE/archive-compat/archive_entry.h" <<'ARC_ENTRY_EOF'
    #ifndef YOS_GUEST_ARCHIVE_ENTRY_H
    #define YOS_GUEST_ARCHIVE_ENTRY_H
    /* bsdcat only passes &entry to archive_read_next_header and never
     * dereferences it, so the opaque forward decl is enough. */
    struct archive_entry;
    #endif
    ARC_ENTRY_EOF
    cat > "$STAGE/archive-compat/archive.h" <<'ARC_H_EOF'
    #ifndef YOS_GUEST_ARCHIVE_H
    #define YOS_GUEST_ARCHIVE_H
    #include <stddef.h>   /* size_t */
    typedef long long la_int64_t;
    #define ARCHIVE_EOF     1
    #define ARCHIVE_OK      0
    #define ARCHIVE_RETRY (-10)
    #define ARCHIVE_WARN  (-20)
    #define ARCHIVE_FAILED (-25)
    #define ARCHIVE_FATAL (-30)
    struct archive;
    struct archive_entry;
    /* Each function declares itself a wasm import on env.<name>; clang
     * -target wasm32 emits the import from the attribute alone. */
    #define YA_IMPORT(n) __attribute__((import_module("env"), import_name(#n)))
    YA_IMPORT(archive_read_new)                  struct archive *archive_read_new(void);
    YA_IMPORT(archive_read_support_filter_all)   int archive_read_support_filter_all(struct archive *);
    YA_IMPORT(archive_read_support_format_empty) int archive_read_support_format_empty(struct archive *);
    YA_IMPORT(archive_read_support_format_raw)   int archive_read_support_format_raw(struct archive *);
    YA_IMPORT(archive_read_open_filename)        int archive_read_open_filename(struct archive *, const char *, size_t);
    YA_IMPORT(archive_read_next_header)          int archive_read_next_header(struct archive *, struct archive_entry **);
    YA_IMPORT(archive_read_data_into_fd)         la_int64_t archive_read_data_into_fd(struct archive *, int);
    YA_IMPORT(archive_read_close)                int archive_read_close(struct archive *);
    YA_IMPORT(archive_read_free)                 int archive_read_free(struct archive *);
    YA_IMPORT(archive_error_string)              const char *archive_error_string(struct archive *);
    YA_IMPORT(archive_version_details)           const char *archive_version_details(void);
    #endif
    ARC_H_EOF

    # ── telnet-compat: <curses.h>/<term.h> stubs ─────────────────────
    # telnet.c includes <curses.h> + <term.h> but defines its OWN
    # setupterm() (wrapping tgetent, which the sysroot libyos_stubs
    # provides) — so these only need to exist and pull in bool. We
    # deliberately do NOT declare setupterm here (telnet defines it).
    mkdir -p "$STAGE/telnet-compat"
    cat > "$STAGE/telnet-compat/curses.h" <<'TEL_CURSES_EOF'
    #ifndef YOS_TELNET_CURSES_H
    #define YOS_TELNET_CURSES_H
    #include <stdbool.h>
    #include <stdio.h>
    #endif
    TEL_CURSES_EOF
    cat > "$STAGE/telnet-compat/term.h" <<'TEL_TERM_EOF'
    #ifndef YOS_TELNET_TERM_H
    #define YOS_TELNET_TERM_H
    /* termcap globals/prototypes come from the sysroot <termcap.h>;
     * telnet provides its own setupterm(). This stub just satisfies the
     * <term.h> include. */
    #include <termcap.h>
    #endif
    TEL_TERM_EOF

    # ── run the build for each tool ───────────────────────────────────
    ${lib.concatStrings (lib.mapAttrsToList toolBuildScript tools)}

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out
    cp -r out/bin     $out/bin
    cp -r out/libexec $out/libexec

    # Per-tool metadata for diffability against the upstream Makefiles.
    {
      echo "yos-freebsd-tools ${freebsd-src.version}"
      echo
      ${lib.concatStrings (lib.mapAttrsToList (pname: cfg: ''
        echo "  ${pname}    src=usr/src/${cfg.srcDir}    srcs=${
          lib.concatStringsSep " " (cfg.srcs or [ "${pname}.c" ])
        }"
      '') tools)}
    } > $out/manifest.txt

    runHook postInstall
  '';

  meta = with lib; {
    description = "FreeBSD-base userland (cat, echo, ls, …) ported to wasm32 / yos";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };

  passthru = {
    # The list of tools we provide — useful for downstream code that
    # wants to enumerate them without re-parsing the derivation.
    toolNames = lib.attrNames tools;
  };
}
