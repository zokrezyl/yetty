#!/usr/bin/env bash
#
# yflame.sh — record, collapse, and render a flame graph inline in yetty.
#
# One command from "what is my program doing" to a flame graph in the
# terminal. Wraps `perf record` + `perf script` + a stack collapser + the
# `yflame` renderer so you never have to remember the pipeline.
#
#   yflame.sh -- ./my_program --args        # profile a command
#   yflame.sh -p 4242                        # profile a running PID for 10s
#   yflame.sh -a -d 5                        # whole system for 5s
#   yflame.sh -i perf.data                   # render an existing capture
#   yflame.sh -F profile.folded              # render existing folded stacks
#   any-profiler --collapsed | yflame.sh     # render folded stacks from stdin
#   yflame.sh --demo                         # synthetic data, no perf needed
#
# Status goes to stderr; the OSC envelope (the picture) goes to stdout, so
# inside a yetty terminal the graph just appears. Run it under yetty, e.g.
#   yetty -e 'yflame.sh -- ./my_program'
#
# Licence note: this script SHELLS OUT to the user's `perf` (GPL) as a
# separate process and bundles no profiler code. The built-in collapser is
# original; pass --stackcollapse to use FlameGraph's stackcollapse-perf.pl.

set -euo pipefail

prog="$(basename "$0")"

# ---- pretty status helpers (stderr only) ----------------------------------
if [ -t 2 ]; then
    c_dim=$'\033[2m'; c_bold=$'\033[1m'; c_grn=$'\033[32m'; c_yel=$'\033[33m'
    c_red=$'\033[31m'; c_rst=$'\033[0m'
else
    c_dim=; c_bold=; c_grn=; c_yel=; c_red=; c_rst=
fi
say()  { printf '%s%s%s\n' "$c_dim" "$*" "$c_rst" >&2; }
step() { printf '%s▸ %s%s\n' "$c_grn" "$*" "$c_rst" >&2; }
warn() { printf '%s! %s%s\n' "$c_yel" "$*" "$c_rst" >&2; }
die()  { printf '%s✗ %s%s\n' "$c_red" "$*" "$c_rst" >&2; exit 1; }

