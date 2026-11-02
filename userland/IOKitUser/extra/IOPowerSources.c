/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#include <CoreFoundation/CoreFoundation.h>
#include "IOPowerSources.h"

IOPSLowBatteryWarningLevel IOPSGetBatteryWarningLevel(void)
{
	return kIOPSLowBatteryWarningNone;
}

CFTimeInterval IOPSGetTimeRemainingEstimate(void)
{
	return kIOPSTimeRemainingUnlimited;
}

CFTypeRef IOPSCopyPowerSourcesInfo(void)
{
	return CFArrayCreate(kCFAllocatorDefault, NULL, 0, &kCFTypeArrayCallBacks);
}

CFArrayRef IOPSCopyPowerSourcesList(CFTypeRef blob)
{
	if (blob == NULL || CFGetTypeID(blob) != CFArrayGetTypeID())
		return NULL;
	return CFArrayCreateCopy(kCFAllocatorDefault, (CFArrayRef)blob);
}

CFDictionaryRef IOPSGetPowerSourceDescription(CFTypeRef blob, CFTypeRef ps)
{
	return blob != NULL ? (CFDictionaryRef)ps : NULL;
}

CFStringRef IOPSGetProvidingPowerSourceType(CFTypeRef snapshot __unused)
{
	return CFSTR(kIOPMACPowerKey);
}

CFDictionaryRef IOPSCopyExternalPowerAdapterDetails(void)
{
	return NULL;
}

static void
ps_never(void *info __unused)
{
}

static CFRunLoopSourceRef
ps_source(void)
{
	CFRunLoopSourceContext ctx = { .version = 0, .perform = ps_never };

	return CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
}

CFRunLoopSourceRef IOPSNotificationCreateRunLoopSource(IOPowerSourceCallbackType callback __unused,
    void *context __unused)
{
	return ps_source();
}

CFRunLoopSourceRef IOPSCreateLimitedPowerNotification(IOPowerSourceCallbackType callback __unused,
    void *context __unused)
{
	return ps_source();
}
