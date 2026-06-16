/*
 * proxy.c — yai's usage proxy.
 *
 * A localhost HTTP/1.1 forwarder that yai puts in front of the engine's
 * Anthropic traffic. yai sets ANTHROPIC_BASE_URL=http://127.0.0.1:<port> for
 * the spawned child; the child's plain-HTTP requests land here, get replayed
 * to the real HTTPS upstream (https://api.anthropic.com, or whatever
 * ANTHROPIC_BASE_URL held before we overrode it) via libcurl, and the
 * response is streamed straight back. While relaying, the proxy captures the
 * `anthropic-ratelimit-*` response headers — the live quota signal — and
 * writes a one-line summary to ~/.claude/usage-status.md and into app state
 * so /usage and the HUD can show it without scraping a TUI.
 *
 * It does NOT ride yai's libuv loop: a dedicated accept thread plus one
 * worker thread per kept-alive client connection keep all blocking socket +
 * libcurl work off the event loop. Workers touch only their own connection
 * and the mutex-guarded status buffer, so the main loop needs no locking.
 */
#include "app.h"
#include "picohttpparser/picohttpparser.h"

#include <yetty/ycore/result.h>

#include <curl/curl.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Upper bound on concurrently-tracked client connections. Claude reuses a
 * keep-alive connection, so live connections stay in the low single digits;
 * finished ones are reaped as new ones arrive. */
#define YAI_PROXY_MAX_CONNS 64

/* Max rate-limit headers captured per response (Anthropic sends ~12). */
#define YAI_PROXY_MAX_FIELDS 40

/* One captured rate-limit header — its name with the `anthropic-ratelimit-`
 * prefix stripped, and its raw value — pending decode into the quota summary. */
struct yai_proxy_field {
    char key[64];
    char value[112];
};

struct yai_proxy {
    struct yai_app *app;
    int listen_fd;
    int port;
    char upstream[512];     /* scheme+host, no trailing slash */
    char status_path[1024]; /* ~/.claude/usage-status.md ("" = no file) */

    pthread_t accept_thread;
    int accept_started;
    atomic_int stop;

    pthread_mutex_t status_mutex;
    char status[4096];  /* latest decoded quota detail (one field per line) */
    char summary[64];   /* compact one-line form for the HUD, e.g. "5h 26% · 7d 55%" */

    pthread_mutex_t conns_mutex;
    struct yai_proxy_conn *conns[YAI_PROXY_MAX_CONNS];
    int conn_count;
};

/* One accepted client connection, handled on its own worker thread. */
struct yai_proxy_conn {
    struct yai_proxy *proxy;
    int fd;
    pthread_t thread;
    atomic_int done;

    /* Per-request response-relay state (reset before each upstream call). */
    int status_code;     /* upstream HTTP status of the current response */
    char *resp_headers;  /* rewritten response header block being assembled */
    size_t resp_len;
    size_t resp_cap;
    int headers_flushed; /* rewritten headers already written to the client */
    int relay_failed;    /* a client-side write failed: abort the transfer */
    struct yai_proxy_field fields[YAI_PROXY_MAX_FIELDS]; /* captured quota headers */
    int field_count;
};

/*---------------------------------------------------------------------------
 * Small byte helpers.
 *---------------------------------------------------------------------------*/

/* Write all `len` bytes; returns 0 on success, -1 on error. */
static int write_full(int fd, const char *bytes, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t wrote = send(fd, bytes + sent, len - sent, MSG_NOSIGNAL);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (wrote == 0) {
            return -1;
        }
        sent += (size_t)wrote;
    }
    return 0;
}

static int header_name_matches(const char *line, size_t line_len, const char *name)
{
    size_t name_len = strlen(name);
    if (line_len < name_len + 1 || line[name_len] != ':') {
        return 0;
    }
    return strncasecmp(line, name, name_len) == 0;
}

/* True for response headers whose framing we re-derive ourselves. */
static int header_is_hop_by_hop(const char *line, size_t line_len)
{
    return header_name_matches(line, line_len, "transfer-encoding") ||
           header_name_matches(line, line_len, "content-length") ||
           header_name_matches(line, line_len, "connection") ||
           header_name_matches(line, line_len, "keep-alive");
}

/*---------------------------------------------------------------------------
 * libcurl callbacks (one connection/request at a time, on a worker thread).
 *---------------------------------------------------------------------------*/

