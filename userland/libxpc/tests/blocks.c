/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <stdlib.h>
#include <string.h>

void *_NSConcreteStackBlock[32];
void *_NSConcreteMallocBlock[32];
void *_NSConcreteGlobalBlock[32];

struct block_descriptor {
	unsigned long reserved;
	unsigned long size;
	void (*copy)(void *dst, const void *src);
	void (*dispose)(const void *block);
};

struct block_layout {
	void *isa;
	int flags;
	int reserved;
	void (*invoke)(void *block);
	struct block_descriptor *descriptor;
};

enum {
	BLOCK_REFCOUNT_MASK = 0xfffe,
	BLOCK_NEEDS_FREE = 1 << 24,
	BLOCK_HAS_COPY_DISPOSE = 1 << 25,
	BLOCK_IS_GLOBAL = 1 << 28,
	BLOCK_FIELD_IS_BLOCK = 7,
	BLOCK_FIELD_IS_BYREF = 8,
};

void *
_Block_copy(const void *arg)
{
	struct block_layout *b = (struct block_layout *)arg;
	if (b == NULL) {
		return NULL;
	}
	if (b->flags & BLOCK_NEEDS_FREE) {
		b->flags += 2;
		return b;
	}
	if (b->flags & BLOCK_IS_GLOBAL) {
		return b;
	}
	struct block_layout *n = malloc(b->descriptor->size);
	memcpy(n, b, b->descriptor->size);
	n->isa = _NSConcreteMallocBlock;
	n->flags &= ~BLOCK_REFCOUNT_MASK;
	n->flags |= BLOCK_NEEDS_FREE | 2;
	if (b->flags & BLOCK_HAS_COPY_DISPOSE) {
		b->descriptor->copy(n, b);
	}
	return n;
}

void
_Block_release(const void *arg)
{
	struct block_layout *b = (struct block_layout *)arg;
	if (b == NULL || !(b->flags & BLOCK_NEEDS_FREE)) {
		return;
	}
	b->flags -= 2;
	if ((b->flags & BLOCK_REFCOUNT_MASK) == 0) {
		if (b->flags & BLOCK_HAS_COPY_DISPOSE) {
			b->descriptor->dispose(b);
		}
		free(b);
	}
}

void
_Block_object_assign(void *dst, const void *obj, const int flags)
{
	if (flags & BLOCK_FIELD_IS_BYREF) {
		abort();
	}
	if ((flags & BLOCK_FIELD_IS_BLOCK) == BLOCK_FIELD_IS_BLOCK) {
		*(void **)dst = _Block_copy(obj);
	} else {
		*(const void **)dst = obj;
	}
}

void
_Block_object_dispose(const void *obj, const int flags)
{
	if ((flags & BLOCK_FIELD_IS_BLOCK) == BLOCK_FIELD_IS_BLOCK && !(flags & BLOCK_FIELD_IS_BYREF)) {
		_Block_release(obj);
	}
}
