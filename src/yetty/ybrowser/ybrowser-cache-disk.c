/*
 * ybrowser-cache-disk.c — persistent tier of the loader's HTTP cache
 * (RFC 9111 private cache, disk storage).
 *
 * The memory LRU in ybrowser-js-web.c stays the source of truth for a
 * running session; this tier makes entries survive restarts. The division
 * of labor: freshness / revalidation / Vary policy are decided by the
 * loader (one implementation for both tiers) — this file only persists
 * and recalls entries. An entry is stored with its validators and its
 * absolute expiry, so a cold start can serve it fresh or turn it into a
 * conditional refetch exactly like a warm one.
 *
 * On-disk format: one file per entry under
 * $XDG_CACHE_HOME/yetty/ybrowser-cache (else ~/.cache/yetty/...), named
 * by an FNV-1a hash of (kind, url). A line-based text header carries the
 * metadata; the raw body follows. Writes go to a same-directory temp
 * file and rename() into place, so readers only ever see complete
 * entries and a crash cannot leave a torn one. Hash collisions are
 * handled by storing the full key in the header — a mismatch is a miss,
 * never a wrong body.
 */

#include "ybrowser-internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ytrace/ytrace.h>

/* Total on-disk budget; eviction trims the oldest entries (by mtime,
 * which serving refreshes) back to the low-water mark. */
#define DISK_CACHE_BUDGET_BYTES (256ull * 1024ull * 1024ull)
#define DISK_CACHE_LOW_WATER_BYTES ((DISK_CACHE_BUDGET_BYTES / 10ull) * 9ull)

/* One entry must not dominate the budget (matches the memory tier's
 * quarter-budget rule in spirit; 16 MB covers every realistic page). */
#define DISK_CACHE_MAX_ENTRY_BYTES (16u * 1024u * 1024u)

#define DISK_CACHE_MAGIC "ybrowser-cache 1"

static uint64_t disk_cache_key_hash(const char *url, int kind)
{
    uint64_t hash = 1469598103934665603ull;
    unsigned char kind_byte = (unsigned char)kind;
    hash = (hash ^ kind_byte) * 1099511628211ull;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        hash = (hash ^ *p) * 1099511628211ull;
    }
    return hash;
}

static void disk_cache_entry_path(const struct yetty_ybrowser_disk_cache *cache, const char *url,
                                  int kind, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%016llx.entry", cache->dir,
             (unsigned long long)disk_cache_key_hash(url, kind));
}

/* mkdir -p for exactly the two levels we create under the cache root. */
static int disk_cache_mkdirs(const char *dir)
{
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *last_slash = strrchr(parent, '/');
    if (last_slash && last_slash != parent) {
        *last_slash = '\0';
        (void)mkdir(parent, 0755);
    }
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

struct yetty_ycore_void_result yetty_ybrowser_disk_cache_init(
    struct yetty_ybrowser_disk_cache *cache)
{
    memset(cache, 0, sizeof(*cache));
    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        return YETTY_ERR(yetty_ycore_void, "disk cache: mutex init failed");
    }
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(cache->dir, sizeof(cache->dir), "%s/yetty/ybrowser-cache", xdg);
    } else if (home && *home) {
        snprintf(cache->dir, sizeof(cache->dir), "%s/.cache/yetty/ybrowser-cache", home);
    } else {
        /* No derivable cache dir — run memory-only. Not an error: the
		 * browser is fully functional without persistence. */
        return YETTY_OK_VOID();
    }
    if (!disk_cache_mkdirs(cache->dir)) {
        ydebug("disk cache: cannot create '%s' (%s) — running memory-only", cache->dir,
               strerror(errno));
        cache->dir[0] = '\0';
        return YETTY_OK_VOID();
    }
    cache->enabled = 1;
    return YETTY_OK_VOID();
}

void yetty_ybrowser_disk_cache_shutdown(struct yetty_ybrowser_disk_cache *cache)
{
    if (!cache) {
        return;
    }
    pthread_mutex_destroy(&cache->mutex);
    cache->enabled = 0;
}

