/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#include <dnsinfo.h>
#include <stddef.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT const char *
dns_configuration_notify_key(void)
{
	return "com.apple.system.SystemConfiguration.dns_configuration";
}

EXPORT dns_config_t *
dns_configuration_copy(void)
{
	return NULL;
}

EXPORT void
dns_configuration_free(dns_config_t *config)
{
	(void)config;
}

EXPORT void
_dns_configuration_ack(dns_config_t *config, const char *bundle_id)
{
	(void)config;
	(void)bundle_id;
}

EXPORT void _libSC_info_fork_prepare(void) {}
EXPORT void _libSC_info_fork_parent(void) {}
EXPORT void _libSC_info_fork_child(void) {}
