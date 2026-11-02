/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_QUARANTINE_H
#define PIE_QUARANTINE_H

#include <stdlib.h>

typedef struct _qtn_file_s *qtn_file_t;

#define qtn_xattr_name "com.apple.quarantine"
#define QTN_NOT_QUARANTINED (-1)

struct _qtn_file_s {
	int unused;
};

static inline qtn_file_t
qtn_file_alloc(void)
{
	return calloc(1, sizeof(struct _qtn_file_s));
}

static inline void
qtn_file_free(qtn_file_t qf)
{
	free(qf);
}

static inline qtn_file_t
qtn_file_clone(qtn_file_t qf)
{
	(void)qf;
	return qtn_file_alloc();
}

static inline int
qtn_file_init_with_fd(qtn_file_t qf, int fd)
{
	(void)qf;
	(void)fd;
	return QTN_NOT_QUARANTINED;
}

static inline int
qtn_file_apply_to_fd(qtn_file_t qf, int fd)
{
	(void)qf;
	(void)fd;
	return 0;
}

static inline int
qtn_file_apply_to_path(qtn_file_t qf, const char *path)
{
	(void)qf;
	(void)path;
	return 0;
}

#endif
