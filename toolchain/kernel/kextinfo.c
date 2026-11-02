/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/*
 * A kext's kmod_info: the kernel's linker (kxld) fills in where the kext
 * was loaded and its size, OSKext stamps it with the bundle identifier and
 * version from Info.plist and calls its start and stop functions.
 * kext.mk compiles this into every kext, in place of libkmod:
 *
 *   -DKEXT_ID='"com.example.foo"' -DKEXT_VERSION='"1.0"'
 *   -DKEXT_START=foo_start -DKEXT_STOP=foo_stop   (optional)
 *
 * KEXT_START and KEXT_STOP are the kext's module start and stop functions,
 * kern_return_t f(kmod_info_t *, void *) with C linkage; without them the
 * kext starts and stops doing nothing, which an IOKit driver kext, whose
 * C++ constructors the kernel runs itself, usually wants.
 */

#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <libkern/OSKextLib.h>

#ifndef KEXT_ID
#error KEXT_ID: the kext's bundle identifier
#endif
#ifndef KEXT_VERSION
#error KEXT_VERSION: the kext's version
#endif

static kern_return_t
kext_nothing(kmod_info_t *ki __unused, void *data __unused)
{
	return KERN_SUCCESS;
}

#ifdef KEXT_START
extern kern_return_t KEXT_START(kmod_info_t *ki, void *data);
#else
#define KEXT_START kext_nothing
#endif
#ifdef KEXT_STOP
extern kern_return_t KEXT_STOP(kmod_info_t *ki, void *data);
#else
#define KEXT_STOP kext_nothing
#endif

__attribute__((visibility("default")))
kmod_info_t kmod_info = {
	.next = NULL,
	.info_version = KMOD_INFO_VERSION,
	.id = UINT32_MAX,
	.name = KEXT_ID,
	.version = KEXT_VERSION,
	.reference_count = -1,
	.reference_list = NULL,
	.address = 0,
	.size = 0,
	.hdr_size = 0,
	.start = KEXT_START,
	.stop = KEXT_STOP,
};

/* What the kext is, for its own code (<libkern/OSKextLib.h>). */
__private_extern__ const char *
OSKextGetCurrentIdentifier(void)
{
	return kmod_info.name;
}

__private_extern__ const char *
OSKextGetCurrentVersionString(void)
{
	return kmod_info.version;
}

__private_extern__ OSKextLoadTag
OSKextGetCurrentLoadTag(void)
{
	return (OSKextLoadTag)kmod_info.id;
}
