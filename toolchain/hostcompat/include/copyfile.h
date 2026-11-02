/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 *
 * Host stand-in for Darwin's <copyfile.h>: just enough copyfile(3) for
 * xnu's SETUP/installfile (COPYFILE_DATA between two paths).
 */
#ifndef _PIE_COPYFILE_H_
#define _PIE_COPYFILE_H_
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

typedef void *copyfile_state_t;
typedef uint32_t copyfile_flags_t;
#define COPYFILE_ACL   (1 << 0)
#define COPYFILE_STAT  (1 << 1)
#define COPYFILE_XATTR (1 << 2)
#define COPYFILE_DATA  (1 << 3)
#define COPYFILE_SECURITY (COPYFILE_STAT | COPYFILE_ACL)
#define COPYFILE_METADATA (COPYFILE_SECURITY | COPYFILE_XATTR)
#define COPYFILE_ALL      (COPYFILE_METADATA | COPYFILE_DATA)

static inline int
fcopyfile(int in, int out, copyfile_state_t state, copyfile_flags_t flags)
{
	char buf[65536];
	ssize_t n;

	(void) state;
	if (!(flags & COPYFILE_DATA)) {
		return 0;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		if (write(out, buf, (size_t) n) != n) {
			return -1;
		}
	}
	return n < 0 ? -1 : 0;
}

static inline int
copyfile(const char *from, const char *to, copyfile_state_t state, copyfile_flags_t flags)
{
	int in, out, ret;

	if ((in = open(from, O_RDONLY)) < 0) {
		return -1;
	}
	if ((out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
		close(in);
		return -1;
	}
	ret = fcopyfile(in, out, state, flags);
	close(in);
	if (close(out) < 0) {
		ret = -1;
	}
	return ret;
}
#endif
