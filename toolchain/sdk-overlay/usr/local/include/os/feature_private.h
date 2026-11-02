/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __OS_FEATURE_PRIVATE_H__
#define __OS_FEATURE_PRIVATE_H__

#include <stdbool.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

bool _os_feature_enabled_impl(const char *domain, const char *feature);

bool _os_feature_enabled_simple_impl(const char *domain, const char *feature,
    bool fallback);

__END_DECLS

#define os_feature_enabled(domain, feature) \
	_os_feature_enabled_impl(#domain, #feature)

#define os_feature_enabled_simple(domain, feature, fallback) \
	_os_feature_enabled_simple_impl(#domain, #feature, (fallback))

#endif
