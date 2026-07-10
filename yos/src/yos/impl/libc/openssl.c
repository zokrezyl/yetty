/* impl/libc/openssl.c — host-side bridges that expose host openssl
 * (libcrypto + libssl) to the wasm guest as env.* imports.
 *
 * Architecture: the yos host binary links against the host system
 * libssl/libcrypto. The wasm guest carries no openssl code — its
 * .wasm just imports `env.SSL_CTX_new`, `env.EVP_DigestUpdate`,
 * `env.RAND_bytes`, ... and yos resolves them to the wrappers below.
 *
 * Why this layout vs cross-compiling openssl to wasm32:
 *   - speed: native libcrypto (with whatever inline asm the host's
 *            openssl was built with), no double interpretation
 *   - size:  one libssl in the host's address space serves every guest
 *   - cross-compile pain: zero — we use the host's already-built libssl
 *   - cost:  yos needs libssl/libcrypto at link time; gated by the
 *            `with_openssl` meson option so iOS/tvOS builds that don't
 *            ship openssl just disable the bridge.
 *
 * Per-guest isolation (THE hard problem). Two wasm guests sharing one
 * libssl must NOT see each other's SSL state. Approach (Tier 1 in
 * build-tools/libbridge/README.md):
 *
 *   - openssl IS multi-state by design. Every API takes an explicit
 *     SSL_CTX, SSL, EVP_MD_CTX pointer — no hidden file-scope statics
 *     for the per-connection state we care about. Each guest creates own
 *     SSL_CTX via env.SSL_CTX_new; the host returns the pointer
 *     wrapped in a per-ctx handle table. Guest A's SSL_CTX is
 *     unreachable from guest B's handle table.
 *
 *   - The remaining file-scope state inside libssl/libcrypto is either
 *     init-once-immutable (algorithm registries — EVP_sha256() always
 *     returns the same EVP_MD* across the lifetime of the process) or
 *     per-thread (ERR_get_error()'s queue lives in ERR_STATE under TLS;
 *     under yos's fork=host-pthread-per-guest model, that's per-guest).
 *
 *   - RNG state is shared across guests intentionally — the host's
 *     RAND_bytes pool is mixed from system entropy + every guest's
 *     contributions. No guest can disable reseed. Cross-guest read of
 *     "what bytes did guest A get last time" is impossible because
 *     RAND output is irreversible by construction.
 *
 *   - openssl's locking has been internal since 1.1.0 (callbacks
 *     removed in 3.0). Thread-safety on the host openssl means we
 *     don't have to think about it.
 *
 * Handle table: every opaque host pointer the bridge returns to the
 * guest is stored in ctx->ssl_handles[idx] and the guest sees the
 * idx as an i32. Slot 0 is reserved (so handle != 0 means valid).
 * Linear scan to find a free slot — fine until we have hundreds of
 * concurrent SSLs per guest; revisit then.
 *
 * Bridge convention: opaque pointers <-> i32 handles. Buffer args use
 * wasm-offset translation (ctx->memory + off, length-checked).
 * Return-by-pointer args (e.g. EVP_DigestFinal_ex writes md_len)
 * convert host int to a wasm-side int32 store before returning. */

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* Forward decls only — see top-of-file rationale for not pulling
 * <openssl/...> into yos's other TUs (avoids polluting every file
 * that transitively #includes types.h with openssl macros). */
typedef struct ssl_st       SSL;
typedef struct ssl_ctx_st   SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_md_st     EVP_MD;
typedef struct engine_st     ENGINE;

extern const SSL_METHOD *TLS_method(void);
extern const SSL_METHOD *TLS_client_method(void);
extern const SSL_METHOD *TLS_server_method(void);

extern SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
extern void     SSL_CTX_free(SSL_CTX *ctx);
extern long     SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg);

extern SSL *SSL_new(SSL_CTX *ctx);
extern void SSL_free(SSL *ssl);
extern int  SSL_set_fd(SSL *ssl, int fd);
extern int  SSL_connect(SSL *ssl);
extern int  SSL_accept(SSL *ssl);
extern int  SSL_read(SSL *ssl, void *buf, int num);
extern int  SSL_write(SSL *ssl, const void *buf, int num);
extern int  SSL_shutdown(SSL *ssl);
extern int  SSL_get_error(const SSL *ssl, int ret);

extern int  OPENSSL_init_ssl(uint64_t opts, const void *settings);
extern int  OPENSSL_init_crypto(uint64_t opts, const void *settings);
extern unsigned long OpenSSL_version_num(void);
extern const char   *OpenSSL_version(int t);

extern int  RAND_bytes(unsigned char *buf, int num);
extern int  RAND_priv_bytes(unsigned char *buf, int num);
extern int  RAND_status(void);
extern int  RAND_poll(void);
extern void RAND_seed(const void *buf, int num);
extern void RAND_add(const void *buf, int num, double entropy);

typedef struct bio_st        BIO;
typedef struct bio_method_st BIO_METHOD;
extern BIO              *BIO_new(const BIO_METHOD *type);
extern int               BIO_free(BIO *a);
extern const BIO_METHOD *BIO_s_mem(void);
extern int               BIO_write(BIO *b, const void *data, int dlen);
extern int               BIO_read(BIO *b, void *data, int dlen);
extern long              BIO_ctrl(BIO *b, int cmd, long larg, void *parg);
extern BIO              *BIO_new_mem_buf(const void *buf, int len);

extern unsigned long ERR_peek_error(void);
extern unsigned long ERR_peek_last_error(void);
extern char         *ERR_error_string(unsigned long e, char *buf);
extern void          ERR_clear_error(void);

typedef struct evp_pkey_st         EVP_PKEY;
typedef struct evp_pkey_ctx_st     EVP_PKEY_CTX;
typedef struct evp_cipher_st       EVP_CIPHER;
typedef struct evp_cipher_ctx_st   EVP_CIPHER_CTX;
typedef struct rsa_st              RSA;
typedef struct ec_key_st           EC_KEY;
typedef struct ec_group_st         EC_GROUP;
typedef struct ec_point_st         EC_POINT;
typedef struct ec_method_st        EC_METHOD;
typedef struct ec_key_method_st    EC_KEY_METHOD;
typedef struct ecdsa_sig_st        ECDSA_SIG;
typedef struct dh_st               DH;
typedef struct bignum_st           BIGNUM;
typedef struct bignum_ctx          BN_CTX;
typedef struct x509_st             X509;
typedef struct X509_name_st        X509_NAME;
typedef struct ossl_param_st {
    const char *key;
    unsigned int data_type;
    void *data;
    size_t data_size;
    size_t return_size;
} OSSL_PARAM;
typedef int (*pem_password_cb)(char *buf, int size, int rwflag, void *userdata);

extern EVP_PKEY *EVP_PKEY_new(void);
extern void      EVP_PKEY_free(EVP_PKEY *pkey);
extern int       EVP_PKEY_up_ref(EVP_PKEY *pkey);
extern int       EVP_PKEY_cmp(const EVP_PKEY *a, const EVP_PKEY *b);
extern int       EVP_PKEY_get_base_id(const EVP_PKEY *pkey);
extern int       EVP_PKEY_get_bits(const EVP_PKEY *pkey);
extern int       EVP_PKEY_get_size(const EVP_PKEY *pkey);
extern int       EVP_PKEY_set1_RSA(EVP_PKEY *pkey, RSA *key);
extern int       EVP_PKEY_set1_EC_KEY(EVP_PKEY *pkey, EC_KEY *key);
extern RSA      *EVP_PKEY_get0_RSA(EVP_PKEY *pkey);
extern EC_KEY   *EVP_PKEY_get0_EC_KEY(EVP_PKEY *pkey);
extern RSA      *EVP_PKEY_get1_RSA(EVP_PKEY *pkey);
extern EC_KEY   *EVP_PKEY_get1_EC_KEY(EVP_PKEY *pkey);
extern int       EVP_PKEY_get_raw_private_key(const EVP_PKEY *pkey, unsigned char *priv, size_t *len);
extern int       EVP_PKEY_get_raw_public_key (const EVP_PKEY *pkey, unsigned char *pub,  size_t *len);
extern EVP_PKEY_CTX *EVP_PKEY_CTX_new_id(int id, ENGINE *e);
extern void          EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx);
extern int           EVP_PKEY_keygen_init(EVP_PKEY_CTX *ctx);
extern int           EVP_PKEY_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey);
extern int           EVP_PKEY_CTX_set_rsa_keygen_bits(EVP_PKEY_CTX *ctx, int mbits);
extern int           EVP_PKEY_CTX_set_ec_paramgen_curve_nid(EVP_PKEY_CTX *ctx, int nid);

extern RSA *RSA_new(void);
extern void RSA_free(RSA *r);
extern int  RSA_size(const RSA *r);
extern int  RSA_blinding_on(RSA *r, BN_CTX *bnctx);

extern EC_KEY  *EC_KEY_new(void);
extern void     EC_KEY_free(EC_KEY *key);
extern EC_KEY  *EC_KEY_new_by_curve_name(int nid);
extern EC_KEY  *EC_KEY_dup(const EC_KEY *src);
extern int      EC_KEY_generate_key(EC_KEY *key);
extern const EC_KEY_METHOD *EC_KEY_OpenSSL(void);

extern BIGNUM *BN_new(void);
extern void    BN_free(BIGNUM *a);
extern void    BN_clear_free(BIGNUM *a);
extern BIGNUM *BN_dup(const BIGNUM *from);
extern int     BN_num_bits(const BIGNUM *a);
extern int     BN_cmp(const BIGNUM *a, const BIGNUM *b);
extern void    BN_set_flags(BIGNUM *a, int flags);
extern int     BN_is_bit_set(const BIGNUM *a, int n);
extern int     BN_is_negative(const BIGNUM *a);
extern const BIGNUM *BN_value_one(void);
extern int     BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
extern int     BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *a, const BIGNUM *d, BN_CTX *ctx);
extern int     BN_mod_inverse(BIGNUM *r, const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx);
extern int     BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
extern int     BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
/* BN_mod is a #define on the host (BN_div with dv=NULL); no symbol. */
extern int     BN_hex2bn(BIGNUM **a, const char *str);
extern BN_CTX *BN_CTX_new(void);
extern void    BN_CTX_free(BN_CTX *c);
extern BIGNUM *BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret);
extern int     BN_bn2bin(const BIGNUM *a, unsigned char *to);

extern DH *DH_new(void);
extern void DH_free(DH *dh);
extern int  DH_size(const DH *dh);
extern int  DH_generate_key(DH *dh);
extern int  DH_compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh);
extern int  DH_set_length(DH *dh, long length);
extern int  DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);
extern void DH_get0_pqg(const DH *dh, const BIGNUM **p, const BIGNUM **q, const BIGNUM **g);
extern void DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key);

extern int  RSA_set0_key       (RSA *r, BIGNUM *n,    BIGNUM *e,    BIGNUM *d);
extern int  RSA_set0_factors   (RSA *r, BIGNUM *p,    BIGNUM *q);
extern int  RSA_set0_crt_params(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp);
extern void RSA_get0_key       (const RSA *r, const BIGNUM **n,    const BIGNUM **e,    const BIGNUM **d);
extern void RSA_get0_factors   (const RSA *r, const BIGNUM **p,    const BIGNUM **q);
extern void RSA_get0_crt_params(const RSA *r, const BIGNUM **dmp1, const BIGNUM **dmq1, const BIGNUM **iqmp);

extern int  EC_KEY_set_group       (EC_KEY *key, const EC_GROUP *group);
extern int  EC_KEY_set_public_key  (EC_KEY *key, const EC_POINT *pub);
extern int  EC_KEY_set_private_key (EC_KEY *key, const BIGNUM *prv);
extern const EC_GROUP *EC_KEY_get0_group       (const EC_KEY *key);
extern const EC_POINT *EC_KEY_get0_public_key  (const EC_KEY *key);
extern const BIGNUM   *EC_KEY_get0_private_key (const EC_KEY *key);

extern EC_GROUP *EC_GROUP_new_by_curve_name(int nid);
extern void      EC_GROUP_free             (EC_GROUP *group);
extern int       EC_GROUP_get_curve_name   (const EC_GROUP *group);
extern int       EC_GROUP_get_degree       (const EC_GROUP *group);
extern int       EC_GROUP_get_order        (const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx);
extern int       EC_GROUP_cmp              (const EC_GROUP *a, const EC_GROUP *b, BN_CTX *ctx);
extern const EC_METHOD *EC_GROUP_method_of (const EC_GROUP *group);
extern void      EC_GROUP_set_asn1_flag    (EC_GROUP *group, int flag);
extern int       EC_METHOD_get_field_type  (const EC_METHOD *meth);

extern EC_POINT *EC_POINT_new        (const EC_GROUP *group);
extern void      EC_POINT_free       (EC_POINT *point);
extern void      EC_POINT_clear_free (EC_POINT *point);
extern int       EC_POINT_is_at_infinity (const EC_GROUP *group, const EC_POINT *point);
extern int       EC_POINT_mul        (const EC_GROUP *group, EC_POINT *r,
                                      const BIGNUM *n, const EC_POINT *q,
                                      const BIGNUM *m, BN_CTX *ctx);
extern int       EC_POINT_get_affine_coordinates_GFp
                                     (const EC_GROUP *group, const EC_POINT *point,
                                      BIGNUM *x, BIGNUM *y, BN_CTX *ctx);
extern int       EC_POINT_oct2point  (const EC_GROUP *group, EC_POINT *p,
                                      const unsigned char *buf, size_t len, BN_CTX *ctx);
extern size_t    EC_POINT_point2oct  (const EC_GROUP *group, const EC_POINT *p,
                                      int form, unsigned char *buf, size_t len, BN_CTX *ctx);

extern ECDSA_SIG *ECDSA_SIG_new (void);
extern void       ECDSA_SIG_free(ECDSA_SIG *sig);
extern void       ECDSA_SIG_get0(const ECDSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps);
extern int        ECDSA_SIG_set0(ECDSA_SIG *sig, BIGNUM *r, BIGNUM *s);

extern int  EVP_Digest(const void *data, size_t count, unsigned char *md,
                       unsigned int *size, const EVP_MD *type, ENGINE *impl);

/* EVP_CIPHER + EVP_CIPHER_CTX */
extern EVP_CIPHER_CTX *EVP_CIPHER_CTX_new (void);
extern void            EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *c);
extern int  EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr);
extern int  EVP_CIPHER_CTX_get_iv_length (const EVP_CIPHER_CTX *ctx);
extern int  EVP_CIPHER_CTX_get_key_length(const EVP_CIPHER_CTX *ctx);
extern int  EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *ctx, int klen);
extern int  EVP_CIPHER_CTX_get_updated_iv(EVP_CIPHER_CTX *ctx, void *buf, size_t len);
extern int  EVP_CIPHER_CTX_set_params(EVP_CIPHER_CTX *ctx, const OSSL_PARAM params[]);
extern int  EVP_CipherInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
                           const unsigned char *key, const unsigned char *iv, int enc);