usage() {
    cat >&2 <<EOF
${c_bold}yflame.sh${c_rst} — record, collapse, and render a flame graph in yetty.

${c_bold}INPUT (choose one; auto-detected from flags):${c_rst}
  -- <cmd> [args...]     profile a command until it exits
  -p, --pid <pid>        profile a running process for --duration seconds
  -a, --all              profile the whole system for --duration seconds
  -i, --input <perf.data>   render an existing perf.data capture
  -F, --folded <file>    render existing folded stacks (skip perf entirely)
      (stdin)            if data is piped in, it is read as folded stacks
      --demo             render built-in synthetic data (no perf needed)

${c_bold}CAPTURE OPTIONS:${c_rst}
  -d, --duration <sec>   duration for --pid/--all (default 10)
      --freq <hz>        sampling frequency (default 997)
      --fp               frame-pointer stacks (default: --call-graph dwarf)
      --perf-data <file> keep the perf.data here (default: temp, removed)
      --keep-folded <file>  also write the collapsed folded stacks here
      --stackcollapse <path>  use an external stackcollapse-perf.pl

${c_bold}RENDER OPTIONS (passed to yflame):${c_rst}
  -w, --width <px>       graph width (default 1600)
      --frame-height <px>   row height (default 18)
      --icicle           root at the top, growing downward
      --no-labels        omit frame-name labels

${c_bold}OTHER:${c_rst}
      --view             when the profiled program exits, open the flame graph
                         in a NEW yetty window. Use this to profile yetty
                         itself, or when you are not already inside yetty.
      --yetty <path>     yetty binary used by --view (default: auto/PATH)
      --dry-run          print the pipeline, run nothing
  -h, --help             this help

${c_bold}EXAMPLES:${c_rst}
  # Profile yetty itself: use it, quit it, and the flame graph pops up.
  yflame.sh --view -- yetty -e 'vim big.c'

  # Profile any program from inside a yetty session (renders inline on exit):
  yetty -e 'yflame.sh -- ./bench --iters 1e7'

  yflame.sh -p \$(pgrep -n myserver) -d 15 -w 2000
  sudo yflame.sh -a -d 5
  py-spy record --format raw -o /dev/stdout -- python app.py | yflame.sh

Privileges: --all and some --pid captures need root or a relaxed
kernel.perf_event_paranoid (\`sudo sysctl kernel.perf_event_paranoid=1\`).
EOF
}

# ---- locate the yflame renderer -------------------------------------------
resolve_yflame() {
    if [ -n "${YFLAME:-}" ] && [ -x "${YFLAME:-}" ]; then printf '%s' "$YFLAME"; return; fi
    if command -v yflame >/dev/null 2>&1; then command -v yflame; return; fi
    local here root
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    root="$(cd "$here/../.." && pwd)"
    for b in "$root"/build-desktop-*/tools/yflame/yflame "$root"/build-*/tools/yflame/yflame; do
        [ -x "$b" ] && { printf '%s' "$b"; return; }
    done
    die "yflame binary not found (set \$YFLAME, or build it, or put it on PATH)"
}

# ---- minimal built-in perf-script collapser -------------------------------
# Reads `perf script` output on stdin, prints folded stacks on stdout.
# Original implementation (not FlameGraph's). For full fidelity (inlining,
# annotations, kernel/user split) pass --stackcollapse stackcollapse-perf.pl.
builtin_collapse() {
    awk '
        function flush() {
            if (depth > 0) {
                s = stack[depth]
                for (i = depth - 1; i >= 1; i--) s = s ";" stack[i]
                count[s]++
            }
            depth = 0
        }
        /^[[:space:]]*$/ { flush(); next }   # blank line ends a sample
        /^[^[:space:]]/  { next }            # non-indented header line
        {
            sym = $2                          # frame: <addr> <symbol>+<off> (dso)
            sub(/\+0x[0-9a-fA-F]+$/, "", sym)
            if (sym == "" || sym == "[unknown]") sym = "[unknown]"
            stack[++depth] = sym
        }
        END { flush(); for (s in count) print s, count[s] }
    '
}

# ---- arg parsing ----------------------------------------------------------
mode=""             # cmd | pid | all | input | folded | demo | stdin
pid=""; input=""; folded=""; duration=10; freq=997; call_graph="dwarf"
perf_data=""; keep_folded=""; stackcollapse=""; dry_run=0
width=1600; frame_height=""; icicle=0; no_labels=0
view=0; yetty_bin=""
declare -a command=()

while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; command=("$@"); mode="cmd"; break ;;
        -p|--pid)        pid="$2"; mode="pid"; shift 2 ;;
        -a|--all)        mode="all"; shift ;;
        -i|--input)      input="$2"; mode="input"; shift 2 ;;
        -F|--folded)     folded="$2"; mode="folded"; shift 2 ;;
        --demo)          mode="demo"; shift ;;
        -d|--duration)   duration="$2"; shift 2 ;;
        --freq)          freq="$2"; shift 2 ;;
        --fp)            call_graph="fp"; shift ;;
        --perf-data)     perf_data="$2"; shift 2 ;;
        --keep-folded)   keep_folded="$2"; shift 2 ;;
        --stackcollapse) stackcollapse="$2"; shift 2 ;;
        -w|--width)      width="$2"; shift 2 ;;
        --frame-height)  frame_height="$2"; shift 2 ;;
        --icicle)        icicle=1; shift ;;
        --no-labels)     no_labels=1; shift ;;
        --view)          view=1; shift ;;
        --yetty)         yetty_bin="$2"; shift 2 ;;
        --dry-run)       dry_run=1; shift ;;
        -h|--help)       usage; exit 0 ;;
        -*)              usage; die "unknown option: $1" ;;
        *)               command=("$@"); mode="cmd"; break ;;
    esac
done

# Default input: piped stdin → folded; otherwise show help.
if [ -z "$mode" ]; then
    if [ ! -t 0 ]; then mode="stdin"; else usage; exit 2; fi
fi

# ---- assemble render flags ------------------------------------------------
render_args=( -w "$width" )
[ -n "$frame_height" ] && render_args+=( --frame-height "$frame_height" )
[ "$icicle" = 1 ]      && render_args+=( --icicle )
[ "$no_labels" = 1 ]   && render_args+=( --no-labels )

YFLAME_BIN="$(resolve_yflame)"

# ---- collapse selector ----------------------------------------------------
collapse() {
    if [ -n "$stackcollapse" ]; then "$stackcollapse"; else builtin_collapse; fi
}

# ---- temp management ------------------------------------------------------
# Single EXIT trap that preserves the real exit status (`rc=$?` first) while
# removing whatever temps were created. Both names start empty so the trap is
# safe even if we exit before they are assigned.
tmp_perf=""; folded_tmp=""; osc_tmp=""; view_script=""
trap 'rc=$?; for f in "$tmp_perf" "$folded_tmp" "$osc_tmp" "$view_script"; do [ -n "$f" ] && rm -f "$f"; done; exit $rc' EXIT

need_perf() { command -v perf >/dev/null 2>&1 || die "perf not found (install linux-perf / linux-tools)"; }

# Locate a yetty binary to view the graph in (for --view).
resolve_yetty() {
    if [ -n "$yetty_bin" ] && [ -x "$yetty_bin" ]; then printf '%s' "$yetty_bin"; return; fi
    if [ -n "${YETTY:-}" ] && [ -x "${YETTY:-}" ]; then printf '%s' "$YETTY"; return; fi
    if command -v yetty >/dev/null 2>&1; then command -v yetty; return; fi
    local here root
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    root="$(cd "$here/../.." && pwd)"
    for b in "$root"/build-desktop-*/yetty "$root"/build-*/yetty; do
        [ -x "$b" ] && { printf '%s' "$b"; return; }
    done
    die "yetty binary not found for --view (set --yetty <path> or \$YETTY)"
}

# Resolve where perf.data should live.
resolve_perf_data() {
    if [ -n "$perf_data" ]; then printf '%s' "$perf_data";
    else tmp_perf="$(mktemp -t yflame-XXXXXX.data)"; printf '%s' "$tmp_perf"; fi
}

run_record() {  # $1=perf.data path; remaining: the `perf record` target args
    local out="$1"; shift
    local -a rec=( perf record -F "$freq" --call-graph "$call_graph" -o "$out" "$@" )
    step "recording: ${rec[*]}"
    if [ "$dry_run" = 1 ]; then return 0; fi
    "${rec[@]}" >&2
}

# ---- produce folded stacks on fd 'folded_out' (a temp), per mode ----------
emit_folded() {  # writes folded stacks to stdout
    case "$mode" in
        demo)
            cat <<'EOF'
main;parse;lex 60
main;parse;expand 18
main;parse;intern 9
main;typecheck;unify 40
main;typecheck;solve 25
main;codegen;lower 55
main;codegen;regalloc;spill 30
main;codegen;regalloc;color 22
main;codegen;emit 14
main;link;gc 12
main;link;reloc 8
main 6
EOF
            ;;
        folded)
            [ -r "$folded" ] || die "cannot read folded file: $folded"
            cat "$folded" ;;
        stdin)
            cat ;;
        input)
            need_perf
            [ -r "$input" ] || die "cannot read perf.data: $input"
            step "perf script -i $input | collapse"
            [ "$dry_run" = 1 ] && return 0
            perf script -i "$input" 2>/dev/null | collapse ;;
        cmd)
            need_perf
            [ "${#command[@]}" -gt 0 ] || die "no command given after --"
            local pd; pd="$(resolve_perf_data)"
            run_record "$pd" -- "${command[@]}"
            [ "$dry_run" = 1 ] && return 0
            step "perf script | collapse"
            perf script -i "$pd" 2>/dev/null | collapse ;;
        pid)
            need_perf
            [ -n "$pid" ] || die "no pid given"
            local pd; pd="$(resolve_perf_data)"
            say "profiling pid $pid for ${duration}s"
            run_record "$pd" -p "$pid" -- sleep "$duration"
            [ "$dry_run" = 1 ] && return 0
            step "perf script | collapse"
            perf script -i "$pd" 2>/dev/null | collapse ;;
        all)
            need_perf
            local pd; pd="$(resolve_perf_data)"
            say "profiling the whole system for ${duration}s (needs privileges)"
            run_record "$pd" -a -- sleep "$duration"
            [ "$dry_run" = 1 ] && return 0
            step "perf script | collapse"
            perf script -i "$pd" 2>/dev/null | collapse ;;
        *) die "internal: bad mode '$mode'" ;;
    esac
}

if [ "$dry_run" = 1 ]; then
    step "would render with: $YFLAME_BIN ${render_args[*]}"
    emit_folded >/dev/null
    say "dry run complete"
    exit 0
fi

# ---- run: folded → (optional save) → render -------------------------------
folded_tmp="$(mktemp -t yflame-folded-XXXXXX.txt)"

emit_folded > "$folded_tmp"
nstacks="$(wc -l < "$folded_tmp" | tr -d ' ')"
[ "$nstacks" -gt 0 ] || die "no stacks collected (empty profile?)"
step "$nstacks unique stacks → rendering"

if [ -n "$keep_folded" ]; then
    cp "$folded_tmp" "$keep_folded"
    say "folded stacks saved to $keep_folded"
fi

if [ "$view" = 1 ]; then
    # Render to an envelope, then open it in a FRESH yetty window. This is the
    # mode for profiling yetty itself: the profiled instance is gone by now, so
    # a new one displays the result. The viewer cats the envelope, then drops
    # into a shell so you can scroll/inspect and close when done.
    YETTY_BIN="$(resolve_yetty)"
    osc_tmp="$(mktemp -t yflame-osc-XXXXXX.bin)"
    "$YFLAME_BIN" "${render_args[@]}" "$folded_tmp" > "$osc_tmp"
    view_script="$(mktemp -t yflame-view-XXXXXX.sh)"
    {
        printf '#!/usr/bin/env bash\n'
        printf 'cat %q\n' "$osc_tmp"
        printf 'printf "\\n\\033[2m── flame graph above · type exit to close ──\\033[0m\\n"\n'
        printf 'exec "${SHELL:-bash}"\n'
    } > "$view_script"
    chmod +x "$view_script"
    step "opening flame graph in a new yetty window"
    "$YETTY_BIN" -e "$view_script"
else
    "$YFLAME_BIN" "${render_args[@]}" "$folded_tmp"
fi
