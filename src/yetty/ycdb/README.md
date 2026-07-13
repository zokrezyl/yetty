# ycdb — constant-database (cdb) key/value store wrapper

`ycdb` is a small Result-typed C API over the cdb ("constant database")
format — an immutable, hash-indexed key/value file that is written once and
then served with O(1) lookups. It abstracts two backend implementations
behind one interface and is used for the MSDF font-atlas files: glyph
records keyed by codepoint, written by the atlas generators and read back at
render time. Depends only on [ycore](../ycore/README.md) results and the
vendored cdb library.

## Backends

Selected at configure time by `YETTY_USE_HOWERJ_CDB`
(`build-tools/yetty/libs/cdb.cmake`):

| backend | when | notes |
|---------|------|-------|
| howerj/cdb | default on Windows / Android / Emscripten (mandatory on Windows) | portable, stdio callback I/O |
| djb cdb | default on Unix desktop | original implementation, mmap-backed reads |

Both compile from the same `ycdb.c`; the API and file format are identical
either way.

## Public API

```c
/* Reader */
struct yetty_ycdb_reader_result reader_res = yetty_ycdb_reader_open(path);
void *data; size_t len;
yetty_ycdb_reader_get(reader_res.value, &key, sizeof(key), &data, &len);
/* key not found is NOT an error: OK with data == NULL. Caller frees data. */
yetty_ycdb_reader_close(reader_res.value);

/* Writer */
struct yetty_ycdb_writer_result writer_res = yetty_ycdb_writer_create(path);
yetty_ycdb_writer_add(writer_res.value, &key, sizeof(key), value, value_len);
yetty_ycdb_writer_finish(writer_res.value);   /* finalizes AND frees the writer */
```

Keys and values are arbitrary byte spans. `writer_finish` writes the hash
tables and closes the file — a cdb is not readable until it has been
finished.

## File map

| file | role |
|------|------|
| `ycdb.c` | both backends behind `#ifdef YETTY_USE_HOWERJ_CDB` |

Public header: `include/yetty/ycdb/ycdb.h`.

## Consumers

- [ymsdf-wgsl](../ymsdf-wgsl/README.md) — writes GPU-generated per-glyph
  MSDF records into the atlas cdb.
- `ymsdf-gen` — the offline atlas generator tool, writes glyph records.
- [yfont](../yfont/README.md) — `msdf-font.c` / `ms-msdf-font.c` open the
  atlas cdb and look up glyph bitmaps + metrics by codepoint at runtime.
