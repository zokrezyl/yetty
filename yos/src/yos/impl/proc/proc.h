#ifndef YOS_PROC_H
#define YOS_PROC_H

#include "yos/types.h"

int32_t yos_exit(struct yos_exec_ctx *ctx, int32_t code);
int32_t yos_fork(struct yos_exec_ctx *ctx);
int yos_fork_rewinding(struct yos_exec_ctx *ctx);
int32_t yos_waitpid(struct yos_exec_ctx *ctx, int32_t pid, uint32_t stat_addr, int32_t options);
int32_t yos_execve(struct yos_exec_ctx *ctx, uint32_t filename, uint32_t argv, uint32_t envp);
int32_t yos_kill(struct yos_exec_ctx *ctx, int32_t pid, int32_t sig);
int32_t yos_wait4(struct yos_exec_ctx *ctx, int32_t pid, uint32_t stat_addr, int32_t options, uint32_t ru);
int32_t yos_proc_clone(struct yos_exec_ctx *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
int32_t yos_vfork(struct yos_exec_ctx *ctx);
int32_t yos_tkill(struct yos_exec_ctx *ctx, int32_t pid, int32_t sig);
int32_t yos_proc_set_thread_area(struct yos_exec_ctx *ctx);
int32_t yos_proc_get_thread_area(struct yos_exec_ctx *ctx);
int32_t yos_proc_exit_group(struct yos_exec_ctx *ctx, int32_t error_code);
int32_t yos_proc_set_tid_address(struct yos_exec_ctx *ctx, uint32_t tidptr);
int32_t yos_tgkill(struct yos_exec_ctx *ctx, int32_t tgid, int32_t pid, int32_t sig);
int32_t yos_waitid(struct yos_exec_ctx *ctx, int32_t which, int32_t pid, uint32_t infop, int32_t options, uint32_t ru);
int32_t yos_proc_arch_prctl(struct yos_exec_ctx *ctx);
int32_t yos_proc_clock_gettime(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t tp);
int32_t yos_proc_clock_getres(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t res);
int32_t yos_proc_nanosleep(struct yos_exec_ctx *ctx, uint32_t rqtp, uint32_t rmtp);

/* Identity getters return guest pids/pgids/sids from the per-runtime
 * proc table — never host TIDs. The whole pgrp/session story
 * (kill/setpgid/setsid/TIOCGPGRP) is virtualized together so the values
 * userspace sees from getpgrp()/tcgetpgrp() agree, which is what
 * busybox ash's foreground-pgrp loop checks. */
int32_t yos_getpid(struct yos_exec_ctx *ctx);
int32_t yos_getppid(struct yos_exec_ctx *ctx);
int32_t yos_getsid(struct yos_exec_ctx *ctx, int32_t pid);
int32_t yos_getpgid(struct yos_exec_ctx *ctx, int32_t pid);
int32_t yos_getpgrp(struct yos_exec_ctx *ctx);
int32_t yos_gettid(struct yos_exec_ctx *ctx);
int32_t yos_setpgid(struct yos_exec_ctx *ctx, int32_t pid, int32_t pgid);
int32_t yos_setsid(struct yos_exec_ctx *ctx);

/* Public kill-by-pid for ctx-less callers (yctl daemon). */
int yos_proc_kill_by_pid(struct yos_runtime *rt, int32_t pid, int32_t sig);

#endif /* YOS_PROC_H */
