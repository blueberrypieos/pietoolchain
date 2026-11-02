/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <mach-o/dyld_priv.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/sysctl.h>

#define PIE_OS_VERSION ((26u << 16) | (5u << 8))

static uint32_t
os_version(void)
{
	static uint32_t version;

	if (version == 0) {
		char buf[32];
		size_t len = sizeof(buf);
		uint32_t v = PIE_OS_VERSION;
		if (sysctlbyname("kern.osproductversion", buf, &len, NULL, 0) == 0 && len > 1) {
			char *p = buf;
			unsigned long major = strtoul(p, &p, 10);
			unsigned long minor = (*p == '.') ? strtoul(p + 1, &p, 10) : 0;
			unsigned long patch = (*p == '.') ? strtoul(p + 1, &p, 10) : 0;
			if (major != 0) {
				v = (uint32_t)((major << 16) | ((minor & 0xff) << 8) | (patch & 0xff));
			}
		}
		version = v;
	}
	return version;
}

bool
_availability_version_check(uint32_t count, dyld_build_version_t versions[])
{
	dyld_platform_t platform = dyld_get_active_platform();

	for (uint32_t i = 0; i < count; i++) {
		if (versions[i].platform == platform) {
			return os_version() >= versions[i].version;
		}
	}
	return true;
}
