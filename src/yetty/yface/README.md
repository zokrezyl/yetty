# yface — semantic envelope layer over ywire

`yface` is the message-type layer that client tools and the terminal use to
exchange structured payloads through the PTY byte stream. The wire mechanics
(envelope framing, base64, LZ4F compression, the scanner state machine) live
in [`ywire`](../ywire/README.md); yface wraps one ywire statemachine, owns
the in/out byte buffers, and presents the API shape its many consumers
expect. Depends only on `ycore` + `ywire`; gated by
`YETTY_ENABLE_FEATURE_YFACE`.

## Wire shape

One uniform envelope for every yface-encoded message, as OSC or DCS:

```
ESC ] <code> ; <b64-args> ; <b64-payload> ESC \        (OSC)
ESC P <code> y <b64(args)> ; <b64[(LZ4F)body]> ESC \   (DCS, final byte 'y')
```

- `<code>` — the OSC/DCS numeric code is the discriminator; there are no
  verbs in the body. Codes are allocated by the consumers (see
  `include/yetty/yterminal/dcs-codes.h` and
  [`yterminal`](../yterminal/README.md)); yface is content-agnostic.
- `<args>` — per-code binary header, base64 on the wire. Codes with no
  parameters leave it empty; the wire still carries `;;` so receivers always
  split the same way.
- `<payload>` — body bytes, base64 on the wire, optionally LZ4F-compressed.

For "bin" codes carrying large payloads the args slot holds a
`struct yetty_yface_bin_meta` (`YETTY_YFACE_BIN_MAGIC`, version,
`compressed` = `YETTY_YFACE_COMP_NONE|LZ4F`, `raw_size`), making the payload
self-describing. Whether to compress is the emitter's call: raw b64 for
short structured payloads (mouse, resize), LZ4F for multi-KB/MB blobs
(serialized drawable lists, ImGui frames, textures, video).

## Public API

```c
struct yetty_yface_ptr_result fr = yetty_yface_create();
struct yetty_yface *yface = fr.value;

/* Outgoing, streaming (hot paths — reuses the LZ4F context per envelope): */
yetty_yface_start_write(yface, YETTY_YWIRE_ENVELOPE_DCS, code, /*compressed=*/1,
                        &meta, sizeof(meta));
yetty_yface_write(yface, chunk, len);          /* any number of times */
yetty_yface_finish_write(yface);               /* out_buf now holds the envelope */
struct yetty_ycore_buffer *out = yetty_yface_out_buf(yface);

/* Incoming, push-scanner: envelopes → on_osc, everything else → on_raw. */
yetty_yface_set_handlers(yface, on_osc, on_raw, user);
yetty_yface_feed_bytes(yface, raw, n);

yetty_yface_destroy(yface);
```

One-shot helpers spin up a transient codec per call — fine for once-per-file
emitters, wrong for per-frame paths:

```c
yetty_yface_emit(code, compressed, args, args_len, body, body_len, &out_buf);
yetty_yface_emit_to_fd(fd, code, compressed, args, args_len, body, body_len); /* DCS */
yetty_yface_decode(b64, n, compressed, &out_buf);
```

A low-level body-only read path (`yetty_yface_start_read` / `_feed` /
`_finish_read`) remains for callers whose framing is parsed elsewhere.

## Consumers

- **Emitters (figure producers / CLI tools)** — serialize a drawable list or
  frame and ship it as a bin envelope: `ycat` (`osc.c`),
  [`ymusic`](../ymusic/README.md), [`yflame`](../yflame/README.md),
  [`ycircuit`](../ycircuit/README.md), [`yplot`](../yplot/README.md),
  [`yimage`](../yimage/README.md), [`yvideo`](../yvideo/README.md),
  [`ymesh`](../ymesh/README.md), [`ymap`](../ymap/README.md), the
  [`ymgui`](../ymgui/README.md) frontend (the streaming reference example),
  the [`yrdawn`](../yrdawn/README.md) client, and most of `tools/*`
  (ychart, ydiagram, ythorvg, yai, …).
- **Receivers** — [`yterminal`](../yterminal/README.md) keeps a long-lived
  `emit_yface` for terminal→client OSC responses;
  [`yclient`](../yclient/README.md) runs the scanner side
  (`set_handlers`/`feed_bytes`) in its event loop.

## Layout of the module

| file | role |
|------|------|
| `yface.c` | buffer ownership, lazy ywire SM, streaming write, scanner glue, one-shots |
| `../../../include/yetty/yface/yface.h` | wire documentation, `bin_meta`, the full API |

Typed helpers for the structured message kinds (mouse, resize, key, focus)
are the planned next layer here — emit/decode pairs over `yetty_ywire_emit`
/ `yetty_ywire_decode`; the codec itself never reappears in this module.
