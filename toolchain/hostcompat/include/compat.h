/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 *
 * Force-included into every host (Linux) compile of xnu's SETUP tools by
 * toolchain/bin/host-cc: small Darwin libc conveniences glibc lacks.
 */
#ifndef _PIE_HOSTCOMPAT_H_
#define _PIE_HOSTCOMPAT_H_
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>

static inline const char *
getprogname(void)
{
	return program_invocation_short_name;
}
/*
 * BSD setmode(3)/getmode(3), numeric (octal) modes only -- that is all the
 * xnu makefiles pass to SETUP/installfile.
 */
static inline void *
setmode(const char *s)
{
	char *end;
	long v;
	mode_t *m;

	v = strtol(s, &end, 8);
	if (!*s || *end || v < 0 || v > 07777) {
		errno = EINVAL;
		return NULL;
	}
	if ((m = (mode_t *) malloc(sizeof(*m))) == NULL) {
		return NULL;
	}
	*m = (mode_t) v;
	return m;
}

static inline mode_t
getmode(const void *set, mode_t mode)
{
	(void) mode;
	return *(const mode_t *) set;
}
#endif