static size_t header_callback(char *buffer, size_t size, size_t count, void *userdata)
{
    struct yai_proxy_conn *conn = userdata;
    size_t len = size * count;

    if (len >= 5 && strncmp(buffer, "HTTP/", 5) == 0) {
        /* Status line: start a fresh response header block. */
        conn->resp_len = 0;
        conn->status_code = 0;
        const char *space = memchr(buffer, ' ', len);
        if (space && (size_t)(space + 1 - buffer) < len) {
            conn->status_code = atoi(space + 1);
        }
    }

    int is_blank = (len == 2 && buffer[0] == '\r' && buffer[1] == '\n') ||
                   (len == 1 && buffer[0] == '\n');
    if (is_blank) {
        /* End of a header block. Ignore 1xx informational blocks; for the
         * real response, append our own framing and flush to the client. */
        if (conn->status_code >= 200 && !conn->headers_flushed) {
            const char *framing = "Transfer-Encoding: chunked\r\n\r\n";
            size_t framing_len = strlen(framing);
            size_t need = conn->resp_len + framing_len;
            if (need > conn->resp_cap) {
                char *grown = realloc(conn->resp_headers, need);
                if (!grown) {
                    return len;
                }
                conn->resp_headers = grown;
                conn->resp_cap = need;
            }
            memcpy(conn->resp_headers + conn->resp_len, framing, framing_len);
            conn->resp_len += framing_len;
            if (write_full(conn->fd, conn->resp_headers, conn->resp_len) != 0) {
                conn->relay_failed = 1;
                return 0;
            }
            conn->headers_flushed = 1;
        } else if (conn->status_code < 200) {
            conn->resp_len = 0; /* drop the informational block */
        }
        return len;
    }

    /* Capture quota headers (name + raw value) regardless of relay. The
     * decode/format into human units happens once, at publish time. */
    if (strncasecmp(buffer, "anthropic-ratelimit-", 20) == 0 &&
        conn->field_count < YAI_PROXY_MAX_FIELDS) {
        const char *colon = memchr(buffer, ':', len);
        if (colon) {
            const char *key = buffer + strlen("anthropic-ratelimit-");
            size_t key_len = (size_t)(colon - key);
            const char *value = colon + 1;
            size_t value_len = len - (size_t)(value - buffer);
            while (value_len && (*value == ' ' || *value == '\t')) {
                value++;
                value_len--;
            }
            while (value_len && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n' ||
                                 value[value_len - 1] == ' ')) {
                value_len--;
            }
            if (key_len > 0) {
                struct yai_proxy_field *field = &conn->fields[conn->field_count++];
                snprintf(field->key, sizeof(field->key), "%.*s", (int)key_len, key);
                snprintf(field->value, sizeof(field->value), "%.*s", (int)value_len, value);
            }
        }
    }

    if (conn->status_code >= 200 && !header_is_hop_by_hop(buffer, len)) {
        size_t need = conn->resp_len + len;
        if (need > conn->resp_cap) {
            size_t new_cap = conn->resp_cap ? conn->resp_cap * 2 : 1024;
            while (new_cap < need) {
                new_cap *= 2;
            }
            char *grown = realloc(conn->resp_headers, new_cap);
            if (!grown) {
                return len; /* skip this header rather than abort */
            }
            conn->resp_headers = grown;
            conn->resp_cap = new_cap;
        }
        memcpy(conn->resp_headers + conn->resp_len, buffer, len);
        conn->resp_len += len;
    }
    return len;
}

static size_t body_callback(char *buffer, size_t size, size_t count, void *userdata)
{
    struct yai_proxy_conn *conn = userdata;
    size_t len = size * count;
    if (len == 0) {
        return 0;
    }
    char chunk_header[32];
    int header_len = snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", len);
    if (header_len < 0 || write_full(conn->fd, chunk_header, (size_t)header_len) != 0 ||
        write_full(conn->fd, buffer, len) != 0 || write_full(conn->fd, "\r\n", 2) != 0) {
        conn->relay_failed = 1;
        return 0; /* abort the transfer */
    }
    return len;
}

/*---------------------------------------------------------------------------
 * Status reporting.
 *---------------------------------------------------------------------------*/