extern int  EVP_Cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                       const unsigned char *in, unsigned int inl);
extern const EVP_CIPHER *EVP_aes_128_cbc(void);
extern const EVP_CIPHER *EVP_aes_192_cbc(void);
extern const EVP_CIPHER *EVP_aes_256_cbc(void);
extern const EVP_CIPHER *EVP_aes_128_ctr(void);
extern const EVP_CIPHER *EVP_aes_192_ctr(void);
extern const EVP_CIPHER *EVP_aes_256_ctr(void);
extern const EVP_CIPHER *EVP_aes_128_gcm(void);
extern const EVP_CIPHER *EVP_aes_256_gcm(void);
extern const EVP_CIPHER *EVP_chacha20(void);
extern const EVP_CIPHER *EVP_des_ede3_cbc(void);

extern int  EVP_MD_get_block_size(const EVP_MD *md);
extern const EVP_MD *EVP_MD_CTX_get0_md(const EVP_MD_CTX *ctx);
extern int  EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in);
extern int  EVP_DigestSignInit  (EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx,
                                 const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
extern int  EVP_DigestSign      (EVP_MD_CTX *ctx, unsigned char *sigret, size_t *siglen,
                                 const unsigned char *tbs, size_t tbslen);
extern int  EVP_DigestVerifyInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx,
                                 const EVP_MD *type, ENGINE *e, EVP_PKEY *pkey);
extern int  EVP_DigestVerify    (EVP_MD_CTX *ctx, const unsigned char *sigret, size_t siglen,
                                 const unsigned char *tbs, size_t tbslen);

/* Sizes used by OSSL_PARAM cross-ABI translation. Host: 40 bytes
 * (x86_64/aarch64 LP64). Wasm32: 20 bytes. Verified per the struct
 * definition above; if the host openssl bumps the struct, update
 * here and in the host-side memcpy below. */
#define YOS_OSSL_PARAM_HOST_SIZE 40
#define YOS_OSSL_PARAM_WASM_SIZE 20

extern EVP_MD_CTX *EVP_MD_CTX_new(void);
extern void        EVP_MD_CTX_free(EVP_MD_CTX *ctx);
extern int         EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
extern int         EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
extern int         EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
extern const EVP_MD *EVP_md5(void);
extern const EVP_MD *EVP_sha1(void);
extern const EVP_MD *EVP_sha256(void);
extern const EVP_MD *EVP_sha512(void);
extern int           EVP_MD_get_size(const EVP_MD *md);

extern unsigned long ERR_get_error(void);
extern void          ERR_error_string_n(unsigned long e, char *buf, size_t len);

/* ── handle table ──────────────────────────────────────────────────── */

#define YOS_SSL_HANDLES_GROW 16

/* Ensure the handle table has at least one free slot. Slot 0 is
 * reserved (never handed out) so a returned handle of 0 means error /
 * no-op, matching the convention every libssl function uses for NULL
 * pointers. */
static int ssl_handles_reserve(struct yos_exec_ctx *ctx)
{
    if (!ctx) return -1;
    if (ctx->ssl_handles_cap == 0) {
        size_t cap = YOS_SSL_HANDLES_GROW;
        void **slots = calloc(cap, sizeof(void *));
        if (!slots) return -1;
        ctx->ssl_handles = slots;
        ctx->ssl_handles_cap = (uint32_t)cap;
        return 0;
    }
    /* Already room? Cheap scan — early exit on first NULL above 0. */
    for (uint32_t i = 1; i < ctx->ssl_handles_cap; ++i) {
        if (!ctx->ssl_handles[i]) return 0;
    }
    /* Grow. */
    size_t newcap = (size_t)ctx->ssl_handles_cap + YOS_SSL_HANDLES_GROW;
    void **next = realloc(ctx->ssl_handles, newcap * sizeof(void *));
    if (!next) return -1;
    memset(next + ctx->ssl_handles_cap, 0,
           (newcap - ctx->ssl_handles_cap) * sizeof(void *));
    ctx->ssl_handles = next;
    ctx->ssl_handles_cap = (uint32_t)newcap;
    return 0;
}

/* Wrap a host pointer in a new handle ID. Returns 0 on alloc failure
 * OR if p is NULL (a NULL host pointer means the openssl call failed;
 * propagate that to the guest as handle 0). */
static uint32_t ssl_handles_wrap(struct yos_exec_ctx *ctx, void *p)
{
    if (!p) return 0;
    if (ssl_handles_reserve(ctx) < 0) return 0;
    for (uint32_t i = 1; i < ctx->ssl_handles_cap; ++i) {
        if (!ctx->ssl_handles[i]) {
            ctx->ssl_handles[i] = p;
            return i;
        }
    }
    return 0;  /* unreachable after reserve */
}

/* Resolve a guest-visible handle to its host pointer. Returns NULL on
 * invalid handle — every bridge then short-circuits to a libssl-style
 * failure (return 0 / -1 / SSL_ERROR_*). Slot 0 is reserved. */
static void *ssl_handles_resolve(struct yos_exec_ctx *ctx, uint32_t h)
{
    if (!ctx || !ctx->ssl_handles) return NULL;
    if (h == 0 || h >= ctx->ssl_handles_cap) return NULL;
    return ctx->ssl_handles[h];
}

/* Drop a handle ID and return the host pointer it held (or NULL).
 * The caller invokes the appropriate destructor. */
static void *ssl_handles_release(struct yos_exec_ctx *ctx, uint32_t h)
{
    if (!ctx || !ctx->ssl_handles) return NULL;
    if (h == 0 || h >= ctx->ssl_handles_cap) return NULL;
    void *p = ctx->ssl_handles[h];
    ctx->ssl_handles[h] = NULL;
    return p;
}

/* ── memory-safe accessors for wasm pointer args ───────────────────── */

static const void *guest_buf_ro(struct yos_exec_ctx *ctx,
                                uint32_t off, uint32_t len)
{
    if (!ctx || !ctx->memory) return NULL;
    if (len == 0) return ctx->memory + off;
    uint64_t end = (uint64_t)off + (uint64_t)len;
    if (off >= ctx->memory_size || end > ctx->memory_size) return NULL;
    return ctx->memory + off;
}

static void *guest_buf_rw(struct yos_exec_ctx *ctx,
                          uint32_t off, uint32_t len)
{
    return (void *)guest_buf_ro(ctx, off, len);
}

static const char *guest_cstr(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (!ctx || !ctx->memory) return NULL;
    if (off >= ctx->memory_size) return NULL;
    /* Bound the scan at memory_size; libssl callers always pass
     * NUL-terminated strings (ciphers list, file paths). */
    const char *p = (const char *)(ctx->memory + off);
    const char *end = (const char *)(ctx->memory + ctx->memory_size);
    for (const char *q = p; q < end; ++q) if (*q == 0) return p;
    return NULL;
}

/* ── m3 raw wrappers ───────────────────────────────────────────────── */

#define CTX(rt)  ((struct yos_exec_ctx *)m3_GetUserData(rt))

/* env.OPENSSL_init_ssl    — int(uint64, *settings) */
static const void *m3_yos_OPENSSL_init_ssl(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    uint64_t opts = _sp[1];
    /* settings ptr (i32) is at _sp[2]; openssl accepts NULL — that's
     * "use defaults", which is what every guest wants. We always pass
     * NULL on the host side to avoid having to translate the
     * OPENSSL_INIT_SETTINGS struct (which is opaque and version-bound). */
    int rc = OPENSSL_init_ssl(opts, NULL);
    _sp[0] = (uint64_t)(uint32_t)rc;
    ydebug("OPENSSL_init_ssl(0x%llx) = %d\n",
           (unsigned long long)opts, rc);
    return NULL;
}

/* env.OPENSSL_init_crypto — int(uint64, *settings) */
static const void *m3_yos_OPENSSL_init_crypto(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    uint64_t opts = _sp[1];
    int rc = OPENSSL_init_crypto(opts, NULL);
    _sp[0] = (uint64_t)(uint32_t)rc;
    ydebug("OPENSSL_init_crypto(0x%llx) = %d\n",
           (unsigned long long)opts, rc);
    return NULL;
}

/* env.OpenSSL_version_num — uint32() on wasm32.
 *
 * Host openssl declares `unsigned long OpenSSL_version_num(void)`.
 * On wasm32 `unsigned long` is 32-bit (i386 ABI), so the wasm side
 * sees i32 even though the host returns 64-bit. Narrow on the way
 * out — the openssl version-number fits comfortably in 32 bits
 * (0xMNNFFPPS format with M, NN, FF, PP, S all <= 0xff). */
static const void *m3_yos_OpenSSL_version_num(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)OpenSSL_version_num();
    return NULL;
}

/* env.OpenSSL_version — i32(int t).
 * Returns a wasm-side offset to a copy of the host's version string.
 * libssl returns a host static C string; we need to push it into wasm
 * linear memory for the guest to read. Simple-and-leaky: stash it
 * one-shot in a static scratch slot in wasm memory at offset
 * ctx->mmap_top - N. Right thing to do later is a per-ctx "openssl
 * string pool"; for the smoke test, a 256-byte buffer per guest
 * (zero-padded) is enough. We return offset 0 if the version doesn't
 * fit (guest's strdup of "" is harmless). */
static const void *m3_yos_OpenSSL_version(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    int t = (int)_sp[1];
    const char *s = OpenSSL_version(t);
    if (!s || !ctx || !ctx->memory) { _sp[0] = 0; return NULL; }
    /* Per-ctx static-string slot: reuse the brk tail. For the smoke
     * test we just refuse if heap_end + 256 collides with mmap_top.
     * Real fix is a per-ctx openssl string arena; left as TODO. */
    uint32_t cap = 256;
    if (ctx->memory_size < cap + 16) { _sp[0] = 0; return NULL; }
    /* Stash near the top of memory, just below mmap_top, in a small
     * region we own. NOT thread-safe across concurrent guest threads
     * — a multi-thread guest needs per-call malloc on the guest's
     * heap, which we don't have yet from the host side. */
    uint32_t at = (ctx->memory_size > cap + 16)
                      ? (ctx->memory_size - cap)
                      : 0;
    size_t n = strnlen(s, cap - 1);
    memcpy(ctx->memory + at, s, n);
    ctx->memory[at + n] = 0;
    _sp[0] = (uint64_t)(uint32_t)at;
    return NULL;
}

/* env.TLS_method / TLS_client_method / TLS_server_method — i32().
 * The SSL_METHOD * is process-global and immutable; wrap it in a
 * handle so the guest can pass it back to SSL_CTX_new. */
static const void *m3_yos_TLS_method(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)TLS_method());
    return NULL;
}
static const void *m3_yos_TLS_client_method(IM3Runtime rt, IM3ImportContext _c,
                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)TLS_client_method());
    return NULL;
}
static const void *m3_yos_TLS_server_method(IM3Runtime rt, IM3ImportContext _c,
                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)TLS_server_method());
    return NULL;
}

/* env.SSL_CTX_new — i32(method_handle).
 *
 * The per-guest isolation point. Every guest must call this BEFORE
 * any SSL_new, so its connections live in its own SSL_CTX. The
 * resulting SSL_CTX is owned by THIS yos_exec_ctx and is freed on
 * teardown (or when the guest calls SSL_CTX_free). */
static const void *m3_yos_SSL_CTX_new(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t method_h = (uint32_t)_sp[1];
    SSL_METHOD *method = (SSL_METHOD *)ssl_handles_resolve(ctx, method_h);
    SSL_CTX *sctx = SSL_CTX_new(method);
    ydebug("SSL_CTX_new(method=%p) = %p\n", (void *)method, (void *)sctx);
    _sp[0] = (uint64_t)ssl_handles_wrap(ctx, sctx);
    return NULL;
}

/* env.SSL_CTX_free — void(ctx_handle). Releases the handle slot. */
static const void *m3_yos_SSL_CTX_free(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    /* "v(i)" — args at sp[0]; see comment above BR_RETPTR_NOARG. */
    (void)_c; (void)_m;
    uint32_t h = (uint32_t)_sp[0];
    SSL_CTX *sctx = (SSL_CTX *)ssl_handles_release(CTX(rt), h);
    ydebug("SSL_CTX_free(h=%u, p=%p)\n", h, (void *)sctx);
    if (sctx) SSL_CTX_free(sctx);
    return NULL;
}

