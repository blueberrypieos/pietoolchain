/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _DIRHELPER_PRIV_H_
#define _DIRHELPER_PRIV_H_

#include <sys/cdefs.h>
#include <sys/types.h>

typedef enum {
	DIRHELPER_USER_LOCAL = 0,
	DIRHELPER_USER_LOCAL_TEMP,
	DIRHELPER_USER_LOCAL_CACHE,
	DIRHELPER_USER_LOCAL_LAST = DIRHELPER_USER_LOCAL_CACHE
} dirhelper_which_t;

__BEGIN_DECLS
char *_dirhelper(dirhelper_which_t which, char *path, size_t pathlen);
__END_DECLS

#endif
