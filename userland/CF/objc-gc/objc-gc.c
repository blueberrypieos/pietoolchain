/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <string.h>
#include <objc/objc-auto.h>
#include <auto_zone.h>

#pragma GCC visibility push(hidden)

BOOL objc_collectingEnabled(void) { return NO; }
BOOL objc_isAuto(id object) { (void)object; return NO; }
malloc_zone_t *objc_collectableZone(void) { return malloc_default_zone(); }
void *objc_memmove_collectable(void *dst, const void *src, size_t size) { return memmove(dst, src, size); }
id objc_assign_strongCast(id value, id *dest) { return *dest = value; }
id objc_assign_global(id value, id *dest) { return *dest = value; }
id objc_assign_ivar(id value, id dest, ptrdiff_t offset) { return *(id *)((char *)dest + offset) = value; }
id objc_assign_weak(id value, id *location) { return *location = value; }
id objc_read_weak(id *location) { return *location; }

static BOOL
cas(id predicate, id replacement, volatile id *location)
{
	return __sync_bool_compare_and_swap(location, predicate, replacement);
}
BOOL objc_atomicCompareAndSwapPtr(id p, id r, volatile id *l) { return cas(p, r, l); }
BOOL objc_atomicCompareAndSwapPtrBarrier(id p, id r, volatile id *l) { return cas(p, r, l); }
BOOL objc_atomicCompareAndSwapGlobal(id p, id r, volatile id *l) { return cas(p, r, l); }
BOOL objc_atomicCompareAndSwapGlobalBarrier(id p, id r, volatile id *l) { return cas(p, r, l); }
BOOL objc_atomicCompareAndSwapInstanceVariable(id p, id r, volatile id *l) { return cas(p, r, l); }
BOOL objc_atomicCompareAndSwapInstanceVariableBarrier(id p, id r, volatile id *l) { return cas(p, r, l); }

void objc_registerThreadWithCollector(void) {}
void objc_assertRegisteredThreadWithCollector(void) {}
void objc_clear_stack(unsigned long options) { (void)options; }
void objc_start_collector_thread(void) {}
void objc_finalizeOnMainThread(Class cls) { (void)cls; }
BOOL objc_is_finalized(void *ptr) { (void)ptr; return NO; }
void objc_collect(unsigned long options) { (void)options; }
void *objc_destructInstance(id obj) { return obj; }
Class object_getClass(id obj) { return obj ? obj->isa : Nil; }
const char *object_getClassName(id obj) { (void)obj; return "nil"; }
const char *sel_getName(SEL sel) { (void)sel; return "<null selector>"; }

void *
auto_zone_allocate_object(auto_zone_t *zone, size_t size, auto_memory_type_t type,
    bool initial_refcount_to_one, bool clear)
{
	(void)type;
	(void)initial_refcount_to_one;
	return clear ? malloc_zone_calloc(zone, 1, size) : malloc_zone_malloc(zone, size);
}
unsigned int auto_zone_retain(auto_zone_t *zone, void *ptr) { (void)zone; (void)ptr; return 1; }
unsigned int auto_zone_release(auto_zone_t *zone, void *ptr) { (void)zone; (void)ptr; return 0; }
void auto_zone_set_unscanned(auto_zone_t *zone, void *ptr) { (void)zone; (void)ptr; }
void auto_zone_set_nofinalize(auto_zone_t *zone, void *ptr) { (void)zone; (void)ptr; }
bool auto_zone_is_valid_pointer(auto_zone_t *zone, const void *ptr) { (void)zone; (void)ptr; return false; }
void *auto_zone_base_pointer(auto_zone_t *zone, const void *ptr) { (void)zone; (void)ptr; return NULL; }

#pragma GCC visibility pop
