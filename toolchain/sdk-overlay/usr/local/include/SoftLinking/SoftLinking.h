/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _SOFTLINKING_SOFTLINKING_H_
#define _SOFTLINKING_SOFTLINKING_H_

#include <dlfcn.h>
#include <stdbool.h>
#include <dispatch/dispatch.h>

#define SOFT_LINK_DYLIB(lib) \
	static void *lib##Library(void) \
	{ \
		static void *handle; \
		static dispatch_once_t once; \
		dispatch_once(&once, ^{ \
			handle = dlopen("/usr/lib/" #lib ".dylib", RTLD_NOW); \
		}); \
		return handle; \
	}

#define SOFT_LINK_FUNCTION(lib, name, localName, ret, decls, args) \
	static ret (*localName##Ptr) decls; \
	static bool is##lib##name##Available(void) \
	{ \
		static dispatch_once_t once; \
		dispatch_once(&once, ^{ \
			void *handle = lib##Library(); \
			if (handle != NULL) \
				localName##Ptr = (ret (*) decls)dlsym(handle, #name); \
		}); \
		return localName##Ptr != NULL; \
	} \
	__attribute__((unused)) static ret localName decls \
	{ \
		(void)is##lib##name##Available(); \
		return localName##Ptr args; \
	}

#endif
