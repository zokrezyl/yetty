//
//  m3_atomic.h
//
//  Threads-proposal support: shared memory wait/notify primitives.
//

#ifndef m3_atomic_h
#define m3_atomic_h

#include "m3_core.h"

d_m3BeginExternC

// Block the calling thread until *addr != expected, or timeout_ns elapses.
// timeout_ns < 0 means wait forever. Returns:
//   0 = "ok" (notified)
//   1 = "not-equal" (value at addr already differed)
//   2 = "timed-out"
i32 m3Atomic_Wait32 (volatile u32 * addr, u32 expected, i64 timeout_ns);
i32 m3Atomic_Wait64 (volatile u64 * addr, u64 expected, i64 timeout_ns);

// Wake up to count waiters parked at addr. Returns the number actually woken.
u32 m3Atomic_Notify (void * addr, u32 count);

d_m3EndExternC

#endif // m3_atomic_h
