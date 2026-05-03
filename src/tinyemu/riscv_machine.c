/*
 * RISCV machine
 *
 * Copyright (c) 2016-2017 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// clang-format off
#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"
#include "elf.h"
#include "riscv_cpu.h"
// clang-format on
#include <pthread.h>
#include <stdatomic.h>
#include "smp.h"
#include "ydebug.h"
static pthread_mutex_t plic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t htif_mutex = PTHREAD_MUTEX_INITIALIZER;
#define PLIC_LOCK() pthread_mutex_lock(&plic_mutex)
#define PLIC_UNLOCK() pthread_mutex_unlock(&plic_mutex)
#define HTIF_LOCK() pthread_mutex_lock(&htif_mutex)
#define HTIF_UNLOCK() pthread_mutex_unlock(&htif_mutex)
/* CLINT uses atomics - no lock needed */

/* RISCV machine */

#define MAX_CPUS SMP_MAX_CPUS

typedef struct RISCVMachine {
  VirtMachine common;
  PhysMemoryMap *mem_map;
  int max_xlen;
  int num_cpus;
  RISCVCPUState *cpu_state[MAX_CPUS];
  uint64_t ram_size;
  /* RTC */
  BOOL rtc_real_time;
  _Atomic uint64_t rtc_start_time;
  uint64_t boot_start_time; /* monotonic ns at machine init */
  /* CLINT - per CPU (atomic for lock-free access) */
  _Atomic uint32_t msip[MAX_CPUS];    /* software interrupt pending */
  _Atomic uint64_t timecmp[MAX_CPUS]; /* timer compare per CPU */
  /* PLIC */
  uint32_t plic_pending_irq;
  uint32_t plic_served_irq[MAX_CPUS * 2];  /* per-context (M + S per CPU) */
  uint32_t plic_enable[MAX_CPUS * 2];      /* per-context enable bits */
  IRQSignal plic_irq[32]; /* IRQ 0 is not used */
  /* HTIF */
  uint64_t htif_tohost, htif_fromhost;
  uint64_t htif_addr; /* dynamically detected from ELF .htif section */

  VIRTIODevice *keyboard_dev;
  VIRTIODevice *mouse_dev;

  int virtio_count;

  SMPState *smp;
} RISCVMachine;

#define LOW_RAM_SIZE 0x00010000 /* 64KB */
#define RAM_BASE_ADDR 0x80000000
#define CLINT_BASE_ADDR 0x02000000
#define CLINT_SIZE 0x000c0000
#define DEFAULT_HTIF_BASE_ADDR 0x40008000
#define VIRTIO_BASE_ADDR 0x40010000
#define VIRTIO_SIZE 0x1000
#define VIRTIO_IRQ 1
#define PLIC_BASE_ADDR 0x40100000
#define PLIC_SIZE 0x00400000
#define FRAMEBUFFER_BASE_ADDR 0x41000000

#define RTC_FREQ 10000000
#define RTC_FREQ_DIV                                                           \
  16 /* arbitrary, relative to CPU freq to have a                              \
        10 MHz frequency */

static uint64_t rtc_get_real_time(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * RTC_FREQ +
         (ts.tv_nsec / (1000000000 / RTC_FREQ));
}

static uint64_t rtc_get_time(RISCVMachine *m) {
  uint64_t val;
  if (m->rtc_real_time) {
    val = rtc_get_real_time() - atomic_load(&m->rtc_start_time);
  } else {
    /* Use max cycles across all CPUs - time advances if ANY CPU runs */
    uint64_t max_cycles = 0;
    for (int i = 0; i < m->num_cpus; i++) {
      uint64_t c = riscv_cpu_get_cycles(m->cpu_state[i]);
      if (c > max_cycles) max_cycles = c;
    }
    val = max_cycles / RTC_FREQ_DIV;
  }
  return val;
}

static uint32_t htif_read(void *opaque, uint32_t offset, int size_log2) {
  RISCVMachine *s = opaque;
  uint32_t val;

  HTIF_LOCK();

  assert(size_log2 == 2);
  /* .htif section layout: fromhost at offset 0, tohost at offset 8 */
  switch (offset) {
  case 0:
    val = s->htif_fromhost;
    break;
  case 4:
    val = s->htif_fromhost >> 32;
    break;
  case 8:
    val = s->htif_tohost;
    break;
  case 12:
    val = s->htif_tohost >> 32;
    break;
  default:
    val = 0;
    break;
  }

  HTIF_UNLOCK();
  return val;
}

/* htif_handle_cmd_unlocked: called WITHOUT holding HTIF_LOCK.
 * tohost_val is a snapshot taken while lock was held. */
static void htif_handle_cmd_unlocked(RISCVMachine *s, uint64_t tohost_val) {
  uint32_t device, cmd;

  device = tohost_val >> 56;
  cmd = (tohost_val >> 48) & 0xff;
  if (tohost_val == 1) {
    /* shuthost */
    printf("\nPower off.\n");
    exit(0);
  } else if (device == 1 && cmd == 1) {
    uint8_t buf[1];
    buf[0] = tohost_val & 0xff;
    /* Console write can block - must NOT hold lock here */
    s->common.console->write_data(s->common.console->opaque, buf, 1);
    /* Update state under lock */
    HTIF_LOCK();
    s->htif_tohost = 0;
    s->htif_fromhost = ((uint64_t)device << 56) | ((uint64_t)cmd << 48);
    HTIF_UNLOCK();
  } else if (device == 1 && cmd == 0) {
    /* request keyboard interrupt */
    HTIF_LOCK();
    s->htif_tohost = 0;
    HTIF_UNLOCK();
  } else {
    printf("HTIF: unsupported tohost=0x%016" PRIx64 "\n", tohost_val);
  }
}

static void htif_write(void *opaque, uint32_t offset, uint32_t val,
                       int size_log2) {
  RISCVMachine *s = opaque;
  uint64_t tohost_snapshot = 0;
  int need_cmd = 0;

  ydebug("htif_write: offset=0x%x val=0x%x", offset, val);
  HTIF_LOCK();

  assert(size_log2 == 2);
  /* .htif section layout: fromhost at offset 0, tohost at offset 8 */
  switch (offset) {
  case 0:
    s->htif_fromhost = (s->htif_fromhost & ~0xffffffff) | val;
    break;
  case 4:
    s->htif_fromhost = (s->htif_fromhost & 0xffffffff) | ((uint64_t)val << 32);
    break;
  case 8:
    s->htif_tohost = (s->htif_tohost & ~0xffffffff) | val;
    break;
  case 12:
    s->htif_tohost = (s->htif_tohost & 0xffffffff) | ((uint64_t)val << 32);
    tohost_snapshot = s->htif_tohost;
    need_cmd = 1;
    break;
  default:
    break;
  }

  HTIF_UNLOCK();

  /* Handle command AFTER releasing lock to avoid deadlock on console I/O */
  if (need_cmd) {
    htif_handle_cmd_unlocked(s, tohost_snapshot);
  }
}

#if 0
static void htif_poll(RISCVMachine *s)
{
    uint8_t buf[1];
    int ret;

    if (s->htif_fromhost == 0) {
        ret = s->console->read_data(s->console->opaque, buf, 1);
        if (ret == 1) {
            s->htif_fromhost = ((uint64_t)1 << 56) | ((uint64_t)0 << 48) |
                buf[0];
        }
    }
}
#endif

