/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fileport.h>
#include <time.h>

#define TYPE(name) const struct _xpc_type_s _xpc_type_##name = { #name, NULL }
TYPE(null);
TYPE(bool);
TYPE(int64);
TYPE(uint64);
TYPE(double);
TYPE(date);
TYPE(data);
TYPE(string);
TYPE(uuid);
TYPE(array);
TYPE(dictionary);
TYPE(error);

static void
port_dispose(struct xpc_obj *o)
{
	if (MACH_PORT_VALID(o->port)) {
		mach_port_deallocate(mach_task_self(), o->port);
	}
}

const struct _xpc_type_s _xpc_type_fd = { "fd", port_dispose };
const struct _xpc_type_s _xpc_type_mach_send = { "mach_send", port_dispose };

const struct _xpc_bool_s _xpc_bool_true = { { &_xpc_type_bool, -1, { .b = true } } };
const struct _xpc_bool_s _xpc_bool_false = { { &_xpc_type_bool, -1, { .b = false } } };
static const struct xpc_obj xpc_null = { &_xpc_type_null, -1, { .i = 0 } };

struct xpc_obj *
_xpc_alloc(xpc_type_t type)
{
	struct xpc_obj *o = calloc(1, sizeof(*o));
	if (o == NULL) {
		abort();
	}
	o->type = type;
	atomic_init(&o->refcnt, 1);
	return o;
}

static void *
xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n);
	if (q == NULL && n != 0) {
		abort();
	}
	return q;
}

xpc_object_t
xpc_retain(xpc_object_t object)
{
	struct xpc_obj *o = XO(object);
	if (o != NULL && atomic_load_explicit(&o->refcnt, memory_order_relaxed) >= 0) {
		atomic_fetch_add_explicit(&o->refcnt, 1, memory_order_relaxed);
	}
	return object;
}

void
xpc_release(xpc_object_t object)
{
	struct xpc_obj *o = XO(object);
	if (o == NULL || atomic_load_explicit(&o->refcnt, memory_order_relaxed) < 0) {
		return;
	}
	if (atomic_fetch_sub_explicit(&o->refcnt, 1, memory_order_acq_rel) != 1) {
		return;
	}
	if (o->type == XPC_TYPE_STRING || o->type == XPC_TYPE_DATA) {
		free(o->buf.ptr);
	} else if (o->type == XPC_TYPE_ARRAY) {
		for (size_t i = 0; i < o->arr.n; i++) {
			xpc_release(o->arr.v[i]);
		}
		free(o->arr.v);
	} else if (o->type == XPC_TYPE_DICTIONARY) {
		for (size_t i = 0; i < o->dict.n; i++) {
			free(o->dict.keys[i]);
			xpc_release(o->dict.vals[i]);
		}
		free(o->dict.keys);
		free(o->dict.vals);
		if (o->dict.msg != NULL) {
			_xpc_port_release(o->dict.msg->reply, MACH_MSG_TYPE_PORT_SEND_ONCE);
			xpc_release(o->dict.msg->conn);
			free(o->dict.msg);
		}
	} else if (o->type->dispose != NULL) {
		o->type->dispose(o);
	}
	free(o);
}

xpc_type_t
xpc_get_type(xpc_object_t object)
{
	return object ? XO(object)->type : NULL;
}

const char *
xpc_type_get_name(xpc_type_t type)
{
	return type ? type->name : "(null)";
}

xpc_object_t xpc_null_create(void) { return (xpc_object_t)&xpc_null; }
xpc_object_t xpc_bool_create(bool value) { return value ? XPC_BOOL_TRUE : XPC_BOOL_FALSE; }

bool
xpc_bool_get_value(xpc_object_t xbool)
{
	return xbool && XO(xbool)->type == XPC_TYPE_BOOL && XO(xbool)->b;
}

#define SCALAR(name, ctype, field, xtype) \
	xpc_object_t xpc_##name##_create(ctype value) { \
		struct xpc_obj *o = _xpc_alloc(xtype); \
		o->field = value; \
		return o; \
	} \
	ctype xpc_##name##_get_value(xpc_object_t x) { \
		return (x && XO(x)->type == xtype) ? XO(x)->field : 0; \
	}