/* env.SSL_new — i32(ctx_handle) */
static const void *m3_yos_SSL_new(IM3Runtime rt, IM3ImportContext _c,
                                  uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    SSL_CTX *sctx = (SSL_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    SSL *ssl = sctx ? SSL_new(sctx) : NULL;
    _sp[0] = (uint64_t)ssl_handles_wrap(ctx, ssl);
    return NULL;
}

/* env.SSL_free — void(ssl_handle). "v(i)" → arg at sp[0]. */
static const void *m3_yos_SSL_free(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_release(CTX(rt), (uint32_t)_sp[0]);
    if (ssl) SSL_free(ssl);
    return NULL;
}

/* env.SSL_set_fd — i32(ssl_handle, int fd).
 * The fd is a wasm-visible int and meaningful only if the guest opened
 * the socket through env.socket / env.connect (those bridges resolve
 * to host fds via yos's fd_map). The fd flowing into the bridge here
 * is the SAME number — yos's bridge resolves wasm fd -> host fd in
 * impl/io.c before any read/write/etc. lands in libc. SSL_set_fd
 * stores the number; subsequent SSL_read/write hit it via OS read/
 * write which yos's libc bridges intercept. */
static const void *m3_yos_SSL_set_fd(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    int  fd  = (int)_sp[2];
    _sp[0] = (uint64_t)(uint32_t)(ssl ? SSL_set_fd(ssl, fd) : 0);
    return NULL;
}

/* env.SSL_connect / SSL_accept / SSL_shutdown — i32(ssl_handle) */
static const void *m3_yos_SSL_connect(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(ssl ? SSL_connect(ssl) : 0);
    return NULL;
}
static const void *m3_yos_SSL_accept(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(ssl ? SSL_accept(ssl) : 0);
    return NULL;
}
static const void *m3_yos_SSL_shutdown(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(ssl ? SSL_shutdown(ssl) : 0);
    return NULL;
}

/* env.SSL_read — i32(ssl_handle, buf_off, num).
 * Buffer is in guest linear memory; bounds-check before handing to libssl. */
static const void *m3_yos_SSL_read(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    SSL *ssl = (SSL *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    int      n   = (int)_sp[3];
    void *buf = (n > 0) ? guest_buf_rw(ctx, off, (uint32_t)n) : NULL;
    int rc = (ssl && buf) ? SSL_read(ssl, buf, n) : -1;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.SSL_write — i32(ssl_handle, buf_off, num) */
static const void *m3_yos_SSL_write(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    SSL *ssl = (SSL *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    int      n   = (int)_sp[3];
    const void *buf = (n > 0) ? guest_buf_ro(ctx, off, (uint32_t)n) : NULL;
    int rc = (ssl && buf) ? SSL_write(ssl, buf, n) : -1;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.SSL_get_error — i32(ssl_handle, int ret) */
static const void *m3_yos_SSL_get_error(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    SSL *ssl = (SSL *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    int ret  = (int)_sp[2];
    _sp[0] = (uint64_t)(uint32_t)(ssl ? SSL_get_error(ssl, ret) : 0);
    return NULL;
}

/* env.RAND_bytes — i32(buf_off, int num) */
static const void *m3_yos_RAND_bytes(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      n   = (int)_sp[2];
    void *buf = (n > 0) ? guest_buf_rw(ctx, off, (uint32_t)n) : NULL;
    int rc = buf ? RAND_bytes((unsigned char *)buf, n) : 0;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.RAND_priv_bytes — i32(buf_off, int num). Same shape as
 * RAND_bytes; openssl 3.x's "private" pool, suitable for key
 * material. Falls through to RAND_bytes if RAND_priv_bytes is not
 * available (older libcrypto). */
static const void *m3_yos_RAND_priv_bytes(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      n   = (int)_sp[2];
    void *buf = (n > 0) ? guest_buf_rw(ctx, off, (uint32_t)n) : NULL;
    int rc = buf ? RAND_priv_bytes((unsigned char *)buf, n) : 0;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.RAND_status — i32(). 1 if host RNG is seeded (always true
 * post-OPENSSL_init_crypto), 0 otherwise. */
static const void *m3_yos_RAND_status(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)RAND_status();
    return NULL;
}

/* env.RAND_poll — i32(). Forces a reseed; 1 on success. */
static const void *m3_yos_RAND_poll(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)RAND_poll();
    return NULL;
}

/* env.RAND_seed — v(buf_off, int num). Mixes guest entropy into the
 * shared host pool. Void bridge: args at sp[0..1]. */
static const void *m3_yos_RAND_seed(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[0];
    int      n   = (int)_sp[1];
    const void *buf = (n > 0) ? guest_buf_ro(ctx, off, (uint32_t)n) : NULL;
    if (buf) RAND_seed(buf, n);
    return NULL;
}

/* env.RAND_add — v(buf_off, int num, double entropy). Void bridge:
 * args at sp[0..2]; sp[2] is the f64 (i64-slotted) entropy. */
static const void *m3_yos_RAND_add(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[0];
    int      n   = (int)_sp[1];
    double entropy;
    memcpy(&entropy, &_sp[2], sizeof(entropy));
    const void *buf = (n > 0) ? guest_buf_ro(ctx, off, (uint32_t)n) : NULL;
    if (buf) RAND_add(buf, n, entropy);
    return NULL;
}

/* env.BIO_new — i32(method_handle). */
static const void *m3_yos_BIO_new(IM3Runtime rt, IM3ImportContext _c,
                                  uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIO_METHOD *m = (BIO_METHOD *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIO *b = m ? BIO_new(m) : NULL;
    _sp[0] = (uint64_t)ssl_handles_wrap(ctx, b);
    return NULL;
}

/* env.BIO_free — i32(bio_handle). Returns 1 on success. */
static const void *m3_yos_BIO_free(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    BIO *b = (BIO *)ssl_handles_release(CTX(rt), (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(b ? BIO_free(b) : 0);
    return NULL;
}

/* env.BIO_s_mem — i32(). Returns the singleton memory BIO method. */
static const void *m3_yos_BIO_s_mem(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)BIO_s_mem());
    return NULL;
}

/* env.BIO_write — i32(bio_h, data_off, int dlen). */
static const void *m3_yos_BIO_write(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIO *b = (BIO *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    int      n   = (int)_sp[3];
    const void *data = (n > 0) ? guest_buf_ro(ctx, off, (uint32_t)n) : NULL;
    int rc = (b && (data || n == 0)) ? BIO_write(b, data, n) : -1;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.BIO_read — i32(bio_h, data_off, int dlen). */
static const void *m3_yos_BIO_read(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIO *b = (BIO *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    int      n   = (int)_sp[3];
    void *data = (n > 0) ? guest_buf_rw(ctx, off, (uint32_t)n) : NULL;
    int rc = (b && data) ? BIO_read(b, data, n) : -1;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.BIO_ctrl — i32(bio_h, int cmd, long larg, parg_off).
 * BIO_ctrl is a control operation with a polymorphic 4th arg. We
 * pass the parg as a wasm offset translated to host pointer; many
 * BIO control ops use parg as an out-pointer (writing back data
 * pointers, lengths) — these will read/write through that offset.
 * 'long' on wasm32 is i32, return is widened to i32.
 *
 * NOTE: BIO_C_GET_MEM_DATA (cmd=115) returns the buffer pointer
 * via larg/parg in libcrypto convention; we can't translate a
 * HOST pointer back to a wasm offset here (the host BIO's
 * internal buffer lives in host memory the guest can't see).
 * Callers that need BIO_get_mem_data should use BIO_read instead. */
static const void *m3_yos_BIO_ctrl(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIO *b = (BIO *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    int  cmd  = (int)_sp[2];
    long larg = (long)(int32_t)_sp[3];
    uint32_t parg_off = (uint32_t)_sp[4];
    void *parg = (parg_off && ctx && ctx->memory && parg_off < ctx->memory_size)
                     ? (void *)(ctx->memory + parg_off) : NULL;
    long rc = b ? BIO_ctrl(b, cmd, larg, parg) : -1;
    _sp[0] = (uint64_t)(uint32_t)(int32_t)rc;
    return NULL;
}

/* env.BIO_new_mem_buf — i32(buf_off, int len). */
static const void *m3_yos_BIO_new_mem_buf(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      n   = (int)_sp[2];
    const void *src = (n > 0) ? guest_buf_ro(ctx, off, (uint32_t)n) : NULL;
    BIO *b = src ? BIO_new_mem_buf(src, n) : NULL;
    _sp[0] = (uint64_t)ssl_handles_wrap(ctx, b);
    return NULL;
}

/* env.ERR_peek_error / ERR_peek_last_error — i32().
 * Same wasm32 long-narrowing as ERR_get_error. */
static const void *m3_yos_ERR_peek_error(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)ERR_peek_error();
    return NULL;
}
static const void *m3_yos_ERR_peek_last_error(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)ERR_peek_last_error();
    return NULL;
}

/* env.ERR_clear_error — v(). */
static const void *m3_yos_ERR_clear_error(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m; (void)_sp;
    ERR_clear_error();
    return NULL;
}

/* ── boilerplate macros for the most common bridge shapes ─────────── */
/*
 * wasm3 raw-function calling convention (see wasm3/source/wasm3.h):
 *   m3ApiReturnType(T)  → consumes sp[0]
 *   m3ApiGetArg(T,…)    → consumes next sp slot
 *
 * Concretely:
 *   "i(...)"  return at sp[0], args at sp[1..n]
 *   "v(...)"  args at sp[0..n-1] (no return slot reserved)
 *
 * That's why BR_VOID_* macros read from sp[0+] and BR_INT_* / BR_PTR_*
 * read from sp[1+].
 */

/* T *fn(void) → wrap returned host pointer in a handle. */
#define BR_RETPTR_NOARG(NAME, HOST_FN)                                       \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)HOST_FN());           \
  return NULL; }

/* void fn(T *) → release handle, call destructor on the released ptr. */
#define BR_VOID_HND(NAME, HOST_FN, HOST_T)                                    \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  HOST_T *p = (HOST_T *)ssl_handles_release(CTX(rt), (uint32_t)_sp[0]);      \
  if (p) HOST_FN(p);                                                          \
  return NULL; }

/* int fn(T *)   → resolve handle in, call, return int. */
#define BR_INT_HND(NAME, HOST_FN, HOST_T)                                     \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  HOST_T *p = (HOST_T *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);      \
  _sp[0] = (uint64_t)(uint32_t)(p ? HOST_FN(p) : 0);                          \
  return NULL; }

/* int fn(T *, int) → resolve + scalar i32, return int. */
#define BR_INT_HND_INT(NAME, HOST_FN, HOST_T)                                 \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  HOST_T *p = (HOST_T *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);      \
  int    n   = (int)_sp[2];                                                   \
  _sp[0] = (uint64_t)(uint32_t)(p ? HOST_FN(p, n) : 0);                       \
  return NULL; }

/* int fn(T *, U *) → resolve two handles, return int. */
#define BR_INT_HND_HND(NAME, HOST_FN, T1, T2)                                 \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  struct yos_exec_ctx *_ctx = CTX(rt);                                        \
  T1 *a = (T1 *)ssl_handles_resolve(_ctx, (uint32_t)_sp[1]);                 \
  T2 *b = (T2 *)ssl_handles_resolve(_ctx, (uint32_t)_sp[2]);                 \
  _sp[0] = (uint64_t)(uint32_t)((a && b) ? HOST_FN(a, b) : 0);                \
  return NULL; }

/* U *fn(T *) → resolve handle in, wrap returned ptr. */
#define BR_PTR_HND(NAME, HOST_FN, HOST_T)                                     \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  struct yos_exec_ctx *_ctx = CTX(rt);                                        \
  HOST_T *p = (HOST_T *)ssl_handles_resolve(_ctx, (uint32_t)_sp[1]);         \
  _sp[0] = (uint64_t)ssl_handles_wrap(_ctx, p ? (void *)HOST_FN(p) : NULL);   \
  return NULL; }

/* int fn(void) — narrow host int to wasm i32. */
#define BR_INT_NOARG(NAME, HOST_FN)                                            \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)rt; (void)_c; (void)_m;                                              \
  _sp[0] = (uint64_t)(uint32_t)HOST_FN();                                     \
  return NULL; }

/* T *fn(int) — accept i32, wrap returned ptr. */
#define BR_PTR_INT(NAME, HOST_FN)                                              \
static const void *m3_yos_##NAME(IM3Runtime rt, IM3ImportContext _c,         \
                                 uint64_t *_sp, void *_m)                    \
{ (void)_c; (void)_m;                                                        \
  int n = (int)_sp[1];                                                        \
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)HOST_FN(n));           \
  return NULL; }

/* ── EVP_PKEY family ──────────────────────────────────────────────── */
BR_RETPTR_NOARG  (EVP_PKEY_new,                EVP_PKEY_new)
BR_VOID_HND      (EVP_PKEY_free,               EVP_PKEY_free,         EVP_PKEY)
BR_INT_HND       (EVP_PKEY_up_ref,             EVP_PKEY_up_ref,       EVP_PKEY)
BR_INT_HND_HND   (EVP_PKEY_cmp,                EVP_PKEY_cmp,          EVP_PKEY, EVP_PKEY)
BR_INT_HND       (EVP_PKEY_get_base_id,        EVP_PKEY_get_base_id,  EVP_PKEY)
BR_INT_HND       (EVP_PKEY_get_bits,           EVP_PKEY_get_bits,     EVP_PKEY)
BR_INT_HND       (EVP_PKEY_get_size,           EVP_PKEY_get_size,     EVP_PKEY)
BR_INT_HND_HND   (EVP_PKEY_set1_RSA,           EVP_PKEY_set1_RSA,     EVP_PKEY, RSA)
BR_INT_HND_HND   (EVP_PKEY_set1_EC_KEY,        EVP_PKEY_set1_EC_KEY,  EVP_PKEY, EC_KEY)
/* EVP_PKEY_get0_RSA — instrumented variant. The plain BR_PTR_HND
 * works but we want to see what's flowing in the dump build. */
static const void *m3_yos_EVP_PKEY_get0_RSA(IM3Runtime rt, IM3ImportContext _c,
                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_PKEY *p = (EVP_PKEY *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    void *rsa = p ? (void *)EVP_PKEY_get0_RSA(p) : NULL;
    uint32_t h = ssl_handles_wrap(ctx, rsa);
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        fprintf(stderr, "[EVP_PKEY_get0_RSA] pkey_h=%u pkey=%p rsa=%p -> rsa_h=%u\n",
                (uint32_t)_sp[1], (void *)p, rsa, h);
    }
    _sp[0] = (uint64_t)h;
    return NULL;
}
BR_PTR_HND       (EVP_PKEY_get0_EC_KEY,        EVP_PKEY_get0_EC_KEY,  EVP_PKEY)
BR_PTR_HND       (EVP_PKEY_get1_RSA,           EVP_PKEY_get1_RSA,     EVP_PKEY)
BR_PTR_HND       (EVP_PKEY_get1_EC_KEY,        EVP_PKEY_get1_EC_KEY,  EVP_PKEY)
BR_INT_HND       (EVP_PKEY_keygen_init,        EVP_PKEY_keygen_init,  EVP_PKEY_CTX)
BR_VOID_HND      (EVP_PKEY_CTX_free,           EVP_PKEY_CTX_free,     EVP_PKEY_CTX)
BR_INT_HND_INT   (EVP_PKEY_CTX_set_rsa_keygen_bits, EVP_PKEY_CTX_set_rsa_keygen_bits, EVP_PKEY_CTX)
BR_INT_HND_INT   (EVP_PKEY_CTX_set_ec_paramgen_curve_nid,
                  EVP_PKEY_CTX_set_ec_paramgen_curve_nid, EVP_PKEY_CTX)

/* EVP_PKEY_CTX_new_id — engine arg ignored (yos doesn't bridge ENGINE_*). */
static const void *m3_yos_EVP_PKEY_CTX_new_id(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    int id = (int)_sp[1];
    /* _sp[2] = engine handle, ignored */
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), EVP_PKEY_CTX_new_id(id, NULL));
    return NULL;
}

/* EVP_PKEY_keygen — i32(ctx_h, ppkey_off). ppkey_off is a wasm-side
 * handle slot the host openssl writes a NEW EVP_PKEY * into. We
 * intercept: keygen into a local host EVP_PKEY *, wrap it as a
 * handle, store the handle ID into the guest's *ppkey slot. */
