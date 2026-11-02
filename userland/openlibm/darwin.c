/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <math.h>

extern void sincos(double, double *, double *);
extern void sincosf(float, float *, float *);

int __fpclassifyf(float x)        { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
int __fpclassifyd(double x)       { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
int __fpclassifyl(long double x)  { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }

int
__math_errhandling(void)
{
	return MATH_ERREXCEPT;
}

int finite(double x)              { return isfinite(x); }
int finitef(float x)              { return isfinite(x); }
double drem(double x, double y)   { return remainder(x, y); }
float dremf(float x, float y)     { return remainderf(x, y); }
double gamma(double x)            { return lgamma(x); }

double
scalb(double x, double fn)
{
	if (isnan(x) || isnan(fn)) {
		return x * fn;
	}
	if (!isfinite(fn)) {
		return fn > 0.0 ? x * fn : x / (-fn);
	}
	if (rint(fn) != fn) {
		return (fn - fn) / (fn - fn);
	}
	if (fn > 65000.0) {
		return scalbn(x, 65000);
	}
	if (-fn > 65000.0) {
		return scalbn(x, -65000);
	}
	return scalbn(x, (int)fn);
}

float scalbf(float x, float fn)   { return (float)scalb(x, fn); }

double
significand(double x)
{
	if (x == 0.0 || !isfinite(x)) {
		return x;
	}
	return scalbn(x, -ilogb(x));
}

float significandf(float x)       { return (float)significand(x); }

double __exp10(double x)          { return pow(10.0, x); }
float __exp10f(float x)           { return powf(10.0f, x); }

double
__sinpi(double x)
{
	if (!isfinite(x)) {
		return x - x;
	}
	double r = remainder(x, 2.0);
	if (r > 0.5) {
		r = 1.0 - r;
	} else if (r < -0.5) {
		r = -1.0 - r;
	}
	if (r == 0.0) {
		return copysign(0.0, x);
	}
	return sin(M_PI * r);
}

double
__cospi(double x)
{
	if (!isfinite(x)) {
		return x - x;
	}
	double ar = fabs(remainder(x, 2.0));
	if (ar <= 0.25) {
		return cos(M_PI * ar);
	}
	if (ar < 0.75) {
		return sin(M_PI * (0.5 - ar));
	}
	return -cos(M_PI * (1.0 - ar));
}

double __tanpi(double x)          { return __sinpi(x) / __cospi(x); }
float __sinpif(float x)           { return (float)__sinpi(x); }
float __cospif(float x)           { return (float)__cospi(x); }
float __tanpif(float x)           { return (float)__tanpi(x); }

struct __double2
__sincos_stret(double x)
{
	struct __double2 r;
	sincos(x, &r.__sinval, &r.__cosval);
	return r;
}

struct __float2
__sincosf_stret(float x)
{
	struct __float2 r;
	sincosf(x, &r.__sinval, &r.__cosval);
	return r;
}

struct __double2
__sincospi_stret(double x)
{
	return (struct __double2){ __sinpi(x), __cospi(x) };
}

struct __float2
__sincospif_stret(float x)
{
	return (struct __float2){ __sinpif(x), __cospif(x) };
}
