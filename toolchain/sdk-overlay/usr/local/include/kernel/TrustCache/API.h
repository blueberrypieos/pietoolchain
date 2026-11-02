/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_TRUSTCACHE_API_H_
#define _PIE_TRUSTCACHE_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRUST_CACHE_API_STUB    1

#define kTCEntryHashSize        20
#ifndef kUUIDSize
#define kUUIDSize               16
#endif

typedef uint8_t TCComponent_t;
typedef uint8_t TCReturnError_t;

enum {
	kTCReturnSuccess    = 0x00,
	kTCReturnError      = 0x01,
	kTCReturnNotFound   = 0x02,
	kTCReturnDuplicate  = 0x03,
	kTCReturnOverflow   = 0x04,
	kTCReturnInvalid    = 0x05,
	kTCReturnUnsupported = 0x06,
};

typedef struct _TCReturn {
	TCComponent_t   component;
	TCReturnError_t error;
	uint16_t        uniqueError;
} TCReturn_t;

typedef uint32_t TCType_t;
enum {
	kTCTypeInvalid = 0,
	kTCTypeLTRS,
	kTCTypeCryptex1BootOS,
	kTCTypeCryptex1BootApp,
	kTCTypeCryptex1PreBootApp,
	kTCTypeGlobalDiskImage,
	kTCTypeEligibilityPolicy,
	kTCTypeDTRS,
	kTCTypeStatic,
	kTCTypeEngineering,
	kTCTypeLegacy,
	kTCTypeTotal
};

typedef uint32_t TCQueryType_t;
enum {
	kTCQueryTypeStatic = 0,
	kTCQueryTypeLoadable,
	kTCQueryTypeTotal,
	kTCQueryTypeAll = kTCQueryTypeTotal
};

typedef uint64_t TCCapabilities_t;
enum {
	kTCCapabilityNone = 0,
};

typedef struct _TCTypeConfig {
	const char *entitlementValue;
} TCTypeConfig_t;

__attribute__((unused))
static const TCTypeConfig_t TCTypeConfig[kTCTypeTotal] = { { NULL } };

typedef struct _TrustCache {
	struct _TrustCache *next;
	TCType_t            type;
	TCCapabilities_t    capabilities;
	const uint8_t      *module;
	size_t              moduleSize;
} TrustCache_t;

typedef struct _TrustCacheQueryToken {
	const TrustCache_t *trustCache;
	const void         *trustCacheEntry;
} TrustCacheQueryToken_t;

typedef struct _TrustCacheMutableRuntime {
	TrustCache_t *loadableTCHead;
} TrustCacheMutableRuntime_t;

typedef struct _TrustCacheRuntime {
	const void                 *image4RT;
	bool                        allowSecondStaticTC;
	bool                        allowEngineeringTC;
	bool                        allowLegacyTC;
	TrustCache_t               *staticTCHead;
	TrustCacheMutableRuntime_t *mutableRT;
} TrustCacheRuntime_t;

static inline TCReturn_t
trustCacheInitializeRuntime(
	TrustCacheRuntime_t        *runtime,
	TrustCacheMutableRuntime_t *mutableRT,
	bool                        allowSecondStaticTC,
	bool                        allowEngineeringTC,
	bool                        allowLegacyTC,
	const void                 *image4RT)
{
	TCReturn_t ret = { 0, kTCReturnSuccess, 0 };

	runtime->image4RT = image4RT;
	runtime->allowSecondStaticTC = allowSecondStaticTC;
	runtime->allowEngineeringTC = allowEngineeringTC;
	runtime->allowLegacyTC = allowLegacyTC;
	runtime->staticTCHead = NULL;
	runtime->mutableRT = mutableRT;
	if (mutableRT) {
		mutableRT->loadableTCHead = NULL;
	}
	return ret;
}

#ifdef __cplusplus
}
#endif

#endif
