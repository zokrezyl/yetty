#ifndef YOS_SIG_H
#define YOS_SIG_H

#include "yos/types.h"

int32_t yos_sig_rt_sigaction(struct yos_exec_ctx *ctx, int32_t signum, uint32_t act, uint32_t oldact, uint32_t sigsetsize);
int32_t yos_sig_rt_sigprocmask(struct yos_exec_ctx *ctx, int32_t how, uint32_t set, uint32_t oset, uint32_t sigsetsize);
int32_t yos_sigaction(struct yos_exec_ctx *ctx, int32_t signum, uint32_t act, uint32_t oldact);
int32_t yos_sigprocmask(struct yos_exec_ctx *ctx, int32_t how, uint32_t set, uint32_t oset);
int32_t yos_pthread_sigmask(struct yos_exec_ctx *ctx, int32_t how, uint32_t set, uint32_t oset);
uint32_t yos_signal(struct yos_exec_ctx *ctx, int32_t signum, uint32_t handler);
int32_t yos_sigpending(struct yos_exec_ctx *ctx, uint32_t set);
int32_t yos_sigaltstack(struct yos_exec_ctx *ctx, uint32_t ss, uint32_t oss);
int32_t yos_sigwait(struct yos_exec_ctx *ctx, uint32_t set, uint32_t sig_out);
int32_t yos_sigwaitinfo(struct yos_exec_ctx *ctx, uint32_t set, uint32_t info);
int32_t yos_sigtimedwait(struct yos_exec_ctx *ctx, uint32_t set, uint32_t info, uint32_t timeout);

#endif /* YOS_SIG_H */