SCALAR(int64, int64_t, i, XPC_TYPE_INT64)
SCALAR(uint64, uint64_t, u, XPC_TYPE_UINT64)
SCALAR(double, double, d, XPC_TYPE_DOUBLE)
SCALAR(date, int64_t, i, XPC_TYPE_DATE)

xpc_object_t
xpc_date_create_from_current(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return xpc_date_create((int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

xpc_object_t
_xpc_buf_create(xpc_type_t type, const void *bytes, size_t length)
{
	struct xpc_obj *o = _xpc_alloc(type);
	o->buf.ptr = malloc(length + 1);
	if (o->buf.ptr == NULL) {
		abort();
	}
	if (length) {
		memcpy(o->buf.ptr, bytes, length);
	}
	o->buf.ptr[length] = '\0';
	o->buf.len = length;
	return o;
}

xpc_object_t xpc_data_create(const void *bytes, size_t length) { return _xpc_buf_create(XPC_TYPE_DATA, bytes, length); }

size_t
xpc_data_get_length(xpc_object_t xdata)
{
	return (xdata && XO(xdata)->type == XPC_TYPE_DATA) ? XO(xdata)->buf.len : 0;
}

const void *
xpc_data_get_bytes_ptr(xpc_object_t xdata)
{
	return (xdata && XO(xdata)->type == XPC_TYPE_DATA) ? XO(xdata)->buf.ptr : NULL;
}

size_t
xpc_data_get_bytes(xpc_object_t xdata, void *buffer, size_t off, size_t length)
{
	size_t len = xpc_data_get_length(xdata);
	if (off >= len) {
		return 0;
	}
	if (length > len - off) {
		length = len - off;
	}
	memcpy(buffer, XO(xdata)->buf.ptr + off, length);
	return length;
}

xpc_object_t
xpc_string_create(const char *string)
{
	return _xpc_buf_create(XPC_TYPE_STRING, string, strlen(string));
}

xpc_object_t
xpc_string_create_with_format(const char *fmt, ...)
{
	char *s = NULL;
	va_list ap;
	va_start(ap, fmt);
	int n = vasprintf(&s, fmt, ap);
	va_end(ap);
	if (n < 0) {
		abort();
	}
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_STRING);
	o->buf.ptr = s;
	o->buf.len = strlen(s);
	return o;
}

size_t
xpc_string_get_length(xpc_object_t xstring)
{
	return (xstring && XO(xstring)->type == XPC_TYPE_STRING) ? XO(xstring)->buf.len : 0;
}

const char *
xpc_string_get_string_ptr(xpc_object_t xstring)
{
	return (xstring && XO(xstring)->type == XPC_TYPE_STRING) ? XO(xstring)->buf.ptr : NULL;
}

xpc_object_t
xpc_uuid_create(const uuid_t uuid)
{
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_UUID);
	memcpy(o->uuid, uuid, sizeof(uuid_t));
	return o;
}

const uint8_t *
xpc_uuid_get_bytes(xpc_object_t xuuid)
{
	return (xuuid && XO(xuuid)->type == XPC_TYPE_UUID) ? XO(xuuid)->uuid : NULL;
}

xpc_object_t
xpc_array_create(xpc_object_t const *objects, size_t count)
{
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_ARRAY);
	for (size_t i = 0; i < count; i++) {
		xpc_array_append_value(o, objects[i]);
	}
	return o;
}

xpc_object_t xpc_array_create_empty(void) { return xpc_array_create(NULL, 0); }

static bool
is_array(xpc_object_t x)
{
	return x && XO(x)->type == XPC_TYPE_ARRAY;
}

size_t
xpc_array_get_count(xpc_object_t xarray)
{
	return is_array(xarray) ? XO(xarray)->arr.n : 0;
}

xpc_object_t
xpc_array_get_value(xpc_object_t xarray, size_t index)
{
	if (!is_array(xarray) || index >= XO(xarray)->arr.n) {
		return NULL;
	}
	return XO(xarray)->arr.v[index];
}

void
xpc_array_append_value(xpc_object_t xarray, xpc_object_t value)
{
	xpc_array_set_value(xarray, XPC_ARRAY_APPEND, value);
}

