/*
 * yos_ps_compat.c — self-contained compat layer for the REAL FreeBSD bin/ps
 * under yos. ps links libkvm/libxo/libutil/libjail; the wasm sysroot has none
 * of them, so this stands in. The important part is libkvm: kvm_getprocs()
 * routes through the STANDARD FreeBSD path — sysctl(CTL_KERN, KERN_PROC,
 * KERN_PROC_*), served by yos's libc sysctl bridge (src/yos/impl/libc/
 * sysctl.c). No /proc, no per-tool reimplementation.
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>   /* struct kinfo_proc — needed for sizeof in kvm_getprocs */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ───────────── libkvm(3) over sysctl(KERN_PROC_*) ───────────── */
struct kinfo_proc;
struct yos_kvm { char err[128]; };
typedef struct yos_kvm kvm_t;

kvm_t *
kvm_openfiles(const char *ef, const char *cf, const char *sf, int fl, char *eb)
{
	static struct yos_kvm h;
	(void)ef; (void)cf; (void)sf; (void)fl;
	h.err[0] = '\0';
	if (eb)
		eb[0] = '\0';
	return &h;
}

int
kvm_close(kvm_t *kd)
{
	(void)kd;
	return 0;
}

char *
kvm_geterr(kvm_t *kd)
{
	return kd ? ((struct yos_kvm *)kd)->err : (char *)"";
}

struct kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
{
	static struct kinfo_proc *buf;
	static size_t cap;
	int mib[4], miblen;
	size_t len = 0;

	(void)kd;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = op;
	mib[3] = arg;
	miblen = (op == KERN_PROC_PID) ? 4 : 3;

	if (sysctl(mib, miblen, NULL, &len, NULL, 0) < 0 || len == 0) {
		*cnt = 0;
		return NULL;
	}
	if (len > cap) {
		free(buf);
		buf = malloc(len);
		cap = buf ? len : 0;
	}
	if (buf == NULL || sysctl(mib, miblen, buf, &len, NULL, 0) < 0) {
		*cnt = 0;
		return NULL;
	}
	*cnt = (int)(len / sizeof(struct kinfo_proc));
	return buf;
}

char **
kvm_getargv(kvm_t *kd, const struct kinfo_proc *p, int n)
{
	(void)kd; (void)p; (void)n;
	return NULL;   /* arguments fall back to ki_comm */
}

char **
kvm_getenvv(kvm_t *kd, const struct kinfo_proc *p, int n)
{
	(void)kd; (void)p; (void)n;
	return NULL;
}

/* ───────────── libutil pwcache: one (numeric) user/group ───────────── */
const char *
user_from_uid(uid_t uid, int nouser)
{
	static char b[16];
	(void)nouser;
	snprintf(b, sizeof(b), "%u", (unsigned)uid);
	return b;
}

const char *
group_from_gid(gid_t gid, int nogroup)
{
	static char b[16];
	(void)nogroup;
	snprintf(b, sizeof(b), "%u", (unsigned)gid);
	return b;
}

/* ───────────── devname(3): no character tty devices under yos ───────────── */
char *
devname(dev_t dev, mode_t type)
{
	static char b[8];
	(void)type;
	if (dev == (dev_t)-1)
		return NULL;
	strcpy(b, "-");
	return b;
}

/* ───────────── strvis/strvisx: visible copy, no escaping needed ───────────── */
char *
strvis(char *dst, const char *src, int flag)
{
	char *d = dst;
	(void)flag;
	while (*src)
		*d++ = *src++;
	*d = '\0';
	return dst;
}

int
strvisx(char *dst, const char *src, size_t len, int flag)
{
	size_t i;
	(void)flag;
	for (i = 0; i < len; i++)
		dst[i] = src[i];
	dst[len] = '\0';
	return (int)len;
}

