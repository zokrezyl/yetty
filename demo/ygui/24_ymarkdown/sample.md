# ymarkdown sample

This file lives next to the demo's `main.c`. Run the demo with this
path to render it instead of the inline default:

```
demo-ygui-24-ymarkdown demo/ygui/24_ymarkdown/sample.md
```

## Inline styles

- **bold**, *italic*, ***bold-italic***
- inline `code` runs
- ~~struck-through~~ text
- a [link](https://example.com) rendered in the accent colour

## Lists

1. first ordered item
2. second ordered item
3. third ordered item

- bullets still work
- with **emphasis** inside

## Blockquote

> Terminals can show rich content.
> > Even nested quotes get their own gutter bar.

## Table

| Feature       | Status | Notes                |
|:--------------|:------:|---------------------:|
| Headings      |   ok   |          six levels  |
| Tables        |   ok   |   aligned + bordered |
| Code blocks   |   ok   |       shared panel   |
| Links         |   ok   |        text only     |

## Code block

```
fn main() {
    println!("hello, ymarkdown");
}
```

---

That horizontal rule marks the end of the sample.
