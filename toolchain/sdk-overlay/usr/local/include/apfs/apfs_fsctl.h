/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _APFS_APFS_FSCTL_H_
#define _APFS_APFS_FSCTL_H_

#include <stdint.h>
#include <sys/ioccom.h>

#define APFS_DIR_STATS_FAST_PATH 0x1

struct apfs_dir_stats_ext {
	uint64_t flags;
	uint64_t total_size;
	uint64_t num_children;
	uint64_t chained_key;
	uint64_t reserved[4];
};

#define APFSIOC_GET_DIR_STATS_EXT _IOWR('J', 36, struct apfs_dir_stats_ext)

#define APFS_CLEAR_PURGEABLE 0x0
#define APFSIOC_MARK_PURGEABLE _IOWR('J', 68, uint64_t)

struct xdstream_obj_id {
	char *xdi_name;
	uint64_t xdi_xdtream_obj_id;
};
#define APFSIOC_XDSTREAM_OBJ_ID _IOWR('J', 53, struct xdstream_obj_id)

#define APFS_PURGEABLE_FLAGS_MASK 0xffffffffULL
#define APFSIOC_GET_PURGEABLE_FILE_FLAGS _IOR('J', 70, uint64_t)

#endif
