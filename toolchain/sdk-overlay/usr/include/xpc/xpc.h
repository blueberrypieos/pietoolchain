/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __XPC_H__
#define __XPC_H__

#include <xpc/base.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>
#include <sys/mman.h>
#include <uuid/uuid.h>
#include <bsm/audit.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

__BEGIN_DECLS

typedef void *xpc_object_t;
typedef const struct _xpc_type_s *xpc_type_t;
typedef struct _xpc_connection_s *xpc_connection_t;
typedef struct _xpc_endpoint_s *xpc_endpoint_t;
#ifdef __BLOCKS__
typedef void (^xpc_handler_t)(xpc_object_t object);
#endif
typedef void (*xpc_finalizer_t)(void *value);

#define XPC_TYPE(type) const struct _xpc_type_s type

XPC_EXPORT XPC_TYPE(_xpc_type_null);
XPC_EXPORT XPC_TYPE(_xpc_type_bool);
XPC_EXPORT XPC_TYPE(_xpc_type_int64);
XPC_EXPORT XPC_TYPE(_xpc_type_uint64);
XPC_EXPORT XPC_TYPE(_xpc_type_double);
XPC_EXPORT XPC_TYPE(_xpc_type_date);
XPC_EXPORT XPC_TYPE(_xpc_type_data);
XPC_EXPORT XPC_TYPE(_xpc_type_string);
XPC_EXPORT XPC_TYPE(_xpc_type_uuid);
XPC_EXPORT XPC_TYPE(_xpc_type_array);
XPC_EXPORT XPC_TYPE(_xpc_type_dictionary);
XPC_EXPORT XPC_TYPE(_xpc_type_error);
XPC_EXPORT XPC_TYPE(_xpc_type_connection);
XPC_EXPORT XPC_TYPE(_xpc_type_endpoint);

#define XPC_TYPE_NULL       (&_xpc_type_null)
#define XPC_TYPE_BOOL       (&_xpc_type_bool)
#define XPC_TYPE_INT64      (&_xpc_type_int64)
#define XPC_TYPE_UINT64     (&_xpc_type_uint64)
#define XPC_TYPE_DOUBLE     (&_xpc_type_double)
#define XPC_TYPE_DATE       (&_xpc_type_date)
#define XPC_TYPE_DATA       (&_xpc_type_data)
#define XPC_TYPE_STRING     (&_xpc_type_string)
#define XPC_TYPE_UUID       (&_xpc_type_uuid)
#define XPC_TYPE_ARRAY      (&_xpc_type_array)
#define XPC_TYPE_DICTIONARY (&_xpc_type_dictionary)
#define XPC_TYPE_ERROR      (&_xpc_type_error)
#define XPC_TYPE_CONNECTION (&_xpc_type_connection)
#define XPC_TYPE_ENDPOINT   (&_xpc_type_endpoint)

XPC_EXPORT const struct _xpc_bool_s _xpc_bool_true;
XPC_EXPORT const struct _xpc_bool_s _xpc_bool_false;
#define XPC_BOOL_TRUE  ((xpc_object_t)&_xpc_bool_true)
#define XPC_BOOL_FALSE ((xpc_object_t)&_xpc_bool_false)

#define XPC_ARRAY_APPEND ((size_t)(-1))

XPC_EXPORT xpc_object_t xpc_retain(xpc_object_t object);
XPC_EXPORT void xpc_release(xpc_object_t object);
XPC_EXPORT xpc_type_t xpc_get_type(xpc_object_t object);
XPC_EXPORT const char *xpc_type_get_name(xpc_type_t type);
XPC_EXPORT xpc_object_t xpc_copy(xpc_object_t object);
XPC_EXPORT bool xpc_equal(xpc_object_t object1, xpc_object_t object2);
XPC_EXPORT size_t xpc_hash(xpc_object_t object);
XPC_EXPORT char *xpc_copy_description(xpc_object_t object);

XPC_EXPORT xpc_object_t xpc_null_create(void);
XPC_EXPORT xpc_object_t xpc_bool_create(bool value);
XPC_EXPORT bool xpc_bool_get_value(xpc_object_t xbool);
XPC_EXPORT xpc_object_t xpc_int64_create(int64_t value);
XPC_EXPORT int64_t xpc_int64_get_value(xpc_object_t xint);
XPC_EXPORT xpc_object_t xpc_uint64_create(uint64_t value);
XPC_EXPORT uint64_t xpc_uint64_get_value(xpc_object_t xuint);
XPC_EXPORT xpc_object_t xpc_double_create(double value);
XPC_EXPORT double xpc_double_get_value(xpc_object_t xdouble);
XPC_EXPORT xpc_object_t xpc_date_create(int64_t interval);
XPC_EXPORT xpc_object_t xpc_date_create_from_current(void);
XPC_EXPORT int64_t xpc_date_get_value(xpc_object_t xdate);
XPC_EXPORT xpc_object_t xpc_data_create(const void *bytes, size_t length);
XPC_EXPORT size_t xpc_data_get_length(xpc_object_t xdata);
XPC_EXPORT const void *xpc_data_get_bytes_ptr(xpc_object_t xdata);
XPC_EXPORT size_t xpc_data_get_bytes(xpc_object_t xdata, void *buffer, size_t off, size_t length);
XPC_EXPORT xpc_object_t xpc_string_create(const char *string);
XPC_EXPORT xpc_object_t xpc_string_create_with_format(const char *fmt, ...) __printflike(1, 2);
XPC_EXPORT size_t xpc_string_get_length(xpc_object_t xstring);
XPC_EXPORT const char *xpc_string_get_string_ptr(xpc_object_t xstring);
XPC_EXPORT xpc_object_t xpc_uuid_create(const uuid_t uuid);
XPC_EXPORT const uint8_t *xpc_uuid_get_bytes(xpc_object_t xuuid);