void
xpc_array_set_value(xpc_object_t xarray, size_t index, xpc_object_t value)
{
	struct xpc_obj *a = XO(xarray);
	if (!is_array(a) || value == NULL) {
		return;
	}
	xpc_retain(value);
	if (index == XPC_ARRAY_APPEND || index == a->arr.n) {
		if (a->arr.n == a->arr.cap) {
			a->arr.cap = a->arr.cap ? 2 * a->arr.cap : 8;
			a->arr.v = xrealloc(a->arr.v, a->arr.cap * sizeof(*a->arr.v));
		}
		a->arr.v[a->arr.n++] = value;
	} else if (index < a->arr.n) {
		xpc_release(a->arr.v[index]);
		a->arr.v[index] = value;
	} else {
		xpc_release(value);
	}
}

bool xpc_array_get_bool(xpc_object_t a, size_t i) { return xpc_bool_get_value(xpc_array_get_value(a, i)); }
int64_t xpc_array_get_int64(xpc_object_t a, size_t i) { return xpc_int64_get_value(xpc_array_get_value(a, i)); }
uint64_t xpc_array_get_uint64(xpc_object_t a, size_t i) { return xpc_uint64_get_value(xpc_array_get_value(a, i)); }
static double
double_or_nan(xpc_object_t x)
{
	return x != NULL && XO(x)->type == XPC_TYPE_DOUBLE ? XO(x)->d : NAN;
}

double xpc_array_get_double(xpc_object_t a, size_t i) { return double_or_nan(xpc_array_get_value(a, i)); }
const char *xpc_array_get_string(xpc_object_t a, size_t i) { return xpc_string_get_string_ptr(xpc_array_get_value(a, i)); }

#define ARRAY_SET(name, ctype, ctor) \
	void xpc_array_set_##name(xpc_object_t a, size_t i, ctype v) { \
		xpc_object_t o = ctor(v); \
		xpc_array_set_value(a, i, o); \
		xpc_release(o); \
	}
ARRAY_SET(bool, bool, xpc_bool_create)
ARRAY_SET(int64, int64_t, xpc_int64_create)
ARRAY_SET(uint64, uint64_t, xpc_uint64_create)
ARRAY_SET(string, const char *, xpc_string_create)

bool
xpc_array_apply(xpc_object_t xarray, xpc_array_applier_t applier)
{
	for (size_t i = 0; i < xpc_array_get_count(xarray); i++) {
		if (!applier(i, XO(xarray)->arr.v[i])) {
			return false;
		}
	}
	return true;
}

static bool
is_dict(xpc_object_t x)
{
	return x && XO(x)->type == XPC_TYPE_DICTIONARY;
}

static bool
is_readable_dict(xpc_object_t x)
{
	return x && (XO(x)->type == XPC_TYPE_DICTIONARY || XO(x)->type == XPC_TYPE_ERROR);
}

xpc_object_t
xpc_dictionary_create(const char *const *keys, const xpc_object_t *values, size_t count)
{
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_DICTIONARY);
	for (size_t i = 0; i < count; i++) {
		xpc_dictionary_set_value(o, keys[i], values[i]);
	}
	return o;
}

xpc_object_t xpc_dictionary_create_empty(void) { return xpc_dictionary_create(NULL, NULL, 0); }

size_t
xpc_dictionary_get_count(xpc_object_t xdict)
{
	return is_readable_dict(xdict) ? XO(xdict)->dict.n : 0;
}

static ssize_t
dict_find(struct xpc_obj *d, const char *key)
{
	for (size_t i = 0; i < d->dict.n; i++) {
		if (strcmp(d->dict.keys[i], key) == 0) {
			return (ssize_t)i;
		}
	}
	return -1;
}

xpc_object_t
xpc_dictionary_get_value(xpc_object_t xdict, const char *key)
{
	if (!is_readable_dict(xdict) || key == NULL) {
		return NULL;
	}
	ssize_t i = dict_find(XO(xdict), key);
	return i < 0 ? NULL : XO(xdict)->dict.vals[i];
}

