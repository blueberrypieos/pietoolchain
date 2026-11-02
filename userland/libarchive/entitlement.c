/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <stdbool.h>
#include "archive_check_entitlement.h"

bool
archive_allow_entitlement_format(const char *format)
{
	(void)format;
	return true;
}

bool
archive_allow_entitlement_filter(const char *filter)
{
	(void)filter;
	return true;
}

void
archive_entitlement_cleanup(void)
{
}