static int str_ends_with(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

/* "in 2d 3h" / "in 4h 12m" / "in 7m" / "now" for a signed second delta. */
static void proxy_format_relative(long seconds, char *out, size_t out_size)
{
    if (seconds <= 0) {
        snprintf(out, out_size, "now");
        return;
    }
    long days = seconds / 86400;
    long hours = (seconds % 86400) / 3600;
    long minutes = (seconds % 3600) / 60;
    if (days > 0) {
        snprintf(out, out_size, "in %ldd %ldh", days, hours);
    } else if (hours > 0) {
        snprintf(out, out_size, "in %ldh %ldm", hours, minutes);
    } else {
        snprintf(out, out_size, "in %ldm", minutes);
    }
}

/* Decode the captured quota fields into a human-readable block — one field
 * per line, fractions as percentages, epoch resets as local time + countdown
 * — then store it (for /usage) and write it to ~/.claude/usage-status.md. */
static void proxy_publish_status(struct yai_proxy *proxy, const struct yai_proxy_conn *conn)
{
    time_t now = time(NULL);
    char timebuf[16] = "";
    struct tm broken;
    if (localtime_r(&now, &broken)) {
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &broken);
    }

    char block[4096];
    size_t offset = 0;
    int written = snprintf(block, sizeof(block), "yai quota (updated %s)\n", timebuf);
    offset = (written > 0) ? (size_t)written : 0;

    for (int index = 0; index < conn->field_count; index++) {
        const struct yai_proxy_field *field = &conn->fields[index];
        size_t room = sizeof(block) > offset ? sizeof(block) - offset : 0;
        if (room == 0) {
            break;
        }
        int wrote = 0;
        if (str_ends_with(field->key, "utilization") || str_ends_with(field->key, "percentage")) {
            wrote = snprintf(block + offset, room, "  %-26s %.0f%%\n", field->key,
                             atof(field->value) * 100.0);
        } else if (str_ends_with(field->key, "reset")) {
            long long epoch = strtoll(field->value, NULL, 10);
            char when[32] = "";
            char relative[24] = "";
            time_t reset_time = (time_t)epoch;
            struct tm reset_broken;
            if (localtime_r(&reset_time, &reset_broken)) {
                strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &reset_broken);
            }
            proxy_format_relative((long)(epoch - (long long)now), relative, sizeof(relative));
            wrote = snprintf(block + offset, room, "  %-26s %s (%s)\n", field->key, when, relative);
        } else {
            wrote = snprintf(block + offset, room, "  %-26s %s\n", field->key, field->value);
        }
        if (wrote < 0) {
            break;
        }
        offset += (size_t)wrote < room ? (size_t)wrote : room - 1;
    }

    /* Compact one-liner for the status bar: the two utilization windows. */
    const char *five_hour = NULL;
    const char *seven_day = NULL;
    for (int index = 0; index < conn->field_count; index++) {
        if (strcmp(conn->fields[index].key, "unified-5h-utilization") == 0) {
            five_hour = conn->fields[index].value;
        } else if (strcmp(conn->fields[index].key, "unified-7d-utilization") == 0) {
            seven_day = conn->fields[index].value;
        }
    }
    char summary[64] = "";
    if (five_hour || seven_day) {
        snprintf(summary, sizeof(summary), "quota 5h %.0f%% · 7d %.0f%%",
                 five_hour ? atof(five_hour) * 100.0 : 0.0,
                 seven_day ? atof(seven_day) * 100.0 : 0.0);
    }

    pthread_mutex_lock(&proxy->status_mutex);
    snprintf(proxy->status, sizeof(proxy->status), "%s", block);
    snprintf(proxy->summary, sizeof(proxy->summary), "%s", summary);
    pthread_mutex_unlock(&proxy->status_mutex);

    if (proxy->status_path[0]) {
        FILE *file = fopen(proxy->status_path, "w");
        if (file) {
            fputs(block, file);
            fclose(file);
        }
    }
}

/*---------------------------------------------------------------------------
 * Request parsing + forwarding.
 *---------------------------------------------------------------------------*/

/* Case-insensitive match of a picohttpparser-parsed header's name. */
static int phr_name_is(const struct phr_header *header, const char *name)
{
    size_t name_len = strlen(name);
    return header->name && header->name_len == name_len &&
           strncasecmp(header->name, name, name_len) == 0;
}

/* Forward one request (already parsed into url/method/header-list by the
 * caller) to the upstream and stream the response back. Returns 0 to keep the
 * connection alive, -1 to close it. */