void
xpc_dictionary_set_value(xpc_object_t xdict, const char *key, xpc_object_t value)
{
	struct xpc_obj *d = XO(xdict);
	if (!is_dict(d) || key == NULL) {
		return;
	}
	ssize_t i = dict_find(d, key);
	if (value == NULL) {
		if (i >= 0) {
			free(d->dict.keys[i]);
			xpc_release(d->dict.vals[i]);
			d->dict.n--;
			memmove(&d->dict.keys[i], &d->dict.keys[i + 1], (d->dict.n - (size_t)i) * sizeof(char *));
			memmove(&d->dict.vals[i], &d->dict.vals[i + 1], (d->dict.n - (size_t)i) * sizeof(void *));
		}
		return;
	}
	xpc_retain(value);
	if (i >= 0) {
		xpc_release(d->dict.vals[i]);
		d->dict.vals[i] = value;
		return;
	}
	_xpc_dictionary_append(d, strdup(key), value);
}

void
_xpc_dictionary_append(xpc_object_t xdict, char *key, xpc_object_t value)
{
	struct xpc_obj *d = XO(xdict);
	if (key == NULL) {
		abort();
	}
	if (d->dict.n == d->dict.cap) {
		d->dict.cap = d->dict.cap ? 2 * d->dict.cap : 8;
		d->dict.keys = xrealloc(d->dict.keys, d->dict.cap * sizeof(char *));
		d->dict.vals = xrealloc(d->dict.vals, d->dict.cap * sizeof(void *));
	}
	d->dict.keys[d->dict.n] = key;
	d->dict.vals[d->dict.n] = value;
	d->dict.n++;
}

static int
key_compare(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

bool
_xpc_dictionary_keys_unique(xpc_object_t xdict)
{
	struct xpc_obj *d = XO(xdict);
	size_t n = d->dict.n;
	if (n < 2) {
		return true;
	}
	char **keys = xrealloc(NULL, n * sizeof(*keys));
	memcpy(keys, d->dict.keys, n * sizeof(*keys));
	heapsort(keys, n, sizeof(*keys), key_compare);
	bool unique = true;
	for (size_t i = 1; i < n && unique; i++) {
		unique = strcmp(keys[i - 1], keys[i]) != 0;
	}
	free(keys);
	return unique;
}

bool xpc_dictionary_get_bool(xpc_object_t d, const char *k) { return xpc_bool_get_value(xpc_dictionary_get_value(d, k)); }
int64_t xpc_dictionary_get_int64(xpc_object_t d, const char *k) { return xpc_int64_get_value(xpc_dictionary_get_value(d, k)); }
uint64_t xpc_dictionary_get_uint64(xpc_object_t d, const char *k) { return xpc_uint64_get_value(xpc_dictionary_get_value(d, k)); }
double xpc_dictionary_get_double(xpc_object_t d, const char *k) { return double_or_nan(xpc_dictionary_get_value(d, k)); }
const char *xpc_dictionary_get_string(xpc_object_t d, const char *k) { return xpc_string_get_string_ptr(xpc_dictionary_get_value(d, k)); }

const void *
xpc_dictionary_get_data(xpc_object_t xdict, const char *key, size_t *length)
{
	xpc_object_t v = xpc_dictionary_get_value(xdict, key);
	if (v == NULL || XO(v)->type != XPC_TYPE_DATA) {
		return NULL;
	}
	if (length) {
		*length = XO(v)->buf.len;
	}
	return XO(v)->buf.ptr;
}

xpc_object_t
xpc_dictionary_get_dictionary(xpc_object_t xdict, const char *key)
{
	xpc_object_t v = xpc_dictionary_get_value(xdict, key);
	return is_dict(v) ? v : NULL;
}

xpc_object_t
xpc_dictionary_get_array(xpc_object_t xdict, const char *key)
{
	xpc_object_t v = xpc_dictionary_get_value(xdict, key);
	return is_array(v) ? v : NULL;
}

#define DICT_SET(name, ctype, ctor) \
	void xpc_dictionary_set_##name(xpc_object_t d, const char *k, ctype v) { \
		xpc_object_t o = ctor(v); \
		xpc_dictionary_set_value(d, k, o); \
		xpc_release(o); \
	}
DICT_SET(bool, bool, xpc_bool_create)
DICT_SET(int64, int64_t, xpc_int64_create)
DICT_SET(uint64, uint64_t, xpc_uint64_create)
DICT_SET(double, double, xpc_double_create)
DICT_SET(string, const char *, xpc_string_create)

void
xpc_dictionary_set_data(xpc_object_t xdict, const char *key, const void *bytes, size_t length)
{
	xpc_object_t o = xpc_data_create(bytes, length);
	xpc_dictionary_set_value(xdict, key, o);
	xpc_release(o);
}

