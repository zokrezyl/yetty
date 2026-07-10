//
//  m3_core.c
//
//  Created by Steven Massey on 4/15/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#define M3_IMPLEMENT_ERROR_STRINGS
#include "wasm3.h"

#include "m3_core.h"
#include "m3_env.h"

#include <sys/mman.h>   /* mmap/munmap for the linear-memory fast path
                         * — keeps pages lazy so fork()'s mincore
                         * snapshot only sees what the guest touched. */
#include <pthread.h>    /* registry mutex */

void m3_Abort(const char* message) {
#ifdef DEBUG
    fprintf(stderr, "Error: %s\n", message);
#endif
    abort();
}

M3_WEAK
M3Result m3_Yield ()
{
    return m3Err_none;
}

#if d_m3FixedHeap

static u8 fixedHeap[d_m3FixedHeap];
static u8* fixedHeapPtr = fixedHeap;
static u8* const fixedHeapEnd = fixedHeap + d_m3FixedHeap;
static u8* fixedHeapLast = NULL;

#if d_m3FixedHeapAlign > 1
#   define HEAP_ALIGN_PTR(P) P = (u8*)(((size_t)(P)+(d_m3FixedHeapAlign-1)) & ~ (d_m3FixedHeapAlign-1));
#else
#   define HEAP_ALIGN_PTR(P)
#endif

void *  m3_Malloc  (size_t i_size)
{
    u8 * ptr = fixedHeapPtr;

    fixedHeapPtr += i_size;
    HEAP_ALIGN_PTR(fixedHeapPtr);

    if (fixedHeapPtr >= fixedHeapEnd)
    {
        return NULL;
    }

    memset (ptr, 0x0, i_size);
    fixedHeapLast = ptr;

    //printf("== alloc %d => %p\n", i_size, ptr);

    return ptr;
}

void  m3_FreeImpl  (void * i_ptr)
{
    // Handle the last chunk
    if (i_ptr && i_ptr == fixedHeapLast) {
        fixedHeapPtr = fixedHeapLast;
        fixedHeapLast = NULL;
        //printf("== free %p\n", io_ptr);
    } else {
        //printf("== free %p [failed]\n", io_ptr);
    }
}

void *  m3_Realloc  (void * i_ptr, size_t i_newSize, size_t i_oldSize)
{
    //printf("== realloc %p => %d\n", io_ptr, i_newSize);

    if (UNLIKELY(i_newSize == i_oldSize)) return i_ptr;

    void * newPtr;

    // Handle the last chunk
    if (i_ptr && i_ptr == fixedHeapLast) {
        fixedHeapPtr = fixedHeapLast + i_newSize;
        HEAP_ALIGN_PTR(fixedHeapPtr);
        if (fixedHeapPtr >= fixedHeapEnd)
        {
            return NULL;
        }
        newPtr = i_ptr;
    } else {
        newPtr = m3_Malloc(i_newSize);
        if (!newPtr) {
            return NULL;
        }
        if (i_ptr) {
            memcpy(newPtr, i_ptr, i_oldSize);
        }
    }

    if (i_newSize > i_oldSize) {
        memset ((u8 *) newPtr + i_oldSize, 0x0, i_newSize - i_oldSize);
    }

    return newPtr;
}

#else

void *  m3_Malloc  (size_t i_size)
{
    void * ptr = calloc (i_size, 1);

//    printf("== alloc %d => %p\n", (u32) i_size, ptr);

    return ptr;
}

/* Tiny registry of mmap-backed wasm3 allocations. m3_FreeImpl can
 * receive pointers from BOTH calloc() (small allocs in m3_Malloc)
 * and mmap() (the linear-memory buffer). It must call the matching
 * deallocator. A sentinel-header-before-user-pointer would be
 * unsafe to dereference for the calloc'd allocations (UB on pointer
 * arithmetic into another mapping). A small registry keeps the
 * test page-safe: lookup is O(N) where N is "number of live linear-
 * memory buffers", which is 1 for a single guest and bounded by
 * concurrent sibling runtimes.
 *
 * Protected by a pthread spinlock-equivalent mutex — m3_Realloc and
 * m3_FreeImpl can both run from different host pthreads
 * (forked-child fork_thread_func runs on its own thread). */