static const void *m3_yos_EVP_PKEY_keygen(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_PKEY_CTX *pctx = (EVP_PKEY_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t ppkey_off = (uint32_t)_sp[2];
    uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, ppkey_off, sizeof(uint32_t));
    EVP_PKEY *out = NULL;
    int rc = (pctx && slot) ? EVP_PKEY_keygen(pctx, &out) : 0;
    if (slot) *slot = ssl_handles_wrap(ctx, out);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* EVP_PKEY_get_raw_{private,public}_key — i32(pkey_h, buf_off, len_off).
 * len_off is uint32_t * (size_t on wasm32 = uint32_t). */
static int br_pkey_get_raw_common(struct yos_exec_ctx *ctx,
                                  uint64_t *sp,
                                  int (*fn)(const EVP_PKEY *, unsigned char *, size_t *))
{
    EVP_PKEY *p = (EVP_PKEY *)ssl_handles_resolve(ctx, (uint32_t)sp[1]);
    if (!p) return 0;
    uint32_t buf_off = (uint32_t)sp[2];
    uint32_t len_off = (uint32_t)sp[3];
    uint32_t *lp = (uint32_t *)guest_buf_rw(ctx, len_off, sizeof(uint32_t));
    if (!lp) return 0;
    size_t want = *lp;
    /* Two call modes: NULL buf to query length, else fill. */
    unsigned char *buf = NULL;
    if (buf_off) {
        buf = (unsigned char *)guest_buf_rw(ctx, buf_off, want);
        if (!buf) return 0;
    }
    int rc = fn(p, buf, &want);
    *lp = (uint32_t)want;
    return rc;
}
static const void *m3_yos_EVP_PKEY_get_raw_private_key(IM3Runtime rt, IM3ImportContext _c,
                                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)br_pkey_get_raw_common(CTX(rt), _sp, EVP_PKEY_get_raw_private_key);
    return NULL;
}
static const void *m3_yos_EVP_PKEY_get_raw_public_key(IM3Runtime rt, IM3ImportContext _c,
                                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)br_pkey_get_raw_common(CTX(rt), _sp, EVP_PKEY_get_raw_public_key);
    return NULL;
}

/* ── RSA family ─────────────────────────────────────────────────── */
BR_RETPTR_NOARG (RSA_new,           RSA_new)
BR_VOID_HND     (RSA_free,          RSA_free,    RSA)
BR_INT_HND      (RSA_size,          RSA_size,    RSA)
/* RSA_blinding_on — i32(rsa_h, bn_ctx_h). NULL bn_ctx is the
 * documented "use default" case; resolve only the RSA strictly. */
static const void *m3_yos_RSA_blinding_on(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BN_CTX *bn = (uint32_t)_sp[2] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)(r ? RSA_blinding_on(r, bn) : 0);
    return NULL;
}

/* ── EC_KEY family ──────────────────────────────────────────────── */
BR_RETPTR_NOARG (EC_KEY_new,                  EC_KEY_new)
BR_VOID_HND     (EC_KEY_free,                 EC_KEY_free,         EC_KEY)
BR_PTR_INT      (EC_KEY_new_by_curve_name,    EC_KEY_new_by_curve_name)
BR_PTR_HND      (EC_KEY_dup,                  EC_KEY_dup,          EC_KEY)
BR_INT_HND      (EC_KEY_generate_key,         EC_KEY_generate_key, EC_KEY)
BR_RETPTR_NOARG (EC_KEY_OpenSSL,              EC_KEY_OpenSSL)

/* ── BIGNUM family ──────────────────────────────────────────────── */
BR_RETPTR_NOARG (BN_new,        BN_new)
BR_VOID_HND     (BN_free,       BN_free,       BIGNUM)
BR_VOID_HND     (BN_clear_free, BN_clear_free, BIGNUM)
BR_PTR_HND      (BN_dup,        BN_dup,        BIGNUM)
BR_INT_HND      (BN_num_bits,   BN_num_bits,   BIGNUM)
BR_INT_HND_HND  (BN_cmp,        BN_cmp,        BIGNUM, BIGNUM)
BR_INT_HND_INT  (BN_is_bit_set, BN_is_bit_set, BIGNUM)
BR_INT_HND      (BN_is_negative,BN_is_negative,BIGNUM)
BR_RETPTR_NOARG (BN_value_one,  BN_value_one)
BR_RETPTR_NOARG (BN_CTX_new,    BN_CTX_new)
BR_VOID_HND     (BN_CTX_free,   BN_CTX_free,   BN_CTX)

/* BN_set_flags — v(bn_h, int flags). Void bridge: args at sp[0..1]. */
static const void *m3_yos_BN_set_flags(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    BIGNUM *b = (BIGNUM *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[0]);
    if (b) BN_set_flags(b, (int)_sp[1]);
    return NULL;
}

/* BN_sub — i32(r_h, a_h, b_h) */
static const void *m3_yos_BN_sub(IM3Runtime rt, IM3ImportContext _c,
                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *r = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIGNUM *a = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM *b = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    _sp[0] = (uint64_t)(uint32_t)((r && a && b) ? BN_sub(r, a, b) : 0);
    return NULL;
}

/* BN_add / BN_mul / BN_mod — same shape as BN_sub but optional BN_CTX
 * for the latter two. */
static const void *m3_yos_BN_add(IM3Runtime rt, IM3ImportContext _c,
                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *r = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIGNUM *a = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM *b = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    _sp[0] = (uint64_t)(uint32_t)((r && a && b) ? BN_add(r, a, b) : 0);
    return NULL;
}

static const void *m3_yos_BN_mul(IM3Runtime rt, IM3ImportContext _c,
                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *r = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIGNUM *a = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM *b = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    BN_CTX *bn = (uint32_t)_sp[4] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[4]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((r && a && b) ? BN_mul(r, a, b, bn) : 0);
    return NULL;
}

/* BN_mod is a host-side #define for BN_div(NULL, rem, m, d, ctx); we
 * bridge it identically through env.BN_mod only if openssh's wasm code
 * actually imports the symbol. Skip declaring an extern (no host
 * symbol) and route guests through BN_div. */
static const void *m3_yos_BN_mod(IM3Runtime rt, IM3ImportContext _c,
                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *rem = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIGNUM *a   = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM *m   = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    BN_CTX *bn  = (uint32_t)_sp[4] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[4]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((rem && a && m) ? BN_div(NULL, rem, a, m, bn) : 0);
    return NULL;
}

/* BN_div — i32(dv_h, rem_h, a_h, d_h, ctx_h). dv and rem may be 0
 * (NULL) to skip writing those outputs. */
static const void *m3_yos_BN_div(IM3Runtime rt, IM3ImportContext _c,
                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *dv  = (uint32_t)_sp[1] ? (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]) : NULL;
    BIGNUM *rem = (uint32_t)_sp[2] ? (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]) : NULL;
    BIGNUM *a   = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    BIGNUM *d   = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[4]);
    BN_CTX *bn  = (uint32_t)_sp[5] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]) : NULL;
    int rc = (a && d) ? BN_div(dv, rem, a, d, bn) : 0;
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        unsigned long err = rc ? 0 : ERR_get_error();
        char ebuf[256] = "";
        if (err) ERR_error_string_n(err, ebuf, sizeof(ebuf));
        fprintf(stderr, "[BN_div] dv=%p rem=%p a=%p(%d) d=%p(%d) ctx=%p rc=%d err=%s\n",
                (void *)dv, (void *)rem,
                (void *)a, a ? BN_num_bits(a) : -1,
                (void *)d, d ? BN_num_bits(d) : -1,
                (void *)bn, rc, ebuf);
    }
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* BN_mod_inverse — i32(r_h, a_h, n_h, ctx_h). r may be 0 (allocate). */
static const void *m3_yos_BN_mod_inverse(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *r = (uint32_t)_sp[1] ? (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]) : NULL;
    BIGNUM *a = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM *n = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    BN_CTX *bn= (uint32_t)_sp[4] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[4]) : NULL;
    /* BN_mod_inverse returns the BIGNUM* result; on failure NULL. We
     * encode as i32 success/0. If r was 0 (caller wanted alloc), wrap
     * the returned bn in a fresh handle and shove it back? wasm-side
     * signature is i(iiii) returning int; openssh treats it as bool. */
    void *res = (a && n) ? (void *)BN_mod_inverse(r, a, n, bn) : NULL;
    _sp[0] = (uint64_t)(uint32_t)(res ? 1 : 0);
    return NULL;
}

/* BN_hex2bn — i32(out_off, str_off). out_off is BIGNUM** in guest. */
static const void *m3_yos_BN_hex2bn(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t out_off = (uint32_t)_sp[1];
    uint32_t str_off = (uint32_t)_sp[2];
    const char *str = guest_cstr(ctx, str_off);
    if (!str) { _sp[0] = 0; return NULL; }
    BIGNUM *bn = NULL;
    int rc = BN_hex2bn(&bn, str);
    if (out_off) {
        uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, out_off, sizeof(uint32_t));
        if (slot) *slot = bn ? ssl_handles_wrap(ctx, bn) : 0;
    }
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* BN_bn2bin — i32(bn_h, to_off). Returns byte count written. */
static const void *m3_yos_BN_bn2bin(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    BIGNUM *a = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!a) { _sp[0] = 0; return NULL; }
    int nbytes = (BN_num_bits(a) + 7) / 8;
    unsigned char *to = (unsigned char *)guest_buf_rw(ctx, (uint32_t)_sp[2],
                                                      (uint32_t)nbytes);
    _sp[0] = (uint64_t)(uint32_t)(to ? BN_bn2bin(a, to) : 0);
    return NULL;
}

/* BN_bin2bn — i32(s_off, int len, ret_h). ret_h may be 0 (NULL) to
 * allocate fresh. Returns the resulting BIGNUM as a handle. */
static const void *m3_yos_BN_bin2bn(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      len = (int)_sp[2];
    uint32_t ret_h = (uint32_t)_sp[3];
    const unsigned char *s = (len > 0) ? (const unsigned char *)guest_buf_ro(ctx, off, (uint32_t)len) : NULL;
    BIGNUM *ret = ret_h ? (BIGNUM *)ssl_handles_resolve(ctx, ret_h) : NULL;
    BIGNUM *out = (s || len == 0) ? BN_bin2bn(s, len, ret) : NULL;
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        fprintf(stderr, "[BN_bin2bn] off=%u len=%d ret_h=%u -> %p (%d bits)\n",
                off, len, ret_h, (void *)out, out ? BN_num_bits(out) : -1);
    }
    /* If caller passed an existing handle, return the same handle ID
     * (openssl writes into it in place). Otherwise wrap fresh. */
    if (out && ret_h && out == ret) _sp[0] = (uint64_t)ret_h;
    else _sp[0] = (uint64_t)ssl_handles_wrap(ctx, out);
    return NULL;
}

/* ── DH family ──────────────────────────────────────────────────── */
BR_RETPTR_NOARG (DH_new,  DH_new)
BR_VOID_HND     (DH_free, DH_free, DH)
BR_INT_HND      (DH_size, DH_size, DH)
BR_INT_HND      (DH_generate_key, DH_generate_key, DH)

/* DH_set_length — i32(dh_h, long length). `long` is i32 on wasm32. */
static const void *m3_yos_DH_set_length(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    DH *dh = (DH *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    long n  = (long)(int32_t)_sp[2];
    _sp[0] = (uint64_t)(uint32_t)(dh ? DH_set_length(dh, n) : 0);
    return NULL;
}

/* DH_compute_key — i32(key_off, pub_key_h, dh_h). */
static const void *m3_yos_DH_compute_key(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t key_off = (uint32_t)_sp[1];
    BIGNUM  *pub = (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    DH      *dh  = (DH *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    if (!pub || !dh) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    /* DH key length = DH_size(dh) bytes. */
    int sz = DH_size(dh);
    unsigned char *out = (sz > 0) ? (unsigned char *)guest_buf_rw(ctx, key_off, (uint32_t)sz) : NULL;
    int rc = out ? DH_compute_key(out, pub, dh) : -1;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* Helper: ownership-transfer "set0" call with N input BIGNUMs.
 * After a successful call, openssl owns the BIGNUMs — we MUST
 * release the handle table slots so the guest can't double-free.
 * Pre-call we keep them; openssl's set0 documentation guarantees:
 * "if the call fails, ownership is retained by the caller". So on
 * failure we DON'T release. */
static void release_handles_on_success(struct yos_exec_ctx *ctx, int rc,
                                       uint32_t h1, uint32_t h2,
                                       uint32_t h3, uint32_t h4)
{
    if (!rc) return;
    if (h1) ssl_handles_release(ctx, h1);
    if (h2) ssl_handles_release(ctx, h2);
    if (h3) ssl_handles_release(ctx, h3);
    if (h4) ssl_handles_release(ctx, h4);
}

/* DH_set0_pqg — i32(dh_h, p_h, q_h, g_h). */
static const void *m3_yos_DH_set0_pqg(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    DH     *dh = (DH *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t h_p = (uint32_t)_sp[2], h_q = (uint32_t)_sp[3], h_g = (uint32_t)_sp[4];
    BIGNUM *p = (BIGNUM *)ssl_handles_resolve(ctx, h_p);
    BIGNUM *q = h_q ? (BIGNUM *)ssl_handles_resolve(ctx, h_q) : NULL;
    BIGNUM *g = (BIGNUM *)ssl_handles_resolve(ctx, h_g);
    int rc = (dh && p && g) ? DH_set0_pqg(dh, p, q, g) : 0;
    release_handles_on_success(ctx, rc, h_p, h_q, h_g, 0);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* Helper for *_get0_* out-pointer writebacks: takes a wasm-side
 * uint32_t* slot and writes a host BIGNUM* there as a NEW handle. */
static void store_handle_at(struct yos_exec_ctx *ctx, uint32_t off, void *p)
{
    if (!off) return;
    uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, off, sizeof(uint32_t));
    if (!slot) return;
    *slot = p ? ssl_handles_wrap(ctx, p) : 0;
}

/* DH_get0_pqg — v(dh_h, p_off, q_off, g_off). Void: args at sp[0..3]. */
static const void *m3_yos_DH_get0_pqg(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    DH *dh = (DH *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *p = NULL, *q = NULL, *g = NULL;
    if (dh) DH_get0_pqg(dh, &p, &q, &g);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)p);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)q);
    store_handle_at(ctx, (uint32_t)_sp[3], (void *)g);
    return NULL;
}

/* DH_get0_key — v(dh_h, pub_off, priv_off). Void: args at sp[0..2]. */
static const void *m3_yos_DH_get0_key(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    DH *dh = (DH *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *pub = NULL, *priv = NULL;
    if (dh) DH_get0_key(dh, &pub, &priv);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)pub);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)priv);
    return NULL;
}

/* ── RSA set0/get0 ──────────────────────────────────────────────── */

/* RSA_set0_key — i32(r_h, n_h, e_h, d_h). e may be NULL for some
 * pure-private cases; in openssh 9.9 the flow is:
 *   1) RSA_set0_key(rsa, n, e, NULL)  — set public components
 *   2) RSA_set0_key(rsa, NULL, NULL, d) — add private exponent
 * So we need to ALLOW n=NULL, e=NULL when d is non-NULL. The host
 * openssl's check is `(r->n==NULL && n==NULL) || (r->e==NULL && e==NULL)`
 * — meaning at least the combined state must be non-NULL. Our bridge
 * MUST forward the call even when n and e are 0 (NULL), letting host
 * libcrypto evaluate that combined state. */