static int proxy_forward(struct yai_proxy_conn *conn, const char *method, const char *url,
                         struct curl_slist *headers, const char *body, size_t body_len)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    conn->status_code = 0;
    conn->resp_len = 0;
    conn->headers_flushed = 0;
    conn->relay_failed = 0;
    conn->field_count = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    /* Stay fully transparent on compression: do NOT set CURLOPT_ACCEPT_ENCODING
     * (which would make curl advertise + auto-decompress). The client's own
     * Accept-Encoding header is forwarded as-is, the upstream body is relayed
     * raw, and we keep its Content-Encoding header — so the client decodes it,
     * exactly as if it had talked to the API directly. */
    if (body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, conn);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, conn);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (conn->relay_failed) {
        return -1; /* client went away mid-stream */
    }
    if (result != CURLE_OK) {
        if (!conn->headers_flushed) {
            const char *gateway_error = "HTTP/1.1 502 Bad Gateway\r\n"
                                        "Content-Length: 0\r\n"
                                        "Connection: close\r\n\r\n";
            write_full(conn->fd, gateway_error, strlen(gateway_error));
        }
        return -1;
    }
    if (conn->headers_flushed) {
        if (write_full(conn->fd, "0\r\n\r\n", 5) != 0) { /* end of chunked body */
            return -1;
        }
    }
    if (conn->field_count > 0) {
        proxy_publish_status(conn->proxy, conn);
    }
    return 0;
}

/* Grow `*buf` to hold at least one more recv, then read into it. Returns the
 * bytes read (>0), or <=0 on close/error (matching recv). */
static ssize_t proxy_fill(struct yai_proxy_conn *conn, char **buf, size_t *buf_cap, size_t buf_len)
{
    if (buf_len + 4096 > *buf_cap) {
        size_t new_cap = *buf_cap ? *buf_cap * 2 : 8192;
        char *grown = realloc(*buf, new_cap);
        if (!grown) {
            return -1;
        }
        *buf = grown;
        *buf_cap = new_cap;
    }
    return recv(conn->fd, *buf + buf_len, *buf_cap - buf_len, 0);
}

/* Keep-alive loop: serve requests on one client connection until it closes. */
static void proxy_serve(struct yai_proxy_conn *conn)
{
    char *buf = NULL;
    size_t buf_cap = 0;
    size_t buf_len = 0;
    size_t last_len = 0;
    while (!atomic_load(&conn->proxy->stop)) {
        /* Read until picohttpparser has a complete request header block. */
        const char *method = NULL;
        const char *path = NULL;
        size_t method_len = 0;
        size_t path_len = 0;
        size_t num_headers = 0;
        int minor_version = 0;
        struct phr_header headers[100];
        int parsed = -2;
        while (parsed == -2 && !atomic_load(&conn->proxy->stop)) {
            num_headers = sizeof(headers) / sizeof(headers[0]);
            parsed = phr_parse_request(buf, buf_len, &method, &method_len, &path, &path_len,
                                       &minor_version, headers, &num_headers, last_len);
            if (parsed != -2) {
                break;
            }
            last_len = buf_len;
            ssize_t got = proxy_fill(conn, &buf, &buf_cap, buf_len);
            if (got <= 0) {
                if (got < 0 && errno == EINTR) {
                    parsed = -2;
                    continue;
                }
                free(buf);
                return;
            }
            buf_len += (size_t)got;
        }
        if (parsed < 0) {
            free(buf); /* malformed request, or stop requested mid-parse */
            return;
        }
        size_t header_end = (size_t)parsed;

        /* Build the upstream URL and forwarded header list NOW, while the
         * parsed pointers into `buf` are valid (the body read may realloc it;
         * curl_slist_append copies, so the list outlives `buf`). Drop the
         * hop-by-hop / framing headers curl manages itself. */
        char method_buf[16];
        snprintf(method_buf, sizeof(method_buf), "%.*s",
                 (int)(method_len < sizeof(method_buf) - 1 ? method_len : sizeof(method_buf) - 1),
                 method);
        char url[1536];
        snprintf(url, sizeof(url), "%s%.*s", conn->proxy->upstream, (int)path_len, path);

        struct curl_slist *forward_headers = curl_slist_append(NULL, "Expect:");
        size_t content_length = 0;
        for (size_t index = 0; index < num_headers; index++) {
            const struct phr_header *header = &headers[index];
            if (phr_name_is(header, "content-length")) {
                content_length = (size_t)strtoull(header->value, NULL, 10);
            }
            if (phr_name_is(header, "host") || phr_name_is(header, "content-length") ||
                phr_name_is(header, "connection") || phr_name_is(header, "keep-alive") ||
                phr_name_is(header, "proxy-connection") || phr_name_is(header, "expect") ||
                phr_name_is(header, "transfer-encoding")) {
                continue;
            }
            char one[4096];
            snprintf(one, sizeof(one), "%.*s: %.*s", (int)header->name_len, header->name,
                     (int)header->value_len, header->value);
            forward_headers = curl_slist_append(forward_headers, one);
        }

        /* Read the rest of the body if it didn't all arrive with the headers. */
        while (buf_len - header_end < content_length && !atomic_load(&conn->proxy->stop)) {
            ssize_t got = proxy_fill(conn, &buf, &buf_cap, buf_len);
            if (got <= 0) {
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                curl_slist_free_all(forward_headers);
                free(buf);
                return;
            }
            buf_len += (size_t)got;
        }

        int forward_result =
            proxy_forward(conn, method_buf, url, forward_headers, buf + header_end, content_length);
        curl_slist_free_all(forward_headers);
        if (forward_result != 0) {
            break;
        }
        /* Drop the consumed request; keep any pipelined bytes. */
        size_t consumed = header_end + content_length;
        memmove(buf, buf + consumed, buf_len - consumed);
        buf_len -= consumed;
        last_len = 0;
    }
    free(buf);
}

