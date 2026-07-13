# ycore — Result/error types, buffers, math, util: the foundation layer

`ycore` is the lowest-level library in the tree. It carries the Result/error
system every function signature is built on, the growable byte buffer, small
geometry/color PODs, base64/file/color-parsing utilities, per-owner memory
accounting, and a handful of header-only helpers. Every other module links
`yetty_ycore`; the core sources depend on nothing yetty-specific (mimalloc
is an optional allocator hook for `memstats`; the one exception is the
`tcp-transport.c` guest — see the placement note below).

## The Result/error system

`result.h` defines `YETTY_YRESULT_DECLARE(name, type)` →
`struct <name>_result`, the `YETTY_OK` / `YETTY_ERR` /
`YETTY_RETURN_IF_ERR` macros, the heap-linked `struct yetty_ycore_error`
cause chain, and the chain's print / snprint / serialize / deserialize
helpers (the serialized form carries a remote error back over the yclass RPC
wire). `yetty_ycore_void_chain` folds best-effort teardown steps into one
chain. The full contract — ownership, chaining, when to use which macro — is
documented in [result.md](../../../docs/result.md); this header is its
implementation. Rule of scope: `result.h` declares result types for **basic C
types and pointers to them only** (`yetty_ycore_void/int/size/uint32/…`);
module struct results live in the owning module's header.

## Everything else, header by header

```c
struct yetty_ycore_buffer_result buf_res = yetty_ycore_buffer_create(4096);
yetty_ycore_buffer_write(&buf_res.value, bytes, len);   /* doubling growth */

struct yetty_ycore_buffer_result file_res = yetty_ycore_read_file(path);
yetty_ycore_parse_hex_color("#6BA892", &packed_rgba);

yetty_ycore_object_id pane_id = yetty_ycore_next_object_id();
```

| header | contents |
|--------|----------|
| `result.h` | Result macros, error chain, wire serialization (see above) |
| `types.h` | `container_of`, `yetty_ycore_object_id` + `next_object_id` (chrome pre-allocates tile ids), `yetty_ycore_buffer` (+named buffer, blob, data, span), grid/pixel/rectangle PODs, `yetty_ycore_rgba` |
| `util.h` | `read_file`, base64 encode/decode, `parse_hex_color` (packed RGBA, matches WGSL `ydraw_unpack_color`) |
| `math.h` | header-only always-inline `yetty_min_u32` / `max` / `clampf` — typed, no double-evaluation |
| `map.h` | header-only fixed-capacity open-addressing u32→u32 hash map (linear probing, FNV-1a) |
| `memtag.h` | `struct yetty_ycore_memtag` — per-owner allocation accounting wrappers (atomics, exact bytes via usable-size, fault injection via `fail_after`) plus the registry that feeds the yctl `memtags` dump |
| `memstats.h` | process-wide committed/resident byte snapshot via mimalloc + OS accounting; errors out on builds without the instrumented allocator |
| `terminal-detect.h` | header-only `TERM_PROGRAM` sniffing for client tools: `yetty_running_under_yetty()`, `yetty_term_program_is_tmux()` (passthrough-wrap decision) |
| `ffi-annotations.h` | `YETTY_ANNOT_OUT/ARRAY/CALLER_OWNED/…` — no-op attributes read by the ffi-gen extractor ([ffi-gen.md](../../../docs/ffi-gen.md)) |

## File map

| file | role |
|------|------|
| `result.c` | error-chain alloc/free, print/snprint, wire serialize/deserialize, `void_chain` |
| `types.c` | buffer create/destroy/append/write, `next_object_id` |
| `util.c` | file reading, base64, hex-color parsing |
| `memtag.c` | tagged alloc/calloc/realloc/free wrappers + registry + table formatter |
| `memstats.c` | mimalloc/OS sampler (stub without `YETTY_ENABLE_LIB_MIMALLOC`) |
| `tcp-transport.c` | TCP backend for `yetty_ytransport_conn_transport` — see note |

Placement note: `tcp-transport.c` is compiled into `yetty_ycore` for link
convenience, but its header lives in `include/yetty/ytransport/` and it
implements the ytransport connection interface over the yevent TCP client —
it is ytransport/yevent surface, not core API. See
[ytransport](../ytransport/README.md) and [yevent](../yevent/README.md).

## Consumers

Everything. Notable dedicated consumers: the yclass RPC layer (error wire
format), [yctl](../yctl/README.md) (`memtags` method formats the memtag
registry), [yvterm](../yvterm/README.md) (ring/archive memtags), yui's
statusbar (memstats readout).

## Cross-references

- [result.md](../../../docs/result.md) — the Result/error contract
- [c-coding-style.md](../../../docs/c-coding-style.md) — conventions the types here serve
- [ffi-gen.md](../../../docs/ffi-gen.md) — how `ffi-annotations.h` is consumed