bool
xpc_dictionary_apply(xpc_object_t xdict, xpc_dictionary_applier_t applier)
{
	if (!is_readable_dict(xdict)) {
		return true;
	}
	struct xpc_obj *d = XO(xdict);
	for (size_t i = 0; i < d->dict.n; i++) {
		if (!applier(d->dict.keys[i], d->dict.vals[i])) {
			return false;
		}
	}
	return true;
}

xpc_object_t
xpc_copy(xpc_object_t object)
{
	struct xpc_obj *o = XO(object);
	if (o == NULL) {
		return NULL;
	}
	if (o->type == XPC_TYPE_ARRAY) {
		struct xpc_obj *c = _xpc_alloc(XPC_TYPE_ARRAY);
		for (size_t i = 0; i < o->arr.n; i++) {
			xpc_object_t e = xpc_copy(o->arr.v[i]);
			xpc_array_append_value(c, e);
			xpc_release(e);
		}
		return c;
	}
	if (o->type == XPC_TYPE_DICTIONARY) {
		struct xpc_obj *c = _xpc_alloc(XPC_TYPE_DICTIONARY);
		for (size_t i = 0; i < o->dict.n; i++) {
			xpc_object_t e = xpc_copy(o->dict.vals[i]);
			xpc_dictionary_set_value(c, o->dict.keys[i], e);
			xpc_release(e);
		}
		return c;
	}
	if (o->type == XPC_TYPE_STRING || o->type == XPC_TYPE_DATA) {
		return _xpc_buf_create(o->type, o->buf.ptr, o->buf.len);
	}
	if (o->type == XPC_TYPE_FD) {
		if (mach_port_mod_refs(mach_task_self(), o->port, MACH_PORT_RIGHT_SEND, 1) != KERN_SUCCESS) {
			return NULL;
		}
		return _xpc_port_create(XPC_TYPE_FD, o->port);
	}
	return xpc_retain(object);
}

bool
xpc_equal(xpc_object_t object1, xpc_object_t object2)
{
	struct xpc_obj *a = XO(object1), *b = XO(object2);
	if (a == b) {
		return true;
	}
	if (a == NULL || b == NULL || a->type != b->type || a->type == XPC_TYPE_FD) {
		return false;
	}
	if (a->type == XPC_TYPE_STRING || a->type == XPC_TYPE_DATA) {
		return a->buf.len == b->buf.len && memcmp(a->buf.ptr, b->buf.ptr, a->buf.len) == 0;
	}
	if (a->type == XPC_TYPE_UUID) {
		return memcmp(a->uuid, b->uuid, sizeof(uuid_t)) == 0;
	}
	if (a->type == XPC_TYPE_ARRAY) {
		if (a->arr.n != b->arr.n) {
			return false;
		}
		for (size_t i = 0; i < a->arr.n; i++) {
			if (!xpc_equal(a->arr.v[i], b->arr.v[i])) {
				return false;
			}
		}
		return true;
	}
	if (a->type == XPC_TYPE_DICTIONARY) {
		if (a->dict.n != b->dict.n) {
			return false;
		}
		for (size_t i = 0; i < a->dict.n; i++) {
			if (!xpc_equal(a->dict.vals[i], xpc_dictionary_get_value(b, a->dict.keys[i]))) {
				return false;
			}
		}
		return true;
	}
	if (a->type == XPC_TYPE_DOUBLE) {
		return a->d == b->d;
	}
	return a->i == b->i;
}

size_t
xpc_hash(xpc_object_t object)
{
	struct xpc_obj *o = XO(object);
	if (o == NULL) {
		return 0;
	}
	size_t h = (size_t)(uintptr_t)o->type;
	if (o->type == XPC_TYPE_STRING || o->type == XPC_TYPE_DATA) {
		for (size_t i = 0; i < o->buf.len; i++) {
			h = h * 31 + (unsigned char)o->buf.ptr[i];
		}
	} else if (o->type == XPC_TYPE_ARRAY || o->type == XPC_TYPE_DICTIONARY) {
		h ^= (o->type == XPC_TYPE_ARRAY ? o->arr.n : o->dict.n);
	} else if (o->type == XPC_TYPE_FD) {
		h ^= (size_t)(uintptr_t)o;
	} else if (o->type != XPC_TYPE_DOUBLE || o->d != 0) {
		h ^= (size_t)o->u;
	}
	return h;
}

