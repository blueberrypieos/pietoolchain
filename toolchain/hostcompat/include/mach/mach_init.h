/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Host stand-in for <mach/mach_init.h> (xnu SETUP tools on Linux). */
#ifndef _PIE_MACH_INIT_H_
#define _PIE_MACH_INIT_H_
#include <stdint.h>
#include <unistd.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
typedef uint64_t  mach_vm_size_t;
#define vm_page_size ((vm_size_t) sysconf(_SC_PAGESIZE))
#define mach_vm_round_page(x) \
	(((mach_vm_size_t)(x) + vm_page_size - 1) & ~((mach_vm_size_t) vm_page_size - 1))
#endif
