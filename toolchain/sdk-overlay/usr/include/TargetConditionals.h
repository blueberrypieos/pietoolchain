/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __TARGETCONDITIONALS__
#define __TARGETCONDITIONALS__

#ifndef TARGET_OS_MAC
#define TARGET_OS_MAC           1
#define TARGET_OS_OSX           1
#define TARGET_OS_IPHONE        0
#define TARGET_OS_IOS           0
#define TARGET_OS_MACCATALYST   0
#define TARGET_OS_TV            0
#define TARGET_OS_WATCH         0
#define TARGET_OS_VISION        0
#define TARGET_OS_DRIVERKIT     0
#define TARGET_OS_SIMULATOR     0
#define TARGET_OS_EMBEDDED      0
#endif

#ifndef TARGET_OS_UIKITFORMAC
#define TARGET_OS_UIKITFORMAC   TARGET_OS_MACCATALYST
#endif
#ifndef TARGET_OS_XR
#define TARGET_OS_XR            TARGET_OS_VISION
#endif
#ifndef TARGET_OS_BRIDGE
#define TARGET_OS_BRIDGE        0
#endif
#ifndef TARGET_OS_NANO
#define TARGET_OS_NANO          TARGET_OS_WATCH
#endif
#ifndef TARGET_OS_EXCLAVECORE
#define TARGET_OS_EXCLAVECORE   0
#endif
#ifndef TARGET_OS_EXCLAVEKIT
#define TARGET_OS_EXCLAVEKIT    0
#endif
#ifndef TARGET_OS_RTKIT
#define TARGET_OS_RTKIT         0
#endif
#ifndef TARGET_OS_WIN32
#define TARGET_OS_WIN32         0
#endif
#ifndef TARGET_OS_WINDOWS
#define TARGET_OS_WINDOWS       0
#endif
#ifndef TARGET_OS_UNIX
#define TARGET_OS_UNIX          0
#endif
#ifndef TARGET_OS_LINUX
#define TARGET_OS_LINUX         0
#endif
#ifndef TARGET_IPHONE_SIMULATOR
#define TARGET_IPHONE_SIMULATOR TARGET_OS_SIMULATOR
#endif
#ifndef TARGET_OS_EMBEDDED_OTHER
#define TARGET_OS_EMBEDDED_OTHER 0
#endif

#define TARGET_CPU_PPC          0
#define TARGET_CPU_PPC64        0
#define TARGET_CPU_68K          0
#if defined(__x86_64__)
#define TARGET_CPU_X86          0
#define TARGET_CPU_X86_64       1
#define TARGET_CPU_ARM          0
#define TARGET_CPU_ARM64        0
#elif defined(__i386__)
#define TARGET_CPU_X86          1
#define TARGET_CPU_X86_64       0
#define TARGET_CPU_ARM          0
#define TARGET_CPU_ARM64        0
#elif defined(__arm64__) || defined(__aarch64__)
#define TARGET_CPU_X86          0
#define TARGET_CPU_X86_64       0
#define TARGET_CPU_ARM          0
#define TARGET_CPU_ARM64        1
#elif defined(__arm__)
#define TARGET_CPU_X86          0
#define TARGET_CPU_X86_64       0
#define TARGET_CPU_ARM          1
#define TARGET_CPU_ARM64        0
#else
#error "TargetConditionals.h: unknown CPU"
#endif

#if defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define TARGET_RT_LITTLE_ENDIAN 1
#define TARGET_RT_BIG_ENDIAN    0
#else
#define TARGET_RT_LITTLE_ENDIAN 0
#define TARGET_RT_BIG_ENDIAN    1
#endif
#if defined(__LP64__)
#define TARGET_RT_64_BIT        1
#else
#define TARGET_RT_64_BIT        0
#endif
#define TARGET_RT_MAC_CFM       0
#define TARGET_RT_MAC_MACHO     1

#endif