static uint32_t clint_read(void *opaque, uint32_t offset, int size_log2) {
  RISCVMachine *m = opaque;
  uint32_t val;
  int cpu_id;

  /* No lock needed - per-CPU data accessed atomically */
  assert(size_log2 == 2);

  /* mtime: 0xbff8-0xbfff */
  if (offset == 0xbff8) {
    val = rtc_get_time(m);
  } else if (offset == 0xbffc) {
    val = rtc_get_time(m) >> 32;
  }
  /* msip: 0x0000-0x3fff (4 bytes per CPU) */
  else if (offset < 0x4000) {
    cpu_id = offset >> 2;
    if (cpu_id < m->num_cpus)
      val = atomic_load(&m->msip[cpu_id]);
    else
      val = 0;
  }
  /* mtimecmp: 0x4000-0xbff7 (8 bytes per CPU) */
  else if (offset >= 0x4000 && offset < 0xbff8) {
    cpu_id = (offset - 0x4000) >> 3;
    if (cpu_id < m->num_cpus) {
      uint64_t tc = atomic_load(&m->timecmp[cpu_id]);
      if ((offset & 4) == 0)
        val = tc;
      else
        val = tc >> 32;
    } else {
      val = 0;
    }
  } else {
    val = 0;
  }
  return val;
}

static void clint_write(void *opaque, uint32_t offset, uint32_t val,
                        int size_log2) {
  RISCVMachine *m = opaque;
  int cpu_id;

  /* No lock needed - per-CPU data accessed atomically */
  assert(size_log2 == 2);

  /* msip: 0x0000-0x3fff (4 bytes per CPU) */
  if (offset < 0x4000) {
    cpu_id = offset >> 2;
    if (cpu_id < m->num_cpus) {
      ydebug("SYNC: CLINT cpu%d MSIP write val=%d mip_before=0x%x",
             cpu_id, val & 1, riscv_cpu_get_mip(m->cpu_state[cpu_id]));
      atomic_store(&m->msip[cpu_id], val & 1);
      atomic_thread_fence(memory_order_seq_cst);
      if (val & 1) {
        riscv_cpu_set_mip(m->cpu_state[cpu_id], MIP_MSIP);
        ydebug("SYNC: CLINT cpu%d MSIP SET mip_after=0x%x",
               cpu_id, riscv_cpu_get_mip(m->cpu_state[cpu_id]));
        smp_wakeup_cpu(m->smp, cpu_id);  /* Wake CPU from WFI */
      } else {
        riscv_cpu_reset_mip(m->cpu_state[cpu_id], MIP_MSIP);
        ydebug("SYNC: CLINT cpu%d MSIP CLEAR mip_after=0x%x",
               cpu_id, riscv_cpu_get_mip(m->cpu_state[cpu_id]));
      }
    }
  }
  /* mtimecmp: 0x4000-0xbff7 (8 bytes per CPU) */
  else if (offset >= 0x4000 && offset < 0xbff8) {
    cpu_id = (offset - 0x4000) >> 3;
    if (cpu_id < m->num_cpus) {
      uint64_t old_tc = atomic_load(&m->timecmp[cpu_id]);
      uint64_t tc = old_tc;
      if ((offset & 4) == 0)
        tc = (tc & ~0xffffffffULL) | val;
      else
        tc = (tc & 0xffffffff) | ((uint64_t)val << 32);
      atomic_store(&m->timecmp[cpu_id], tc);
      ydebug("SYNC: CLINT cpu%d TIMECMP %s old=0x%llx new=0x%llx rtc=0x%llx mip_before=0x%x",
             cpu_id, (offset & 4) ? "HIGH" : "LOW",
             (unsigned long long)old_tc, (unsigned long long)tc,
             (unsigned long long)rtc_get_time(m),
             riscv_cpu_get_mip(m->cpu_state[cpu_id]));
      riscv_cpu_reset_mip(m->cpu_state[cpu_id], MIP_MTIP);
      ydebug("SYNC: CLINT cpu%d MTIP CLEAR mip_after=0x%x",
             cpu_id, riscv_cpu_get_mip(m->cpu_state[cpu_id]));
    }
  }
}

static void plic_update_mip(RISCVMachine *s) {
  int i;
  uint32_t m_pending, s_pending;
  static uint64_t call_count = 0;
  call_count++;
  for (i = 0; i < s->num_cpus; i++) {
    int s_ctx = i * 2;
    int m_ctx = i * 2 + 1;
    m_pending = s->plic_pending_irq & s->plic_enable[m_ctx] & ~s->plic_served_irq[m_ctx];
    s_pending = s->plic_pending_irq & s->plic_enable[s_ctx] & ~s->plic_served_irq[s_ctx];
    uint32_t old_mip = riscv_cpu_get_mip(s->cpu_state[i]);
    if ((call_count < 100) || (call_count % 10000) == 0 || m_pending || s_pending) {
      ydebug("SYNC: PLIC_UPD cpu%d pend=0x%x en_s=0x%x en_m=0x%x srv_s=0x%x srv_m=0x%x m_pend=0x%x s_pend=0x%x",
             i, s->plic_pending_irq, s->plic_enable[s_ctx], s->plic_enable[m_ctx],
             s->plic_served_irq[s_ctx], s->plic_served_irq[m_ctx], m_pending, s_pending);
    }
    if (m_pending) {
      riscv_cpu_set_mip(s->cpu_state[i], MIP_MEIP);
      ydebug("SYNC: PLIC cpu%d MEIP SET pend=0x%x mip 0x%x->0x%x",
             i, m_pending, old_mip, riscv_cpu_get_mip(s->cpu_state[i]));
      ydebug("SYNC: PLIC cpu%d WAKEUP_MEIP pd=%d", i, riscv_cpu_get_power_down(s->cpu_state[i]));
      smp_wakeup_cpu(s->smp, i);
    } else {
      riscv_cpu_reset_mip(s->cpu_state[i], MIP_MEIP);
    }
    if (s_pending) {
      riscv_cpu_set_mip(s->cpu_state[i], MIP_SEIP);
      ydebug("SYNC: PLIC cpu%d SEIP SET pend=0x%x mip 0x%x->0x%x",
             i, s_pending, old_mip, riscv_cpu_get_mip(s->cpu_state[i]));
      ydebug("SYNC: PLIC cpu%d WAKEUP_SEIP pd=%d", i, riscv_cpu_get_power_down(s->cpu_state[i]));
      smp_wakeup_cpu(s->smp, i);
    } else {
      riscv_cpu_reset_mip(s->cpu_state[i], MIP_SEIP);
    }
  }
}

#define PLIC_HART_BASE 0x200000
#define PLIC_HART_SIZE 0x1000

#define PLIC_ENABLE_BASE 0x2000
#define PLIC_ENABLE_STRIDE 0x80