XPC_EXPORT xpc_object_t xpc_array_create(xpc_object_t const *objects, size_t count);
XPC_EXPORT xpc_object_t xpc_array_create_empty(void);
XPC_EXPORT size_t xpc_array_get_count(xpc_object_t xarray);
XPC_EXPORT xpc_object_t xpc_array_get_value(xpc_object_t xarray, size_t index);
XPC_EXPORT void xpc_array_set_value(xpc_object_t xarray, size_t index, xpc_object_t value);
XPC_EXPORT void xpc_array_append_value(xpc_object_t xarray, xpc_object_t value);
XPC_EXPORT bool xpc_array_get_bool(xpc_object_t xarray, size_t index);
XPC_EXPORT int64_t xpc_array_get_int64(xpc_object_t xarray, size_t index);
XPC_EXPORT uint64_t xpc_array_get_uint64(xpc_object_t xarray, size_t index);
XPC_EXPORT double xpc_array_get_double(xpc_object_t xarray, size_t index);
XPC_EXPORT const char *xpc_array_get_string(xpc_object_t xarray, size_t index);
XPC_EXPORT void xpc_array_set_bool(xpc_object_t xarray, size_t index, bool value);
XPC_EXPORT void xpc_array_set_int64(xpc_object_t xarray, size_t index, int64_t value);
XPC_EXPORT void xpc_array_set_uint64(xpc_object_t xarray, size_t index, uint64_t value);
XPC_EXPORT void xpc_array_set_string(xpc_object_t xarray, size_t index, const char *string);
#ifdef __BLOCKS__
typedef bool (^xpc_array_applier_t)(size_t index, xpc_object_t value);
XPC_EXPORT bool xpc_array_apply(xpc_object_t xarray, xpc_array_applier_t applier);
#endif

XPC_EXPORT xpc_object_t xpc_dictionary_create(const char *const *keys,
    const xpc_object_t *values, size_t count);
XPC_EXPORT xpc_object_t xpc_dictionary_create_empty(void);
XPC_EXPORT size_t xpc_dictionary_get_count(xpc_object_t xdict);
XPC_EXPORT xpc_object_t xpc_dictionary_get_value(xpc_object_t xdict, const char *key);
XPC_EXPORT void xpc_dictionary_set_value(xpc_object_t xdict, const char *key, xpc_object_t value);
XPC_EXPORT bool xpc_dictionary_get_bool(xpc_object_t xdict, const char *key);
XPC_EXPORT int64_t xpc_dictionary_get_int64(xpc_object_t xdict, const char *key);
XPC_EXPORT uint64_t xpc_dictionary_get_uint64(xpc_object_t xdict, const char *key);
XPC_EXPORT double xpc_dictionary_get_double(xpc_object_t xdict, const char *key);
XPC_EXPORT const char *xpc_dictionary_get_string(xpc_object_t xdict, const char *key);
XPC_EXPORT const void *xpc_dictionary_get_data(xpc_object_t xdict, const char *key, size_t *length);
XPC_EXPORT int xpc_dictionary_dup_fd(xpc_object_t xdict, const char *key);
XPC_EXPORT xpc_object_t xpc_dictionary_get_dictionary(xpc_object_t xdict, const char *key);
XPC_EXPORT xpc_object_t xpc_dictionary_get_array(xpc_object_t xdict, const char *key);
XPC_EXPORT void xpc_dictionary_set_bool(xpc_object_t xdict, const char *key, bool value);
XPC_EXPORT void xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value);
XPC_EXPORT void xpc_dictionary_set_uint64(xpc_object_t xdict, const char *key, uint64_t value);
XPC_EXPORT void xpc_dictionary_set_double(xpc_object_t xdict, const char *key, double value);
XPC_EXPORT void xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *string);
XPC_EXPORT void xpc_dictionary_set_data(xpc_object_t xdict, const char *key, const void *bytes, size_t length);
#ifdef __BLOCKS__
typedef bool (^xpc_dictionary_applier_t)(const char *key, xpc_object_t value);
XPC_EXPORT bool xpc_dictionary_apply(xpc_object_t xdict, xpc_dictionary_applier_t applier);
#endif
XPC_EXPORT xpc_connection_t xpc_dictionary_get_remote_connection(xpc_object_t xdict);
XPC_EXPORT xpc_object_t xpc_dictionary_create_reply(xpc_object_t original);

__END_DECLS

#include <xpc/connection.h>

#endif
