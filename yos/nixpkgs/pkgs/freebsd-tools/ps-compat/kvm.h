#ifndef _YOS_KVM_H_
#define _YOS_KVM_H_
/* libkvm(3) surface the real bin/ps links. yos has no libkvm; these route
 * through sysctl(CTL_KERN, KERN_PROC, KERN_PROC_*) — see yos_ps_compat.c. */
#include <sys/types.h>
struct kinfo_proc;
typedef struct yos_kvm kvm_t;
kvm_t *kvm_openfiles(const char *, const char *, const char *, int, char *);
int    kvm_close(kvm_t *);
struct kinfo_proc *kvm_getprocs(kvm_t *, int, int, int *);
char **kvm_getargv(kvm_t *, const struct kinfo_proc *, int);
char **kvm_getenvv(kvm_t *, const struct kinfo_proc *, int);
char  *kvm_geterr(kvm_t *);
#endif
