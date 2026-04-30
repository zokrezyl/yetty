# Result Types

Yetty uses typed result unions for error propagation — no exceptions, no errno.

## YETTY_YRESULT_DECLARE

Generates a type-specific result struct containing a tagged union of value or error:

```c
#define YETTY_YRESULT_DECLARE(name, value_type) \
    struct name##_result { \
        int ok; \
        union { \
            value_type value; \
            struct yetty_ycore_error error; \
        }; \
    }
```

## Declaring Result Types

Each module declares its own result types in its header:

```c
/* terminal.h */
YETTY_YRESULT_DECLARE(yetty_yterm_terminal, struct yetty_yterm_terminal *);
YETTY_YRESULT_DECLARE(yetty_yterm_terminal_layer, struct yetty_yterm_terminal_layer *);

/* Generates:
 *   struct yetty_yterm_terminal_result
 *   struct yetty_yterm_terminal_layer_result
 */
```

## Common Result Types

Defined in `include/yetty/ycore/result.h`:

```c
YETTY_YRESULT_DECLARE(yetty_ycore_void, int);   /* void result — success/failure only */
YETTY_YRESULT_DECLARE(yetty_ycore_int, int);
YETTY_YRESULT_DECLARE(yetty_ycore_size, size_t);
```

## Error structure and chaining

```c
struct yetty_ycore_error {
    const char *msg;                    /* typically a string literal */
    struct yetty_ycore_error *cause;    /* heap-allocated chain; NULL = no cause */
};
```

The top-of-chain error lives by **value** inside a Result. Deeper levels (`cause`)
are heap-allocated. Future fields (file, line, source position, error code) will
be appended to the struct alongside `msg`.

### Ownership

When a callee returns an error Result, the immediate caller owns its `cause`
chain. Two ways to discharge that ownership:

1. **Wrap and forward** with the 3-arg `YETTY_ERR`:
   ```c
   return YETTY_ERR(yetty_outer, "outer context", inner_res);
   ```
   Pass the WHOLE result struct (not its `.error` field) — the macro extracts
   it. Ownership of the inner cause chain transfers into the new error.

   Or, more concise, with `YETTY_RETURN_IF_ERR`:
   ```c
   YETTY_RETURN_IF_ERR(yetty_outer, inner_res, "outer context");
   ```

2. **Drop** with `yetty_ycore_error_destroy`:
   ```c
   yetty_ycore_error_destroy(inner_res.error);
   ```
   Walks the chain and frees every node. NULL-safe. Note: only the chain
   (`cause` and onward) is on the heap; the top error itself is a value.

The end consumer (whoever finally surfaces the error to the user / log) is
responsible for calling `yetty_ycore_error_destroy` on the top error to free
the chain. Forgetting leaks the chain.

## Creating Results

```c
/* Success */
return YETTY_OK(yetty_yterm_terminal, terminal);

/* Success (void) */
return YETTY_OK_VOID();

/* Error — root, no cause */
return YETTY_ERR(yetty_yterm_terminal, "failed to allocate");

/* Error — wrapping an upstream cause (ownership transferred) */
return YETTY_ERR(yetty_yterm_terminal, "terminal init failed", inner);
```

`YETTY_ERR` is variadic and accepts 2 or 3 args; the 2-arg form means "no
upstream cause" (chain ends here). The third arg, when present, is the WHOLE
result struct of the upstream error — not its `.error` field.

In all three macros (`YETTY_OK`, `YETTY_ERR`, `YETTY_YRESULT_DECLARE`,
`YETTY_RETURN_IF_ERR`), the `type` parameter is the **result type
identifier** — the prefix you registered with `YETTY_YRESULT_DECLARE`,
without the `_result` suffix.

## Checking and Propagating

```c
struct yetty_yterm_terminal_result res = yetty_yterm_terminal_create(80, 24);
YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yui: terminal create failed");
struct yetty_yterm_terminal *terminal = res.value;
```

Or, manually:
```c
if (YETTY_IS_ERR(res)) {
    return YETTY_ERR(yetty_ycore_void, "yui: terminal create failed", res);
}
```

Avoid forwarding only `res.error.msg` — that loses the chain.

## Surfacing errors at the boundary

```c
struct yetty_yterm_terminal_result res = ...;
if (YETTY_IS_ERR(res)) {
    yerror("terminal: %s", res.error.msg);
    /* TODO: walk res.error.cause for full chain */
    yetty_ycore_error_destroy(res.error);
    return -1;
}
```

## Header

`include/yetty/ycore/result.h`
