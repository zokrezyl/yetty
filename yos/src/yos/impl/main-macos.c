/* impl/main-macos.c — macOS-only host signal infrastructure.
 *
 * Mach exception ports + sigaltstack-backed BSD-signal landing pad.
 *
 * On macOS, synchronous CPU faults (illegal instruction, bad access,
 * arithmetic) are routed through the Mach exception subsystem BEFORE
 * the BSD signal layer ever runs. Catching them needs a Mach port,
 * task_set_exception_ports(), and a dedicated mach_msg-loop thread.
 * iOS / tvOS app bundles can't use this (task_set_exception_ports is
 * marked unavailable); they get the no-op stubs in main-darwin-app.c.
 *
 * NO #ifdef inside this file — meson selects it only when the host
 * is macOS (host_machine.system() == 'darwin' AND NOT ios_unblock
 * AND NOT tvos_unblock). Linux uses main-linux.c.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "impl/main-internal.h"

void yos_main_install_altstack(void *sp, size_t sz)
{
    stack_t ss = {0};
    ss.ss_sp = sp;
    ss.ss_size = sz;
    ss.ss_flags = 0;
    (void)sigaltstack(&ss, NULL);
}

/* ── Darwin Mach exception handler ──────────────────────────────────
 *
 * On macOS, synchronous CPU faults (illegal instruction, bad access,
 * arithmetic) are routed through the Mach exception subsystem BEFORE
 * the BSD signal layer ever runs. If no Mach handler is registered,
 * the kernel hands the exception to ReportCrash and the process dies
 * with the BSD signal recorded in waitpid(2)'s status, but our POSIX
 * sigaction handler is never invoked — we get no register dump, no
 * faulting PC, no diagnostic at all.
 *
 * Catching it requires:
 *   1. A Mach port to receive exception messages on
 *   2. task_set_exception_ports() to route synchronous faults there
 *   3. A dedicated thread that mach_msg-loops on the port, dumps the
 *      faulting thread's register state, then _exit()s.
 */

#include <mach/mach.h>
#include <mach/exception_types.h>
#include <mach/thread_status.h>
#include <pthread.h>
#include <dlfcn.h>

/* Layout of an EXCEPTION_DEFAULT|MACH_EXCEPTION_CODES request, mirrors
 * what MIG would generate from mach_exc.defs. We avoid pulling in MIG
 * so the build doesn't need a generated stub library. */
typedef struct {
    mach_msg_header_t           Head;
    mach_msg_body_t             msgh_body;
    mach_msg_port_descriptor_t  thread;
    mach_msg_port_descriptor_t  task;
    NDR_record_t                NDR;
    exception_type_t            exception;
    mach_msg_type_number_t      codeCnt;
    int64_t                     code[2];
    char                        pad[64];   /* trailer + slack */
} yos_mach_exc_request_t;

static mach_port_t yos_mach_exc_port = MACH_PORT_NULL;

