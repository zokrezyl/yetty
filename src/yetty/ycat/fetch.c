/*
 * fetch.c - HTTPS(S) URL fetcher via libcurl.
 *
 * Single-shot easy-handle fetch into a growable buffer. Follows redirects
 * up to 10 hops, sends a "ycat" user agent, and extracts the response
 * Content-Type (before any ";charset=...") for the caller to use as MIME.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ytrace.h>

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#ifdef YETTY_YCAT_HAS_LIBCURL
#include <curl/curl.h>
#endif

int yetty_ycat_is_url(const char *s)
{
	if (!s)
		return 0;
	return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

#ifdef YETTY_YCAT_HAS_LIBCURL

struct growable {
	uint8_t *data;
	size_t len;
	size_t cap;
};

static int growable_append(struct growable *b, const void *src, size_t n)
{
	if (b->len + n > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 65536;
		while (nc < b->len + n)
			nc *= 2;
		uint8_t *nd = realloc(b->data, nc);
		if (!nd)
			return -1;
		b->data = nd;
		b->cap = nc;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
	struct growable *g = (struct growable *)ud;
	size_t total = size * nmemb;
	if (growable_append(g, ptr, total) < 0)
		return 0;
	return total;
}

static char *trim_mime(const char *ct)
{
	if (!ct)
		return NULL;
	while (*ct == ' ' || *ct == '\t')
		ct++;
	const char *semi = strchr(ct, ';');
	size_t len = semi ? (size_t)(semi - ct) : strlen(ct);
	while (len > 0 && (ct[len - 1] == ' ' || ct[len - 1] == '\t' ||
			   ct[len - 1] == '\r' || ct[len - 1] == '\n'))
		len--;
	char *out = malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, ct, len);
	out[len] = '\0';
	return out;
}

int yetty_ycat_fetch_url(const char *url,
			 uint8_t **out, size_t *out_len,
			 char **content_type_out)
{
	if (!url || !out || !out_len)
		return -1;

	static int inited = 0;
	if (!inited) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		inited = 1;
	}

	CURL *c = curl_easy_init();
	if (!c)
		return -1;

	struct growable body = {0};
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "ycat/1.0");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);

	CURLcode rc = curl_easy_perform(c);
	if (rc != CURLE_OK) {
		fprintf(stderr, "ycat: %s: %s\n", url,
			curl_easy_strerror(rc));
		free(body.data);
		curl_easy_cleanup(c);
		return -1;
	}

	long http = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
	if (http >= 400) {
		fprintf(stderr, "ycat: %s: HTTP %ld\n", url, http);
		free(body.data);
		curl_easy_cleanup(c);
		return -1;
	}

	if (content_type_out) {
		char *ct = NULL;
		curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct);
		*content_type_out = trim_mime(ct);
	}

	curl_easy_cleanup(c);
	*out = body.data;
	*out_len = body.len;
	return 0;
}

#else /* !YETTY_YCAT_HAS_LIBCURL */

int yetty_ycat_fetch_url(const char *url, uint8_t **out, size_t *out_len,
			 char **content_type_out)
{
	(void)url; (void)out; (void)out_len; (void)content_type_out;
	return -1;
}

#endif
