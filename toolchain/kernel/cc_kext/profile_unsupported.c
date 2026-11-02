/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 *
 * The profile-runtime entry points the kernel calls to export the PGO
 * counters of instrumented kexts. They report that there is no profile
 * data.
 */
#include <stdint.h>

uint64_t __llvm_profile_get_size_for_buffer_internal(
	const char *DataBegin, const char *DataEnd,
	const char *CountersBegin, const char *CountersEnd,
	const char *BitmapBegin, const char *BitmapEnd,
	const char *NamesBegin, const char *NamesEnd,
	const char *VTableBegin, const char *VTableEnd,
	const char *VNamesBegin, const char *VNamesEnd);
int __llvm_profile_write_buffer_internal(
	char *Buffer,
	const char *DataBegin, const char *DataEnd,
	const char *CountersBegin, const char *CountersEnd,
	const char *BitmapBegin, const char *BitmapEnd,
	const char *NamesBegin, const char *NamesEnd);

uint64_t
__llvm_profile_get_size_for_buffer_internal(
	const char *DataBegin, const char *DataEnd,
	const char *CountersBegin, const char *CountersEnd,
	const char *BitmapBegin, const char *BitmapEnd,
	const char *NamesBegin, const char *NamesEnd,
	const char *VTableBegin, const char *VTableEnd,
	const char *VNamesBegin, const char *VNamesEnd)
{
	(void)DataBegin; (void)DataEnd; (void)CountersBegin; (void)CountersEnd;
	(void)BitmapBegin; (void)BitmapEnd; (void)NamesBegin; (void)NamesEnd;
	(void)VTableBegin; (void)VTableEnd; (void)VNamesBegin; (void)VNamesEnd;
	return 0;
}

int
__llvm_profile_write_buffer_internal(
	char *Buffer,
	const char *DataBegin, const char *DataEnd,
	const char *CountersBegin, const char *CountersEnd,
	const char *BitmapBegin, const char *BitmapEnd,
	const char *NamesBegin, const char *NamesEnd)
{
	(void)Buffer; (void)DataBegin; (void)DataEnd; (void)CountersBegin;
	(void)CountersEnd; (void)BitmapBegin; (void)BitmapEnd; (void)NamesBegin;
	(void)NamesEnd;
	return -1;
}