static uint32_t plic_read(void *opaque, uint32_t offset, int size_log2) {
  RISCVMachine *s = opaque;
  uint32_t val, mask;
  int i, context, local_off;
  PLIC_LOCK();
  assert(size_log2 == 2);

  /* Enable registers: 0x2000 + context * 0x80, each context has 32 words.
   * TinyEMU only supports 31 IRQs, so only word 0 (offset 0) matters. */
  if (offset >= PLIC_ENABLE_BASE && offset < PLIC_HART_BASE) {
    uint32_t ctx_offset = offset - PLIC_ENABLE_BASE;
    context = ctx_offset / PLIC_ENABLE_STRIDE;
    uint32_t word_offset = (ctx_offset % PLIC_ENABLE_STRIDE) / 4;
    if (context < s->num_cpus * 2 && word_offset == 0) {
      val = s->plic_enable[context];
    } else {
      val = 0;  /* Out of bounds or word 1+ (no IRQs there) */
    }
  }
  /* Per-hart context registers at 0x200000 + context * 0x1000 */
  else if (offset >= PLIC_HART_BASE) {
    context = (offset - PLIC_HART_BASE) / PLIC_HART_SIZE;
    local_off = offset & (PLIC_HART_SIZE - 1);
    if (context < s->num_cpus * 2) {
      if (local_off == 0) {
        /* Threshold register */
        val = 0;
      } else if (local_off == 4) {
        /* Claim register - return highest pending IRQ for THIS context */
        /* PLIC bit layout: bit N = interrupt source N, so return bit position directly */
        mask = s->plic_pending_irq & s->plic_enable[context] & ~s->plic_served_irq[context];
        if (mask != 0) {
          i = ctz32(mask);
          s->plic_served_irq[context] |= 1 << i;
          plic_update_mip(s);
          val = i;  /* bit N = IRQ N */
        } else {
          val = 0;
        }
      } else {
        val = 0;
      }
    } else {
      val = 0;
    }
  } else {
    val = 0;
  }
  PLIC_UNLOCK();
  return val;
}

static void plic_write(void *opaque, uint32_t offset, uint32_t val,
                       int size_log2) {
  RISCVMachine *s = opaque;
  int context, local_off;

  PLIC_LOCK();
  assert(size_log2 == 2);

  /* Enable registers: 0x2000 + context * 0x80, each context has 32 words.
   * TinyEMU only supports 31 IRQs, so only word 0 (offset 0) matters. */
  if (offset >= PLIC_ENABLE_BASE && offset < PLIC_HART_BASE) {
    uint32_t ctx_offset = offset - PLIC_ENABLE_BASE;
    context = ctx_offset / PLIC_ENABLE_STRIDE;
    uint32_t word_offset = (ctx_offset % PLIC_ENABLE_STRIDE) / 4;
    ydebug("SYNC: PLIC_WR_EN off=0x%x ctx=%d word=%d val=0x%x", offset, context, word_offset, val);
    if (context < s->num_cpus * 2 && word_offset == 0) {
      s->plic_enable[context] = val;
      ydebug("SYNC: PLIC_EN_SET ctx=%d val=0x%x", context, val);
      plic_update_mip(s);
    }
    /* Ignore writes to word 1+ (no IRQs there for TinyEMU) */
  }
  /* Per-hart context registers at 0x200000 + context * 0x1000 */
  else if (offset >= PLIC_HART_BASE) {
    context = (offset - PLIC_HART_BASE) / PLIC_HART_SIZE;
    local_off = offset & (PLIC_HART_SIZE - 1);
    if (context < s->num_cpus * 2) {
      if (local_off == 0) {
        /* Threshold register - ignore */
      } else if (local_off == 4) {
        /* Complete register - mark IRQ as no longer being serviced */
        /* PLIC bit layout: bit N = IRQ N, so use val directly (no decrement) */
        if (val > 0 && val < 32) {
          s->plic_served_irq[context] &= ~(1 << val);
          plic_update_mip(s);
        }
      }
    }
  }
  PLIC_UNLOCK();
}

static void plic_set_irq(void *opaque, int irq_num, int state) {
  RISCVMachine *s = opaque;
  uint32_t mask;

  PLIC_LOCK();
  /* PLIC bit layout: bit N = interrupt source N (source 0 is reserved) */
  mask = 1 << irq_num;
  ydebug("PLIC: set_irq irq=%d state=%d pending=0x%x mask=0x%x", irq_num, state, s->plic_pending_irq, mask);
  if (state)
    s->plic_pending_irq |= mask;
  else
    s->plic_pending_irq &= ~mask;
  plic_update_mip(s);
  PLIC_UNLOCK();
}

static uint8_t *get_ram_ptr(RISCVMachine *s, uint64_t paddr, BOOL is_rw) {
  return phys_mem_get_ram_ptr(s->mem_map, paddr, is_rw);
}

/* FDT machine description */

#define FDT_MAGIC 0xd00dfeed
#define FDT_VERSION 17

struct fdt_header {
  uint32_t magic;
  uint32_t totalsize;
  uint32_t off_dt_struct;
  uint32_t off_dt_strings;
  uint32_t off_mem_rsvmap;
  uint32_t version;
  uint32_t last_comp_version; /* <= 17 */
  uint32_t boot_cpuid_phys;
  uint32_t size_dt_strings;
  uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
  uint64_t address;
  uint64_t size;
};

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_NOP 4
#define FDT_END 9

typedef struct {
  uint32_t *tab;
  int tab_len;
  int tab_size;
  int open_node_count;

  char *string_table;
  int string_table_len;
  int string_table_size;
} FDTState;

static FDTState *fdt_init(void) {
  FDTState *s;
  s = mallocz(sizeof(*s));
  return s;
}

static void fdt_alloc_len(FDTState *s, int len) {
  int new_size;
  if (unlikely(len > s->tab_size)) {
    new_size = max_int(len, s->tab_size * 3 / 2);
    s->tab = realloc(s->tab, new_size * sizeof(uint32_t));
    s->tab_size = new_size;
  }
}

static void fdt_put32(FDTState *s, int v) {
  fdt_alloc_len(s, s->tab_len + 1);
  s->tab[s->tab_len++] = cpu_to_be32(v);
}

/* the data is zero padded */
static void fdt_put_data(FDTState *s, const uint8_t *data, int len) {
  int len1;

  len1 = (len + 3) / 4;
  fdt_alloc_len(s, s->tab_len + len1);
  memcpy(s->tab + s->tab_len, data, len);
  memset((uint8_t *)(s->tab + s->tab_len) + len, 0, -len & 3);
  s->tab_len += len1;
}

static void fdt_begin_node(FDTState *s, const char *name) {
  fdt_put32(s, FDT_BEGIN_NODE);
  fdt_put_data(s, (uint8_t *)name, strlen(name) + 1);
  s->open_node_count++;
}

static void fdt_begin_node_num(FDTState *s, const char *name, uint64_t n) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s@%" PRIx64, name, n);
  fdt_begin_node(s, buf);
}

static void fdt_end_node(FDTState *s) {
  fdt_put32(s, FDT_END_NODE);
  s->open_node_count--;
}

static int fdt_get_string_offset(FDTState *s, const char *name) {
  int pos, new_size, name_size, new_len;

  pos = 0;
  while (pos < s->string_table_len) {
    if (!strcmp(s->string_table + pos, name))
      return pos;
    pos += strlen(s->string_table + pos) + 1;
  }
  /* add a new string */
  name_size = strlen(name) + 1;
  new_len = s->string_table_len + name_size;
  if (new_len > s->string_table_size) {
    new_size = max_int(new_len, s->string_table_size * 3 / 2);
    s->string_table = realloc(s->string_table, new_size);
    s->string_table_size = new_size;
  }
  pos = s->string_table_len;
  memcpy(s->string_table + pos, name, name_size);
  s->string_table_len = new_len;
  return pos;
}

static void fdt_prop(FDTState *s, const char *prop_name, const void *data,
                     int data_len) {
  fdt_put32(s, FDT_PROP);
  fdt_put32(s, data_len);
  fdt_put32(s, fdt_get_string_offset(s, prop_name));
  fdt_put_data(s, data, data_len);
}

