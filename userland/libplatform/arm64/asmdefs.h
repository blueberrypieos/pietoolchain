/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#ifndef _PIE_ASMDEFS_H
#define _PIE_ASMDEFS_H

#define SEP %%

#define __memchr_aarch64	_platform_memchr
#define __memcmp_aarch64	_platform_memcmp
#define __memcpy_aarch64_simd	_platform_memmove
#define __memset_aarch64	_platform_memset
#define __strchr_aarch64	_platform_strchr
#define __strcmp_aarch64	_platform_strcmp
#define __strcpy_aarch64	_platform_strcpy
#define __strlen_aarch64	_platform_strlen
#define __strncmp_aarch64	_platform_strncmp
#define __strnlen_aarch64	_platform_strnlen

#if VARIANT_STATIC
#define LIBC_NAME(name)		.globl _ ## name SEP _ ## name:
#define HIDDEN_LIBC_NAME(name)	.globl _ ## name SEP _ ## name:
#else
#define LIBC_NAME(name)
#define HIDDEN_LIBC_NAME(name)	.private_extern _ ## name SEP _ ## name:
#endif

#define OTHER_NAMES__platform_bzero		LIBC_NAME(bzero) SEP LIBC_NAME(__bzero)
#define OTHER_NAMES__platform_memchr		LIBC_NAME(memchr)
#define OTHER_NAMES__platform_memcmp		LIBC_NAME(memcmp) SEP LIBC_NAME(bcmp)
#define OTHER_NAMES__platform_memcmp_zero_aligned8
#define OTHER_NAMES__platform_memmove		LIBC_NAME(memmove) SEP LIBC_NAME(memcpy)
#define OTHER_NAMES__platform_memset		HIDDEN_LIBC_NAME(memset)
#define OTHER_NAMES__platform_memset_pattern4	LIBC_NAME(memset_pattern4)
#define OTHER_NAMES__platform_memset_pattern8	LIBC_NAME(memset_pattern8)
#define OTHER_NAMES__platform_memset_pattern16	LIBC_NAME(memset_pattern16)
#define OTHER_NAMES__platform_strchr		LIBC_NAME(strchr)
#define OTHER_NAMES__platform_strcmp		LIBC_NAME(strcmp)
#define OTHER_NAMES__platform_strcpy		LIBC_NAME(strcpy)
#define OTHER_NAMES__platform_strlen		LIBC_NAME(strlen)
#define OTHER_NAMES__platform_strncmp		LIBC_NAME(strncmp)
#define OTHER_NAMES__platform_strnlen		LIBC_NAME(strnlen)

#define ENTRY(name)	ENTRY_(name, 6)
#define ENTRY_ALIGN(name, alignment) ENTRY_(name, alignment)
#define ENTRY_(name, alignment)	\
	.text SEP \
	.p2align alignment SEP \
	.globl _ ## name SEP \
	_ ## name: SEP \
	OTHER_NAMES_ ## name SEP \
	.cfi_startproc

#define ENTRY_ALIAS(name)
#define END(name)	.cfi_endproc

#define L(label)	L ## label

#endif