/* Read one header line's value: `name value...\n` (value = rest of line,
 * so Content-Type's spaces survive). Returns NULL when the line doesn't
 * carry `name`. */
static const char *header_line_value(const char *line, const char *name)
{
    size_t name_len = strlen(name);
    if (strncmp(line, name, name_len) != 0 || line[name_len] != ' ') {
        return NULL;
    }
    return line + name_len + 1;
}

static void meta_field_copy(char *dst, size_t dst_size, const char *value)
{
    if (strcmp(value, "-") == 0) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", value);
}

int yetty_ybrowser_disk_cache_load(struct yetty_ybrowser_disk_cache *cache, const char *url,
                                   int kind, struct yetty_ybrowser_disk_cache_meta *meta,
                                   char **out_body, size_t *out_body_len)
{
    if (!cache || !cache->enabled || !url || !meta || !out_body || !out_body_len) {
        return 0;
    }
    memset(meta, 0, sizeof(*meta));
    *out_body = NULL;
    *out_body_len = 0;

    char path[1200];
    disk_cache_entry_path(cache, url, kind, path, sizeof(path));

    pthread_mutex_lock(&cache->mutex);
    FILE *file = fopen(path, "rb");
    if (!file) {
        pthread_mutex_unlock(&cache->mutex);
        return 0;
    }

    char line[2048];
    int ok = 0;
    int url_matches = 0;
    long long body_len = -1;
    if (fgets(line, sizeof(line), file) && strncmp(line, DISK_CACHE_MAGIC, 16) == 0) {
        while (body_len < 0 && fgets(line, sizeof(line), file)) {
            size_t line_len = strlen(line);
            if (line_len > 0 && line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
            }
            const char *value;
            if ((value = header_line_value(line, "kind")) != NULL) {
                if (atoi(value) != kind) {
                    break; /* hash collision with another kind */
                }
            } else if ((value = header_line_value(line, "url")) != NULL) {
                if (strcmp(value, url) != 0) {
                    break; /* hash collision with another URL */
                }
                url_matches = 1;
            } else if ((value = header_line_value(line, "status")) != NULL) {
                meta->status = atol(value);
            } else if ((value = header_line_value(line, "expires")) != NULL) {
                meta->expires_at = (time_t)atoll(value);
            } else if ((value = header_line_value(line, "stored")) != NULL) {
                meta->stored_at = (time_t)atoll(value);
            } else if ((value = header_line_value(line, "etag")) != NULL) {
                meta_field_copy(meta->etag, sizeof(meta->etag), value);
            } else if ((value = header_line_value(line, "lastmod")) != NULL) {
                meta_field_copy(meta->last_modified, sizeof(meta->last_modified), value);
            } else if ((value = header_line_value(line, "ctype")) != NULL) {
                meta_field_copy(meta->content_type, sizeof(meta->content_type), value);
            } else if ((value = header_line_value(line, "effurl")) != NULL) {
                meta_field_copy(meta->effective_url, sizeof(meta->effective_url), value);
            } else if ((value = header_line_value(line, "body")) != NULL) {
                body_len = atoll(value);
            }
        }
    }
    if (url_matches && body_len >= 0 && body_len <= (long long)DISK_CACHE_MAX_ENTRY_BYTES) {
        char *body = malloc((size_t)body_len + 1);
        if (body) {
            if (fread(body, 1, (size_t)body_len, file) == (size_t)body_len) {
                body[body_len] = '\0';
                *out_body = body;
                *out_body_len = (size_t)body_len;
                ok = 1;
            } else {
                free(body);
            }
        }
    }
    fclose(file);
    if (ok) {
        /* Touch: mtime is the eviction's LRU signal. */
        struct timespec now_times[2];
        clock_gettime(CLOCK_REALTIME, &now_times[0]);
        now_times[1] = now_times[0];
        (void)utimensat(AT_FDCWD, path, now_times, 0);
    } else {
        /* Corrupt / truncated / collided — drop it so the slot recovers. */
        (void)unlink(path);
    }
    pthread_mutex_unlock(&cache->mutex);
    return ok;
}

/* Trim the directory back under the low-water mark, oldest mtime first.
 * Called with the cache mutex held, after a store pushed the total up. */
