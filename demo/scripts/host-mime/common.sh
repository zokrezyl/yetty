# host-mime demos — shared prolog. Sourced by the per-type scripts.
#
# THE POINT: no client tool is needed. A YETTY_DCS_MIME_FILE envelope is
# plain-printable — bash + base64 are enough, because the payload may be
# sent UNCOMPRESSED (the terminal detects an LZ4F frame by its magic and
# passes anything else through as-is). The whole wire format is:
#
#   ESC P 600005 y <b64(32-byte meta)> ; <b64(prologue + file bytes)> ESC \
#
# meta (32 bytes, little-endian):
#   "IFIL" | version=1 | compressed=0 | algo=0 | flags=3 (FIRST|LAST) |
#   stream_id=0 | sequence=0 | total_raw_size=0 (unknown) |
#   chunk_raw_size=0 | reserved=0
# With all sizes "unknown" the meta is the same 32 bytes for EVERY file,
# so it is encoded once below.
#
# prologue (head of the payload):
#   u16 total_len | u8 mime_len + mime | u8 name_len + name |
#   u16 args_len + args
# The basename alone is enough of a hint — the terminal detects the type
# from the extension and the content itself.
#
# The base64 must be a single line: the terminal's streaming decoder
# treats a quartet containing '\n' as invalid, hence `tr -d '\n'`.

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
ASSETS_ROOT="$ROOT/demo/assets"
PAUSE="${DEMO_PAUSE:-0}"

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

# The 32-byte file meta is constant (sizes unknown = 0) — encode it once.
META_B64="$({ printf 'IFIL\x01\x00\x00\x03'; head -c 24 /dev/zero; } | base64 | tr -d '\n')"

# Two raw bytes, u16 little-endian.
u16le() { printf "$(printf '\\x%02x\\x%02x' $(($1 & 255)) $(($1 >> 8)))"; }

# prologue <basename> — no mime hint (extension + content sniff suffice),
# no render args.
prologue() {
    local name="$1"
    u16le $((2 + 1 + 0 + 1 + ${#name} + 2))
    printf '\x00'
    printf "$(printf '\\x%02x' "${#name}")"
    printf '%s' "$name"
    printf '\x00\x00'
}

# show <file> — ship the raw file to the terminal, straight from bash.
show() {
    local file="$1"
    local name
    name="$(basename "$file")"
    echo "  > ${file#"$ROOT"/}  (raw bytes on the wire; yetty renders)"
    printf '\033P600005y%s;' "$META_B64"
    { prologue "$name"; cat "$file"; } | base64 | tr -d '\n'
    printf '\033\\'
    p
}