static const void *m3_yos_RSA_set0_key(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t h_n = (uint32_t)_sp[2], h_e = (uint32_t)_sp[3], h_d = (uint32_t)_sp[4];
    BIGNUM *n = h_n ? (BIGNUM *)ssl_handles_resolve(ctx, h_n) : NULL;
    BIGNUM *e = h_e ? (BIGNUM *)ssl_handles_resolve(ctx, h_e) : NULL;
    BIGNUM *d = h_d ? (BIGNUM *)ssl_handles_resolve(ctx, h_d) : NULL;
    int rc = r ? RSA_set0_key(r, n, e, d) : 0;
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        unsigned long err = rc ? 0 : ERR_get_error();
        char ebuf[256] = "";
        if (err) ERR_error_string_n(err, ebuf, sizeof(ebuf));
        fprintf(stderr, "[RSA_set0_key] r_h=%u r=%p  n_h=%u n=%p(%d bits)  e_h=%u e=%p(%d bits)  d_h=%u -> rc=%d  err=%s\n",
                (uint32_t)_sp[1], (void *)r,
                h_n, (void *)n, n ? BN_num_bits(n) : -1,
                h_e, (void *)e, e ? BN_num_bits(e) : -1,
                h_d, rc, ebuf);
    }
    release_handles_on_success(ctx, rc, h_n, h_e, h_d, 0);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* RSA_set0_factors — i32(r_h, p_h, q_h). */
static const void *m3_yos_RSA_set0_factors(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t h_p = (uint32_t)_sp[2], h_q = (uint32_t)_sp[3];
    BIGNUM *p = (BIGNUM *)ssl_handles_resolve(ctx, h_p);
    BIGNUM *q = (BIGNUM *)ssl_handles_resolve(ctx, h_q);
    int rc = (r && p && q) ? RSA_set0_factors(r, p, q) : 0;
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        unsigned long err = rc ? 0 : ERR_get_error();
        char ebuf[256] = "";
        if (err) ERR_error_string_n(err, ebuf, sizeof(ebuf));
        fprintf(stderr, "[RSA_set0_factors] r=%p p=%p(%d) q=%p(%d) rc=%d err=%s\n",
                (void *)r, (void *)p, p ? BN_num_bits(p) : -1,
                (void *)q, q ? BN_num_bits(q) : -1, rc, ebuf);
    }
    release_handles_on_success(ctx, rc, h_p, h_q, 0, 0);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* RSA_set0_crt_params — i32(r_h, dmp1_h, dmq1_h, iqmp_h). */
static const void *m3_yos_RSA_set0_crt_params(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t h1 = (uint32_t)_sp[2], h2 = (uint32_t)_sp[3], h3 = (uint32_t)_sp[4];
    BIGNUM *dmp1 = (BIGNUM *)ssl_handles_resolve(ctx, h1);
    BIGNUM *dmq1 = (BIGNUM *)ssl_handles_resolve(ctx, h2);
    BIGNUM *iqmp = (BIGNUM *)ssl_handles_resolve(ctx, h3);
    int rc = (r && dmp1 && dmq1 && iqmp) ? RSA_set0_crt_params(r, dmp1, dmq1, iqmp) : 0;
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        unsigned long err = rc ? 0 : ERR_get_error();
        char ebuf[256] = "";
        if (err) ERR_error_string_n(err, ebuf, sizeof(ebuf));
        fprintf(stderr, "[RSA_set0_crt_params] r=%p dmp1=%p(%d) dmq1=%p(%d) iqmp=%p(%d) rc=%d err=%s\n",
                (void *)r,
                (void *)dmp1, dmp1 ? BN_num_bits(dmp1) : -1,
                (void *)dmq1, dmq1 ? BN_num_bits(dmq1) : -1,
                (void *)iqmp, iqmp ? BN_num_bits(iqmp) : -1,
                rc, ebuf);
    }
    release_handles_on_success(ctx, rc, h1, h2, h3, 0);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* RSA_get0_key — v(r_h, n_off, e_off, d_off). Void: args at sp[0..3]. */
static const void *m3_yos_RSA_get0_key(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *n = NULL, *e = NULL, *d = NULL;
    if (r) RSA_get0_key(r, &n, &e, &d);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)n);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)e);
    store_handle_at(ctx, (uint32_t)_sp[3], (void *)d);
    return NULL;
}

/* RSA_get0_factors — v(r_h, p_off, q_off). Void: args at sp[0..2]. */
static const void *m3_yos_RSA_get0_factors(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *p = NULL, *q = NULL;
    if (r) RSA_get0_factors(r, &p, &q);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)p);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)q);
    return NULL;
}

/* RSA_get0_crt_params — v(r_h, dmp1_off, dmq1_off, iqmp_off). Void: args sp[0..3]. */
static const void *m3_yos_RSA_get0_crt_params(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    RSA *r = (RSA *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *a = NULL, *b = NULL, *c = NULL;
    if (r) RSA_get0_crt_params(r, &a, &b, &c);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)a);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)b);
    store_handle_at(ctx, (uint32_t)_sp[3], (void *)c);
    return NULL;
}

/* ── EC_KEY set/get0 ─────────────────────────────────────────────── */
BR_INT_HND_HND  (EC_KEY_set_group,        EC_KEY_set_group,       EC_KEY, EC_GROUP)
BR_INT_HND_HND  (EC_KEY_set_public_key,   EC_KEY_set_public_key,  EC_KEY, EC_POINT)
BR_INT_HND_HND  (EC_KEY_set_private_key,  EC_KEY_set_private_key, EC_KEY, BIGNUM)
BR_PTR_HND      (EC_KEY_get0_group,       EC_KEY_get0_group,      EC_KEY)
BR_PTR_HND      (EC_KEY_get0_public_key,  EC_KEY_get0_public_key, EC_KEY)
BR_PTR_HND      (EC_KEY_get0_private_key, EC_KEY_get0_private_key,EC_KEY)

/* ── EC_GROUP / EC_POINT / EC_METHOD ─────────────────────────────── */
BR_PTR_INT      (EC_GROUP_new_by_curve_name, EC_GROUP_new_by_curve_name)
BR_VOID_HND     (EC_GROUP_free,              EC_GROUP_free,            EC_GROUP)
BR_INT_HND      (EC_GROUP_get_curve_name,    EC_GROUP_get_curve_name,  EC_GROUP)
BR_INT_HND      (EC_GROUP_get_degree,        EC_GROUP_get_degree,      EC_GROUP)
BR_PTR_HND      (EC_GROUP_method_of,         EC_GROUP_method_of,       EC_GROUP)
BR_INT_HND      (EC_METHOD_get_field_type,   EC_METHOD_get_field_type, EC_METHOD)

/* EC_GROUP_set_asn1_flag — v(group_h, int flag). Void: args sp[0..1]. */
static const void *m3_yos_EC_GROUP_set_asn1_flag(IM3Runtime rt, IM3ImportContext _c,
                                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    EC_GROUP *g = (EC_GROUP *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[0]);
    if (g) EC_GROUP_set_asn1_flag(g, (int)_sp[1]);
    return NULL;
}

/* EC_GROUP_get_order — i32(group_h, order_h, ctx_h) */
static const void *m3_yos_EC_GROUP_get_order(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *g = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    BIGNUM   *o = (BIGNUM *)  ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BN_CTX   *bn = (uint32_t)_sp[3] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((g && o) ? EC_GROUP_get_order(g, o, bn) : 0);
    return NULL;
}

/* EC_GROUP_cmp — i32(a_h, b_h, ctx_h) */
static const void *m3_yos_EC_GROUP_cmp(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *a = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    EC_GROUP *b = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BN_CTX   *bn= (uint32_t)_sp[3] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]) : NULL;
    /* EC_GROUP_cmp returns 0 if equal, 1 if not — pass nonexistent as
     * "different" (1). */
    _sp[0] = (uint64_t)(uint32_t)((a && b) ? EC_GROUP_cmp(a, b, bn) : 1);
    return NULL;
}

BR_PTR_HND     (EC_POINT_new,        EC_POINT_new,       EC_GROUP)
BR_VOID_HND    (EC_POINT_free,       EC_POINT_free,      EC_POINT)
BR_VOID_HND    (EC_POINT_clear_free, EC_POINT_clear_free,EC_POINT)
BR_INT_HND_HND (EC_POINT_is_at_infinity, EC_POINT_is_at_infinity, EC_GROUP, EC_POINT)

/* EC_POINT_mul — i32(group_h, r_h, n_h, q_h, m_h, ctx_h). Any of n/q/m
 * may be NULL (encoded as handle 0). */
static const void *m3_yos_EC_POINT_mul(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *g = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    EC_POINT *r = (EC_POINT *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM   *n = (uint32_t)_sp[3] ? (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]) : NULL;
    EC_POINT *q = (uint32_t)_sp[4] ? (EC_POINT *)ssl_handles_resolve(ctx, (uint32_t)_sp[4]) : NULL;
    BIGNUM   *mm= (uint32_t)_sp[5] ? (BIGNUM *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]) : NULL;
    BN_CTX   *bn= (uint32_t)_sp[6] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[6]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((g && r) ? EC_POINT_mul(g, r, n, q, mm, bn) : 0);
    return NULL;
}

/* EC_POINT_get_affine_coordinates_GFp — i32(group_h, point_h, x_h, y_h, ctx_h). */
static const void *m3_yos_EC_POINT_get_affine_coordinates_GFp(IM3Runtime rt, IM3ImportContext _c,
                                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *g = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    EC_POINT *p = (EC_POINT *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    BIGNUM   *x = (BIGNUM *)  ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    BIGNUM   *y = (BIGNUM *)  ssl_handles_resolve(ctx, (uint32_t)_sp[4]);
    BN_CTX   *bn= (uint32_t)_sp[5] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((g && p && x && y) ? EC_POINT_get_affine_coordinates_GFp(g, p, x, y, bn) : 0);
    return NULL;
}

/* EC_POINT_oct2point — i32(group_h, point_h, buf_off, size_t len, ctx_h). */
static const void *m3_yos_EC_POINT_oct2point(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *g  = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    EC_POINT *pt = (EC_POINT *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    uint32_t off = (uint32_t)_sp[3];
    uint32_t len = (uint32_t)_sp[4];
    BN_CTX   *bn = (uint32_t)_sp[5] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]) : NULL;
    const unsigned char *buf = (len > 0) ? (const unsigned char *)guest_buf_ro(ctx, off, len) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((g && pt && (buf || len == 0))
                                   ? EC_POINT_oct2point(g, pt, buf, len, bn) : 0);
    return NULL;
}