struct desc {
	char *buf;
	size_t len, cap;
};

static void
dput(struct desc *d, const char *fmt, ...)
{
	va_list ap;
	for (;;) {
		va_start(ap, fmt);
		int n = vsnprintf(d->buf + d->len, d->cap - d->len, fmt, ap);
		va_end(ap);
		if (n < 0) {
			return;
		}
		if ((size_t)n < d->cap - d->len) {
			d->len += (size_t)n;
			return;
		}
		d->cap = 2 * d->cap + (size_t)n;
		d->buf = xrealloc(d->buf, d->cap);
	}
}

static void
describe(struct desc *d, struct xpc_obj *o, int depth)
{
	if (o == NULL) {
		dput(d, "(null)");
	} else if (o->type == XPC_TYPE_DICTIONARY) {
		dput(d, "<dictionary: %zu> {\n", o->dict.n);
		for (size_t i = 0; i < o->dict.n; i++) {
			dput(d, "%*s\"%s\" => ", 2 * (depth + 1), "", o->dict.keys[i]);
			describe(d, o->dict.vals[i], depth + 1);
			dput(d, "\n");
		}
		dput(d, "%*s}", 2 * depth, "");
	} else if (o->type == XPC_TYPE_ARRAY) {
		dput(d, "<array: %zu> [\n", o->arr.n);
		for (size_t i = 0; i < o->arr.n; i++) {
			dput(d, "%*s%zu: ", 2 * (depth + 1), "", i);
			describe(d, o->arr.v[i], depth + 1);
			dput(d, "\n");
		}
		dput(d, "%*s]", 2 * depth, "");
	} else if (o->type == XPC_TYPE_STRING) {
		dput(d, "<string: \"%s\">", o->buf.ptr);
	} else if (o->type == XPC_TYPE_DATA) {
		dput(d, "<data: %zu bytes>", o->buf.len);
	} else if (o->type == XPC_TYPE_BOOL) {
		dput(d, "<bool: %s>", o->b ? "true" : "false");
	} else if (o->type == XPC_TYPE_INT64) {
		dput(d, "<int64: %lld>", (long long)o->i);
	} else if (o->type == XPC_TYPE_UINT64) {
		dput(d, "<uint64: %llu>", (unsigned long long)o->u);
	} else if (o->type == XPC_TYPE_DOUBLE) {
		dput(d, "<double: %g>", o->d);
	} else if (o->type == XPC_TYPE_DATE) {
		dput(d, "<date: %lld>", (long long)o->i);
	} else if (o->type == XPC_TYPE_UUID) {
		uuid_string_t s;
		uuid_unparse_upper(o->uuid, s);
		dput(d, "<uuid: %s>", s);
	} else {
		dput(d, "<%s>", o->type->name);
	}
}

char *
xpc_copy_description(xpc_object_t object)
{
	struct desc d = { malloc(64), 0, 64 };
	if (d.buf == NULL) {
		abort();
	}
	d.buf[0] = '\0';
	describe(&d, XO(object), 0);
	return d.buf;
}

void
_xpc_port_release(mach_port_t port, mach_msg_type_name_t type)
{
	if (!MACH_PORT_VALID(port)) {
		return;
	}
	if (type == MACH_MSG_TYPE_PORT_RECEIVE) {
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
	} else {
		mach_port_deallocate(mach_task_self(), port);
	}
}

xpc_object_t
_xpc_port_create(xpc_type_t type, mach_port_t port)
{
	struct xpc_obj *o = _xpc_alloc(type);
	o->port = port;
	return o;
}

xpc_object_t
xpc_fd_create(int fd)
{
	mach_port_t port = MACH_PORT_NULL;
	if (fileport_makeport(fd, &port) != 0 || !MACH_PORT_VALID(port)) {
		return NULL;
	}
	return _xpc_port_create(XPC_TYPE_FD, port);
}

int
xpc_fd_dup(xpc_object_t xfd)
{
	if (xfd == NULL || XO(xfd)->type != XPC_TYPE_FD) {
		return -1;
	}
	return fileport_makefd(XO(xfd)->port);
}