/* ───────────── sysctlbyname for the scalars ps reads (else ENOENT) ───────────── */
int
sysctlbyname(const char *name, void *oldp, size_t *oldlenp,
    const void *newp, size_t newlen)
{
	int v;
	(void)newp; (void)newlen;

	if (strcmp(name, "kern.ccpu") == 0)
		v = 0;
	else if (strcmp(name, "kern.fscale") == 0)
		v = 2048;
	else if (strcmp(name, "hw.availpages") == 0)
		v = 65536;
	else if (strcmp(name, "kern.pid_max") == 0)
		v = 99999;
	else {
		errno = ENOENT;
		return -1;
	}
	if (oldp && oldlenp && *oldlenp >= sizeof(int)) {
		*(int *)oldp = v;
		*oldlenp = sizeof(int);
	} else if (oldlenp) {
		*oldlenp = sizeof(int);
	}
	return 0;
}

/* ───────────── libjail stubs ───────────── */
int
jail_getid(const char *name)
{
	(void)name;
	return -1;
}

char *
jail_getname(int jid)
{
	(void)jid;
	return NULL;
}

/* ───────────── minimal libxo: render the TEXT style to stdio ─────────────
 * ps's xo_emit format language (the subset ps uses):
 *   {P: }        padding — literal text after ':'
 *   {T:/%-*hs}   column title — printf fmt after '/', consumes args
 *   {l:key/%hs}  leaf value — printf fmt after '/', consumes args
 *   {:name/fmt}  leaf value
 * Every role here EMITS in TEXT style except encode-only {e:}/{n:}. The libxo
 * 'h' length modifier (UTF-8 string marker, e.g. %hs / %*hs / %-*hs) is dropped
 * to get a plain printf spec. '*' in the spec consumes an int width/precision
 * arg before the value. */
static void
xo_emit_field(FILE *fp, const char *spec, va_list *ap)
{
	char pf[48];
	size_t pl = 0;
	int stars = 0;
	char conv = 's';
	int wide = 0;        /* long long / intmax */
	char seg[2200];

	for (const char *c = spec; *c && pl + 1 < sizeof(pf); c++) {
		if (*c == 'h' || *c == 'L')    /* libxo UTF-8/length marker — drop */
			continue;
		if (*c == '.' && c[1] == '.') { /* libxo MIN..MAX -> printf MIN.MAX */
			pf[pl++] = '.';
			c++;
			continue;
		}
		if (*c == '*')
			stars++;
		if (*c == 'j' || *c == 'q')
			wide = 1;
		if (strchr("diouxXeEfgGscp", *c))
			conv = *c;
		pf[pl++] = *c;
	}
	pf[pl] = '\0';
	if (pl == 0) {
		strcpy(pf, "%s");
		conv = 's';
		stars = 0;
	}
	/* widths/precisions are int args, consumed before the value */
	int w0 = 0, w1 = 0;
	if (stars >= 1)
		w0 = va_arg(*ap, int);
	if (stars >= 2)
		w1 = va_arg(*ap, int);

	if (conv == 's') {
		char *a = va_arg(*ap, char *);
		if (!a)
			a = "";
		if (stars == 0)      snprintf(seg, sizeof(seg), pf, a);
		else if (stars == 1) snprintf(seg, sizeof(seg), pf, w0, a);
		else                 snprintf(seg, sizeof(seg), pf, w0, w1, a);
	} else if (conv == 'c') {
		int a = va_arg(*ap, int);
		if (stars == 0)      snprintf(seg, sizeof(seg), pf, a);
		else if (stars == 1) snprintf(seg, sizeof(seg), pf, w0, a);
		else                 snprintf(seg, sizeof(seg), pf, w0, w1, a);
	} else if (strchr("eEfgG", conv)) {
		double a = va_arg(*ap, double);
		if (stars == 0)      snprintf(seg, sizeof(seg), pf, a);
		else if (stars == 1) snprintf(seg, sizeof(seg), pf, w0, a);
		else                 snprintf(seg, sizeof(seg), pf, w0, w1, a);
	} else if (wide) {
		long long a = va_arg(*ap, long long);
		if (stars == 0)      snprintf(seg, sizeof(seg), pf, a);
		else if (stars == 1) snprintf(seg, sizeof(seg), pf, w0, a);
		else                 snprintf(seg, sizeof(seg), pf, w0, w1, a);
	} else {
		long a = va_arg(*ap, long);
		if (stars == 0)      snprintf(seg, sizeof(seg), pf, a);
		else if (stars == 1) snprintf(seg, sizeof(seg), pf, w0, a);
		else                 snprintf(seg, sizeof(seg), pf, w0, w1, a);
	}
	fputs(seg, fp);
}

