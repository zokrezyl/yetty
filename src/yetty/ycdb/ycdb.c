#include <yetty/ycdb/ycdb.h>
#include <stdlib.h>
#include <string.h>

#ifdef YETTY_USE_HOWERJ_CDB
/*=============================================================================
 * howerj/cdb — portable, file I/O callbacks
 *===========================================================================*/
#include <cdb.h>
#include <stdio.h>

struct yetty_ycdb_reader {
    cdb_t *cdb;
    cdb_options_t opts;
};

struct yetty_ycdb_writer {
    cdb_t *cdb;
    cdb_options_t opts;
};

static cdb_word_t hcdb_read(void *f, void *buf, size_t len)
{
    return (cdb_word_t)fread(buf, 1, len, (FILE *)f);
}
static cdb_word_t hcdb_write(void *f, void *buf, size_t len)
{
    return (cdb_word_t)fwrite(buf, 1, len, (FILE *)f);
}
static int hcdb_seek(void *f, long off)
{
    return fseek((FILE *)f, off, SEEK_SET);
}
static void *hcdb_open(const char *name, int mode)
{
    return fopen(name, mode == CDB_RW_MODE ? "wb+" : "rb");
}
static int hcdb_close(void *f)
{
    return fclose((FILE *)f);
}
static int hcdb_flush(void *f)
{
    return fflush((FILE *)f);
}
static void *hcdb_alloc(void *a, void *p, size_t o, size_t n)
{
    (void)a;
    (void)o;
    if (!n) {
        free(p);
        return NULL;
    }
    return realloc(p, n);
}

static cdb_options_t hcdb_make_opts(void)
{
    cdb_options_t o = {0};
    o.allocator = hcdb_alloc;
    o.read = hcdb_read;
    o.write = hcdb_write;
    o.seek = hcdb_seek;
    o.open = hcdb_open;
    o.close = hcdb_close;
    o.flush = hcdb_flush;
    return o;
}

struct yetty_ycdb_reader_result yetty_ycdb_reader_open(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_ycdb_reader, "path is NULL");
    }

    struct yetty_ycdb_reader *r = calloc(1, sizeof(*r));
    if (!r) {
        return YETTY_ERR(yetty_ycdb_reader, "allocation failed");
    }

    r->opts = hcdb_make_opts();
    if (cdb_open(&r->cdb, &r->opts, CDB_RO_MODE, path) < 0) {
        r->cdb = NULL;
        free(r);
        return YETTY_ERR(yetty_ycdb_reader, "cdb_open failed");
    }

    return YETTY_OK(yetty_ycdb_reader, r);
}

void yetty_ycdb_reader_close(struct yetty_ycdb_reader *r)
{
    if (!r) {
        return;
    }
    if (r->cdb) {
        cdb_close(r->cdb);
    }
    free(r);
}

struct yetty_ycore_void_result yetty_ycdb_reader_get(struct yetty_ycdb_reader *r, const void *key,
                                                     size_t key_len, void **out_data,
                                                     size_t *out_len)
{
    if (!r || !key || !out_data || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "invalid arguments");
    }

    *out_data = NULL;
    *out_len = 0;

    cdb_buffer_t kb = {0};
    kb.length = key_len;
    kb.buffer = (char *)key;
    cdb_file_pos_t pos = {0};

    if (cdb_get(r->cdb, &kb, &pos) <= 0) {
        return YETTY_OK_VOID(); /* not found */
    }

    if (cdb_seek(r->cdb, pos.position) < 0) {
        return YETTY_ERR(yetty_ycore_void, "cdb_seek failed");
    }

    void *data = malloc(pos.length);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "allocation failed");
    }

    if (cdb_read(r->cdb, data, pos.length) < 0) {
        free(data);
        return YETTY_ERR(yetty_ycore_void, "cdb_read failed");
    }

    *out_data = data;
    *out_len = pos.length;
    return YETTY_OK_VOID();
}

struct yetty_ycdb_writer_result yetty_ycdb_writer_create(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_ycdb_writer, "path is NULL");
    }

    struct yetty_ycdb_writer *w = calloc(1, sizeof(*w));
    if (!w) {
        return YETTY_ERR(yetty_ycdb_writer, "allocation failed");
    }

    w->opts = hcdb_make_opts();
    if (cdb_open(&w->cdb, &w->opts, CDB_RW_MODE, path) < 0) {
        w->cdb = NULL;
        free(w);
        return YETTY_ERR(yetty_ycdb_writer, "cdb_open failed");
    }

    return YETTY_OK(yetty_ycdb_writer, w);
}

