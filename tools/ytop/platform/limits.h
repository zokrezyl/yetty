/*
 * limits.h — fixed fan-out caps shared by every ytop abstraction.
 *
 * Kept separate from the umbrella platform.h so an abstraction header can pull
 * in just the limits it needs without dragging in every sibling abstraction.
 */
#ifndef YTOP_PLATFORM_LIMITS_H
#define YTOP_PLATFORM_LIMITS_H

/* Largest per-core / per-interface / per-mount fan-out ytop tracks. Fixed
 * caps keep the monitors free of heap churn on the sampling path. */
#define YTOP_MAX_CORES 256
#define YTOP_MAX_INTERFACES 32
#define YTOP_MAX_MOUNTS 32
#define YTOP_MAX_PROCS 8192

#endif /* YTOP_PLATFORM_LIMITS_H */