static void
xo_vemit(FILE *fp, const char *fmt, va_list ap)
{
	const char *p = fmt;

	while (*p) {
		if (*p != '{') {
			fputc(*p++, fp);
			continue;
		}
		const char *q = p + 1;
		char role = *q ? *q : '\0';
		const char *slash = NULL, *colon = NULL, *end = q;
		while (*end && *end != '}') {
			if (*end == '/' && slash == NULL)
				slash = end;
			if (*end == ':' && colon == NULL)
				colon = end;
			end++;
		}
		if (*end != '}') {       /* literal '{' */
			fputc(*p++, fp);
			continue;
		}
		int encode_only = (role == 'e' || role == 'n' || role == 'V');
		/* Only Title/Padding/Label roles emit their literal colon text. For
		 * leaf/value roles ({l:key/fmt}, {:key/fmt}, {d:.../fmt}, …) the colon
		 * text is the field's KEY name, which TEXT style never prints — only the
		 * /fmt value is emitted. */
		int literal_label = (role == 'T' || role == 'P' || role == 'L' ||
		                     role == 't' || role == '[' || role == ']');
		if (colon && literal_label) {
			const char *lim = slash ? slash : end;
			for (const char *c = colon + 1; c < lim; c++)
				fputc(*c, fp);
		}
		/* value field: printf fmt after '/' consuming args */
		if (slash) {
			char spec[48];
			size_t sl = 0;
			for (const char *c = slash + 1; c < end && sl + 1 < sizeof(spec); c++)
				spec[sl++] = *c;
			spec[sl] = '\0';
			if (encode_only) {
				/* still consume the arg(s) so the va_list stays aligned */
				if (strchr(spec, 's'))      (void)va_arg(ap, char *);
				else if (strpbrk(spec, "eEfgG")) (void)va_arg(ap, double);
				else                        (void)va_arg(ap, long);
			} else {
				xo_emit_field(fp, spec, &ap);
			}
		}
		p = end + 1;
	}
}

int xo_parse_args(int argc, char **argv) { (void)argv; return argc; }
int xo_emit(const char *fmt, ...) { va_list ap; va_start(ap, fmt); xo_vemit(stdout, fmt, ap); va_end(ap); return 0; }
int xo_emit_h(void *h, const char *fmt, ...) { (void)h; va_list ap; va_start(ap, fmt); xo_vemit(stdout, fmt, ap); va_end(ap); return 0; }
void xo_open_list(const char *n) { (void)n; }
void xo_close_list(const char *n) { (void)n; }
void xo_open_instance(const char *n) { (void)n; }
void xo_close_instance(const char *n) { (void)n; }
void xo_open_container(const char *n) { (void)n; }
void xo_close_container(const char *n) { (void)n; }
int xo_finish(void) { fflush(stdout); return 0; }
void xo_set_flags(void *h, unsigned f) { (void)h; (void)f; }
unsigned xo_get_style(void *h) { (void)h; return 0 /* XO_STYLE_TEXT */; }
void xo_warn(const char *fmt, ...)  { va_list ap; va_start(ap, fmt); fputs("ps: ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fprintf(stderr, ": %s\n", strerror(errno)); }
void xo_warnx(const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("ps: ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }
void xo_err(int e, const char *fmt, ...)  { va_list ap; va_start(ap, fmt); fputs("ps: ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fprintf(stderr, ": %s\n", strerror(errno)); exit(e); }
void xo_errx(int e, const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("ps: ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); exit(e); }
void xo_error(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