struct yetty_ycore_void_result yetty_ycdb_writer_add(struct yetty_ycdb_writer *w, const void *key,
                                                     size_t key_len, const void *value,
                                                     size_t value_len)
{
    if (!w || !w->cdb) {
        return YETTY_ERR(yetty_ycore_void, "writer is NULL");
    }

    cdb_buffer_t kb = {0};
    kb.length = key_len;
    kb.buffer = (char *)key;
    cdb_buffer_t vb = {0};
    vb.length = value_len;
    vb.buffer = (char *)value;

    if (cdb_add(w->cdb, &kb, &vb) < 0) {
        return YETTY_ERR(yetty_ycore_void, "cdb_add failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycdb_writer_finish(struct yetty_ycdb_writer *w)
{
    if (!w || !w->cdb) {
        return YETTY_ERR(yetty_ycore_void, "writer is NULL");
    }

    int ret = cdb_close(w->cdb);
    w->cdb = NULL;
    free(w);

    if (ret < 0) {
        return YETTY_ERR(yetty_ycore_void, "cdb_close failed");
    }
    return YETTY_OK_VOID();
}

#else /* djb/cdb */
/*=============================================================================
 * djb/cdb — Unix only, mmap
 *===========================================================================*/
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cdb.h>
#include <cdb_make.h>

struct yetty_ycdb_reader {
    int fd;
    void *mapped;
    size_t size;
    struct cdb cdb;
};

struct yetty_ycdb_writer {
    int fd;
    struct cdb_make cdbm;
    int started;
};

struct yetty_ycdb_reader_result yetty_ycdb_reader_open(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_ycdb_reader, "path is NULL");
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return YETTY_ERR(yetty_ycdb_reader, "open failed");
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return YETTY_ERR(yetty_ycdb_reader, "fstat failed");
    }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return YETTY_ERR(yetty_ycdb_reader, "mmap failed");
    }

    struct yetty_ycdb_reader *r = calloc(1, sizeof(*r));
    if (!r) {
        munmap(mapped, st.st_size);
        close(fd);
        return YETTY_ERR(yetty_ycdb_reader, "allocation failed");
    }

    r->fd = fd;
    r->mapped = mapped;
    r->size = st.st_size;
    cdb_init(&r->cdb, fd);

    return YETTY_OK(yetty_ycdb_reader, r);
}

void yetty_ycdb_reader_close(struct yetty_ycdb_reader *r)
{
    if (!r) {
        return;
    }
    cdb_free(&r->cdb);
    if (r->mapped && r->mapped != MAP_FAILED) {
        munmap(r->mapped, r->size);
    }
    if (r->fd >= 0) {
        close(r->fd);
    }
    free(r);
}

struct yetty_ycore_void_result yetty_ycdb_reader_get(struct yetty_ycdb_reader *r, const void *key,
                                                     size_t key_len, void **out_data,
                                                     size_t *out_len)
{
    if (!r || !key || !out_data || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "invalid arguments");
    }

    *out_data = NULL;
    *out_len = 0;

    if (cdb_find(&r->cdb, (const char *)key, key_len) <= 0) {
        return YETTY_OK_VOID(); /* not found */
    }

    unsigned int dlen = cdb_datalen(&r->cdb);
    unsigned int dpos = cdb_datapos(&r->cdb);

    void *data = malloc(dlen);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "allocation failed");
    }

    if (cdb_read(&r->cdb, (char *)data, dlen, dpos) < 0) {
        free(data);
        return YETTY_ERR(yetty_ycore_void, "cdb_read failed");
    }

    *out_data = data;
    *out_len = dlen;
    return YETTY_OK_VOID();
}

struct yetty_ycdb_writer_result yetty_ycdb_writer_create(const char *path)
{
    if (!path) {
        return YETTY_ERR(yetty_ycdb_writer, "path is NULL");
    }

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return YETTY_ERR(yetty_ycdb_writer, "open failed");
    }

    struct yetty_ycdb_writer *w = calloc(1, sizeof(*w));
    if (!w) {
        close(fd);
        return YETTY_ERR(yetty_ycdb_writer, "allocation failed");
    }

    w->fd = fd;
    cdb_make_start(&w->cdbm, fd);
    w->started = 1;

    return YETTY_OK(yetty_ycdb_writer, w);
}

struct yetty_ycore_void_result yetty_ycdb_writer_add(struct yetty_ycdb_writer *w, const void *key,
                                                     size_t key_len, const void *value,
                                                     size_t value_len)
{
    if (!w || !w->started) {
        return YETTY_ERR(yetty_ycore_void, "writer is NULL");
    }

    if (cdb_make_add(&w->cdbm, (const char *)key, key_len, (const char *)value, value_len) < 0) {
        return YETTY_ERR(yetty_ycore_void, "cdb_make_add failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycdb_writer_finish(struct yetty_ycdb_writer *w)
{
    if (!w || !w->started) {
        return YETTY_ERR(yetty_ycore_void, "writer is NULL");
    }

    int ret = cdb_make_finish(&w->cdbm);
    w->started = 0;

    /* Free cdb_make internals */
    struct cdb_hplist *x = w->cdbm.head;
    while (x) {
        struct cdb_hplist *next = x->next;
        free(x);
        x = next;
    }
    if (w->cdbm.split) {
        free(w->cdbm.split);
    }

    if (w->fd >= 0) {
        close(w->fd);
    }
    free(w);

    if (ret < 0) {
        return YETTY_ERR(yetty_ycore_void, "cdb_make_finish failed");
    }
    return YETTY_OK_VOID();
}

#endif /* YETTY_USE_HOWERJ_CDB */