static void yos_mach_dump_crash(int sig, exception_type_t exc,
                                int64_t code0, int64_t code1,
                                mach_port_t thread)
{
    int fd = open("/tmp/yos-host-crash.log",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[1024];
    int n = snprintf(buf, sizeof buf,
                     "yos: HOST CRASH (mach) signal=%d exc=%d "
                     "code0=0x%llx code1=0x%llx last_bridge=%s\n",
                     sig, (int)exc,
                     (long long)code0, (long long)code1,
                     yos_brg_last_call ? yos_brg_last_call : "<none>");
    if (n > 0) (void)!write(fd, buf, (size_t)n);

    /* Resolve faulting address (code1) via dladdr — tells us which dylib
     * / function the crashing thread was inside. */
    {
        Dl_info dli;
        if (code1 && dladdr((void *)(uintptr_t)code1, &dli)) {
            n = snprintf(buf, sizeof buf,
                         "  code1.dli: file=%s sym=%s sym_addr=%p base=%p\n",
                         dli.dli_fname ? dli.dli_fname : "?",
                         dli.dli_sname ? dli.dli_sname : "?",
                         dli.dli_saddr, dli.dli_fbase);
            if (n > 0) (void)!write(fd, buf, (size_t)n);
        }
    }

#if defined(__x86_64__)
    x86_thread_state64_t st;
    mach_msg_type_number_t cnt = x86_THREAD_STATE64_COUNT;
    if (thread_get_state(thread, x86_THREAD_STATE64,
                         (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
        n = snprintf(buf, sizeof buf,
            "  rip=0x%llx rflags=0x%llx\n"
            "  rsp=0x%llx rbp=0x%llx\n"
            "  rax=0x%llx rbx=0x%llx rcx=0x%llx rdx=0x%llx\n"
            "  rdi=0x%llx rsi=0x%llx  r8=0x%llx  r9=0x%llx\n"
            "  r10=0x%llx r11=0x%llx r12=0x%llx r13=0x%llx\n"
            "  r14=0x%llx r15=0x%llx\n",
            (unsigned long long)st.__rip, (unsigned long long)st.__rflags,
            (unsigned long long)st.__rsp, (unsigned long long)st.__rbp,
            (unsigned long long)st.__rax, (unsigned long long)st.__rbx,
            (unsigned long long)st.__rcx, (unsigned long long)st.__rdx,
            (unsigned long long)st.__rdi, (unsigned long long)st.__rsi,
            (unsigned long long)st.__r8,  (unsigned long long)st.__r9,
            (unsigned long long)st.__r10, (unsigned long long)st.__r11,
            (unsigned long long)st.__r12, (unsigned long long)st.__r13,
            (unsigned long long)st.__r14, (unsigned long long)st.__r15);
        if (n > 0) (void)!write(fd, buf, (size_t)n);

        /* Dump 32 bytes at faulting PC, useful to spot a 0xCC int3 or
         * a 0x0f 0x0b ud2 (clang's __builtin_trap). */
        unsigned char ib[32];
        if (st.__rip) {
            vm_size_t got = 0;
            kern_return_t kr = vm_read_overwrite(
                mach_task_self(), (vm_address_t)st.__rip,
                sizeof ib, (vm_address_t)ib, &got);
            if (kr == KERN_SUCCESS && got > 0) {
                int off = snprintf(buf, sizeof buf, "  bytes@rip:");
                for (vm_size_t i = 0; i < got && off + 4 < (int)sizeof buf; i++) {
                    off += snprintf(buf + off, sizeof buf - off,
                                    " %02x", ib[i]);
                }
                if (off + 1 < (int)sizeof buf) buf[off++] = '\n';
                (void)!write(fd, buf, (size_t)off);
            }
        }
        /* Resolve RIP and RAX through dladdr too — pinpoint the
         * crashing function and the indirect-call target. */
        Dl_info dli;
        if (st.__rip && dladdr((void *)(uintptr_t)st.__rip, &dli)) {
            n = snprintf(buf, sizeof buf,
                         "  rip.dli: file=%s sym=%s sym_addr=%p base=%p off=0x%lx\n",
                         dli.dli_fname ? dli.dli_fname : "?",
                         dli.dli_sname ? dli.dli_sname : "?",
                         dli.dli_saddr, dli.dli_fbase,
                         (unsigned long)((uintptr_t)st.__rip
                             - (uintptr_t)dli.dli_saddr));
            if (n > 0) (void)!write(fd, buf, (size_t)n);
        }
        if (st.__rax && dladdr((void *)(uintptr_t)st.__rax, &dli)) {
            n = snprintf(buf, sizeof buf,
                         "  rax.dli: file=%s sym=%s sym_addr=%p base=%p\n",
                         dli.dli_fname ? dli.dli_fname : "?",
                         dli.dli_sname ? dli.dli_sname : "?",
                         dli.dli_saddr, dli.dli_fbase);
            if (n > 0) (void)!write(fd, buf, (size_t)n);
        }
    }
#elif defined(__arm64__) || defined(__aarch64__)
    arm_thread_state64_t st;
    mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(thread, ARM_THREAD_STATE64,
                         (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
        n = snprintf(buf, sizeof buf,
            "  pc=0x%llx sp=0x%llx fp=0x%llx lr=0x%llx cpsr=0x%x\n",
            (unsigned long long)__darwin_arm_thread_state64_get_pc(st),
            (unsigned long long)__darwin_arm_thread_state64_get_sp(st),
            (unsigned long long)__darwin_arm_thread_state64_get_fp(st),
            (unsigned long long)__darwin_arm_thread_state64_get_lr(st),
            (unsigned)st.__cpsr);
        if (n > 0) (void)!write(fd, buf, (size_t)n);
        for (int i = 0; i < 29; i += 4) {
            n = snprintf(buf, sizeof buf,
                "  x%02d=0x%llx x%02d=0x%llx x%02d=0x%llx x%02d=0x%llx\n",
                i,   (unsigned long long)st.__x[i],
                i+1, (unsigned long long)st.__x[i+1],
                i+2, (unsigned long long)st.__x[i+2],
                i+3, (unsigned long long)st.__x[i+3]);
            if (n > 0) (void)!write(fd, buf, (size_t)n);
        }
    }
#endif

    /* Dump the recent bridge ring — tells us which libc bridge the
     * guest had just called. */
    uint64_t end = atomic_load(&yos_brg_ring_seq);
    int dump = 30;
    if ((uint64_t)dump > end) dump = (int)end;
    for (int i = 0; i < dump; i++) {
        uint64_t s = end - 1 - i;
        struct yos_brg_rec *r = &yos_brg_ring[s % YOS_BRG_RING];
        n = snprintf(buf, sizeof buf,
                     "  #%d tid=%d %-14s a0=%lx a1=%lx a2=%lx a3=%lx\n",
                     (int)(end - r->seq), (int)r->tid,
                     r->name ? r->name : "?",
                     (unsigned long)r->args[0],
                     (unsigned long)r->args[1],
                     (unsigned long)r->args[2],
                     (unsigned long)r->args[3]);
        if (n > 0) (void)!write(fd, buf, (size_t)n);
    }
    fsync(fd);
    (void)close(fd);
}

static void *yos_mach_exc_thread(void *arg)
{
    (void)arg;
    /* Don't let this thread itself be caught by our own port — would
     * deadlock. Set its per-thread exception ports to MACH_PORT_NULL
     * so it falls back to system default (kills process). */
    thread_set_exception_ports(mach_thread_self(),
        EXC_MASK_BAD_INSTRUCTION | EXC_MASK_BAD_ACCESS |
            EXC_MASK_ARITHMETIC | EXC_MASK_BREAKPOINT,
        MACH_PORT_NULL,
        EXCEPTION_DEFAULT, THREAD_STATE_NONE);

    for (;;) {
        yos_mach_exc_request_t req;
        kern_return_t kr = mach_msg(&req.Head,
                                    MACH_RCV_MSG | MACH_RCV_LARGE,
                                    0,
                                    sizeof req,
                                    yos_mach_exc_port,
                                    MACH_MSG_TIMEOUT_NONE,
                                    MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) continue;

        int sig = SIGILL;
        switch (req.exception) {
        case EXC_BAD_INSTRUCTION: sig = SIGILL;  break;
        case EXC_BAD_ACCESS:      sig = SIGSEGV; break;
        case EXC_ARITHMETIC:      sig = SIGFPE;  break;
        case EXC_BREAKPOINT:      sig = SIGTRAP; break;
        default:                  sig = SIGILL;  break;
        }
        yos_mach_dump_crash(sig, req.exception,
                            req.code[0], req.code[1],
                            req.thread.name);
        _exit(128 + sig);
    }
    return NULL;
}

void yos_main_install_signal_infra(void)
{
    int dbg = open("/tmp/yos-startup.log",
                   O_WRONLY | O_CREAT | O_APPEND, 0644);
    char b[256];

    kern_return_t kr = mach_port_allocate(mach_task_self(),
                                          MACH_PORT_RIGHT_RECEIVE,
                                          &yos_mach_exc_port);
    if (kr != KERN_SUCCESS) {
        if (dbg >= 0) {
            int n = snprintf(b, sizeof b,
                             "mach_port_allocate failed: %d\n", kr);
            (void)!write(dbg, b, (size_t)n);
            (void)close(dbg);
        }
        return;
    }
    kr = mach_port_insert_right(mach_task_self(), yos_mach_exc_port,
                                yos_mach_exc_port,
                                MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        if (dbg >= 0) {
            int n = snprintf(b, sizeof b,
                             "mach_port_insert_right failed: %d\n", kr);
            (void)!write(dbg, b, (size_t)n);
            (void)close(dbg);
        }
        /* Release the receive port we allocated above. Without this
         * the port stays in the task's port-name space forever even
         * though nothing references it. */
        (void)mach_port_mod_refs(mach_task_self(), yos_mach_exc_port,
                                 MACH_PORT_RIGHT_RECEIVE, -1);
        yos_mach_exc_port = MACH_PORT_NULL;
        return;
    }
    kr = task_set_exception_ports(mach_task_self(),
        EXC_MASK_BAD_INSTRUCTION | EXC_MASK_BAD_ACCESS |
            EXC_MASK_ARITHMETIC | EXC_MASK_BREAKPOINT,
        yos_mach_exc_port,
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
        THREAD_STATE_NONE);
    if (kr != KERN_SUCCESS) {
        if (dbg >= 0) {
            int n = snprintf(b, sizeof b,
                             "task_set_exception_ports failed: %d\n", kr);
            (void)!write(dbg, b, (size_t)n);
            (void)close(dbg);
        }
        /* Drop both rights we installed: the send right we made above
         * and the original receive right from mach_port_allocate. */
        (void)mach_port_mod_refs(mach_task_self(), yos_mach_exc_port,
                                 MACH_PORT_RIGHT_SEND, -1);
        (void)mach_port_mod_refs(mach_task_self(), yos_mach_exc_port,
                                 MACH_PORT_RIGHT_RECEIVE, -1);
        yos_mach_exc_port = MACH_PORT_NULL;
        return;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int rc = pthread_create(&t, &attr, yos_mach_exc_thread, NULL);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        /* No thread to drain the exception port — un-install the
         * exception handler and free the port so messages don't
         * queue up forever. The kernel falls back to the host's
         * default handler (signal-delivery) when there's no port. */
        (void)task_set_exception_ports(mach_task_self(),
            EXC_MASK_BAD_INSTRUCTION | EXC_MASK_BAD_ACCESS |
                EXC_MASK_ARITHMETIC | EXC_MASK_BREAKPOINT,
            MACH_PORT_NULL,
            EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
            THREAD_STATE_NONE);
        (void)mach_port_mod_refs(mach_task_self(), yos_mach_exc_port,
                                 MACH_PORT_RIGHT_SEND, -1);
        (void)mach_port_mod_refs(mach_task_self(), yos_mach_exc_port,
                                 MACH_PORT_RIGHT_RECEIVE, -1);
        yos_mach_exc_port = MACH_PORT_NULL;
        if (dbg >= 0) {
            int n = snprintf(b, sizeof b,
                             "yos-mach-exc pthread_create failed rc=%d, "
                             "rolled back exception handler\n", rc);
            (void)!write(dbg, b, (size_t)n);
            (void)close(dbg);
        }
        return;
    }

    if (dbg >= 0) {
        int n = snprintf(b, sizeof b,
                         "yos-mach-exc installed pthread_create=%d port=%u\n",
                         rc, (unsigned)yos_mach_exc_port);
        (void)!write(dbg, b, (size_t)n);
        (void)close(dbg);
    }
}
