/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_COREENTITLEMENTS_V2_API_H_
#define _PIE_COREENTITLEMENTS_V2_API_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <CoreEntitlements/CoreEntitlements.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CEReturn_t;
enum {
	kCEReturnSuccess                      = 0,
	kCEReturnError                        = 1,
	kCEReturnNotFound                     = 2,
	kCEReturnUnsupported                  = 3,
	kCEReturnV1NotEligibleForAcceleration = 4,
};

typedef uint32_t CEContextType_t;
enum {
	kCEContextTypeEntitlements                    = 0,
	kCEContextTypeProvisioningProfile             = 1,
	kCEContextTypeProvisioningProfileEntitlements = 2,
};

typedef CEBuffer CEBuffer_t;

typedef struct CEElement {
	uintptr_t opaque[4];
} CEElement_t;

typedef struct CEContext {
	uintptr_t opaque[16];
} CEContext_t;

typedef struct CEKernelAPI {
	uint32_t version;
	CEReturn_t (*contextInitWithTypeLegacy)(CERuntime_t runtime, CEContextType_t type,
	    CEContext_t *context, const uint8_t *data, size_t length);
	CEReturn_t (*contextValueForKey)(const CEContext_t *context, const char *key,
	    CEElement_t *element);
	CEReturn_t (*contextGetLegacyContext)(const CEContext_t *context,
	    const void **legacyContext);
	CEReturn_t (*contextCheckAcceleration)(const CEContext_t *context);
	CEReturn_t (*contextEvaluateAcceleration)(const CEContext_t *context, size_t *length);
	CEReturn_t (*contextAccelerate)(CEContext_t *context);
	CEReturn_t (*elementMatchBool)(const CEElement_t *element, bool value);
	CEReturn_t (*elementGetCEBuffer)(const CEElement_t *element, CEBuffer_t *buffer);
} CEKernelAPI_t;

#ifdef __cplusplus
}
#endif

#endif