static void fdt_prop_tab_u32(FDTState *s, const char *prop_name, uint32_t *tab,
                             int tab_len) {
  int i;
  fdt_put32(s, FDT_PROP);
  fdt_put32(s, tab_len * sizeof(uint32_t));
  fdt_put32(s, fdt_get_string_offset(s, prop_name));
  for (i = 0; i < tab_len; i++)
    fdt_put32(s, tab[i]);
}

static void fdt_prop_u32(FDTState *s, const char *prop_name, uint32_t val) {
  fdt_prop_tab_u32(s, prop_name, &val, 1);
}

static void fdt_prop_tab_u64(FDTState *s, const char *prop_name, uint64_t v0) {
  uint32_t tab[2];
  tab[0] = v0 >> 32;
  tab[1] = v0;
  fdt_prop_tab_u32(s, prop_name, tab, 2);
}

static void fdt_prop_tab_u64_2(FDTState *s, const char *prop_name, uint64_t v0,
                               uint64_t v1) {
  uint32_t tab[4];
  tab[0] = v0 >> 32;
  tab[1] = v0;
  tab[2] = v1 >> 32;
  tab[3] = v1;
  fdt_prop_tab_u32(s, prop_name, tab, 4);
}

static void fdt_prop_str(FDTState *s, const char *prop_name, const char *str) {
  fdt_prop(s, prop_name, str, strlen(str) + 1);
}

/* NULL terminated string list */
static void fdt_prop_tab_str(FDTState *s, const char *prop_name, ...) {
  va_list ap;
  int size, str_size;
  char *ptr, *tab;

  va_start(ap, prop_name);
  size = 0;
  for (;;) {
    ptr = va_arg(ap, char *);
    if (!ptr)
      break;
    str_size = strlen(ptr) + 1;
    size += str_size;
  }
  va_end(ap);

  tab = malloc(size);
  va_start(ap, prop_name);
  size = 0;
  for (;;) {
    ptr = va_arg(ap, char *);
    if (!ptr)
      break;
    str_size = strlen(ptr) + 1;
    memcpy(tab + size, ptr, str_size);
    size += str_size;
  }
  va_end(ap);

  fdt_prop(s, prop_name, tab, size);
  free(tab);
}

/* write the FDT to 'dst1'. return the FDT size in bytes.
 * `reserve` is an array of `reserve_count` {addr,size} u64 pairs to emit
 * in the /memreserve/ table — the kernel keeps its hands off these
 * regions. Used to keep Linux's page allocator out of the OpenSBI
 * runtime so virtio-blk DMA can't clobber it. */
int fdt_output(FDTState *s, uint8_t *dst,
               const uint64_t (*reserve)[2], int reserve_count) {
  struct fdt_header *h;
  struct fdt_reserve_entry *re;
  int dt_struct_size;
  int dt_strings_size;
  int pos, i;

  assert(s->open_node_count == 0);

  fdt_put32(s, FDT_END);

  dt_struct_size = s->tab_len * sizeof(uint32_t);
  dt_strings_size = s->string_table_len;

  h = (struct fdt_header *)dst;
  h->magic = cpu_to_be32(FDT_MAGIC);
  h->version = cpu_to_be32(FDT_VERSION);
  h->last_comp_version = cpu_to_be32(16);
  h->boot_cpuid_phys = cpu_to_be32(0);
  h->size_dt_strings = cpu_to_be32(dt_strings_size);
  h->size_dt_struct = cpu_to_be32(dt_struct_size);

  pos = sizeof(struct fdt_header);

  h->off_dt_struct = cpu_to_be32(pos);
  memcpy(dst + pos, s->tab, dt_struct_size);
  pos += dt_struct_size;

  /* align to 8 */
  while ((pos & 7) != 0) {
    dst[pos++] = 0;
  }
  h->off_mem_rsvmap = cpu_to_be32(pos);
  for (i = 0; i < reserve_count; i++) {
    put_be64(dst + pos,     reserve[i][0]);
    put_be64(dst + pos + 8, reserve[i][1]);
    pos += sizeof(struct fdt_reserve_entry);
  }
  /* terminator (zero address + zero size, big-endian — but zero is the same) */
  re = (struct fdt_reserve_entry *)(dst + pos);
  re->address = 0;
  re->size = 0;
  pos += sizeof(struct fdt_reserve_entry);

  h->off_dt_strings = cpu_to_be32(pos);
  memcpy(dst + pos, s->string_table, dt_strings_size);
  pos += dt_strings_size;

  /* align to 8, just in case */
  while ((pos & 7) != 0) {
    dst[pos++] = 0;
  }

  h->totalsize = cpu_to_be32(pos);
  return pos;
}

void fdt_end(FDTState *s) {
  free(s->tab);
  free(s->string_table);
  free(s);
}

