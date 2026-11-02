/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <os/feature_private.h>

bool
_os_feature_enabled_impl(const char *domain, const char *feature)
{
	(void)domain;
	(void)feature;
	return false;
}

bool
_os_feature_enabled_simple_impl(const char *domain, const char *feature,
    bool fallback)
{
	(void)domain;
	(void)feature;
	return fallback;
}