/* Dynamic mmap-pointer registry.
 *
 * History: this was a fixed 256-slot array. Under workloads that
 * spawn many short-lived runtimes (every fork() under yos creates
 * a fresh wasm3 runtime + linear-memory mmap), the array filled
 * up and `m3_mmap_reg_add` silently dropped subsequent entries.
 * The next call to free that pointer then fell through to glibc
 * free()/realloc(), which detects the non-malloc'd pointer and
 * aborts with "realloc(): invalid pointer" or "free(): invalid
 * pointer". The fork tests in perf-stress (1 + 10 + 100 procs)
 * tripped this around fork #65.
 *
 * The registry now grows on demand. Slot reuse is still O(N)
 * linear scan, which is fine — N is bounded by the number of
 * live wasm3 runtimes in the parent process, not by total fork
 * count, so the working set stays small in practice. The growth
 * is uncontended-amortized — we double the array each time it
 * fills, so even pathological N gets log-N reallocs. */
static struct m3_mmap_reg_entry {
    void *ptr;     /* user pointer (NULL = slot free) */
    size_t size;   /* mmap'd size */
} *g_m3_mmap_reg = NULL;
static size_t g_m3_mmap_reg_cap = 0;
static pthread_mutex_t g_m3_mmap_reg_lock = PTHREAD_MUTEX_INITIALIZER;

/* Caller MUST hold g_m3_mmap_reg_lock. Returns 0 on success, -1 on
 * malloc failure. */
static int m3_mmap_reg_grow_locked (void)
{
    size_t old_cap = g_m3_mmap_reg_cap;
    size_t new_cap = old_cap ? old_cap * 2 : 256;
    struct m3_mmap_reg_entry *np = (struct m3_mmap_reg_entry *)
        realloc (g_m3_mmap_reg, new_cap * sizeof(*np));
    if (!np) return -1;
    memset (np + old_cap, 0,
            (new_cap - old_cap) * sizeof(*np));
    g_m3_mmap_reg = np;
    g_m3_mmap_reg_cap = new_cap;
    return 0;
}

static void m3_mmap_reg_add (void *p, size_t sz)
{
    pthread_mutex_lock (&g_m3_mmap_reg_lock);
    for (;;) {
        for (size_t i = 0; i < g_m3_mmap_reg_cap; i++) {
            if (g_m3_mmap_reg[i].ptr == NULL) {
                g_m3_mmap_reg[i].ptr  = p;
                g_m3_mmap_reg[i].size = sz;
                pthread_mutex_unlock (&g_m3_mmap_reg_lock);
                return;
            }
        }
        /* No free slot — grow and retry. */
        if (m3_mmap_reg_grow_locked () != 0) break;
    }
    pthread_mutex_unlock (&g_m3_mmap_reg_lock);
    /* Registry grow failed (out of host memory). The mmap leaks; the
     * caller will later try to free this pointer through glibc free()
     * and abort with "free(): invalid pointer". Log loudly so the
     * abort isn't a mystery.
     *
     * Old code path: array was fixed-size at 256 — caller's mmap leaks if it ever frees this
     * pointer (which they will). Increase M3_MMAP_REG_MAX. */
}

static size_t m3_mmap_reg_take (void *p)
{
    pthread_mutex_lock (&g_m3_mmap_reg_lock);
    for (size_t i = 0; i < g_m3_mmap_reg_cap; i++) {
        if (g_m3_mmap_reg[i].ptr == p) {
            size_t sz = g_m3_mmap_reg[i].size;
            g_m3_mmap_reg[i].ptr  = NULL;
            g_m3_mmap_reg[i].size = 0;
            pthread_mutex_unlock (&g_m3_mmap_reg_lock);
            return sz;
        }
    }
    pthread_mutex_unlock (&g_m3_mmap_reg_lock);
    return 0;
}

