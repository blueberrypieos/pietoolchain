/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <limits.h>
#include <string.h>
#include "sysdir.h"
#include "NSSystemDirectories.h"

#define U SYSDIR_DOMAIN_MASK_USER
#define L SYSDIR_DOMAIN_MASK_LOCAL
#define N SYSDIR_DOMAIN_MASK_NETWORK
#define S SYSDIR_DOMAIN_MASK_SYSTEM

static const struct {
	unsigned int dir;
	unsigned int domain;
	const char *path;
} dirs[] = {
	{ SYSDIR_DIRECTORY_APPLICATION, U, "~/Applications" },
	{ SYSDIR_DIRECTORY_APPLICATION, L, "/Applications" },
	{ SYSDIR_DIRECTORY_APPLICATION, N, "/Network/Applications" },
	{ SYSDIR_DIRECTORY_APPLICATION, S, "/System/Applications" },
	{ SYSDIR_DIRECTORY_DEMO_APPLICATION, U, "~/Applications/Demos" },
	{ SYSDIR_DIRECTORY_DEMO_APPLICATION, L, "/Applications/Demos" },
	{ SYSDIR_DIRECTORY_DEMO_APPLICATION, N, "/Network/Applications/Demos" },
	{ SYSDIR_DIRECTORY_DEVELOPER_APPLICATION, U, "~/Developer/Applications" },
	{ SYSDIR_DIRECTORY_DEVELOPER_APPLICATION, L, "/Developer/Applications" },
	{ SYSDIR_DIRECTORY_DEVELOPER_APPLICATION, N, "/Network/Developer/Applications" },
	{ SYSDIR_DIRECTORY_ADMIN_APPLICATION, U, "~/Applications/Utilities" },
	{ SYSDIR_DIRECTORY_ADMIN_APPLICATION, L, "/Applications/Utilities" },
	{ SYSDIR_DIRECTORY_ADMIN_APPLICATION, N, "/Network/Applications/Utilities" },
	{ SYSDIR_DIRECTORY_ADMIN_APPLICATION, S, "/System/Applications/Utilities" },
	{ SYSDIR_DIRECTORY_LIBRARY, U, "~/Library" },
	{ SYSDIR_DIRECTORY_LIBRARY, L, "/Library" },
	{ SYSDIR_DIRECTORY_LIBRARY, N, "/Network/Library" },
	{ SYSDIR_DIRECTORY_LIBRARY, S, "/System/Library" },
	{ SYSDIR_DIRECTORY_DEVELOPER, U, "~/Developer" },
	{ SYSDIR_DIRECTORY_DEVELOPER, L, "/Developer" },
	{ SYSDIR_DIRECTORY_DEVELOPER, N, "/Network/Developer" },
	{ SYSDIR_DIRECTORY_USER, L, "/Users" },
	{ SYSDIR_DIRECTORY_USER, N, "/Network/Users" },
	{ SYSDIR_DIRECTORY_DOCUMENTATION, U, "~/Library/Documentation" },
	{ SYSDIR_DIRECTORY_DOCUMENTATION, L, "/Library/Documentation" },
	{ SYSDIR_DIRECTORY_DOCUMENTATION, N, "/Network/Library/Documentation" },
	{ SYSDIR_DIRECTORY_DOCUMENTATION, S, "/System/Library/Documentation" },
	{ SYSDIR_DIRECTORY_DOCUMENT, U, "~/Documents" },
	{ SYSDIR_DIRECTORY_CORESERVICE, S, "/System/Library/CoreServices" },
	{ SYSDIR_DIRECTORY_AUTOSAVED_INFORMATION, U, "~/Library/Autosave Information" },
	{ SYSDIR_DIRECTORY_DESKTOP, U, "~/Desktop" },
	{ SYSDIR_DIRECTORY_CACHES, U, "~/Library/Caches" },
	{ SYSDIR_DIRECTORY_CACHES, L, "/Library/Caches" },
	{ SYSDIR_DIRECTORY_CACHES, S, "/System/Library/Caches" },
	{ SYSDIR_DIRECTORY_APPLICATION_SUPPORT, U, "~/Library/Application Support" },
	{ SYSDIR_DIRECTORY_APPLICATION_SUPPORT, L, "/Library/Application Support" },
	{ SYSDIR_DIRECTORY_APPLICATION_SUPPORT, N, "/Network/Library/Application Support" },
	{ SYSDIR_DIRECTORY_DOWNLOADS, U, "~/Downloads" },
	{ SYSDIR_DIRECTORY_INPUT_METHODS, U, "~/Library/Input Methods" },
	{ SYSDIR_DIRECTORY_INPUT_METHODS, L, "/Library/Input Methods" },
	{ SYSDIR_DIRECTORY_INPUT_METHODS, N, "/Network/Library/Input Methods" },
	{ SYSDIR_DIRECTORY_INPUT_METHODS, S, "/System/Library/Input Methods" },
	{ SYSDIR_DIRECTORY_MOVIES, U, "~/Movies" },
	{ SYSDIR_DIRECTORY_MUSIC, U, "~/Music" },
	{ SYSDIR_DIRECTORY_PICTURES, U, "~/Pictures" },
	{ SYSDIR_DIRECTORY_PRINTER_DESCRIPTION, S, "/System/Library/Printers/PPDs" },
	{ SYSDIR_DIRECTORY_SHARED_PUBLIC, U, "~/Public" },
	{ SYSDIR_DIRECTORY_PREFERENCE_PANES, U, "~/Library/PreferencePanes" },
	{ SYSDIR_DIRECTORY_PREFERENCE_PANES, L, "/Library/PreferencePanes" },
	{ SYSDIR_DIRECTORY_PREFERENCE_PANES, S, "/System/Library/PreferencePanes" },
};
#define NDIRS (sizeof(dirs) / sizeof(dirs[0]))

