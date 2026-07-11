# Markdown, rendered in the terminal

This page is parsed by **ymarkdown** and drawn with **ydraw** — real
typography inside the terminal grid, scrolling with your shell.

## Text styles

Regular, **bold**, *italic*, and `inline code`. Links look like
[yetty.dev](https://yetty.dev).

## Lists

1. Ordered items
2. Nest as you like
   - unordered children
   - with more levels

## Code

```c
struct yetty_ycore_void_result yetty_hello(void)
{
    printf("terminal, unchained\n");
    return YETTY_OK_VOID();
}
```

## Quote

> One wasm binary, every platform — the terminal carries its own
> userspace with it.

---

Rendered by `ycat` running as a wasm32 guest under **yos**.