void  m3_FreeImpl  (void * io_ptr)
{
    if (!io_ptr) return;
    /* If the registry knows this pointer, it's one of our mmap'd
     * linear-memory buffers — release with munmap, not free. */
    size_t mmap_size = m3_mmap_reg_take (io_ptr);
    if (mmap_size > 0) {
        munmap (io_ptr, mmap_size);
        return;
    }
    free (io_ptr);
}

void *  m3_Realloc  (void * i_ptr, size_t i_newSize, size_t i_oldSize)
{
    if (UNLIKELY(i_newSize == i_oldSize)) return i_ptr;

    /* Fresh allocation: mmap(MAP_ANONYMOUS). Pages stay LAZY on
     * every supported OS — no physical commit until first touch.
     * The original realloc()+memset() path committed all 64 K pages
     * of a 256 MiB linear-memory grow eagerly just to zero them
     * (~100 ms wasted per call) AND made fork()'s mincore snapshot
     * see them all as resident. mmap fixes both. */
    if (i_ptr == NULL) {
        void *p = mmap (NULL, i_newSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return NULL;
        m3_mmap_reg_add (p, i_newSize);
        return p;
    }

    /* Grow path. If the pointer is in our registry, allocate a new
     * mmap, copy live data, munmap old. Else fall back to realloc. */
    size_t old_mmap_size = m3_mmap_reg_take (i_ptr);
    if (old_mmap_size > 0) {
        void *np = mmap (NULL, i_newSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (np == MAP_FAILED) {
            /* Re-add to registry so future free still works. */
            m3_mmap_reg_add (i_ptr, old_mmap_size);
            return NULL;
        }
        size_t copy = (i_oldSize < i_newSize) ? i_oldSize : i_newSize;
        if (copy > 0) memcpy (np, i_ptr, copy);
        munmap (i_ptr, old_mmap_size);
        m3_mmap_reg_add (np, i_newSize);
        return np;
    }

    void * newPtr = realloc (i_ptr, i_newSize);
    if (LIKELY(newPtr))
    {
        if (i_newSize > i_oldSize) {
            memset ((u8 *) newPtr + i_oldSize, 0x0, i_newSize - i_oldSize);
        }
        return newPtr;
    }
    return NULL;
}

#endif

void *  m3_CopyMem  (const void * i_from, size_t i_size)
{
    void * ptr = m3_Malloc(i_size);
    if (ptr) {
        memcpy (ptr, i_from, i_size);
    }
    return ptr;
}

//--------------------------------------------------------------------------------------------

#if d_m3LogNativeStack

static size_t stack_start;
static size_t stack_end;

void        m3StackCheckInit ()
{
    char stack;
    stack_end = stack_start = (size_t)&stack;
}

void        m3StackCheck ()
{
    char stack;
    size_t addr = (size_t)&stack;

    size_t stackEnd = stack_end;
    stack_end = M3_MIN (stack_end, addr);

//    if (stackEnd != stack_end)
//        printf ("maxStack: %ld\n", m3StackGetMax ());
}

int      m3StackGetMax  ()
{
    return stack_start - stack_end;
}

#endif

//--------------------------------------------------------------------------------------------

M3Result NormalizeType (u8 * o_type, i8 i_convolutedWasmType)
{
    M3Result result = m3Err_none;

    u8 type = -i_convolutedWasmType;

    if (type == 0x40)
        type = c_m3Type_none;
    else if (type < c_m3Type_i32 or type > c_m3Type_f64)
        result = m3Err_invalidTypeId;

    * o_type = type;

    return result;
}


bool  IsFpType  (u8 i_m3Type)
{
    return (i_m3Type == c_m3Type_f32 or i_m3Type == c_m3Type_f64);
}


bool  IsIntType  (u8 i_m3Type)
{
    return (i_m3Type == c_m3Type_i32 or i_m3Type == c_m3Type_i64);
}


bool  Is64BitType  (u8 i_m3Type)
{
    if (i_m3Type == c_m3Type_i64 or i_m3Type == c_m3Type_f64)
        return true;
    else if (i_m3Type == c_m3Type_i32 or i_m3Type == c_m3Type_f32 or i_m3Type == c_m3Type_none)
        return false;
    else
        return (sizeof (voidptr_t) == 8); // all other cases are pointers
}

u32  SizeOfType  (u8 i_m3Type)
{
    if (i_m3Type == c_m3Type_i32 or i_m3Type == c_m3Type_f32)
        return sizeof (i32);

    return sizeof (i64);
}


//-- Binary Wasm parsing utils  ------------------------------------------------------------------------------------------


M3Result  Read_u64  (u64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (u64);

    if (ptr <= i_end)
    {
        memcpy(o_value, * io_bytes, sizeof(u64));
        M3_BSWAP_u64(*o_value);
        * io_bytes = ptr;
        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}


M3Result  Read_u32  (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (u32);

    if (ptr <= i_end)
    {
        memcpy(o_value, * io_bytes, sizeof(u32));
        M3_BSWAP_u32(*o_value);
        * io_bytes = ptr;
        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}

#if d_m3ImplementFloat

M3Result  Read_f64  (f64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (f64);

    if (ptr <= i_end)
    {
        memcpy(o_value, * io_bytes, sizeof(f64));
        M3_BSWAP_f64(*o_value);
        * io_bytes = ptr;
        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}


M3Result  Read_f32  (f32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (f32);

    if (ptr <= i_end)
    {
        memcpy(o_value, * io_bytes, sizeof(f32));
        M3_BSWAP_f32(*o_value);
        * io_bytes = ptr;
        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}

#endif

M3Result  Read_u8  (u8 * o_value, bytes_t  * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;

    if (ptr < i_end)
    {
        * o_value = * ptr;
        * io_bytes = ptr + 1;

        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}

M3Result  Read_opcode  (m3opcode_t * o_value, bytes_t  * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;

    if (ptr < i_end)
    {
        m3opcode_t opcode = * ptr++;

#ifndef d_m3EnableExtendedOpcodes
        if (UNLIKELY(opcode == 0xFC))
        {
            if (ptr < i_end)
            {
                opcode = (opcode << 8) | (* ptr++);
            }
            else return m3Err_wasmUnderrun;
        }
#endif
        // Threads proposal: 0xFE-prefixed atomic opcodes (always merged so dispatch
        // can route through GetOpInfo's `case 0xFE`).
        if (UNLIKELY(opcode == 0xFE))
        {
            if (ptr < i_end)
            {
                opcode = (opcode << 8) | (* ptr++);
            }
            else return m3Err_wasmUnderrun;
        }
        * o_value = opcode;
        * io_bytes = ptr;

        return m3Err_none;
    }
    else return m3Err_wasmUnderrun;
}


M3Result  ReadLebUnsigned  (u64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_wasmUnderrun;

    u64 value = 0;

    u32 shift = 0;
    const u8 * ptr = * io_bytes;

    while (ptr < i_end)
    {
        u64 byte = * (ptr++);

        value |= ((byte & 0x7f) << shift);
        shift += 7;

        if ((byte & 0x80) == 0)
        {
            result = m3Err_none;
            break;
        }

        if (shift >= i_maxNumBits)
        {
            result = m3Err_lebOverflow;
            break;
        }
    }

    * o_value = value;
    * io_bytes = ptr;

    return result;
}


M3Result  ReadLebSigned  (i64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = m3Err_wasmUnderrun;

    i64 value = 0;

    u32 shift = 0;
    const u8 * ptr = * io_bytes;

    while (ptr < i_end)
    {
        u64 byte = * (ptr++);

        value |= ((byte & 0x7f) << shift);
        shift += 7;

        if ((byte & 0x80) == 0)
        {
            result = m3Err_none;

            if ((byte & 0x40) and (shift < 64))    // do sign extension
            {
                u64 extend = 0;
                value |= (~extend << shift);
            }

            break;
        }

        if (shift >= i_maxNumBits)
        {
            result = m3Err_lebOverflow;
            break;
        }
    }

    * o_value = value;
    * io_bytes = ptr;

    return result;
}


M3Result  ReadLEB_u32  (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    u64 value;
    M3Result result = ReadLebUnsigned (& value, 32, io_bytes, i_end);
    * o_value = (u32) value;

    return result;
}


M3Result  ReadLEB_u7  (u8 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    u64 value;
    M3Result result = ReadLebUnsigned (& value, 7, io_bytes, i_end);
    * o_value = (u8) value;

    return result;
}


M3Result  ReadLEB_i7  (i8 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 7, io_bytes, i_end);
    * o_value = (i8) value;

    return result;
}


M3Result  ReadLEB_i32  (i32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 32, io_bytes, i_end);
    * o_value = (i32) value;

    return result;
}


M3Result  ReadLEB_i64  (i64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 64, io_bytes, i_end);
    * o_value = value;

    return result;
}


M3Result  Read_utf8  (cstr_t * o_utf8, bytes_t * io_bytes, cbytes_t i_end)
{
    *o_utf8 = NULL;

    u32 utf8Length;
    M3Result result = ReadLEB_u32 (& utf8Length, io_bytes, i_end);

    if (not result)
    {
        if (utf8Length <= d_m3MaxSaneUtf8Length)
        {
            const u8 * ptr = * io_bytes;
            const u8 * end = ptr + utf8Length;

            if (end <= i_end)
            {
                char * utf8 = (char *)m3_Malloc (utf8Length + 1);

                if (utf8)
                {
                    memcpy (utf8, ptr, utf8Length);
                    utf8 [utf8Length] = 0;
                    * o_utf8 = utf8;
                }

                * io_bytes = end;
            }
            else result = m3Err_wasmUnderrun;
        }
        else result = m3Err_missingUTF8;
    }

    return result;
}

#if d_m3RecordBacktraces
u32  FindModuleOffset  (IM3Runtime i_runtime, pc_t i_pc)
{
    // walk the code pages
    IM3CodePage curr = i_runtime->pagesOpen;
    bool pageFound = false;

    while (curr)
    {
        if (ContainsPC (curr, i_pc))
        {
            pageFound = true;
            break;
        }
        curr = curr->info.next;
    }

    if (!pageFound)
    {
        curr = i_runtime->pagesFull;
        while (curr)
        {
            if (ContainsPC (curr, i_pc))
            {
                pageFound = true;
                break;
            }
            curr = curr->info.next;
        }
    }

    if (pageFound)
    {
        u32 result = 0;

        bool pcFound = MapPCToOffset (curr, i_pc, & result);
                                                                                d_m3Assert (pcFound);

        return result;
    }
    else return 0;
}


void  PushBacktraceFrame  (IM3Runtime io_runtime, pc_t i_pc)
{
    // don't try to push any more frames if we've already had an alloc failure
    if (UNLIKELY (io_runtime->backtrace.lastFrame == M3_BACKTRACE_TRUNCATED))
        return;

    M3BacktraceFrame * newFrame = m3_AllocStruct(M3BacktraceFrame);

    if (!newFrame)
    {
        io_runtime->backtrace.lastFrame = M3_BACKTRACE_TRUNCATED;
        return;
    }

    newFrame->moduleOffset = FindModuleOffset (io_runtime, i_pc);

    if (!io_runtime->backtrace.frames || !io_runtime->backtrace.lastFrame)
        io_runtime->backtrace.frames = newFrame;
    else
        io_runtime->backtrace.lastFrame->next = newFrame;
    io_runtime->backtrace.lastFrame = newFrame;
}


void  FillBacktraceFunctionInfo  (IM3Runtime io_runtime, IM3Function i_function)
{
    // If we've had an alloc failure then the last frame doesn't refer to the
    // frame we want to fill in the function info for.
    if (UNLIKELY (io_runtime->backtrace.lastFrame == M3_BACKTRACE_TRUNCATED))
        return;

    if (!io_runtime->backtrace.lastFrame)
        return;

    io_runtime->backtrace.lastFrame->function = i_function;
}


void  ClearBacktrace  (IM3Runtime io_runtime)
{
    M3BacktraceFrame * currentFrame = io_runtime->backtrace.frames;
    while (currentFrame)
    {
        M3BacktraceFrame * nextFrame = currentFrame->next;
        m3_Free (currentFrame);
        currentFrame = nextFrame;
    }

    io_runtime->backtrace.frames = NULL;
    io_runtime->backtrace.lastFrame = NULL;
}
#endif // d_m3RecordBacktraces
