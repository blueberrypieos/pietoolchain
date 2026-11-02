/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_OBJC_OBJC_AUTO_H
#define PIE_OBJC_OBJC_AUTO_H
#include <stddef.h>
#include <malloc/malloc.h>
#include <objc/objc.h>
#include <sys/cdefs.h>

__BEGIN_DECLS
BOOL objc_collectingEnabled(void);
BOOL objc_isAuto(id object);
malloc_zone_t *objc_collectableZone(void);
void *objc_memmove_collectable(void *dst, const void *src, size_t size);
id objc_assign_strongCast(id value, id *dest);
id objc_assign_global(id value, id *dest);
id objc_assign_ivar(id value, id dest, ptrdiff_t offset);
id objc_assign_weak(id value, id *location);
id objc_read_weak(id *location);
BOOL objc_atomicCompareAndSwapPtr(id predicate, id replacement, volatile id *objectLocation);
BOOL objc_atomicCompareAndSwapPtrBarrier(id predicate, id replacement, volatile id *objectLocation);
BOOL objc_atomicCompareAndSwapGlobal(id predicate, id replacement, volatile id *objectLocation);
BOOL objc_atomicCompareAndSwapGlobalBarrier(id predicate, id replacement, volatile id *objectLocation);
BOOL objc_atomicCompareAndSwapInstanceVariable(id predicate, id replacement, volatile id *objectLocation);
BOOL objc_atomicCompareAndSwapInstanceVariableBarrier(id predicate, id replacement, volatile id *objectLocation);
void objc_registerThreadWithCollector(void);
void objc_assertRegisteredThreadWithCollector(void);
void objc_clear_stack(unsigned long options);
void objc_start_collector_thread(void);
void objc_finalizeOnMainThread(Class cls);
BOOL objc_is_finalized(void *ptr);
void objc_collect(unsigned long options);
void *objc_destructInstance(id obj);
Class object_getClass(id obj);
const char *object_getClassName(id obj);
const char *sel_getName(SEL sel);
__END_DECLS
#endif
