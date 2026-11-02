/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __NS_SYSTEM_DIRECTORIES_H__
#define __NS_SYSTEM_DIRECTORIES_H__

#include <sysdir.h>

typedef enum {
	NSApplicationDirectory = 1,
	NSDemoApplicationDirectory,
	NSDeveloperApplicationDirectory,
	NSAdminApplicationDirectory,
	NSLibraryDirectory,
	NSDeveloperDirectory,
	NSUserDirectory,
	NSDocumentationDirectory,
	NSDocumentDirectory,
	NSCoreServiceDirectory,
	NSAutosavedInformationDirectory = 11,
	NSDesktopDirectory = 12,
	NSCachesDirectory = 13,
	NSApplicationSupportDirectory = 14,
	NSDownloadsDirectory = 15,
	NSInputMethodsDirectory = 16,
	NSMoviesDirectory = 17,
	NSMusicDirectory = 18,
	NSPicturesDirectory = 19,
	NSPrinterDescriptionDirectory = 20,
	NSSharedPublicDirectory = 21,
	NSPreferencePanesDirectory = 22,
	NSAllApplicationsDirectory = 100,
	NSAllLibrariesDirectory = 101
} NSSearchPathDirectory;

typedef enum {
	NSUserDomainMask = 1,
	NSLocalDomainMask = 2,
	NSNetworkDomainMask = 4,
	NSSystemDomainMask = 8,
	NSAllDomainsMask = 0x0ffff
} NSSearchPathDomainMask;

typedef unsigned int NSSearchPathEnumerationState;

__BEGIN_DECLS
NSSearchPathEnumerationState NSStartSearchPathEnumeration(NSSearchPathDirectory dir,
    NSSearchPathDomainMask domainMask);
NSSearchPathEnumerationState NSGetNextSearchPathEnumeration(NSSearchPathEnumerationState state,
    char *path);
__END_DECLS

#endif