static int riscv_build_fdt(RISCVMachine *m, uint8_t *dst, uint64_t kernel_start,
                           uint64_t kernel_size, uint64_t initrd_start,
                           uint64_t initrd_size, const char *cmd_line,
                           uint64_t bios_addr, uint64_t bios_size) {
  FDTState *s;
  int size, max_xlen, i, j, cur_phandle, plic_phandle;
  int intc_phandle[MAX_CPUS];
  char isa_string[128], *q;
  uint32_t misa;
  uint32_t tab[MAX_CPUS * 4]; /* 4 entries per CPU for CLINT/PLIC */
  FBDevice *fb_dev;
  int num_cpus = m->num_cpus;

  s = fdt_init();

  cur_phandle = 1;

  fdt_begin_node(s, "");
  fdt_prop_u32(s, "#address-cells", 2);
  fdt_prop_u32(s, "#size-cells", 2);
  fdt_prop_str(s, "compatible", "ucbbar,riscvemu-bar_dev");
  fdt_prop_str(s, "model", "ucbbar,riscvemu-bare");

  /* CPU list */
  fdt_begin_node(s, "cpus");
  fdt_prop_u32(s, "#address-cells", 1);
  fdt_prop_u32(s, "#size-cells", 0);
  fdt_prop_u32(s, "timebase-frequency", RTC_FREQ);

  max_xlen = m->max_xlen;
  misa = riscv_cpu_get_misa(m->cpu_state[0]);
  q = isa_string;
  q += snprintf(isa_string, sizeof(isa_string), "rv%d", max_xlen);
  /* ISA extensions must be in canonical order: i, m, a, f, d, c, ... */
  if (misa & (1 << ('i' - 'a')))
    *q++ = 'i';
  if (misa & (1 << ('m' - 'a')))
    *q++ = 'm';
  if (misa & (1 << ('a' - 'a')))
    *q++ = 'a';
  if (misa & (1 << ('f' - 'a')))
    *q++ = 'f';
  if (misa & (1 << ('d' - 'a')))
    *q++ = 'd';
  if (misa & (1 << ('c' - 'a')))
    *q++ = 'c';
  *q = '\0';
  ydebug("riscv_build_fdt: misa=0x%x isa_string=%s", misa, isa_string);

  /* Create a cpu node for each CPU */
  for (j = 0; j < num_cpus; j++) {
    fdt_begin_node_num(s, "cpu", j);
    fdt_prop_str(s, "device_type", "cpu");
    fdt_prop_u32(s, "reg", j);
    fdt_prop_str(s, "status", "okay");
    fdt_prop_str(s, "compatible", "riscv");
    fdt_prop_str(s, "riscv,isa", isa_string);
    fdt_prop_str(s, "mmu-type", max_xlen <= 32 ? "riscv,sv32" : "riscv,sv48");
    fdt_prop_u32(s, "clock-frequency", 2000000000);

    fdt_begin_node(s, "interrupt-controller");
    fdt_prop_u32(s, "#interrupt-cells", 1);
    fdt_prop(s, "interrupt-controller", NULL, 0);
    fdt_prop_str(s, "compatible", "riscv,cpu-intc");
    intc_phandle[j] = cur_phandle++;
    fdt_prop_u32(s, "phandle", intc_phandle[j]);
    fdt_end_node(s); /* interrupt-controller */

    fdt_end_node(s); /* cpu */
  }

  fdt_end_node(s); /* cpus */

  fdt_begin_node_num(s, "memory", RAM_BASE_ADDR);
  fdt_prop_str(s, "device_type", "memory");
  tab[0] = (uint64_t)RAM_BASE_ADDR >> 32;
  tab[1] = RAM_BASE_ADDR;
  tab[2] = m->ram_size >> 32;
  tab[3] = m->ram_size;
  fdt_prop_tab_u32(s, "reg", tab, 4);

  fdt_end_node(s); /* memory */

  fdt_begin_node(s, "htif");
  fdt_prop_str(s, "compatible", "ucb,htif0");
  fdt_end_node(s); /* htif */

  fdt_begin_node(s, "soc");
  fdt_prop_u32(s, "#address-cells", 2);
  fdt_prop_u32(s, "#size-cells", 2);
  fdt_prop_tab_str(s, "compatible", "ucbbar,riscvemu-bar-soc", "simple-bus",
                   NULL);
  fdt_prop(s, "ranges", NULL, 0);

  fdt_begin_node_num(s, "clint", CLINT_BASE_ADDR);
  fdt_prop_str(s, "compatible", "riscv,clint0");

  /* CLINT interrupts-extended: for each CPU, M IPI (3) and M timer (7) */
  for (j = 0; j < num_cpus; j++) {
    tab[j * 4 + 0] = intc_phandle[j];
    tab[j * 4 + 1] = 3; /* M IPI irq */
    tab[j * 4 + 2] = intc_phandle[j];
    tab[j * 4 + 3] = 7; /* M timer irq */
  }
  fdt_prop_tab_u32(s, "interrupts-extended", tab, num_cpus * 4);

  fdt_prop_tab_u64_2(s, "reg", CLINT_BASE_ADDR, CLINT_SIZE);

  fdt_end_node(s); /* clint */

  fdt_begin_node_num(s, "plic", PLIC_BASE_ADDR);
  fdt_prop_u32(s, "#interrupt-cells", 1);
  fdt_prop(s, "interrupt-controller", NULL, 0);
  fdt_prop_str(s, "compatible", "riscv,plic0");
  fdt_prop_u32(s, "riscv,ndev", 31);
  fdt_prop_tab_u64_2(s, "reg", PLIC_BASE_ADDR, PLIC_SIZE);

  /* PLIC interrupts-extended: for each CPU, S ext (9) and M ext (11) */
  for (j = 0; j < num_cpus; j++) {
    tab[j * 4 + 0] = intc_phandle[j];
    tab[j * 4 + 1] = 9; /* S ext irq */
    tab[j * 4 + 2] = intc_phandle[j];
    tab[j * 4 + 3] = 11; /* M ext irq */
  }
  fdt_prop_tab_u32(s, "interrupts-extended", tab, num_cpus * 4);

  plic_phandle = cur_phandle++;
  fdt_prop_u32(s, "phandle", plic_phandle);

  fdt_end_node(s); /* plic */

  for (i = 0; i < m->virtio_count; i++) {
    fdt_begin_node_num(s, "virtio", VIRTIO_BASE_ADDR + i * VIRTIO_SIZE);
    fdt_prop_str(s, "compatible", "virtio,mmio");
    fdt_prop_tab_u64_2(s, "reg", VIRTIO_BASE_ADDR + i * VIRTIO_SIZE,
                       VIRTIO_SIZE);
    tab[0] = plic_phandle;
    tab[1] = VIRTIO_IRQ + i;
    fdt_prop_tab_u32(s, "interrupts-extended", tab, 2);
    fdt_end_node(s); /* virtio */
  }

  fb_dev = m->common.fb_dev;
  if (fb_dev) {
    fdt_begin_node_num(s, "framebuffer", FRAMEBUFFER_BASE_ADDR);
    fdt_prop_str(s, "compatible", "simple-framebuffer");
    fdt_prop_tab_u64_2(s, "reg", FRAMEBUFFER_BASE_ADDR, fb_dev->fb_size);
    fdt_prop_u32(s, "width", fb_dev->width);
    fdt_prop_u32(s, "height", fb_dev->height);
    fdt_prop_u32(s, "stride", fb_dev->stride);
    fdt_prop_str(s, "format", "a8r8g8b8");
    fdt_end_node(s); /* framebuffer */
  }

  fdt_end_node(s); /* soc */

  fdt_begin_node(s, "chosen");
  fdt_prop_str(s, "bootargs", cmd_line ? cmd_line : "");
  fdt_prop_str(s, "stdout-path", "/htif");
  if (kernel_size > 0) {
    fdt_prop_tab_u64(s, "riscv,kernel-start", kernel_start);
    fdt_prop_tab_u64(s, "riscv,kernel-end", kernel_start + kernel_size);
  }
  if (initrd_size > 0) {
    fdt_prop_tab_u64(s, "linux,initrd-start", initrd_start);
    fdt_prop_tab_u64(s, "linux,initrd-end", initrd_start + initrd_size);
  }

  fdt_end_node(s); /* chosen */

  fdt_end_node(s); /* / */

  /* Reserve the entire pre-kernel region [bios_addr, kernel_start) via
   * /memreserve/. Without this, Linux can hand a low physical address
   * (e.g. 0x80000000) to a virtio-blk DMA target and clobber OpenSBI
   * runtime code or scratch mid-flight — the next SBI ECALL (timer,
   * IPI, console) then hangs the guest. Reproduced with the extended
   * Alpine rootfs, whose larger binaries trigger 1.4 MB virtio-blk
   * reads into contiguous bounce buffers. Reserving only the static
   * bios image is not enough: OpenSBI's runtime scratch + stacks live
   * just above the static image, so cover the full 2 MiB up to the
   * kernel base. */
  uint64_t reserve_size = (kernel_start > bios_addr) ?
      (kernel_start - bios_addr) : bios_size;
  uint64_t reserve_tab[1][2] = {{bios_addr, reserve_size}};
  size = fdt_output(s, dst, bios_size > 0 ? reserve_tab : NULL,
                    bios_size > 0 ? 1 : 0);
#if 0
    {
        FILE *f;
        f = fopen("/tmp/riscvemu.dtb", "wb");
        fwrite(dst, 1, size, f);
        fclose(f);
    }
#endif
  fdt_end(s);
  return size;
}

