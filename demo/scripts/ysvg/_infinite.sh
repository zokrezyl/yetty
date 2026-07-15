#!/bin/bash
# _infinite.sh — shared plumbing for the ysvg "infinite gallery" demos
# (wikimedia.sh, emoji.sh, icons.sh). Not a demo itself; the gallery scripts
# source it.
#
# It locates the ycat binary, provides show_url() to fetch + render one SVG
# through ycat's svg handler, and a small counter / pacing / offline-guard
# layer plus jsDelivr index helpers.
#
# Knobs (env):
#   DEMO_PAUSE=<seconds>  pause between figures (default 0.5; 0 = flat out)
#   DEMO_WIDTH=<cells>    card width in terminal cells (default 12; small
#                         thumbnails — raise for larger renders)
#   DEMO_COUNT=<n>        stop after n figures (default 0 = endless)
#   YCAT=<path>           override the ycat binary

# Resolve relative to THIS file so it works however it is sourced.
YSVG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$YSVG_DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
TMP="$ROOT/tmp"
USER_AGENT="yetty-ysvg-demo/1.0 (+https://github.com/zokrezyl/yetty)"
# Some art sites gate on a browser-looking User-Agent; use this when fetching
# bytes ourselves (openclipart.sh, svgrepo.sh).
BROWSER_UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"

PAUSE="${DEMO_PAUSE:-0.5}"
WIDTH="${DEMO_WIDTH:-12}"
COUNT="${DEMO_COUNT:-0}"

# SVGs the svg handler cannot parse are recorded here for later triage / test
# assets: their URLs are appended to FAIL_LOG and the raw bytes saved under
# FAIL_DIR (so you don't have to re-download them from a throttling host).
FAIL_LOG="${YSVG_FAIL_LOG:-$TMP/ysvg-parse-failures.txt}"
FAIL_DIR="${YSVG_FAIL_DIR:-$TMP/ysvg-parse-failures}"

mkdir -p "$TMP"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

for tool in curl python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required for the infinite ysvg demos but was not found" >&2
        exit 1
    }
done

shown_count=0
pause() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

# dbg MSG... — trace to stderr (so it never corrupts the OSC envelope on
# stdout). On by default; silence with DEMO_DEBUG=0.
DEMO_DEBUG="${DEMO_DEBUG:-1}"
dbg() { [ "$DEMO_DEBUG" = 0 ] || printf '[dbg] %s\n' "$*" >&2; }

# show_url URL [LABEL] — render one SVG. Never aborts the caller's loop.
# Returns 0 when the render command succeeds, 1 otherwise.
show_url() {
    local url="$1" label="${2:-}"
    echo
    echo "\$ ycat -w $WIDTH $url"
    [ -n "$label" ] && echo "  # $label"
    if "$YCAT" -c svg -w "$WIDTH" "$url"; then
        shown_count=$((shown_count + 1))
        pause
        return 0
    fi
    return 1
}

# more — false once DEMO_COUNT figures have been shown (COUNT=0 → always true).
more() { [ "$COUNT" -eq 0 ] || [ "$shown_count" -lt "$COUNT" ]; }

