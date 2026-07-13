# yexpr — arena-based math expression parser

yexpr parses mathematical expressions into a small AST allocated from a
caller-provided fixed arena — no heap, no destroy call. It feeds
[yplot](../yplot/README.md) (whose expressions are then compiled to
bytecode by [yfsvm](../yfsvm/README.md) and evaluated on the GPU) and
[yecho](../yecho/README.md). Depends only on `ycore` (Result types).

## Grammar

```
expr    = term (('+' | '-') term)*
term    = factor (('*' | '/') factor)*
factor  = unary ('^' unary)?
unary   = '-'? primary
primary = NUMBER | IDENT | IDENT '(' args ')' | '(' expr ')' | '@' IDENT
```

Node kinds: number, identifier, buffer reference (`@name`), binary op,
unary negation, call (up to `YETTY_YEXPR_MAX_CALL_ARGS` = 4 arguments).
Function names are not validated here — the parser records the identifier
and the consumer (the yfsvm compiler) decides what `sin`, `fract`, … mean.

## Plot syntax

`yetty_yexpr_parse_plot` additionally understands the multi-definition
plot language used by yplot's wire format:

```
f = sin(x); g = f(x) * 0.5;      # named definitions (max 16)
@f.color = #FF0000;              # per-plot attributes (name/attr/value)
x = 0..10; y = -1..1;            # evaluation domain
@view = 0..5, -2..2;             # initial viewport
d = buffer; @d.size = 256;       # named data buffers (max 8):
@d.values = 1, 2, 3;             #   inline values, or
@d.values = "data.bin";          #   a file path, or
@d.values = ;                    #   bare -> expect from payload
```

The result is a value struct (`struct yetty_yexpr_plot_expr`) holding
definitions, attributes, buffer declarations and ranges; only
`defs[].expression` points into the arena.

## Public API sketch

```c
struct yetty_yexpr_arena arena;                 /* plain value storage */
struct yetty_yexpr_node_ptr_result root_res =
    yetty_yexpr_parse("sin(x) + cos(x)", len, &arena);

struct yetty_yexpr_plot_expr_result plot_res =
    yetty_yexpr_parse_plot("f = sin(x); @f.color = #00FF00", len, &arena);
```

The arena (`YETTY_YEXPR_MAX_NODES` = 256 nodes) is reset on entry and needs
no initialisation. Every returned node pointer points into it, so the AST
stays valid exactly as long as the arena does — keep the arena wherever the
AST must remain dereferenceable, and never copy structs holding node
pointers past its lifetime.

## File map

| file | role |
|------|------|
| `yexpr.c` | lexer + recursive-descent parser (single expression and plot syntax) |
| `include/yetty/yexpr/yexpr.h` | AST node/arena types, plot structs, the two parse entry points |

## Consumers

- `../yplot/yplot.c`, `../yplot/yplot-yaml.c` — parse the plot expression
  language arriving over the wire, resolve attributes and buffers, then
  compile each definition's AST with `yetty_yfsvm_compile`.
- `../yfsvm/compiler.c` — consumes `struct yetty_yexpr_node` ASTs
  (`yetty_yfsvm_compile`, `yetty_yfsvm_compile_multi`) into stack-VM
  bytecode executed inside WGSL.
- `../yecho/yecho.c` — a `{plot; ...: f=sin(x); ...}` block in yecho text
  is parsed with the same plot syntax and compiled the same way.
