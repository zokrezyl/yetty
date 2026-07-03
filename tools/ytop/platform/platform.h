/*
 * platform.h — umbrella header + shared limits for ytop's OS abstraction
 * layer.
 *
 * Every OS-specific read lives behind one of the sibling abstractions
 * (cpu / memory / process / network / disk / sysinfo). The headers here are
 * platform-independent; each abstraction's implementation lives in
 * platform/<abstraction>/<os>.c (linux.c today; macos.c / bsd.c later, chosen
 * by CMake). Presentation code (ui.c, main.c) consumes only these structs and
 * never touches /proc, sysctl, or any OS interface directly.
 */
#ifndef YTOP_PLATFORM_PLATFORM_H
#define YTOP_PLATFORM_PLATFORM_H

#include "platform/limits.h"

#include "platform/cpu/cpu.h"
#include "platform/disk/disk.h"
#include "platform/memory/memory.h"
#include "platform/network/network.h"
#include "platform/process/process.h"
#include "platform/sysinfo/sysinfo.h"

#endif /* YTOP_PLATFORM_PLATFORM_H */