static void copy_bios(RISCVMachine *s, const uint8_t *buf, int buf_len,
                      const uint8_t *kernel_buf, int kernel_buf_len,
                      const uint8_t *initrd_buf, int initrd_buf_len,
                      const char *cmd_line) {
  uint64_t fdt_addr, kernel_base, kernel_size;
  uint64_t bios_base, bios_size, initrd_base, initrd_size;
  uint64_t image_start, image_len;
  uint8_t *ram_ptr;
  uint32_t *q;

  ydebug("copy_bios: bios_len=%d kernel_len=%d initrd_len=%d", buf_len,
         kernel_buf_len, initrd_buf_len);

  ram_ptr = get_ram_ptr(s, RAM_BASE_ADDR, TRUE);

  /* copy the bios - use ELF loader if it's an ELF file */
  if (elf_detect_magic(buf, buf_len)) {
    if (elf_load(buf, buf_len, ram_ptr, s->ram_size, &image_start,
                 &image_len) == -1) {
      vm_error("Failed to load ELF BIOS\n");
      exit(1);
    }
    bios_base = image_start - RAM_BASE_ADDR;
    bios_size = image_len;
    ydebug("copy_bios: ELF bios loaded, start=0x%lx size=0x%lx",
           (unsigned long)image_start, (unsigned long)image_len);
  } else {
    if ((uint64_t)buf_len > s->ram_size) {
      vm_error("BIOS too big\n");
      exit(1);
    }
    memcpy(ram_ptr, buf, buf_len);
    bios_base = 0;
    bios_size = buf_len;
    ydebug("copy_bios: raw bios copied to 0x%x", RAM_BASE_ADDR);
  }

  /* copy the kernel if present */
  kernel_base = 0;
  kernel_size = 0;
  if (kernel_buf_len > 0) {
    /* Kernel must be at 0x80200000 to match OpenSBI FW_JUMP_ADDR */
    kernel_base = 0x200000;

    if (elf_detect_magic(kernel_buf, kernel_buf_len)) {
      if (elf_load(kernel_buf, kernel_buf_len, ram_ptr + kernel_base,
                   s->ram_size - kernel_base, &image_start, &image_len) == -1) {
        vm_error("Failed to load ELF kernel\n");
        exit(1);
      }
      kernel_base += image_start;
      kernel_size = image_len;
      ydebug("copy_bios: ELF kernel loaded at offset 0x%lx size=0x%lx",
             (unsigned long)kernel_base, (unsigned long)kernel_size);
    } else {
      memcpy(ram_ptr + kernel_base, kernel_buf, kernel_buf_len);
      kernel_size = kernel_buf_len;
      ydebug("copy_bios: raw kernel copied to 0x%lx",
             (unsigned long)(RAM_BASE_ADDR + kernel_base));
    }
  }

  /* copy the initrd if present */
  initrd_base = 0;
  initrd_size = 0;
  if (initrd_buf_len > 0) {
    if (kernel_size > 0) {
      initrd_base = kernel_base + kernel_size;
    } else {
      initrd_base = bios_base + bios_size;
    }
    initrd_base = (initrd_base + 0xFFF) & ~0xFFF; /* page align */
    if (initrd_base + initrd_buf_len > s->ram_size) {
      vm_error("initrd too big\n");
      exit(1);
    }
    memcpy(ram_ptr + initrd_base, initrd_buf, initrd_buf_len);
    initrd_size = initrd_buf_len;
  }

  ram_ptr = get_ram_ptr(s, 0, TRUE);

  fdt_addr = 0x1000 + 8 * 8;

  ydebug("copy_bios: building FDT at 0x%lx", (unsigned long)fdt_addr);
  riscv_build_fdt(s, ram_ptr + fdt_addr, RAM_BASE_ADDR + kernel_base,
                  kernel_size, RAM_BASE_ADDR + initrd_base, initrd_size,
                  cmd_line, RAM_BASE_ADDR + bios_base, bios_size);

  /* jump_addr = 0x80000000 */
  q = (uint32_t *)(ram_ptr + 0x1000);
  q[0] = 0x297 + 0x80000000 - 0x1000;      /* auipc t0, jump_addr */
  q[1] = 0x597;                            /* auipc a1, dtb */
  q[2] = 0x58593 + ((fdt_addr - 4) << 20); /* addi a1, a1, dtb */
  q[3] = 0xf1402573;                       /* csrr a0, mhartid */
  q[4] = 0x00028067;                       /* jalr zero, t0, jump_addr */
  ydebug("copy_bios: trampoline at 0x1000, jump to 0x80000000");
}

static void riscv_flush_tlb_write_range(void *opaque, uint8_t *ram_addr,
                                        size_t ram_size) {
  RISCVMachine *s = opaque;
  int i;
  /* Flush TLB on all CPUs */
  for (i = 0; i < s->num_cpus; i++) {
    riscv_cpu_flush_tlb_write_range_ram(s->cpu_state[i], ram_addr, ram_size);
  }
}

/* VirtMachineClass callback - allows machine to set default params.
 * RISC-V machine has no defaults to set, but callback must exist. */
static void riscv_machine_set_defaults(VirtMachineParams *p) { (void)p; }