static int
dir_matches(unsigned int want, unsigned int dir)
{
	switch (want) {
	case SYSDIR_DIRECTORY_ALL_APPLICATIONS:
		return dir == SYSDIR_DIRECTORY_APPLICATION || dir == SYSDIR_DIRECTORY_ADMIN_APPLICATION ||
		       dir == SYSDIR_DIRECTORY_DEVELOPER_APPLICATION || dir == SYSDIR_DIRECTORY_DEMO_APPLICATION;
	case SYSDIR_DIRECTORY_ALL_LIBRARIES:
		return dir == SYSDIR_DIRECTORY_LIBRARY || dir == SYSDIR_DIRECTORY_DEVELOPER;
	default:
		return dir == want;
	}
}

static unsigned int
next_state(unsigned int dir, unsigned int mask, unsigned int from)
{
	for (unsigned int domain = 1; domain <= S; domain <<= 1) {
		if (!(mask & domain)) {
			continue;
		}
		for (unsigned int i = 0; i < NDIRS; i++) {
			if (dirs[i].domain == domain && dir_matches(dir, dirs[i].dir)) {
				if (from > 0) {
					from--;
					continue;
				}
				return i + 1;
			}
		}
	}
	return 0;
}

static unsigned int
make_state(unsigned int dir, unsigned int mask, unsigned int visited)
{
	if (next_state(dir, mask, visited) == 0) {
		return 0;
	}
	return (dir & 0xff) << 24 | (mask & 0xffff) << 8 | ((visited + 1) & 0xff);
}

sysdir_search_path_enumeration_state
sysdir_start_search_path_enumeration(sysdir_search_path_directory_t dir,
    sysdir_search_path_domain_mask_t domainMask)
{
	return make_state(dir, domainMask, 0);
}

sysdir_search_path_enumeration_state
sysdir_get_next_search_path_enumeration(sysdir_search_path_enumeration_state state, char *path)
{
	unsigned int dir = state >> 24, mask = (state >> 8) & 0xffff, visited = (state & 0xff) - 1;
	unsigned int i = state ? next_state(dir, mask, visited) : 0;

	if (i == 0) {
		if (path != NULL) {
			path[0] = '\0';
		}
		return 0;
	}
	if (path != NULL) {
		strlcpy(path, dirs[i - 1].path, PATH_MAX);
	}
	return make_state(dir, mask, visited + 1);
}

NSSearchPathEnumerationState
NSStartSearchPathEnumeration(NSSearchPathDirectory dir, NSSearchPathDomainMask domainMask)
{
	return sysdir_start_search_path_enumeration(dir, domainMask);
}

NSSearchPathEnumerationState
NSGetNextSearchPathEnumeration(NSSearchPathEnumerationState state, char *path)
{
	return sysdir_get_next_search_path_enumeration(state, path);
}
