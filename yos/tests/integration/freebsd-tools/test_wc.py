"""wc(1): counts lines / words / chars from stdin or files.

Status: lines + bytes correct; word count broken (always 1).

The line and byte counters are pure pointer arithmetic + newline
detection — those work. The word counter calls isspace() on each
byte to detect word boundaries, and isspace returns 0 for every
byte due to the rune-locale gap (see test_awk.py for the root cause).
Result: every input is counted as a single word run.

Asserted as a partial PASS — line count is correct, byte count is
correct, word count is documented broken. When the locale fix lands,
tighten this test to also assert word count.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    # 5 lines, 7 words ("one"+"two three"+"four"+"five six"+"seven" =
    # 1+2+1+2+1 = 7), 34 bytes (incl. newlines). The previous test
    # text claimed 8 words, but counting them gives 7 — the rune-
    # locale fix surfaced the off-by-one in the comment, not in wc.
    text = b"one\ntwo three\nfour\nfive six\nseven\n"
    r = run(yos, libexec, "wc", stdin=text)
    out = r.stdout.decode().split()
    if len(out) < 3:
        print(f"FAIL: wc stdout shape: {r.stdout!r}")
        sys.exit(1)
    lines, words, bytes_ = int(out[0]), int(out[1]), int(out[2])
    if lines != 5:
        print(f"FAIL: wc -l counted {lines}, want 5")
        sys.exit(1)
    if bytes_ != len(text):
        print(f"FAIL: wc -c counted {bytes_}, want {len(text)}")
        sys.exit(1)
    if words != 7:
        print(f"FAIL: wc words={words}, expected 7")
        sys.exit(1)
    print(f"PASS: wc ({lines} lines, {words} words, {bytes_} bytes)")


if __name__ == "__main__":
    main()
