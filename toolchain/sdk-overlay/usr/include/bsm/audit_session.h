/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _BSM_AUDIT_SESSION_H_
#define _BSM_AUDIT_SESSION_H_

#include <sys/cdefs.h>
#include <stdio.h>
#include <bsm/audit.h>
#include <bsm/audit_kevents.h>

#define AU_SDEVF_ALLSESSIONS	0x00000001

__BEGIN_DECLS
FILE	*au_sdev_open(int flags);
int	 au_sdev_close(FILE *fp);
int	 au_sdev_fd(FILE *fp);
int	 au_sdev_read_aia(FILE *fp, int *event, auditinfo_addr_t *aia_p);
__END_DECLS

#endif
