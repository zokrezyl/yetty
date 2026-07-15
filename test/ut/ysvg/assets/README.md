# ysvg end-tag-whitespace regression assets

Real-world SVGs (and one minimal synthetic reduction) that the ysvg parser
**used to reject**, even though they are well-formed XML that browsers,
`xmllint`, and Python's `xml.etree` all accept. Captured from Openclipart
(public domain) while exercising `demo/scripts/ysvg/openclipart.sh`. **They now
parse** — these files are the regression pin for that fix.

## The bug (fixed)

`yetty_ysvg_render` used to fail these with `ysvg-parse: yxml syntax error`. The
underlying XML parser (vendored `yxml`, Yorhel/yxml — a prebuilt static lib)
rejects **whitespace between the element name and `>` in the root element's
end tag**:

| end tag        | yxml verdict |
|----------------|--------------|
| `</svg>`        | OK           |
| `</svg >`       | **rejected** |
| `</svg␤   >`    | **rejected** |
| `</g␤></svg>`   | OK (whitespace in an *inner* end tag is fine) |

The XML grammar explicitly permits it: `ETag ::= '</' Name S? '>'` (the `S?`
is optional whitespace). So this is a strictness bug in the XML layer, not an
unsupported SVG feature. Only the **root** element's end tag triggers it —
inner end tags with the same whitespace parse fine — so the failure always
lands on the final `>` of `</svg␤   >`, near EOF.

Inkscape/Sodipodi pretty-print their `</…>` closers on their own indented
line, so a large fraction of real Openclipart art (~24% in a 132-file sample)
hits this and falls back to the raw/tree-sitter path instead of rendering.

## The files

- `root-endtag-whitespace.svg` — minimal synthetic reduction. Identical to a
  valid SVG except the root closes as `</svg␤>`. This is the crisp pin.
- `nicubunu-Woman-Silhouette-14.svg`, `simple-booth.svg`, `go-bottom.svg` —
  small real Openclipart exports (public domain) exhibiting the same tail.

## Reproduce

```sh
build-desktop-ytrace-release/test/ut/ysvg/ysvg_diag-test test/ut/ysvg/assets/*.svg
```

`ysvg_diag-test` (see `../svg-diag.c`) prints the full error cause chain per
file. All four now report `OK`.

## The fix

`yetty_ysvg_parse` (in `src/yetty/ysvg/ysvg-parse.c`) retries once with end-tag
whitespace normalized away (`normalize_endtag_ws`) when the strict yxml pass
fails — cheap, and only on the already-failed path, so well-formed documents
pay nothing. Comment/CDATA spans are copied verbatim. `svg-test.c` pins it with
`test_endtag_whitespace_parses`.
