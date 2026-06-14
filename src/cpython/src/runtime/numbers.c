/* Numeric literal boxes (int/float/complex) and the PyOS string->number
 * helpers. The int box keeps the literal spelling (no bignum needed for an
 * AST emitter); small values are also parsed into ival. Libpython-free. */
#include "Python.h"
#include <errno.h>
#include <math.h>

PyObject *pyp_new(enum pyp_kind kind, PyTypeObject *type);

/* CPython float repr: shortest round-tripping decimal, formatted with Python's
 * fixed-vs-exponential rule (exponential iff decimal point position <= -4 or
 * > 16), always including a '.' / 'e' / inf / nan. */
static void
format_double_repr(double d, char *out, size_t outsz)
{
    if (isnan(d)) { snprintf(out, outsz, "nan"); return; }
    if (isinf(d)) { snprintf(out, outsz, "%sinf", d < 0 ? "-" : ""); return; }
    if (d == 0.0) { snprintf(out, outsz, "%s0.0", signbit(d) ? "-" : ""); return; }

    int neg = d < 0;
    double a = neg ? -d : d;

    /* shortest significant digits via %e (one digit before the point) */
    char tmp[64];
    int prec;
    for (prec = 1; prec <= 17; prec++) {
        snprintf(tmp, sizeof tmp, "%.*e", prec - 1, a);
        if (strtod(tmp, NULL) == a) break;
    }
    char digits[24];
    int ndig = 0;
    const char *p = tmp;
    digits[ndig++] = *p++;
    if (*p == '.') {
        p++;
        while (*p && *p != 'e' && *p != 'E') digits[ndig++] = *p++;
    }
    while (*p && *p != 'e' && *p != 'E') p++;
    int exp10 = (int)strtol(p + 1, NULL, 10);
    while (ndig > 1 && digits[ndig - 1] == '0') ndig--;
    digits[ndig] = '\0';

    int decpt = exp10 + 1;
    char *o = out;
    if (neg) *o++ = '-';
    if (decpt <= -4 || decpt > 16) {
        *o++ = digits[0];
        if (ndig > 1) { *o++ = '.'; for (int i = 1; i < ndig; i++) *o++ = digits[i]; }
        int e = decpt - 1;
        *o++ = 'e'; *o++ = (e < 0) ? '-' : '+';
        int ae = e < 0 ? -e : e;
        o += snprintf(o, 8, "%02d", ae);
    } else if (decpt <= 0) {
        *o++ = '0'; *o++ = '.';
        for (int i = 0; i < -decpt; i++) *o++ = '0';
        for (int i = 0; i < ndig; i++) *o++ = digits[i];
        *o = '\0';
    } else if (decpt >= ndig) {
        for (int i = 0; i < ndig; i++) *o++ = digits[i];
        for (int i = 0; i < decpt - ndig; i++) *o++ = '0';
        *o++ = '.'; *o++ = '0'; *o = '\0';
    } else {
        for (int i = 0; i < ndig; i++) { if (i == decpt) *o++ = '.'; *o++ = digits[i]; }
        *o = '\0';
    }
    (void)outsz;
}

PyObject *
PyLong_FromLong(long v)
{
    PyObject *o = pyp_new(PYP_LONG, &PyLong_Type);
    if (o == NULL) {
        return NULL;
    }
    o->ival = v;
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%ld", v);
    o->data = malloc((size_t)n + 1);
    if (o->data) {
        memcpy(o->data, buf, (size_t)n + 1);
        o->len = n;
    }
    return o;
}

/* Detect base from a Python integer prefix (0x/0o/0b) when base==0, and return
 * a pointer past the prefix. */
static int
detect_base(const char *p, int base, const char **after_prefix)
{
    int detected = base;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) detected = 16;
        else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) detected = 8;
        else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) detected = 2;
        else detected = 10;
    }
    if ((detected == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ||
        (detected == 8  && p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) ||
        (detected == 2  && p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))) {
        p += 2;
    }
    *after_prefix = p;
    return detected;
}

static int
digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

