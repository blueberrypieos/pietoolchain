/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "dirhelper_priv.h"

#define FOLDERS "/private/var/folders"

static int
make_dir(const char *path, mode_t mode, uid_t uid)
{
	struct stat st;
	if (mkdir(path, mode) == 0) {
		return 0;
	}
	if (errno != EEXIST || lstat(path, &st) != 0) {
		return -1;
	}
	if (!S_ISDIR(st.st_mode) || (uid != (uid_t)-1 && st.st_uid != uid)) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

char *
_dirhelper(dirhelper_which_t which, char *path, size_t pathlen)
{
	static const char *const suffix[] = { "0", "T", "C" };
	uid_t uid = geteuid();
	char user[64];

	if ((unsigned)which > DIRHELPER_USER_LOCAL_LAST || path == NULL) {
		errno = EINVAL;
		return NULL;
	}
	snprintf(user, sizeof(user), FOLDERS "/zz/u%u", (unsigned)uid);
	if ((size_t)snprintf(path, pathlen, "%s/%s/", user, suffix[which]) >= pathlen) {
		errno = ERANGE;
		return NULL;
	}
	if (make_dir(FOLDERS "/zz", 01777, (uid_t)-1) != 0 || make_dir(user, 0700, uid) != 0) {
		return NULL;
	}
	path[strlen(path) - 1] = '\0';
	int r = make_dir(path, 0700, uid);
	path[strlen(path)] = '/';
	return r == 0 ? path : NULL;
}

void
_libcoreservices_fork_child(void)
{
}
