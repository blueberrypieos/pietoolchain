/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_OBJC_OBJC_H
#define PIE_OBJC_OBJC_H
#include <stdbool.h>
typedef struct objc_class *Class;
typedef struct objc_object { Class isa; } *id;
typedef struct objc_selector *SEL;
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#ifndef nil
#define nil ((id)0)
#endif
#ifndef Nil
#define Nil ((Class)0)
#endif
#endif
