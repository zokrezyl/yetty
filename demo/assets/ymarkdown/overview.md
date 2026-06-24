# ymarkdown

Markdown rendered straight into the **ydraw layer** — headings, lists, code,
tables and inline styles become MSDF glyph runs and SDF rules that *scroll with
the terminal*.

## Inline styles

You get **bold**, *italic*, ***bold italic***, `inline code`, and
[links](https://example.com) in a single paragraph.

## Lists

- unordered item
- nested:
  - second level
  - second level again
- back to the top level

1. ordered item
2. ordered item
3. ordered item

## Code block

```c
struct yetty_ycore_void_result yetty_demo_run(void)
{
    return YETTY_OK_VOID();
}
```

## Table

| Feature   | Backend   | Scrolls |
|-----------|-----------|---------|
| headings  | MSDF font | yes     |
| rules     | SDF line  | yes     |
| code      | MSDF mono | yes     |

## Blockquote

> Rich content sits beside text and shares one scroll origin —
> that is the whole point of yetty.