static void disk_cache_evict_locked(struct yetty_ybrowser_disk_cache *cache)
{
    struct evict_item {
        char name[64];
        time_t mtime;
        uint64_t size;
    };
    DIR *dir = opendir(cache->dir);
    if (!dir) {
        return;
    }
    struct evict_item *items = NULL;
    size_t item_count = 0, item_cap = 0;
    uint64_t total = 0;
    struct dirent *dirent_entry;
    while ((dirent_entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(dirent_entry->d_name, '.');
        if (!dot || strcmp(dot, ".entry") != 0) {
            continue;
        }
        char full[1300];
        snprintf(full, sizeof(full), "%s/%s", cache->dir, dirent_entry->d_name);
        struct stat entry_stat;
        if (stat(full, &entry_stat) != 0) {
            continue;
        }
        if (item_count == item_cap) {
            size_t new_cap = item_cap ? item_cap * 2 : 64;
            struct evict_item *grown = realloc(items, new_cap * sizeof(*grown));
            if (!grown) {
                break;
            }
            items = grown;
            item_cap = new_cap;
        }
        snprintf(items[item_count].name, sizeof(items[item_count].name), "%s",
                 dirent_entry->d_name);
        items[item_count].mtime = entry_stat.st_mtime;
        items[item_count].size = (uint64_t)entry_stat.st_size;
        total += (uint64_t)entry_stat.st_size;
        item_count++;
    }
    closedir(dir);
    if (total > DISK_CACHE_BUDGET_BYTES) {
        /* Selection sort by mtime while trimming — entry counts here are
		 * hundreds, not millions. */
        while (total > DISK_CACHE_LOW_WATER_BYTES && item_count > 0) {
            size_t oldest = 0;
            for (size_t i = 1; i < item_count; i++) {
                if (items[i].mtime < items[oldest].mtime) {
                    oldest = i;
                }
            }
            char full[1300];
            snprintf(full, sizeof(full), "%s/%s", cache->dir, items[oldest].name);
            if (unlink(full) == 0) {
                total -= items[oldest].size;
            }
            items[oldest] = items[item_count - 1];
            item_count--;
        }
    }
    free(items);
}

void yetty_ybrowser_disk_cache_store(struct yetty_ybrowser_disk_cache *cache, const char *url,
                                     int kind, const struct yetty_ybrowser_disk_cache_meta *meta,
                                     const char *body, size_t body_len)
{
    if (!cache || !cache->enabled || !url || !meta || !body ||
        body_len > DISK_CACHE_MAX_ENTRY_BYTES) {
        return;
    }
    char path[1200];
    disk_cache_entry_path(cache, url, kind, path, sizeof(path));
    char tmp_path[1220];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    pthread_mutex_lock(&cache->mutex);
    FILE *file = fopen(tmp_path, "wb");
    if (!file) {
        pthread_mutex_unlock(&cache->mutex);
        return;
    }
    int header_ok = fprintf(file,
                            DISK_CACHE_MAGIC "\n"
                                             "kind %d\n"
                                             "status %ld\n"
                                             "expires %lld\n"
                                             "stored %lld\n"
                                             "etag %s\n"
                                             "lastmod %s\n"
                                             "ctype %s\n"
                                             "effurl %s\n"
                                             "url %s\n"
                                             "body %zu\n",
                            kind, meta->status, (long long)meta->expires_at,
                            (long long)meta->stored_at, meta->etag[0] ? meta->etag : "-",
                            meta->last_modified[0] ? meta->last_modified : "-",
                            meta->content_type[0] ? meta->content_type : "-",
                            meta->effective_url[0] ? meta->effective_url : "-", url, body_len) > 0;
    int body_ok = header_ok && fwrite(body, 1, body_len, file) == body_len;
    int close_ok = fclose(file) == 0;
    if (body_ok && close_ok) {
        if (rename(tmp_path, path) != 0) {
            (void)unlink(tmp_path);
        }
        disk_cache_evict_locked(cache);
    } else {
        (void)unlink(tmp_path);
    }
    pthread_mutex_unlock(&cache->mutex);
}
