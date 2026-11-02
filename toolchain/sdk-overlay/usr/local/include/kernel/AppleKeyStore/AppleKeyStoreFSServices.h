/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_APPLEKEYSTORE_FSSERVICES_H_
#define _PIE_APPLEKEYSTORE_FSSERVICES_H_

#include <stdint.h>
#include <sys/types.h>

#define kAKSFileSystemKeyServices       "aks_file_system_key_services"

typedef uint32_t cp_key_class_t;

typedef struct aks_cred_s {
	union {
		ino64_t         inode;
		uint64_t        crypto_id;
	};
	uint32_t                volume;
	pid_t                   pid;
	uid_t                   uid;
	uint16_t                key_revision;
} *aks_cred_t;

typedef struct aks_wrapped_key_s {
	void                    *key;
	unsigned                key_len;
	uint32_t                dp_class;
} *aks_wrapped_key_t;

#define AKS_RAW_KEY_WRAPPEDKEY  0x00000001

typedef struct aks_raw_key_s {
	void                    *key;
	unsigned                key_len;
	void                    *iv_key;
	unsigned                iv_key_len;
	uint32_t                flags;
} *aks_raw_key_t;

typedef struct aks_file_system_key_services_s {
	int (*unwrap_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in,
	    aks_raw_key_t key_out);
	int (*rewrap_key)(aks_cred_t access, cp_key_class_t dp_class,
	    const aks_wrapped_key_t wrapped_key_in, aks_wrapped_key_t wrapped_key_out);
	int (*new_key)(aks_cred_t access, cp_key_class_t dp_class, aks_raw_key_t key_out,
	    aks_wrapped_key_t wrapped_key_out);
	int (*backup_key)(aks_cred_t access, const aks_wrapped_key_t wrapped_key_in,
	    aks_wrapped_key_t wrapped_key_out);
} aks_file_system_key_services_t;

#endif