/*---------------------------------------------------------------------------
 * Threads + lifecycle.
 *---------------------------------------------------------------------------*/

static void *proxy_worker_main(void *arg)
{
    struct yai_proxy_conn *conn = arg;
    proxy_serve(conn);
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    free(conn->resp_headers);
    conn->resp_headers = NULL;
    atomic_store(&conn->done, 1);
    return NULL;
}

/* Join + free any worker that has finished. Caller holds conns_mutex. */
static void proxy_reap_done(struct yai_proxy *proxy)
{
    for (int index = 0; index < proxy->conn_count;) {
        struct yai_proxy_conn *conn = proxy->conns[index];
        if (atomic_load(&conn->done)) {
            pthread_join(conn->thread, NULL);
            free(conn);
            proxy->conns[index] = proxy->conns[--proxy->conn_count];
        } else {
            index++;
        }
    }
}

static void *proxy_accept_main(void *arg)
{
    struct yai_proxy *proxy = arg;
    while (!atomic_load(&proxy->stop)) {
        int client_fd = accept(proxy->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break; /* listen socket closed at stop, or fatal error */
        }
        struct yai_proxy_conn *conn = calloc(1, sizeof(*conn));
        if (!conn) {
            close(client_fd);
            continue;
        }
        conn->proxy = proxy;
        conn->fd = client_fd;

        pthread_mutex_lock(&proxy->conns_mutex);
        proxy_reap_done(proxy);
        if (proxy->conn_count >= YAI_PROXY_MAX_CONNS) {
            pthread_mutex_unlock(&proxy->conns_mutex);
            close(client_fd);
            free(conn);
            continue;
        }
        if (pthread_create(&conn->thread, NULL, proxy_worker_main, conn) != 0) {
            pthread_mutex_unlock(&proxy->conns_mutex);
            close(client_fd);
            free(conn);
            continue;
        }
        proxy->conns[proxy->conn_count++] = conn;
        pthread_mutex_unlock(&proxy->conns_mutex);
    }
    return NULL;
}