# show_svg_from_url URL [LABEL] [REJECT_SUBSTR] — fetch the SVG ourselves
# (browser UA, follow redirects) into a temp file, sanity-check it really is
# SVG, then render it through ycat. Use this for sources that redirect, gate on
# User-Agent, or hand back the odd non-SVG error page. When REJECT_SUBSTR is
# given and the final (post-redirect) URL contains it, the fetch is treated as
# a placeholder miss. SVGs the svg handler can't parse (ycat falls back to
# tree-sitter) are also skipped rather than dumped as raw text. Return codes:
# 0 = rendered, 2 = soft skip (placeholder, or unparseable — a valid fetch the
# caller should NOT count as a network failure), 1 = hard failure (curl error /
# non-200 / not SVG). Never aborts the caller's loop.
show_svg_from_url() {
    local url="$1" label="${2:-}" reject="${3:-}"
    local tmpfile="$TMP/ysvg-fetch-$$.svg"
    local wfile="$TMP/ysvg-w-$$.txt" cerr="$TMP/ysvg-curl-$$.txt"
    dbg "───────────────────────────────────────────────"
    dbg "fetch: $url"

    # -sS (not -s): keep curl's error message so we can see WHY a fetch fails.
    # Capture the write-out and curl's stderr to files so the exit code, http
    # code, byte count, timing and error text are all visible.
    curl -sS --max-time 25 -L -A "$BROWSER_UA" \
         -H 'Accept: image/svg+xml,image/*,*/*;q=0.8' \
         -o "$tmpfile" \
         -w '%{http_code} %{size_download} %{time_total} %{url_effective}' \
         "$url" > "$wfile" 2> "$cerr"
    local curl_rc=$?
    local code size ttime effective
    read -r code size ttime effective < "$wfile" 2>/dev/null
    local on_disk=0
    [ -f "$tmpfile" ] && on_disk=$(wc -c < "$tmpfile")
    dbg "curl_rc=$curl_rc http=${code:-?} bytes=${size:-?} on_disk=$on_disk time=${ttime:-?}s"
    dbg "effective=${effective:-?}"
    if [ "$curl_rc" -ne 0 ] && [ -s "$cerr" ]; then
        dbg "curl error: $(tr '\n' ' ' < "$cerr")"
    fi
    rm -f "$wfile" "$cerr"

    if [ "$curl_rc" -ne 0 ] || [ "$code" != 200 ]; then
        dbg "→ HARD FAIL (rc=1): curl_rc=$curl_rc http=${code:-?}"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -qi '<svg' "$tmpfile" 2>/dev/null; then
        dbg "→ HARD FAIL (rc=1): no '<svg' in body; head=[$(head -c 90 "$tmpfile" 2>/dev/null | tr '\n\t' '  ')]"
        rm -f "$tmpfile"
        return 1
    fi
    if [ -n "$reject" ] && [ "${effective#*"$reject"}" != "$effective" ]; then
        dbg "→ SKIP (rc=2): placeholder — effective URL contains '$reject'"
        rm -f "$tmpfile"   # redirected to a placeholder (e.g. site logo)
        return 2
    fi

    # Render into a buffer first: if the svg handler rejects it and ycat falls
    # back to tree-sitter, skip it instead of scrolling raw XML into the term.
    dbg "render: $YCAT -c svg -w $WIDTH ($on_disk bytes)"
    local outfile="$TMP/ysvg-out-$$.bin" errfile="$TMP/ysvg-err-$$.txt"
    "$YCAT" -c svg -w "$WIDTH" "$tmpfile" > "$outfile" 2> "$errfile"
    local rc=$?
    dbg "ycat_rc=$rc envelope=$(wc -c < "$outfile" 2>/dev/null || echo 0)B"
    [ -s "$errfile" ] && dbg "ycat stderr: $(tr '\n' ' ' < "$errfile")"
    if [ "$rc" -ne 0 ] || grep -qE 'parse failed|handler failed' "$errfile" 2>/dev/null; then
        # Record the failing SVG: URL → FAIL_LOG, bytes → FAIL_DIR/<slug>.svg.
        local slug
        slug=$(basename "${effective:-$url}")
        case "$slug" in *.svg) ;; *) slug="${slug:-svg}.svg" ;; esac
        mkdir -p "$FAIL_DIR"
        cp -f "$tmpfile" "$FAIL_DIR/$slug" 2>/dev/null
        printf '%s\t%s\n' "${effective:-$url}" "$url" >> "$FAIL_LOG"
        dbg "→ SKIP (rc=2): svg handler rejected it → logged to $FAIL_LOG, saved $FAIL_DIR/$slug"
        rm -f "$tmpfile" "$outfile" "$errfile"
        return 2
    fi
    rm -f "$tmpfile"

    dbg "→ RENDER (rc=0): emitting envelope"
    echo
    echo "\$ ycat -w $WIDTH $url"
    [ -n "$label" ] && echo "  # $label"
    cat "$outfile"
    rm -f "$outfile" "$errfile"
    shown_count=$((shown_count + 1))
    pause
}

# jsdelivr_index PKG PREFIX — print full CDN URLs of every *.svg under PREFIX
# in the latest published version of npm package PKG. Empty output on failure.
jsdelivr_index() {
    local pkg="$1" prefix="$2" version
    version=$(curl -s --max-time 20 -A "$USER_AGENT" \
                "https://data.jsdelivr.com/v1/packages/npm/$pkg" \
              | python3 -c 'import sys, json
try:
    print(json.load(sys.stdin)["versions"][0]["version"])
except Exception:
    pass')
    [ -n "$version" ] || return 0
    curl -s --max-time 40 -A "$USER_AGENT" \
        "https://data.jsdelivr.com/v1/packages/npm/$pkg@$version?structure=flat" \
      | python3 -c 'import sys, json
pkg, version, prefix = sys.argv[1:4]
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
base = "https://cdn.jsdelivr.net/npm/%s@%s" % (pkg, version)
for entry in data.get("files", []):
    name = entry.get("name", "")
    if name.startswith(prefix) and name.endswith(".svg"):
        print(base + name)' "$pkg" "$version" "$prefix"
}

# cache_index CACHEFILE PKG PREFIX — ensure CACHEFILE holds the URL list.
# Returns 0 when the cache is non-empty.
cache_index() {
    local cache="$1" pkg="$2" prefix="$3"
    if [ ! -s "$cache" ]; then
        jsdelivr_index "$pkg" "$prefix" > "$cache" 2>/dev/null || true
    fi
    [ -s "$cache" ]
}

# random_line FILE — print one uniformly-random line of FILE.
random_line() {
    local file="$1" total line_number
    total=$(wc -l < "$file")
    [ "$total" -gt 0 ] || return 1
    # Two draws widen $RANDOM's 0..32767 range to cover larger indexes.
    line_number=$(( (RANDOM * 32768 + RANDOM) % total + 1 ))
    sed -n "${line_number}p" "$file"
}