xpc_object_t
xpc_mach_send_create(mach_port_t send)
{
	if (!MACH_PORT_VALID(send) ||
	    mach_port_mod_refs(mach_task_self(), send, MACH_PORT_RIGHT_SEND, 1) != KERN_SUCCESS) {
		return NULL;
	}
	return _xpc_port_create(XPC_TYPE_MACH_SEND, send);
}

mach_port_t
xpc_mach_send_copy_right(xpc_object_t xsend)
{
	if (xsend == NULL || XO(xsend)->type != XPC_TYPE_MACH_SEND) {
		return MACH_PORT_NULL;
	}
	mach_port_t port = XO(xsend)->port;
	if (mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, 1) != KERN_SUCCESS) {
		return MACH_PORT_DEAD;
	}
	return port;
}

void
xpc_dictionary_set_fd(xpc_object_t xdict, const char *key, int fd)
{
	xpc_object_t o = xpc_fd_create(fd);
	if (o != NULL) {
		xpc_dictionary_set_value(xdict, key, o);
		xpc_release(o);
	}
}

int
xpc_dictionary_dup_fd(xpc_object_t xdict, const char *key)
{
	return xpc_fd_dup(xpc_dictionary_get_value(xdict, key));
}

void
xpc_dictionary_set_mach_send(xpc_object_t xdict, const char *key, mach_port_t send)
{
	xpc_object_t o = xpc_mach_send_create(send);
	if (o != NULL) {
		xpc_dictionary_set_value(xdict, key, o);
		xpc_release(o);
	}
}

mach_port_t
xpc_dictionary_copy_mach_send(xpc_object_t xdict, const char *key)
{
	return xpc_mach_send_copy_right(xpc_dictionary_get_value(xdict, key));
}

void
xpc_dictionary_set_uuid(xpc_object_t xdict, const char *key, const uuid_t uuid)
{
	xpc_object_t o = xpc_uuid_create(uuid);
	xpc_dictionary_set_value(xdict, key, o);
	xpc_release(o);
}

const uint8_t *
xpc_dictionary_get_uuid(xpc_object_t xdict, const char *key)
{
	return xpc_uuid_get_bytes(xpc_dictionary_get_value(xdict, key));
}

struct xpc_msginfo *
_xpc_dictionary_msginfo(xpc_object_t xdict, bool create)
{
	struct xpc_obj *d = XO(xdict);
	if (!is_dict(d)) {
		return NULL;
	}
	if (d->dict.msg == NULL && create) {
		d->dict.msg = calloc(1, sizeof(*d->dict.msg));
		if (d->dict.msg == NULL) {
			abort();
		}
	}
	return d->dict.msg;
}

void
_xpc_dictionary_set_connection(xpc_object_t xdict, xpc_connection_t conn)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(xdict, true);
	xpc_retain(conn);
	xpc_release(m->conn);
	m->conn = conn;
}

mach_port_t
_xpc_dictionary_take_reply(xpc_object_t xdict)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(xdict, false);
	if (m == NULL || !m->is_reply) {
		return MACH_PORT_NULL;
	}
	mach_port_t port = m->reply;
	m->reply = MACH_PORT_NULL;
	return port;
}

void
xpc_dictionary_get_audit_token(xpc_object_t xdict, audit_token_t *token)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(xdict, false);
	if (m != NULL && m->has_token) {
		*token = m->token;
	} else {
		memset(token, 0xff, sizeof(*token));
	}
}

xpc_connection_t
xpc_dictionary_get_remote_connection(xpc_object_t xdict)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(xdict, false);
	return m != NULL ? m->conn : NULL;
}

xpc_object_t
xpc_dictionary_create_reply(xpc_object_t original)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(original, false);
	if (m == NULL || m->is_reply || !MACH_PORT_VALID(m->reply)) {
		return NULL;
	}
	xpc_object_t reply = xpc_dictionary_create(NULL, NULL, 0);
	struct xpc_msginfo *r = _xpc_dictionary_msginfo(reply, true);
	r->is_reply = true;
	r->reply = m->reply;
	m->reply = MACH_PORT_NULL;
	if (m->conn != NULL) {
		r->conn = xpc_retain(m->conn);
	}
	return reply;
}
