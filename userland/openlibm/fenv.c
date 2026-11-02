/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <fenv.h>
#include <stdint.h>

#define FPCR_RMODE_MASK 0x00C00000ULL
#define FPCR_TRAP_SHIFT 8

const fenv_t _FE_DFL_ENV = { 0, 0 };

static inline uint64_t
get_fpsr(void)
{
	uint64_t r;
	__asm__ __volatile__("mrs %0, fpsr" : "=r"(r));
	return r;
}

static inline void
set_fpsr(uint64_t r)
{
	__asm__ __volatile__("msr fpsr, %0" : : "r"(r));
}

static inline uint64_t
get_fpcr(void)
{
	uint64_t r;
	__asm__ __volatile__("mrs %0, fpcr" : "=r"(r));
	return r;
}

static inline void
set_fpcr(uint64_t r)
{
	__asm__ __volatile__("msr fpcr, %0" : : "r"(r));
}

int
feclearexcept(int excepts)
{
	set_fpsr(get_fpsr() & ~(uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = (fexcept_t)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
feraiseexcept(int excepts)
{
	set_fpsr(get_fpsr() | (uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	uint64_t mask = (uint64_t)(excepts & FE_ALL_EXCEPT);
	set_fpsr((get_fpsr() & ~mask) | (*flagp & mask));
	return 0;
}

int
fetestexcept(int excepts)
{
	return (int)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
}

int
fegetround(void)
{
	return (int)(get_fpcr() & FPCR_RMODE_MASK);
}

int
fesetround(int round)
{
	if ((uint64_t)round & ~FPCR_RMODE_MASK) {
		return -1;
	}
	set_fpcr((get_fpcr() & ~FPCR_RMODE_MASK) | (uint64_t)round);
	return 0;
}

int
fegetenv(fenv_t *envp)
{
	envp->__fpsr = get_fpsr();
	envp->__fpcr = get_fpcr();
	return 0;
}

int
feholdexcept(fenv_t *envp)
{
	fegetenv(envp);
	set_fpsr(envp->__fpsr & ~(uint64_t)FE_ALL_EXCEPT);
	set_fpcr(envp->__fpcr & ~((uint64_t)FE_ALL_EXCEPT << FPCR_TRAP_SHIFT));
	return 0;
}

int
fesetenv(const fenv_t *envp)
{
	set_fpsr(envp->__fpsr);
	set_fpcr(envp->__fpcr);
	return 0;
}

int
feupdateenv(const fenv_t *envp)
{
	int raised = (int)(get_fpsr() & FE_ALL_EXCEPT);
	fesetenv(envp);
	return feraiseexcept(raised);
}