static VirtMachine *riscv_machine_init(const VirtMachineParams *p) {
  RISCVMachine *s;
  VIRTIODevice *blk_dev;
  int irq_num, i, max_xlen, ram_flags;
  VIRTIOBusDef vbus_s, *vbus = &vbus_s;
  int num_cpus;

  if (!strcmp(p->machine_name, "riscv32")) {
    max_xlen = 32;
  } else if (!strcmp(p->machine_name, "riscv64")) {
    max_xlen = 64;
  } else if (!strcmp(p->machine_name, "riscv128")) {
    max_xlen = 128;
  } else {
    vm_error("unsupported machine: %s\n", p->machine_name);
    return NULL;
  }

  num_cpus = p->ncpus > 0 ? p->ncpus : 1;
  if (num_cpus > SMP_MAX_CPUS) {
    vm_error("ncpus=%d exceeds SMP_MAX_CPUS=%d\n", num_cpus, SMP_MAX_CPUS);
    return NULL;
  }
  printf("SMP: Using %d CPUs\n", num_cpus);

  s = mallocz(sizeof(*s));
  s->common.vmc = p->vmc;
  s->ram_size = p->ram_size;
  s->max_xlen = max_xlen;
  s->num_cpus = num_cpus;

  /* Initialize timecmp to max value so timer interrupts don't fire
   * until the guest explicitly sets them. Without this, timecmp=0
   * causes MTIP to fire immediately since rtc > 0. */
  for (i = 0; i < MAX_CPUS; i++) {
    atomic_store(&s->timecmp[i], UINT64_MAX);
  }

  s->mem_map = phys_mem_map_init();
  /* needed to handle the RAM dirty bits */
  s->mem_map->opaque = s;
  s->mem_map->flush_tlb_write_range = riscv_flush_tlb_write_range;

  /* Create all CPUs */
  for (i = 0; i < num_cpus; i++) {
    s->cpu_state[i] = riscv_cpu_init(s->mem_map, max_xlen);
    if (!s->cpu_state[i]) {
      vm_error("unsupported max_xlen=%d\n", max_xlen);
      /* XXX: should free resources */
      return NULL;
    }
    /* Set hart ID for each CPU */
    riscv_cpu_set_mhartid(s->cpu_state[i], i);
    /* All HARTs start running - OpenSBI will put secondary HARTs in WFI */
  }

  s->smp = smp_init(num_cpus);
  for (i = 0; i < num_cpus; i++) {
    s->smp->threads[i].cpu = s->cpu_state[i];
    riscv_cpu_set_smp(s->cpu_state[i], s->smp);
  }

  /* HTIF - detect address from .htif section in ELF BIOS FIRST
   * Must register before RAM so HTIF takes priority over RAM at same address
   * .htif section contains: tohost (8 bytes) then fromhost (8 bytes) */
  s->htif_addr = DEFAULT_HTIF_BASE_ADDR;
  if (p->files[VM_FILE_BIOS].buf) {
    const uint8_t *bios_buf = p->files[VM_FILE_BIOS].buf;
    int bios_len = p->files[VM_FILE_BIOS].len;
    if (elf_detect_magic(bios_buf, bios_len)) {
      uint64_t addr;
      if (elf_find_section(bios_buf, ".htif", &addr, NULL)) {
        s->htif_addr = addr;
        ydebug("HTIF: .htif section at 0x%lx, device registered there",
               (unsigned long)addr);
      }
    }
  }
  cpu_register_device(s->mem_map, s->htif_addr, 16, s, htif_read, htif_write,
                      DEVIO_SIZE32);
  s->common.console = p->console;

  /* RAM - registered AFTER HTIF so HTIF device takes priority */
  ram_flags = 0;
  cpu_register_ram(s->mem_map, RAM_BASE_ADDR, p->ram_size, ram_flags);
  cpu_register_ram(s->mem_map, 0x00000000, LOW_RAM_SIZE, 0);
  s->rtc_real_time = p->rtc_real_time;
  if (p->rtc_real_time) {
    atomic_store(&s->rtc_start_time, rtc_get_real_time());
  }

  cpu_register_device(s->mem_map, CLINT_BASE_ADDR, CLINT_SIZE, s, clint_read,
                      clint_write, DEVIO_SIZE32);
  cpu_register_device(s->mem_map, PLIC_BASE_ADDR, PLIC_SIZE, s, plic_read,
                      plic_write, DEVIO_SIZE32);
  for (i = 1; i < 32; i++) {
    irq_init(&s->plic_irq[i], plic_set_irq, s, i);
  }

  memset(vbus, 0, sizeof(*vbus));
  vbus->mem_map = s->mem_map;
  vbus->addr = VIRTIO_BASE_ADDR;
  irq_num = VIRTIO_IRQ;

  /* virtio console */
  if (p->console) {
    vbus->irq = &s->plic_irq[irq_num];
    s->common.console_dev = virtio_console_init(vbus, p->console);
    vbus->addr += VIRTIO_SIZE;
    irq_num++;
    s->virtio_count++;
  }

  /* virtio net device */
  for (i = 0; i < p->eth_count; i++) {
    vbus->irq = &s->plic_irq[irq_num];
    virtio_net_init(vbus, p->tab_eth[i].net);
    s->common.net = p->tab_eth[i].net;
    vbus->addr += VIRTIO_SIZE;
    irq_num++;
    s->virtio_count++;
  }

  /* virtio block device */
  for (i = 0; i < p->drive_count; i++) {
    vbus->irq = &s->plic_irq[irq_num];
    blk_dev = virtio_block_init(vbus, p->tab_drive[i].block_dev);
    (void)blk_dev;
    vbus->addr += VIRTIO_SIZE;
    irq_num++;
    s->virtio_count++;
  }

  /* virtio filesystem */
  for (i = 0; i < p->fs_count; i++) {
    VIRTIODevice *fs_dev;
    vbus->irq = &s->plic_irq[irq_num];
    fs_dev = virtio_9p_init(vbus, p->tab_fs[i].fs_dev, p->tab_fs[i].tag);
    (void)fs_dev;
    //        virtio_set_debug(fs_dev, VIRTIO_DEBUG_9P);
    vbus->addr += VIRTIO_SIZE;
    irq_num++;
    s->virtio_count++;
  }

  if (p->display_device) {
    FBDevice *fb_dev;
    fb_dev = mallocz(sizeof(*fb_dev));
    s->common.fb_dev = fb_dev;
    if (!strcmp(p->display_device, "simplefb")) {
      simplefb_init(s->mem_map, FRAMEBUFFER_BASE_ADDR, fb_dev, p->width,
                    p->height);

    } else {
      vm_error("unsupported display device: %s\n", p->display_device);
      exit(1);
    }
  }

  if (p->input_device) {
    if (!strcmp(p->input_device, "virtio")) {
      vbus->irq = &s->plic_irq[irq_num];
      s->keyboard_dev = virtio_input_init(vbus, VIRTIO_INPUT_TYPE_KEYBOARD);
      vbus->addr += VIRTIO_SIZE;
      irq_num++;
      s->virtio_count++;

      vbus->irq = &s->plic_irq[irq_num];
      s->mouse_dev = virtio_input_init(vbus, VIRTIO_INPUT_TYPE_TABLET);
      vbus->addr += VIRTIO_SIZE;
      irq_num++;
      s->virtio_count++;
    } else {
      vm_error("unsupported input device: %s\n", p->input_device);
      exit(1);
    }
  }

  if (!p->files[VM_FILE_BIOS].buf) {
    vm_error("No bios found");
  }

  ydebug("riscv_machine_init: calling copy_bios");
  copy_bios(s, p->files[VM_FILE_BIOS].buf, p->files[VM_FILE_BIOS].len,
            p->files[VM_FILE_KERNEL].buf, p->files[VM_FILE_KERNEL].len,
            p->files[VM_FILE_INITRD].buf, p->files[VM_FILE_INITRD].len,
            p->cmdline);
  ydebug("riscv_machine_init: copy_bios done");

  /* Record boot start time */
  {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s->boot_start_time = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  }
  /* Memory barrier to ensure all initialization is visible to CPU threads */
  atomic_thread_fence(memory_order_seq_cst);
  /* Start CPU threads */
  ydebug("riscv_machine_init: starting %d CPU threads", s->num_cpus);
  smp_start_cpus(s->smp, (VirtMachine *)s);
  ydebug("riscv_machine_init: CPU threads started");

  return (VirtMachine *)s;
}

static void riscv_machine_end(VirtMachine *s1) {
  RISCVMachine *s = (RISCVMachine *)s1;
  int i;

  if (s->smp) {
    smp_stop_cpus(s->smp);
    smp_free(s->smp);
  }

  for (i = 0; i < s->num_cpus; i++) {
    if (s->cpu_state[i])
      riscv_cpu_end(s->cpu_state[i]);
  }
  phys_mem_map_end(s->mem_map);
  free(s);
}