PyObject *
PyLong_FromString(const char *str, char **pend, int base)
{
    /* Convert an integer literal of any base into its canonical decimal string
     * (arbitrary precision). '_' separators are already stripped upstream. */
    PyObject *o = pyp_new(PYP_LONG, &PyLong_Type);
    if (o == NULL) {
        return NULL;
    }
    const char *p = str;
    int neg = 0;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    const char *digits_start;
    int detected = detect_base(p, base, &digits_start);
    p = digits_start;

    /* Little-endian decimal-digit accumulator: dec = dec*detected + d. */
    size_t cap = 16, ndec = 1;
    unsigned char *dec = malloc(cap);
    dec[0] = 0;
    for (; *p; p++) {
        int d = digit_value(*p);
        if (d < 0 || d >= detected) break;
        int carry = d;
        for (size_t i = 0; i < ndec; i++) {
            int v = dec[i] * detected + carry;
            dec[i] = (unsigned char)(v % 10);
            carry = v / 10;
        }
        while (carry) {
            if (ndec == cap) { cap *= 2; dec = realloc(dec, cap); }
            dec[ndec++] = (unsigned char)(carry % 10);
            carry /= 10;
        }
    }
    while (ndec > 1 && dec[ndec - 1] == 0) ndec--;   /* strip leading zeros */

    char *out = malloc(ndec + 2);
    size_t oi = 0;
    if (neg && !(ndec == 1 && dec[0] == 0)) out[oi++] = '-';
    for (size_t i = 0; i < ndec; i++) out[oi++] = (char)('0' + dec[ndec - 1 - i]);
    out[oi] = '\0';
    free(dec);

    o->data = out;
    o->len = (Py_ssize_t)oi;
    errno = 0;
    o->ival = strtol(out, NULL, 10);   /* best-effort numeric (dumper uses data) */
    if (pend) *pend = (char *)p;
    return o;
}

PyObject *
PyFloat_FromDouble(double v)
{
    PyObject *o = pyp_new(PYP_FLOAT, NULL);
    if (o == NULL) {
        return NULL;
    }
    o->dval = v;
    char buf[40];
    format_double_repr(v, buf, sizeof buf);
    size_t n = strlen(buf);
    o->data = malloc(n + 1);
    if (o->data) {
        memcpy(o->data, buf, n + 1);
        o->len = (Py_ssize_t)n;
    }
    return o;
}

PyObject *
PyComplex_FromCComplex(Py_complex v)
{
    PyObject *o = pyp_new(PYP_COMPLEX, &PyComplex_Type);
    if (o == NULL) {
        return NULL;
    }
    o->dval = v.real;
    o->imag = v.imag;
    /* Imaginary literals: CPython prints "<imag>j" and drops a trailing ".0". */
    char buf[64];
    format_double_repr(v.imag, buf, sizeof buf);
    size_t n = strlen(buf);
    if (n >= 2 && buf[n - 2] == '.' && buf[n - 1] == '0') {
        n -= 2; buf[n] = '\0';
    }
    o->data = malloc(n + 2);
    if (o->data) {
        memcpy(o->data, buf, n);
        o->data[n] = 'j';
        o->data[n + 1] = '\0';
        o->len = (Py_ssize_t)n + 1;
    }
    return o;
}

double
PyOS_string_to_double(const char *s, char **endptr, PyObject *overflow_exception)
{
    (void)overflow_exception;
    char *end = NULL;
    double v = strtod(s, &end);
    if (endptr) {
        *endptr = end;
    }
    return v;
}

/* Python-semantics strtoul: understands 0x/0o/0b prefixes with base 0, sets
 * errno=ERANGE on overflow. (C strtoul only knows 0x and C-octal.) */
unsigned long
PyOS_strtoul(const char *str, char **ptr, int base)
{
    const char *p = str;
    const char *digits_start;
    int detected = detect_base(p, base, &digits_start);
    p = digits_start;
    unsigned long result = 0;
    int overflow = 0;
    for (; *p; p++) {
        int d = digit_value(*p);
        if (d < 0 || d >= detected) break;
        if (result > (ULONG_MAX - (unsigned long)d) / (unsigned long)detected) overflow = 1;
        result = result * (unsigned long)detected + (unsigned long)d;
    }
    if (ptr) *ptr = (char *)p;
    if (overflow) { errno = ERANGE; return ULONG_MAX; }
    return result;
}

long
PyOS_strtol(const char *str, char **ptr, int base)
{
    const char *p = str;
    int neg = 0;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    errno = 0;
    unsigned long mag = PyOS_strtoul(p, ptr, base);
    if (errno == ERANGE) return neg ? LONG_MIN : LONG_MAX;
    if (neg) {
        if (mag > (unsigned long)LONG_MAX + 1) { errno = ERANGE; return LONG_MIN; }
        return -(long)mag;
    }
    if (mag > (unsigned long)LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    return (long)mag;
}
