/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_UUID_H
#define PIE_HOST_UUID_H

typedef unsigned char uuid_t[16];
typedef char uuid_string_t[37];

void uuid_unparse_upper(const uuid_t uu, char *out);

#endif