/* in ms */
static int riscv_machine_get_sleep_duration(VirtMachine *s1, int delay) {
  RISCVMachine *m = (RISCVMachine *)s1;
  int64_t delay1;
  int i;
  static uint64_t call_count = 0;
  static uint64_t timer_set_count = 0;

  call_count++;

  /* SMP mode: First check if there's work (any CPU not in WFI).
   * This must be checked BEFORE firing interrupts, because firing
   * an interrupt clears power_down. */
  BOOL all_idle = TRUE;
  for (i = 0; i < m->num_cpus; i++) {
    if (!riscv_cpu_get_power_down(m->cpu_state[i])) {
      all_idle = FALSE;
      break;
    }
  }

  /* Now handle timers */
  for (i = 0; i < m->num_cpus; i++) {
    RISCVCPUState *cpu = m->cpu_state[i];
    if (!(riscv_cpu_get_mip(cpu) & MIP_MTIP)) {
      uint64_t tc = atomic_load(&m->timecmp[i]);
      uint64_t rtc = rtc_get_time(m);
      if (rtc >= tc) {
        ydebug("SYNC: TIMER cpu%d FIRE rtc=0x%llx tc=0x%llx mip=0x%x",
               i, (unsigned long long)rtc, (unsigned long long)tc,
               riscv_cpu_get_mip(cpu));
        riscv_cpu_set_mip(cpu, MIP_MTIP);
        smp_wakeup_cpu(m->smp, i);  /* Wake CPU thread from condvar sleep */
        timer_set_count++;
      } else {
        /* Only update delay if the difference fits in int64_t and is reasonable */
        uint64_t diff = tc - rtc;
        if (diff < (uint64_t)INT64_MAX) {
          delay1 = (int64_t)diff / (RTC_FREQ / 1000);
          if (delay1 >= 0 && delay1 < delay)
            delay = delay1;
        }
      }
    }
  }

#ifdef EMSCRIPTEN
  /* Single-threaded wasm: jsemu's mainloop only executes CPU cycles when
   * sleep_duration returns 0 (delay==0). With pthread CPU threads stubbed
   * out under emscripten without -pthread, returning a positive delay
   * here means the CPU never runs. Force delay=0 whenever any CPU is
   * runnable so virt_machine_interp drives it from the JS tick. */
  if (!all_idle) {
    return 0;
  }
#endif

  /* Cap delay to ensure timers fire promptly:
   * - During boot (first 10s): 1ms for CPU bringup
   * - After boot with active CPUs: 1ms for responsiveness
   * - After boot all idle: 10ms max to balance power vs latency */
  {
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    uint64_t _now = (uint64_t)_ts.tv_sec * 1000000000ULL + _ts.tv_nsec;
    BOOL booting = (_now - m->boot_start_time < 10000000000ULL);
    if (booting || !all_idle) {
      if (delay > 1)
        delay = 1;
    } else {
      /* All idle after boot - still cap delay so timers fire */
      if (delay > 10)
        delay = 10;
    }
    /* Debug: log main loop state */
    if ((call_count % 1000) == 0) {
      uint64_t rtc = rtc_get_time(m);
      uint64_t tc0 = atomic_load(&m->timecmp[0]);
      ydebug("SYNC: MAINLOOP idle=%d rtc=0x%llx delay=%d boot=%d tc0=0x%llx gap=%lld",
             all_idle, (unsigned long long)rtc, delay, booting,
             (unsigned long long)tc0,
             (long long)(tc0 - rtc));
    }
  }

  if ((call_count % 100000) == 0) {
    ydebug("main_loop: calls=%lu timer_sets=%lu delay=%d",
           (unsigned long)call_count, (unsigned long)timer_set_count, delay);
  }
  return delay;
}

static void riscv_machine_interp(VirtMachine *s1, int max_exec_cycle) {
#ifdef EMSCRIPTEN
  /* Single-threaded wasm: pthread_create on emscripten without -pthread
   * does not actually run the thread, so the SMP thread loop never
   * executes any CPU instructions. Drive the CPU directly from the
   * mainloop instead. Mirrors the inner core of riscv_cpu_thread_func
   * minus the pthread sleep/wakeup machinery (single CPU, no idle race
   * to coordinate). */
  RISCVMachine *s = (RISCVMachine *)s1;
  int i;
  for (i = 0; i < s->num_cpus; i++) {
    RISCVCPUState *cpu = s->cpu_state[i];
    if (riscv_cpu_get_power_down(cpu)) {
      if (riscv_cpu_has_pending_irq(cpu)) {
        riscv_cpu_set_power_down(cpu, FALSE);
      } else {
        continue; /* still halted, skip */
      }
    }
    riscv_cpu_interp(cpu, max_exec_cycle);
  }
#else
  /* Native build: CPUs run in their own pthreads via riscv_cpu_thread_func. */
  (void)s1;
  (void)max_exec_cycle;
#endif
}

static void riscv_vm_send_key_event(VirtMachine *s1, BOOL is_down,
                                    uint16_t key_code) {
  RISCVMachine *s = (RISCVMachine *)s1;
  if (s->keyboard_dev) {
    virtio_input_send_key_event(s->keyboard_dev, is_down, key_code);
  }
}

/* VirtMachineClass callback - RISC-V always uses absolute mouse coords */
static BOOL riscv_vm_mouse_is_absolute(VirtMachine *s) {
  (void)s;
  return TRUE;
}

static void riscv_vm_send_mouse_event(VirtMachine *s1, int dx, int dy, int dz,
                                      unsigned int buttons) {
  RISCVMachine *s = (RISCVMachine *)s1;
  if (s->mouse_dev) {
    virtio_input_send_mouse_event(s->mouse_dev, dx, dy, dz, buttons);
  }
}

/*
 * CPU thread function - runs interpreter loop for each CPU
 *
 * Based on QEMU's MTTCG model:
 * 1. Handle halt/wait at START of loop (like qemu_process_cpu_events)
 * 2. Only execute if not in power_down
 * 3. Clear power_down only when has_pending_irq returns TRUE
 */
void riscv_cpu_thread_func(SMPCPUThread *t) {
  RISCVCPUState *cpu = t->cpu;
  const int cycles_per_iteration = 100000;

  ydebug("CPU %d: thread loop starting, cpu=%p", t->cpu_id, (void *)cpu);

  while (atomic_load(&t->running)) {
    pthread_mutex_lock(&t->wakeup_mutex);
    BOOL pd = riscv_cpu_get_power_down(cpu);
    BOOL has_irq = riscv_cpu_has_pending_irq(cpu);
    int wp = t->wakeup_pending;
    ydebug("SYNC: cpu%d CHECK pd=%d irq=%d wp=%d mip=0x%x",
           t->cpu_id, pd, has_irq, wp, riscv_cpu_get_mip(cpu));
    while (riscv_cpu_get_power_down(cpu) &&
           !riscv_cpu_has_pending_irq(cpu) &&
           !t->wakeup_pending &&
           atomic_load(&t->running)) {
      ydebug("SYNC: cpu%d SLEEP mip=0x%x", t->cpu_id, riscv_cpu_get_mip(cpu));
      pthread_cond_wait(&t->wakeup_cond, &t->wakeup_mutex);
      ydebug("SYNC: cpu%d WOKE wp=%d mip=0x%x",
             t->cpu_id, t->wakeup_pending, riscv_cpu_get_mip(cpu));
    }
    BOOL was_signaled = t->wakeup_pending;
    t->wakeup_pending = 0;

    if (riscv_cpu_has_pending_irq(cpu) || was_signaled) {
      ydebug("SYNC: cpu%d RESUME irq=%d sig=%d mip=0x%x",
             t->cpu_id, riscv_cpu_has_pending_irq(cpu), was_signaled,
             riscv_cpu_get_mip(cpu));
      riscv_cpu_set_power_down(cpu, FALSE);
    } else if (pd) {
      ydebug("SYNC: cpu%d STAY_HALTED mip=0x%x", t->cpu_id, riscv_cpu_get_mip(cpu));
    }
    pthread_mutex_unlock(&t->wakeup_mutex);

    if (!riscv_cpu_get_power_down(cpu)) {
      riscv_cpu_interp(cpu, cycles_per_iteration);
    }
  }
  ydebug("CPU %d: thread loop exiting", t->cpu_id);
}

const VirtMachineClass riscv_machine_class = {
    "riscv32,riscv64,riscv128",
    riscv_machine_set_defaults,
    riscv_machine_init,
    riscv_machine_end,
    riscv_machine_get_sleep_duration,
    riscv_machine_interp,
    riscv_vm_mouse_is_absolute,
    riscv_vm_send_mouse_event,
    riscv_vm_send_key_event,
};