/* EC_POINT_point2oct — i32(group_h, point_h, int form, buf_off, size_t len, ctx_h). */
static const void *m3_yos_EC_POINT_point2oct(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EC_GROUP *g  = (EC_GROUP *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    EC_POINT *pt = (EC_POINT *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    int      form = (int)_sp[3];
    uint32_t off = (uint32_t)_sp[4];
    uint32_t len = (uint32_t)_sp[5];
    BN_CTX   *bn = (uint32_t)_sp[6] ? (BN_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[6]) : NULL;
    unsigned char *buf = (off && len > 0) ? (unsigned char *)guest_buf_rw(ctx, off, len) : NULL;
    /* off==0 + len==0 = "query required size" mode; pass NULL+0. */
    size_t rc = (g && pt) ? EC_POINT_point2oct(g, pt, form, buf, len, bn) : 0;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── ECDSA_SIG ───────────────────────────────────────────────────── */
BR_RETPTR_NOARG (ECDSA_SIG_new,  ECDSA_SIG_new)
BR_VOID_HND     (ECDSA_SIG_free, ECDSA_SIG_free, ECDSA_SIG)

/* ECDSA_SIG_get0 — v(sig_h, r_off, s_off). Void: args sp[0..2]. */
static const void *m3_yos_ECDSA_SIG_get0(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    ECDSA_SIG *sig = (ECDSA_SIG *)ssl_handles_resolve(ctx, (uint32_t)_sp[0]);
    const BIGNUM *r = NULL, *s = NULL;
    if (sig) ECDSA_SIG_get0(sig, &r, &s);
    store_handle_at(ctx, (uint32_t)_sp[1], (void *)r);
    store_handle_at(ctx, (uint32_t)_sp[2], (void *)s);
    return NULL;
}

/* ECDSA_SIG_set0 — i32(sig_h, r_h, s_h). Ownership transfers on success. */
static const void *m3_yos_ECDSA_SIG_set0(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    ECDSA_SIG *sig = (ECDSA_SIG *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t h_r = (uint32_t)_sp[2], h_s = (uint32_t)_sp[3];
    BIGNUM *r = (BIGNUM *)ssl_handles_resolve(ctx, h_r);
    BIGNUM *s = (BIGNUM *)ssl_handles_resolve(ctx, h_s);
    int rc = (sig && r && s) ? ECDSA_SIG_set0(sig, r, s) : 0;
    release_handles_on_success(ctx, rc, h_r, h_s, 0, 0);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── EVP_CIPHER (algorithm getters) ────────────────────────────── */
BR_RETPTR_NOARG (EVP_aes_128_cbc, EVP_aes_128_cbc)
BR_RETPTR_NOARG (EVP_aes_192_cbc, EVP_aes_192_cbc)
BR_RETPTR_NOARG (EVP_aes_256_cbc, EVP_aes_256_cbc)
BR_RETPTR_NOARG (EVP_aes_128_ctr, EVP_aes_128_ctr)
BR_RETPTR_NOARG (EVP_aes_192_ctr, EVP_aes_192_ctr)
BR_RETPTR_NOARG (EVP_aes_256_ctr, EVP_aes_256_ctr)
BR_RETPTR_NOARG (EVP_aes_128_gcm, EVP_aes_128_gcm)
BR_RETPTR_NOARG (EVP_aes_256_gcm, EVP_aes_256_gcm)
BR_RETPTR_NOARG (EVP_chacha20,    EVP_chacha20)
BR_RETPTR_NOARG (EVP_des_ede3_cbc, EVP_des_ede3_cbc)

/* ── EVP_CIPHER_CTX ────────────────────────────────────────────── */
BR_RETPTR_NOARG (EVP_CIPHER_CTX_new,  EVP_CIPHER_CTX_new)
BR_VOID_HND     (EVP_CIPHER_CTX_free, EVP_CIPHER_CTX_free, EVP_CIPHER_CTX)
BR_INT_HND      (EVP_CIPHER_CTX_get_iv_length,  EVP_CIPHER_CTX_get_iv_length,  EVP_CIPHER_CTX)
BR_INT_HND      (EVP_CIPHER_CTX_get_key_length, EVP_CIPHER_CTX_get_key_length, EVP_CIPHER_CTX)
BR_INT_HND_INT  (EVP_CIPHER_CTX_set_key_length, EVP_CIPHER_CTX_set_key_length, EVP_CIPHER_CTX)

/* EVP_CIPHER_CTX_ctrl — i32(ctx_h, int type, int arg, ptr_off). The
 * ptr arg is a polymorphic out-pointer like BIO_ctrl. Pass the guest
 * memory pointer through. NOTE: some EVP_CTRL_* control codes pass
 * SIZES + arrays through ptr — those may need per-cmd translation
 * later; the simple ones (set tag, get tag bytes) work as-is. */
static const void *m3_yos_EVP_CIPHER_CTX_ctrl(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_CIPHER_CTX *c = (EVP_CIPHER_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    int      type = (int)_sp[2];
    int      arg  = (int)_sp[3];
    uint32_t ptr_off = (uint32_t)_sp[4];
    void *ptr = (ptr_off && ctx->memory && ptr_off < ctx->memory_size)
                  ? (void *)(ctx->memory + ptr_off) : NULL;
    _sp[0] = (uint64_t)(uint32_t)(c ? EVP_CIPHER_CTX_ctrl(c, type, arg, ptr) : 0);
    return NULL;
}

/* EVP_CIPHER_CTX_get_updated_iv — i32(ctx_h, buf_off, size_t len). */
static const void *m3_yos_EVP_CIPHER_CTX_get_updated_iv(IM3Runtime rt, IM3ImportContext _c,
                                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_CIPHER_CTX *c = (EVP_CIPHER_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    uint32_t len = (uint32_t)_sp[3];
    void *buf = (len > 0) ? guest_buf_rw(ctx, off, len) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((c && (buf || len == 0))
                                    ? EVP_CIPHER_CTX_get_updated_iv(c, buf, len) : 0);
    return NULL;
}

/* EVP_CIPHER_CTX_set_params — i32(ctx_h, params_off).
 * Translate a NUL-terminated array of WASM-OSSL_PARAM (20 bytes each)
 * to a host-side OSSL_PARAM array (40 bytes each), pointer-translate
 * each .key and .data field, then call. The PARAM array is short
 * (typically 1-3 entries); a stack-allocated buffer of 8 entries is
 * comfortable. */
static const void *m3_yos_EVP_CIPHER_CTX_set_params(IM3Runtime rt, IM3ImportContext _c,
                                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_CIPHER_CTX *c = (EVP_CIPHER_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t arr_off = (uint32_t)_sp[2];
    if (!c || !arr_off) { _sp[0] = 0; return NULL; }

    /* Bound the array at 8 entries. Each wasm-side entry is 20 bytes;
     * we walk until we see a key==NULL terminator. */
    enum { MAX = 8 };
    OSSL_PARAM hp[MAX + 1];
    memset(hp, 0, sizeof(hp));
    uint32_t n = 0;
    for (n = 0; n < MAX; ++n) {
        uint32_t base = arr_off + n * YOS_OSSL_PARAM_WASM_SIZE;
        const uint8_t *w = (const uint8_t *)guest_buf_ro(ctx, base, YOS_OSSL_PARAM_WASM_SIZE);
        if (!w) { _sp[0] = 0; return NULL; }
        uint32_t key_off  = *(const uint32_t *)(w + 0);
        uint32_t dtype    = *(const uint32_t *)(w + 4);
        uint32_t data_off = *(const uint32_t *)(w + 8);
        uint32_t dsize    = *(const uint32_t *)(w + 12);
        if (key_off == 0) break;   /* terminator */
        const char *key = guest_cstr(ctx, key_off);
        void *data = data_off ? (void *)(ctx->memory + data_off) : NULL;
        if (!key) { _sp[0] = 0; return NULL; }
        hp[n].key        = key;
        hp[n].data_type  = dtype;
        hp[n].data       = data;
        hp[n].data_size  = dsize;
        hp[n].return_size = (size_t)-1;
    }
    /* hp[n] stays zeroed → OSSL_PARAM_END sentinel. */
    _sp[0] = (uint64_t)(uint32_t)EVP_CIPHER_CTX_set_params(c, hp);
    /* Write back the return_size each entry got from openssl, in case
     * the guest needs it (rarely used; cheap to do). */
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t base = arr_off + i * YOS_OSSL_PARAM_WASM_SIZE;
        uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, base + 16, sizeof(uint32_t));
        if (slot) *slot = (uint32_t)hp[i].return_size;
    }
    return NULL;
}

/* env.OSSL_PARAM_construct_octet_string — wasm-side struct constructor.
 *
 * Host openssl returns OSSL_PARAM by value. wasm32 clang lowers the
 * by-value struct return to an sret-style first arg (the wasm-side
 * type is `void(out_ptr, key, buf, bsize)`). The bridge writes the
 * wasm-layout struct directly; no host call needed. Lowering to
 * host's 40-byte OSSL_PARAM happens later in EVP_CIPHER_CTX_set_params
 * when the array is consumed. Void bridge: args at sp[0..3]. */
static const void *m3_yos_OSSL_PARAM_construct_octet_string(IM3Runtime rt, IM3ImportContext _c,
                                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t out_off = (uint32_t)_sp[0];
    uint32_t key_off = (uint32_t)_sp[1];
    uint32_t buf_off = (uint32_t)_sp[2];
    uint32_t bsize   = (uint32_t)_sp[3];
    uint8_t *out = (uint8_t *)guest_buf_rw(ctx, out_off, YOS_OSSL_PARAM_WASM_SIZE);
    if (!out) return NULL;
    /* OSSL_PARAM_OCTET_STRING == 5 in openssl-3.x core_dispatch.h. */
    *(uint32_t *)(out + 0)  = key_off;
    *(uint32_t *)(out + 4)  = 5;
    *(uint32_t *)(out + 8)  = buf_off;
    *(uint32_t *)(out + 12) = bsize;
    *(uint32_t *)(out + 16) = (uint32_t)-1;
    return NULL;
}

/* env.OSSL_PARAM_construct_end — v(out_off). Sentinel: all zeros.
 * Void bridge: arg at sp[0]. */
static const void *m3_yos_OSSL_PARAM_construct_end(IM3Runtime rt, IM3ImportContext _c,
                                                   uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t out_off = (uint32_t)_sp[0];
    uint8_t *out = (uint8_t *)guest_buf_rw(ctx, out_off, YOS_OSSL_PARAM_WASM_SIZE);
    if (out) memset(out, 0, YOS_OSSL_PARAM_WASM_SIZE);
    return NULL;
}

/* EVP_CipherInit — i32(ctx_h, cipher_h, key_off, iv_off, int enc). */
static const void *m3_yos_EVP_CipherInit(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_CIPHER_CTX *c = (EVP_CIPHER_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    const EVP_CIPHER *cph = (const EVP_CIPHER *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    uint32_t key_off = (uint32_t)_sp[3];
    uint32_t iv_off  = (uint32_t)_sp[4];
    int enc = (int)_sp[5];
    /* key/iv length is implied by cipher; assume up to 64 bytes for
     * bounds. Better: query the cipher's iv/key length, but most
     * SSH ciphers are <= 32 bytes. */
    const unsigned char *key = key_off ? (const unsigned char *)guest_buf_ro(ctx, key_off, 64) : NULL;
    const unsigned char *iv  = iv_off  ? (const unsigned char *)guest_buf_ro(ctx, iv_off,  64) : NULL;
    _sp[0] = (uint64_t)(uint32_t)(c ? EVP_CipherInit(c, cph, key, iv, enc) : 0);
    return NULL;
}

/* EVP_Cipher — i32(ctx_h, out_off, in_off, uint inl). */
static const void *m3_yos_EVP_Cipher(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_CIPHER_CTX *c = (EVP_CIPHER_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t out_off = (uint32_t)_sp[2];
    uint32_t in_off  = (uint32_t)_sp[3];
    uint32_t inl     = (uint32_t)_sp[4];
    unsigned char *out = (inl > 0) ? (unsigned char *)guest_buf_rw(ctx, out_off, inl) : NULL;
    const unsigned char *in = (inl > 0) ? (const unsigned char *)guest_buf_ro(ctx, in_off, inl) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((c && (inl == 0 || (out && in)))
                                    ? EVP_Cipher(c, out, in, inl) : 0);
    return NULL;
}

/* ── EVP_MD extras ──────────────────────────────────────────────── */
BR_INT_HND  (EVP_MD_get_block_size, EVP_MD_get_block_size, EVP_MD)
BR_PTR_HND  (EVP_MD_CTX_get0_md,    EVP_MD_CTX_get0_md,    EVP_MD_CTX)
BR_INT_HND_HND (EVP_MD_CTX_copy_ex, EVP_MD_CTX_copy_ex,    EVP_MD_CTX, EVP_MD_CTX)

/* EVP_DigestSignInit — i32(md_ctx_h, pctx_off, md_h, engine_h, pkey_h).
 * pctx_off is a wasm-side EVP_PKEY_CTX** out-pointer. We pass NULL on
 * the host side if pctx_off is 0; otherwise we accept a host-side
 * EVP_PKEY_CTX* and wrap it as a handle written to *pctx_off. */
static const void *m3_yos_EVP_DigestSignInit(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc  = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t pctx_off = (uint32_t)_sp[2];
    const EVP_MD *md = (const EVP_MD *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    /* engine arg [4] ignored */
    EVP_PKEY *pkey = (EVP_PKEY *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]);
    EVP_PKEY_CTX *out_pctx = NULL;
    int rc = (mc && pkey) ? EVP_DigestSignInit(mc, pctx_off ? &out_pctx : NULL,
                                                md, NULL, pkey) : 0;
    if (pctx_off) store_handle_at(ctx, pctx_off, out_pctx);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* EVP_DigestSign — i32(md_ctx_h, sigret_off, siglen_off, tbs_off, size_t tbslen). */
static const void *m3_yos_EVP_DigestSign(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t sig_off = (uint32_t)_sp[2];
    uint32_t len_off = (uint32_t)_sp[3];
    uint32_t tbs_off = (uint32_t)_sp[4];
    uint32_t tbslen  = (uint32_t)_sp[5];
    uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, len_off, sizeof(uint32_t));
    if (!mc || !slot) { _sp[0] = 0; return NULL; }
    size_t want = *slot;
    unsigned char *sig = sig_off ? (unsigned char *)guest_buf_rw(ctx, sig_off, want) : NULL;
    const unsigned char *tbs = tbslen ? (const unsigned char *)guest_buf_ro(ctx, tbs_off, tbslen) : NULL;
    int rc = EVP_DigestSign(mc, sig, &want, tbs, tbslen);
    *slot = (uint32_t)want;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* EVP_DigestVerifyInit — same shape as Sign. */
static const void *m3_yos_EVP_DigestVerifyInit(IM3Runtime rt, IM3ImportContext _c,
                                               uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc  = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t pctx_off = (uint32_t)_sp[2];
    const EVP_MD *md = (const EVP_MD *)ssl_handles_resolve(ctx, (uint32_t)_sp[3]);
    EVP_PKEY *pkey = (EVP_PKEY *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]);
    EVP_PKEY_CTX *out_pctx = NULL;
    int rc = (mc && pkey) ? EVP_DigestVerifyInit(mc, pctx_off ? &out_pctx : NULL,
                                                  md, NULL, pkey) : 0;
    if (pctx_off) store_handle_at(ctx, pctx_off, out_pctx);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* EVP_DigestVerify — i32(md_ctx_h, sigret_off, size_t siglen, tbs_off, size_t tbslen). */
static const void *m3_yos_EVP_DigestVerify(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t sig_off = (uint32_t)_sp[2];
    uint32_t siglen  = (uint32_t)_sp[3];
    uint32_t tbs_off = (uint32_t)_sp[4];
    uint32_t tbslen  = (uint32_t)_sp[5];
    const unsigned char *sig = siglen ? (const unsigned char *)guest_buf_ro(ctx, sig_off, siglen) : NULL;
    const unsigned char *tbs = tbslen ? (const unsigned char *)guest_buf_ro(ctx, tbs_off, tbslen) : NULL;
    _sp[0] = (uint64_t)(uint32_t)((mc && (siglen == 0 || sig) && (tbslen == 0 || tbs))
                                    ? EVP_DigestVerify(mc, sig, siglen, tbs, tbslen) : 0);
    return NULL;
}

/* EVP_Digest — i32(data_off, size_t count, md_off, size_off, md_type_h, engine_h).
 * One-shot hash. size_off is uint32_t * (writes the digest length).
 * Engine handle ignored. */
static const void *m3_yos_EVP_Digest(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t data_off = (uint32_t)_sp[1];
    uint32_t count    = (uint32_t)_sp[2];
    uint32_t md_off   = (uint32_t)_sp[3];
    uint32_t size_off = (uint32_t)_sp[4];
    const EVP_MD *md_type = (const EVP_MD *)ssl_handles_resolve(ctx, (uint32_t)_sp[5]);
    if (!md_type) { _sp[0] = 0; return NULL; }
    const void   *data = count ? guest_buf_ro(ctx, data_off, count) : NULL;
    unsigned char *md = (unsigned char *)guest_buf_rw(ctx, md_off, 64);
    if (!md || (count > 0 && !data)) { _sp[0] = 0; return NULL; }
    unsigned int sz = 0;
    int rc = EVP_Digest(data, count, md, &sz, md_type, NULL);
    if (size_off) {
        uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, size_off, sizeof(uint32_t));
        if (slot) *slot = (uint32_t)sz;
    }
    /* Setting YOS_OPENSSL_DUMP_DIGEST=1 in the env enables a hex dump
     * of every hashed message + the resulting digest, used while
     * pinning down the publickey-auth signing path. Off by default. */
    if (getenv("YOS_OPENSSL_DUMP_DIGEST")) {
        const unsigned char *p = (const unsigned char *)data;
        fprintf(stderr, "[EVP_Digest] count=%u rc=%d sz=%u bytes=", count, rc, sz);
        for (uint32_t i = 0; i < count && i < 64; ++i) fprintf(stderr, "%02x", p[i]);
        if (count > 64) fprintf(stderr, "...");
        fprintf(stderr, "  digest=");
        for (uint32_t i = 0; i < sz && i < 32; ++i) fprintf(stderr, "%02x", md[i]);
        fprintf(stderr, "\n");
    }
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.ERR_error_string — i32(uint32 code, buf_off).
 *
 * Host signature: `char *ERR_error_string(unsigned long e, char *buf)`.
 * If buf is NULL, returns a pointer into a static 256-byte buffer
 * inside libcrypto — we can't expose that to the guest, so when
 * buf_off is 0 we return 0 (guests are expected to pass a buffer).
 * When buf_off is provided, openssl writes up to 256 bytes there;
 * we bound-check that range and return buf_off back to the guest
 * to match the host's "returns its buf arg" semantics. */
static const void *m3_yos_ERR_error_string(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    unsigned long e = (unsigned long)(uint32_t)_sp[1];
    uint32_t off = (uint32_t)_sp[2];
    if (!off) { _sp[0] = 0; return NULL; }
    char *buf = (char *)guest_buf_rw(ctx, off, 256);
    if (!buf) { _sp[0] = 0; return NULL; }
    ERR_error_string(e, buf);
    _sp[0] = (uint64_t)off;
    return NULL;
}

/* env.EVP_MD_CTX_new / EVP_MD_CTX_free — handle-table wrappers */
static const void *m3_yos_EVP_MD_CTX_new(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), EVP_MD_CTX_new());
    return NULL;
}
static const void *m3_yos_EVP_MD_CTX_free(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    /* "v(i)" — args at sp[0] (not sp[1]; see comment above BR_RETPTR_NOARG).
     * Reading sp[1] before this fix gave a stale value from an earlier
     * bridge return slot, which on linux happened to fall in-range of
     * ctx->ssl_handles, so ssl_handles_release returned the WRONG host
     * EVP_MD_CTX *. The subsequent host EVP_MD_CTX_free called
     * EVP_PKEY_CTX_free on that mismatched object's .pctx field —
     * SIGSEGV inside libcrypto right after "Authenticating to ...". */
    (void)_c; (void)_m;
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_release(CTX(rt), (uint32_t)_sp[0]);
    if (mc) EVP_MD_CTX_free(mc);
    return NULL;
}

/* env.EVP_md5 / sha1 / sha256 / sha512 — i32() */
static const void *m3_yos_EVP_md5(IM3Runtime rt, IM3ImportContext _c,
                                  uint64_t *_sp, void *_m)
{ (void)_c; (void)_m;
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)EVP_md5());
  return NULL; }
static const void *m3_yos_EVP_sha1(IM3Runtime rt, IM3ImportContext _c,
                                   uint64_t *_sp, void *_m)
{ (void)_c; (void)_m;
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)EVP_sha1());
  return NULL; }
static const void *m3_yos_EVP_sha256(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{ (void)_c; (void)_m;
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)EVP_sha256());
  return NULL; }
static const void *m3_yos_EVP_sha512(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{ (void)_c; (void)_m;
  _sp[0] = (uint64_t)ssl_handles_wrap(CTX(rt), (void *)EVP_sha512());
  return NULL; }

/* env.EVP_MD_get_size — i32(md_handle) */
static const void *m3_yos_EVP_MD_get_size(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    EVP_MD *md = (EVP_MD *)ssl_handles_resolve(CTX(rt), (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(md ? EVP_MD_get_size(md) : 0);
    return NULL;
}

/* env.EVP_DigestInit_ex — i32(md_ctx_h, md_h, engine_h). Engine handle
 * is ignored — yos refuses to bridge ENGINE_* (engines are a legacy
 * provider-discovery hook removed from openssl 3.0's default API). */
static const void *m3_yos_EVP_DigestInit_ex(IM3Runtime rt, IM3ImportContext _c,
                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    const EVP_MD *md = (const EVP_MD *)ssl_handles_resolve(ctx, (uint32_t)_sp[2]);
    /* arg [3] = engine handle, ignored */
    _sp[0] = (uint64_t)(uint32_t)((mc && md) ? EVP_DigestInit_ex(mc, md, NULL) : 0);
    return NULL;
}

/* env.EVP_DigestUpdate — i32(md_ctx_h, data_off, count) */
static const void *m3_yos_EVP_DigestUpdate(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    uint32_t cnt = (uint32_t)_sp[3];
    const void *data = cnt ? guest_buf_ro(ctx, off, cnt) : NULL;
    int rc = (mc && (data || cnt == 0)) ? EVP_DigestUpdate(mc, data, cnt) : 0;
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.EVP_DigestFinal_ex — i32(md_ctx_h, md_buf_off, md_len_off).
 * md_buf is guest-writable (must be at least EVP_MAX_MD_SIZE = 64);
 * md_len_off is a uint32_t* in guest memory. We write the host's
 * unsigned int back as a wasm-side little-endian uint32. */
static const void *m3_yos_EVP_DigestFinal_ex(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    EVP_MD_CTX *mc = (EVP_MD_CTX *)ssl_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t md_off  = (uint32_t)_sp[2];
    uint32_t len_off = (uint32_t)_sp[3];

    /* 64 == EVP_MAX_MD_SIZE in openssl 3.x — bound the guest buffer to
     * that so a wild md handle can't trample memory. */
    unsigned char *md = (unsigned char *)guest_buf_rw(ctx, md_off, 64);
    uint32_t      *lp = (uint32_t *)guest_buf_rw(ctx, len_off, sizeof(uint32_t));
    if (!mc || !md) { _sp[0] = 0; return NULL; }

    unsigned int n = 0;
    int rc = EVP_DigestFinal_ex(mc, md, &n);
    if (lp) *lp = (uint32_t)n;   /* wasm32 is little-endian; host write is fine */
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.ERR_get_error — uint32() on wasm32.
 *
 * Host: `unsigned long ERR_get_error(void)` → 64-bit on host. wasm32
 * sees i32 because i386 ABI's `unsigned long` is 32. Narrow on the
 * way back; openssl's error codes fit in 32 bits by construction
 * (a packed (lib, reason) tuple). */
static const void *m3_yos_ERR_get_error(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)rt; (void)_c; (void)_m;
    _sp[0] = (uint64_t)(uint32_t)ERR_get_error();
    return NULL;
}

/* env.ERR_error_string_n — void(uint32 code, buf_off, buf_len).
 *
 * `unsigned long e` on wasm32 is i32 (see ERR_get_error above for the
 * ABI rationale). Widen to host `unsigned long` before passing.
 * Void bridge: args at sp[0..2]. */
static const void *m3_yos_ERR_error_string_n(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    unsigned long e = (unsigned long)(uint32_t)_sp[0];
    uint32_t off    = (uint32_t)_sp[1];
    uint32_t len    = (uint32_t)_sp[2];
    char *buf = (char *)guest_buf_rw(ctx, off, len);
    if (buf && len > 0) ERR_error_string_n(e, buf, len);
    return NULL;
}

/* Public entry point — called from main.c during import linkage. */
void yos_openssl_link(IM3Module mod)
{
    if (!mod) return;
    /* Format: <ret>(<args>). 'v'=void, 'i'=i32, 'I'=i64. */
    m3_LinkRawFunction(mod, "env", "OPENSSL_init_ssl",    "i(Ii)",   m3_yos_OPENSSL_init_ssl);
    m3_LinkRawFunction(mod, "env", "OPENSSL_init_crypto", "i(Ii)",   m3_yos_OPENSSL_init_crypto);
    /* OpenSSL_version_num + ERR_get_error return `unsigned long`,
     * which is i32 on wasm32 (i386 ABI) and i64 on the host. The
     * bridge narrows; see the wrapper bodies for context. */
    m3_LinkRawFunction(mod, "env", "OpenSSL_version_num", "i()",     m3_yos_OpenSSL_version_num);
    m3_LinkRawFunction(mod, "env", "OpenSSL_version",     "i(i)",    m3_yos_OpenSSL_version);

    m3_LinkRawFunction(mod, "env", "TLS_method",          "i()",     m3_yos_TLS_method);
    m3_LinkRawFunction(mod, "env", "TLS_client_method",   "i()",     m3_yos_TLS_client_method);
    m3_LinkRawFunction(mod, "env", "TLS_server_method",   "i()",     m3_yos_TLS_server_method);

    m3_LinkRawFunction(mod, "env", "SSL_CTX_new",         "i(i)",    m3_yos_SSL_CTX_new);
    m3_LinkRawFunction(mod, "env", "SSL_CTX_free",        "v(i)",    m3_yos_SSL_CTX_free);

    m3_LinkRawFunction(mod, "env", "SSL_new",             "i(i)",    m3_yos_SSL_new);
    m3_LinkRawFunction(mod, "env", "SSL_free",            "v(i)",    m3_yos_SSL_free);
    m3_LinkRawFunction(mod, "env", "SSL_set_fd",          "i(ii)",   m3_yos_SSL_set_fd);
    m3_LinkRawFunction(mod, "env", "SSL_connect",         "i(i)",    m3_yos_SSL_connect);
    m3_LinkRawFunction(mod, "env", "SSL_accept",          "i(i)",    m3_yos_SSL_accept);
    m3_LinkRawFunction(mod, "env", "SSL_read",            "i(iii)",  m3_yos_SSL_read);
    m3_LinkRawFunction(mod, "env", "SSL_write",           "i(iii)",  m3_yos_SSL_write);
    m3_LinkRawFunction(mod, "env", "SSL_shutdown",        "i(i)",    m3_yos_SSL_shutdown);
    m3_LinkRawFunction(mod, "env", "SSL_get_error",       "i(ii)",   m3_yos_SSL_get_error);

    m3_LinkRawFunction(mod, "env", "RAND_bytes",          "i(ii)",   m3_yos_RAND_bytes);
    m3_LinkRawFunction(mod, "env", "RAND_priv_bytes",     "i(ii)",   m3_yos_RAND_priv_bytes);
    m3_LinkRawFunction(mod, "env", "RAND_status",         "i()",     m3_yos_RAND_status);
    m3_LinkRawFunction(mod, "env", "RAND_poll",           "i()",     m3_yos_RAND_poll);
    m3_LinkRawFunction(mod, "env", "RAND_seed",           "v(ii)",   m3_yos_RAND_seed);
    m3_LinkRawFunction(mod, "env", "RAND_add",            "v(iiF)",  m3_yos_RAND_add);

    m3_LinkRawFunction(mod, "env", "BIO_new",             "i(i)",    m3_yos_BIO_new);
    m3_LinkRawFunction(mod, "env", "BIO_free",            "i(i)",    m3_yos_BIO_free);
    m3_LinkRawFunction(mod, "env", "BIO_s_mem",           "i()",     m3_yos_BIO_s_mem);
    m3_LinkRawFunction(mod, "env", "BIO_write",           "i(iii)",  m3_yos_BIO_write);
    m3_LinkRawFunction(mod, "env", "BIO_read",            "i(iii)",  m3_yos_BIO_read);
    m3_LinkRawFunction(mod, "env", "BIO_ctrl",            "i(iiii)", m3_yos_BIO_ctrl);
    m3_LinkRawFunction(mod, "env", "BIO_new_mem_buf",     "i(ii)",   m3_yos_BIO_new_mem_buf);

    m3_LinkRawFunction(mod, "env", "ERR_peek_error",      "i()",     m3_yos_ERR_peek_error);
    m3_LinkRawFunction(mod, "env", "ERR_peek_last_error", "i()",     m3_yos_ERR_peek_last_error);
    m3_LinkRawFunction(mod, "env", "ERR_clear_error",     "v()",     m3_yos_ERR_clear_error);
    m3_LinkRawFunction(mod, "env", "ERR_error_string",    "i(ii)",   m3_yos_ERR_error_string);

    /* EVP_PKEY family */
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_new",           "i()",    m3_yos_EVP_PKEY_new);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_free",          "v(i)",   m3_yos_EVP_PKEY_free);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_up_ref",        "i(i)",   m3_yos_EVP_PKEY_up_ref);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_cmp",           "i(ii)",  m3_yos_EVP_PKEY_cmp);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get_base_id",   "i(i)",   m3_yos_EVP_PKEY_get_base_id);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get_bits",      "i(i)",   m3_yos_EVP_PKEY_get_bits);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get_size",      "i(i)",   m3_yos_EVP_PKEY_get_size);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_set1_RSA",      "i(ii)",  m3_yos_EVP_PKEY_set1_RSA);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_set1_EC_KEY",   "i(ii)",  m3_yos_EVP_PKEY_set1_EC_KEY);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get0_RSA",      "i(i)",   m3_yos_EVP_PKEY_get0_RSA);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get0_EC_KEY",   "i(i)",   m3_yos_EVP_PKEY_get0_EC_KEY);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get1_RSA",      "i(i)",   m3_yos_EVP_PKEY_get1_RSA);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get1_EC_KEY",   "i(i)",   m3_yos_EVP_PKEY_get1_EC_KEY);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get_raw_private_key", "i(iii)", m3_yos_EVP_PKEY_get_raw_private_key);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_get_raw_public_key",  "i(iii)", m3_yos_EVP_PKEY_get_raw_public_key);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_CTX_new_id",    "i(ii)",  m3_yos_EVP_PKEY_CTX_new_id);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_CTX_free",      "v(i)",   m3_yos_EVP_PKEY_CTX_free);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_keygen_init",   "i(i)",   m3_yos_EVP_PKEY_keygen_init);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_keygen",        "i(ii)",  m3_yos_EVP_PKEY_keygen);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_CTX_set_rsa_keygen_bits",         "i(ii)", m3_yos_EVP_PKEY_CTX_set_rsa_keygen_bits);
    m3_LinkRawFunction(mod, "env", "EVP_PKEY_CTX_set_ec_paramgen_curve_nid",   "i(ii)", m3_yos_EVP_PKEY_CTX_set_ec_paramgen_curve_nid);

    /* RSA */
    m3_LinkRawFunction(mod, "env", "RSA_new",                "i()",    m3_yos_RSA_new);
    m3_LinkRawFunction(mod, "env", "RSA_free",               "v(i)",   m3_yos_RSA_free);
    m3_LinkRawFunction(mod, "env", "RSA_size",               "i(i)",   m3_yos_RSA_size);
    m3_LinkRawFunction(mod, "env", "RSA_blinding_on",        "i(ii)",  m3_yos_RSA_blinding_on);

    /* EC_KEY */
    m3_LinkRawFunction(mod, "env", "EC_KEY_new",             "i()",    m3_yos_EC_KEY_new);
    m3_LinkRawFunction(mod, "env", "EC_KEY_free",            "v(i)",   m3_yos_EC_KEY_free);
    m3_LinkRawFunction(mod, "env", "EC_KEY_new_by_curve_name", "i(i)", m3_yos_EC_KEY_new_by_curve_name);
    m3_LinkRawFunction(mod, "env", "EC_KEY_dup",             "i(i)",   m3_yos_EC_KEY_dup);
    m3_LinkRawFunction(mod, "env", "EC_KEY_generate_key",    "i(i)",   m3_yos_EC_KEY_generate_key);
    m3_LinkRawFunction(mod, "env", "EC_KEY_OpenSSL",         "i()",    m3_yos_EC_KEY_OpenSSL);

    /* BIGNUM */
    m3_LinkRawFunction(mod, "env", "BN_new",                 "i()",    m3_yos_BN_new);
    m3_LinkRawFunction(mod, "env", "BN_free",                "v(i)",   m3_yos_BN_free);
    m3_LinkRawFunction(mod, "env", "BN_clear_free",          "v(i)",   m3_yos_BN_clear_free);
    m3_LinkRawFunction(mod, "env", "BN_dup",                 "i(i)",   m3_yos_BN_dup);
    m3_LinkRawFunction(mod, "env", "BN_num_bits",            "i(i)",   m3_yos_BN_num_bits);
    m3_LinkRawFunction(mod, "env", "BN_cmp",                 "i(ii)",  m3_yos_BN_cmp);
    m3_LinkRawFunction(mod, "env", "BN_set_flags",           "v(ii)",  m3_yos_BN_set_flags);
    m3_LinkRawFunction(mod, "env", "BN_is_bit_set",          "i(ii)",  m3_yos_BN_is_bit_set);
    m3_LinkRawFunction(mod, "env", "BN_is_negative",         "i(i)",   m3_yos_BN_is_negative);
    m3_LinkRawFunction(mod, "env", "BN_value_one",           "i()",    m3_yos_BN_value_one);
    m3_LinkRawFunction(mod, "env", "BN_sub",                 "i(iii)", m3_yos_BN_sub);
    m3_LinkRawFunction(mod, "env", "BN_add",                 "i(iii)", m3_yos_BN_add);
    m3_LinkRawFunction(mod, "env", "BN_mul",                 "i(iiii)",m3_yos_BN_mul);
    m3_LinkRawFunction(mod, "env", "BN_mod",                 "i(iiii)",m3_yos_BN_mod);
    m3_LinkRawFunction(mod, "env", "BN_div",                 "i(iiiii)",m3_yos_BN_div);
    m3_LinkRawFunction(mod, "env", "BN_mod_inverse",         "i(iiii)",m3_yos_BN_mod_inverse);
    m3_LinkRawFunction(mod, "env", "BN_hex2bn",              "i(ii)",  m3_yos_BN_hex2bn);
    m3_LinkRawFunction(mod, "env", "BN_CTX_new",             "i()",    m3_yos_BN_CTX_new);
    m3_LinkRawFunction(mod, "env", "BN_CTX_free",            "v(i)",   m3_yos_BN_CTX_free);
    m3_LinkRawFunction(mod, "env", "BN_bn2bin",              "i(ii)",  m3_yos_BN_bn2bin);
    m3_LinkRawFunction(mod, "env", "BN_bin2bn",              "i(iii)", m3_yos_BN_bin2bn);

    /* DH */
    m3_LinkRawFunction(mod, "env", "DH_new",                 "i()",    m3_yos_DH_new);
    m3_LinkRawFunction(mod, "env", "DH_free",                "v(i)",   m3_yos_DH_free);
    m3_LinkRawFunction(mod, "env", "DH_size",                "i(i)",   m3_yos_DH_size);
    m3_LinkRawFunction(mod, "env", "DH_generate_key",        "i(i)",   m3_yos_DH_generate_key);
    m3_LinkRawFunction(mod, "env", "DH_compute_key",         "i(iii)", m3_yos_DH_compute_key);
    m3_LinkRawFunction(mod, "env", "DH_set_length",          "i(ii)",  m3_yos_DH_set_length);
    m3_LinkRawFunction(mod, "env", "DH_set0_pqg",            "i(iiii)",m3_yos_DH_set0_pqg);
    m3_LinkRawFunction(mod, "env", "DH_get0_pqg",            "v(iiii)",m3_yos_DH_get0_pqg);
    m3_LinkRawFunction(mod, "env", "DH_get0_key",            "v(iii)", m3_yos_DH_get0_key);

    /* RSA set0/get0 */
    m3_LinkRawFunction(mod, "env", "RSA_set0_key",           "i(iiii)",m3_yos_RSA_set0_key);
    m3_LinkRawFunction(mod, "env", "RSA_set0_factors",       "i(iii)", m3_yos_RSA_set0_factors);
    m3_LinkRawFunction(mod, "env", "RSA_set0_crt_params",    "i(iiii)",m3_yos_RSA_set0_crt_params);
    m3_LinkRawFunction(mod, "env", "RSA_get0_key",           "v(iiii)",m3_yos_RSA_get0_key);
    m3_LinkRawFunction(mod, "env", "RSA_get0_factors",       "v(iii)", m3_yos_RSA_get0_factors);
    m3_LinkRawFunction(mod, "env", "RSA_get0_crt_params",    "v(iiii)",m3_yos_RSA_get0_crt_params);

    /* EC_KEY set/get0 */
    m3_LinkRawFunction(mod, "env", "EC_KEY_set_group",         "i(ii)", m3_yos_EC_KEY_set_group);
    m3_LinkRawFunction(mod, "env", "EC_KEY_set_public_key",    "i(ii)", m3_yos_EC_KEY_set_public_key);
    m3_LinkRawFunction(mod, "env", "EC_KEY_set_private_key",   "i(ii)", m3_yos_EC_KEY_set_private_key);
    m3_LinkRawFunction(mod, "env", "EC_KEY_get0_group",        "i(i)",  m3_yos_EC_KEY_get0_group);
    m3_LinkRawFunction(mod, "env", "EC_KEY_get0_public_key",   "i(i)",  m3_yos_EC_KEY_get0_public_key);
    m3_LinkRawFunction(mod, "env", "EC_KEY_get0_private_key",  "i(i)",  m3_yos_EC_KEY_get0_private_key);

    /* EC_GROUP / EC_METHOD */
    m3_LinkRawFunction(mod, "env", "EC_GROUP_new_by_curve_name","i(i)",   m3_yos_EC_GROUP_new_by_curve_name);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_free",             "v(i)",   m3_yos_EC_GROUP_free);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_get_curve_name",   "i(i)",   m3_yos_EC_GROUP_get_curve_name);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_get_degree",       "i(i)",   m3_yos_EC_GROUP_get_degree);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_get_order",        "i(iii)", m3_yos_EC_GROUP_get_order);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_cmp",              "i(iii)", m3_yos_EC_GROUP_cmp);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_method_of",        "i(i)",   m3_yos_EC_GROUP_method_of);
    m3_LinkRawFunction(mod, "env", "EC_GROUP_set_asn1_flag",    "v(ii)",  m3_yos_EC_GROUP_set_asn1_flag);
    m3_LinkRawFunction(mod, "env", "EC_METHOD_get_field_type",  "i(i)",   m3_yos_EC_METHOD_get_field_type);

    /* EC_POINT */
    m3_LinkRawFunction(mod, "env", "EC_POINT_new",              "i(i)",      m3_yos_EC_POINT_new);
    m3_LinkRawFunction(mod, "env", "EC_POINT_free",             "v(i)",      m3_yos_EC_POINT_free);
    m3_LinkRawFunction(mod, "env", "EC_POINT_clear_free",       "v(i)",      m3_yos_EC_POINT_clear_free);
    m3_LinkRawFunction(mod, "env", "EC_POINT_is_at_infinity",   "i(ii)",     m3_yos_EC_POINT_is_at_infinity);
    m3_LinkRawFunction(mod, "env", "EC_POINT_mul",              "i(iiiiii)", m3_yos_EC_POINT_mul);
    m3_LinkRawFunction(mod, "env", "EC_POINT_get_affine_coordinates_GFp",
                       "i(iiiii)", m3_yos_EC_POINT_get_affine_coordinates_GFp);
    m3_LinkRawFunction(mod, "env", "EC_POINT_oct2point",        "i(iiiii)",  m3_yos_EC_POINT_oct2point);
    m3_LinkRawFunction(mod, "env", "EC_POINT_point2oct",        "i(iiiiii)", m3_yos_EC_POINT_point2oct);

    /* ECDSA_SIG */
    m3_LinkRawFunction(mod, "env", "ECDSA_SIG_new",  "i()",     m3_yos_ECDSA_SIG_new);
    m3_LinkRawFunction(mod, "env", "ECDSA_SIG_free", "v(i)",    m3_yos_ECDSA_SIG_free);
    m3_LinkRawFunction(mod, "env", "ECDSA_SIG_get0", "v(iii)",  m3_yos_ECDSA_SIG_get0);
    m3_LinkRawFunction(mod, "env", "ECDSA_SIG_set0", "i(iii)",  m3_yos_ECDSA_SIG_set0);

    m3_LinkRawFunction(mod, "env", "EVP_Digest",     "i(iiiiii)", m3_yos_EVP_Digest);

    /* EVP_CIPHER algorithm getters */
    m3_LinkRawFunction(mod, "env", "EVP_aes_128_cbc","i()",     m3_yos_EVP_aes_128_cbc);
    m3_LinkRawFunction(mod, "env", "EVP_aes_192_cbc","i()",     m3_yos_EVP_aes_192_cbc);
    m3_LinkRawFunction(mod, "env", "EVP_aes_256_cbc","i()",     m3_yos_EVP_aes_256_cbc);
    m3_LinkRawFunction(mod, "env", "EVP_aes_128_ctr","i()",     m3_yos_EVP_aes_128_ctr);
    m3_LinkRawFunction(mod, "env", "EVP_aes_192_ctr","i()",     m3_yos_EVP_aes_192_ctr);
    m3_LinkRawFunction(mod, "env", "EVP_aes_256_ctr","i()",     m3_yos_EVP_aes_256_ctr);
    m3_LinkRawFunction(mod, "env", "EVP_aes_128_gcm","i()",     m3_yos_EVP_aes_128_gcm);
    m3_LinkRawFunction(mod, "env", "EVP_aes_256_gcm","i()",     m3_yos_EVP_aes_256_gcm);
    m3_LinkRawFunction(mod, "env", "EVP_chacha20",   "i()",     m3_yos_EVP_chacha20);
    m3_LinkRawFunction(mod, "env", "EVP_des_ede3_cbc","i()",    m3_yos_EVP_des_ede3_cbc);

    /* EVP_CIPHER_CTX */
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_new",    "i()",    m3_yos_EVP_CIPHER_CTX_new);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_free",   "v(i)",   m3_yos_EVP_CIPHER_CTX_free);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_ctrl",   "i(iiii)",m3_yos_EVP_CIPHER_CTX_ctrl);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_get_iv_length",  "i(i)", m3_yos_EVP_CIPHER_CTX_get_iv_length);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_get_key_length", "i(i)", m3_yos_EVP_CIPHER_CTX_get_key_length);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_set_key_length", "i(ii)",m3_yos_EVP_CIPHER_CTX_set_key_length);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_get_updated_iv", "i(iii)",m3_yos_EVP_CIPHER_CTX_get_updated_iv);
    m3_LinkRawFunction(mod, "env", "EVP_CIPHER_CTX_set_params",     "i(ii)", m3_yos_EVP_CIPHER_CTX_set_params);
    m3_LinkRawFunction(mod, "env", "EVP_CipherInit",                "i(iiiii)", m3_yos_EVP_CipherInit);
    m3_LinkRawFunction(mod, "env", "EVP_Cipher",                    "i(iiii)",  m3_yos_EVP_Cipher);

    /* OSSL_PARAM constructors — wasm-side struct writes only. The
     * by-value struct return on the host is lowered to a sret first
     * arg in wasm32. */
    m3_LinkRawFunction(mod, "env", "OSSL_PARAM_construct_octet_string", "v(iiii)", m3_yos_OSSL_PARAM_construct_octet_string);
    m3_LinkRawFunction(mod, "env", "OSSL_PARAM_construct_end",          "v(i)",    m3_yos_OSSL_PARAM_construct_end);
    /* (note: both above are void in the wasm imports — confirmed via
     * wasm-tools print. Args therefore start at sp[0] inside the
     * bridge bodies — see comment above BR_RETPTR_NOARG.) */

    /* EVP_MD extras */
    m3_LinkRawFunction(mod, "env", "EVP_MD_get_block_size", "i(i)",  m3_yos_EVP_MD_get_block_size);
    m3_LinkRawFunction(mod, "env", "EVP_MD_CTX_get0_md",    "i(i)",  m3_yos_EVP_MD_CTX_get0_md);
    m3_LinkRawFunction(mod, "env", "EVP_MD_CTX_copy_ex",    "i(ii)", m3_yos_EVP_MD_CTX_copy_ex);

    /* EVP_DigestSign / Verify */
    m3_LinkRawFunction(mod, "env", "EVP_DigestSignInit",   "i(iiiii)",  m3_yos_EVP_DigestSignInit);
    m3_LinkRawFunction(mod, "env", "EVP_DigestSign",       "i(iiiii)",  m3_yos_EVP_DigestSign);
    m3_LinkRawFunction(mod, "env", "EVP_DigestVerifyInit", "i(iiiii)",  m3_yos_EVP_DigestVerifyInit);
    m3_LinkRawFunction(mod, "env", "EVP_DigestVerify",     "i(iiiii)",  m3_yos_EVP_DigestVerify);

    m3_LinkRawFunction(mod, "env", "EVP_MD_CTX_new",      "i()",     m3_yos_EVP_MD_CTX_new);
    m3_LinkRawFunction(mod, "env", "EVP_MD_CTX_free",     "v(i)",    m3_yos_EVP_MD_CTX_free);
    m3_LinkRawFunction(mod, "env", "EVP_md5",             "i()",     m3_yos_EVP_md5);
    m3_LinkRawFunction(mod, "env", "EVP_sha1",            "i()",     m3_yos_EVP_sha1);
    m3_LinkRawFunction(mod, "env", "EVP_sha256",          "i()",     m3_yos_EVP_sha256);
    m3_LinkRawFunction(mod, "env", "EVP_sha512",          "i()",     m3_yos_EVP_sha512);
    m3_LinkRawFunction(mod, "env", "EVP_MD_get_size",     "i(i)",    m3_yos_EVP_MD_get_size);
    m3_LinkRawFunction(mod, "env", "EVP_DigestInit_ex",   "i(iii)",  m3_yos_EVP_DigestInit_ex);
    m3_LinkRawFunction(mod, "env", "EVP_DigestUpdate",    "i(iii)",  m3_yos_EVP_DigestUpdate);
    m3_LinkRawFunction(mod, "env", "EVP_DigestFinal_ex",  "i(iii)",  m3_yos_EVP_DigestFinal_ex);

    m3_LinkRawFunction(mod, "env", "ERR_get_error",       "i()",     m3_yos_ERR_get_error);
    m3_LinkRawFunction(mod, "env", "ERR_error_string_n",  "v(iii)",  m3_yos_ERR_error_string_n);
}

/* Per-ctx teardown — close any handles still held when the guest
 * exits. yos's proc shutdown calls this so a guest that forgot to
 * SSL_free its connections doesn't leak host openssl state. */
void yos_openssl_ctx_free(struct yos_exec_ctx *ctx)
{
    if (!ctx || !ctx->ssl_handles) return;
    for (uint32_t i = 1; i < ctx->ssl_handles_cap; ++i) {
        void *p = ctx->ssl_handles[i];
        if (!p) continue;
        /* We do NOT have type info per slot; the safe move is to leak
         * (host openssl tolerates it — the process is going down).
         * A future revision can either tag each slot with a type byte
         * or split the table per-type. For the SSL_CTX held by a
         * normally-exiting guest this is fine because SSL_CTX_free
         * gets called explicitly. */
        (void)p;
    }
    free(ctx->ssl_handles);
    ctx->ssl_handles = NULL;
    ctx->ssl_handles_cap = 0;
}