struct yetty_ycore_void_result yai_usage_proxy_start(struct yai_app *app)
{
    if (app->usage_proxy) {
        return YETTY_OK_VOID();
    }
    struct yai_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_ycore_void, "usage proxy: calloc");
    }
    proxy->app = app;
    proxy->listen_fd = -1;
    atomic_init(&proxy->stop, 0);
    pthread_mutex_init(&proxy->status_mutex, NULL);
    pthread_mutex_init(&proxy->conns_mutex, NULL);

    /* Upstream = the ANTHROPIC_BASE_URL the child would otherwise have used,
     * before we redirect it at ourselves. */
    const char *existing = getenv("ANTHROPIC_BASE_URL");
    snprintf(proxy->upstream, sizeof(proxy->upstream), "%s",
             (existing && existing[0]) ? existing : "https://api.anthropic.com");
    size_t upstream_len = strlen(proxy->upstream);
    while (upstream_len > 0 && proxy->upstream[upstream_len - 1] == '/') {
        proxy->upstream[--upstream_len] = '\0';
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(proxy->status_path, sizeof(proxy->status_path), "%s/.claude/usage-status.md", home);
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        pthread_mutex_destroy(&proxy->status_mutex);
        pthread_mutex_destroy(&proxy->conns_mutex);
        free(proxy);
        return YETTY_ERR(yetty_ycore_void, "usage proxy: socket");
    }
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0); /* ephemeral: the OS hands us a free port */
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_fd, 16) != 0) {
        close(listen_fd);
        pthread_mutex_destroy(&proxy->status_mutex);
        pthread_mutex_destroy(&proxy->conns_mutex);
        free(proxy);
        return YETTY_ERR(yetty_ycore_void, "usage proxy: bind/listen");
    }
    struct sockaddr_in bound = {0};
    socklen_t bound_len = sizeof(bound);
    if (getsockname(listen_fd, (struct sockaddr *)&bound, &bound_len) != 0) {
        close(listen_fd);
        pthread_mutex_destroy(&proxy->status_mutex);
        pthread_mutex_destroy(&proxy->conns_mutex);
        free(proxy);
        return YETTY_ERR(yetty_ycore_void, "usage proxy: getsockname");
    }
    proxy->port = ntohs(bound.sin_port);
    proxy->listen_fd = listen_fd;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (pthread_create(&proxy->accept_thread, NULL, proxy_accept_main, proxy) != 0) {
        close(listen_fd);
        curl_global_cleanup();
        pthread_mutex_destroy(&proxy->status_mutex);
        pthread_mutex_destroy(&proxy->conns_mutex);
        free(proxy);
        return YETTY_ERR(yetty_ycore_void, "usage proxy: pthread_create");
    }
    proxy->accept_started = 1;

    /* Point the child at us. Whatever the child read before is now `upstream`. */
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", proxy->port);
    setenv("ANTHROPIC_BASE_URL", base_url, 1);

    app->usage_proxy = proxy;
    return YETTY_OK_VOID();
}

void yai_usage_proxy_stop(struct yai_app *app)
{
    struct yai_proxy *proxy = app->usage_proxy;
    if (!proxy) {
        return;
    }
    app->usage_proxy = NULL;

    atomic_store(&proxy->stop, 1);
    if (proxy->listen_fd >= 0) {
        shutdown(proxy->listen_fd, SHUT_RDWR);
        close(proxy->listen_fd);
        proxy->listen_fd = -1;
    }
    if (proxy->accept_started) {
        pthread_join(proxy->accept_thread, NULL);
    }
    /* Unblock and join every worker. */
    pthread_mutex_lock(&proxy->conns_mutex);
    for (int index = 0; index < proxy->conn_count; index++) {
        struct yai_proxy_conn *conn = proxy->conns[index];
        if (conn->fd >= 0) {
            shutdown(conn->fd, SHUT_RDWR);
        }
    }
    for (int index = 0; index < proxy->conn_count; index++) {
        struct yai_proxy_conn *conn = proxy->conns[index];
        pthread_join(conn->thread, NULL);
        free(conn);
    }
    proxy->conn_count = 0;
    pthread_mutex_unlock(&proxy->conns_mutex);

    curl_global_cleanup();
    pthread_mutex_destroy(&proxy->status_mutex);
    pthread_mutex_destroy(&proxy->conns_mutex);
    free(proxy);
}

void yai_usage_proxy_status(struct yai_app *app, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    struct yai_proxy *proxy = app->usage_proxy;
    if (!proxy) {
        return;
    }
    pthread_mutex_lock(&proxy->status_mutex);
    snprintf(out, out_size, "%s", proxy->status);
    pthread_mutex_unlock(&proxy->status_mutex);
}

void yai_usage_proxy_summary(struct yai_app *app, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    struct yai_proxy *proxy = app->usage_proxy;
    if (!proxy) {
        return;
    }
    pthread_mutex_lock(&proxy->status_mutex);
    snprintf(out, out_size, "%s", proxy->summary);
    pthread_mutex_unlock(&proxy->status_mutex);
}
