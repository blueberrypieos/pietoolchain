/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct parser {
	const char *p, *end;
};

static void
skip_space(struct parser *ps)
{
	for (;;) {
		while (ps->p < ps->end && isspace((unsigned char)*ps->p)) {
			ps->p++;
		}
		size_t left = (size_t)(ps->end - ps->p);
		if (left >= 4 && memcmp(ps->p, "<!--", 4) == 0) {
			const char *q = memmem(ps->p + 4, left - 4, "-->", 3);
			ps->p = q ? q + 3 : ps->end;
		} else if (left >= 2 && (memcmp(ps->p, "<?", 2) == 0 || memcmp(ps->p, "<!", 2) == 0)) {
			const char *q = memchr(ps->p, '>', left);
			ps->p = q ? q + 1 : ps->end;
		} else {
			return;
		}
	}
}

static bool
read_tag(struct parser *ps, char *name, size_t size, bool *closing, bool *empty)
{
	skip_space(ps);
	if (ps->p >= ps->end || *ps->p != '<') {
		return false;
	}
	const char *q = memchr(ps->p, '>', (size_t)(ps->end - ps->p));
	if (q == NULL) {
		return false;
	}
	const char *s = ps->p + 1;
	*closing = (*s == '/');
	if (*closing) {
		s++;
	}
	*empty = (q[-1] == '/');
	size_t n = 0;
	while (s < q && !isspace((unsigned char)*s) && *s != '/' && n + 1 < size) {
		name[n++] = *s++;
	}
	name[n] = '\0';
	ps->p = q + 1;
	return n > 0;
}

static char *
read_text(struct parser *ps, const char *name, size_t *outlen)
{
	char close[64];
	int cl = snprintf(close, sizeof(close), "</%s>", name);
	const char *start = ps->p;
	const char *end = NULL;
	for (const char *s = start; s < ps->end; s++) {
		if (*s == '<' && (size_t)(ps->end - s) >= 9 && memcmp(s, "<![CDATA[", 9) == 0) {
			const char *q = memmem(s + 9, (size_t)(ps->end - s - 9), "]]>", 3);
			if (q == NULL) {
				return NULL;
			}
			s = q + 2;
		} else if (*s == '<') {
			if ((size_t)(ps->end - s) >= (size_t)cl && memcmp(s, close, (size_t)cl) == 0) {
				end = s;
			}
			break;
		}
	}
	if (end == NULL) {
		return NULL;
	}
	char *out = malloc((size_t)(end - start) + 1);
	if (out == NULL) {
		return NULL;
	}
	size_t n = 0;
	for (const char *s = start; s < end;) {
		if (*s == '<' && memcmp(s, "<![CDATA[", 9) == 0) {
			const char *q = memmem(s + 9, (size_t)(end - s - 9), "]]>", 3);
			memcpy(out + n, s + 9, (size_t)(q - s - 9));
			n += (size_t)(q - s - 9);
			s = q + 3;
		} else if (*s == '&') {
			const char *semi = memchr(s, ';', (size_t)(end - s));
			if (semi == NULL) {
				out[n++] = *s++;
				continue;
			}
			size_t elen = (size_t)(semi - s - 1);
			const char *e = s + 1;
			unsigned long cp = 0;
			if (elen == 2 && memcmp(e, "lt", 2) == 0) cp = '<';
			else if (elen == 2 && memcmp(e, "gt", 2) == 0) cp = '>';
			else if (elen == 3 && memcmp(e, "amp", 3) == 0) cp = '&';
			else if (elen == 4 && memcmp(e, "quot", 4) == 0) cp = '"';
			else if (elen == 4 && memcmp(e, "apos", 4) == 0) cp = '\'';
			else if (elen > 1 && e[0] == '#') {
				cp = (e[1] == 'x') ? strtoul(e + 2, NULL, 16) : strtoul(e + 1, NULL, 10);
			}
			if (cp == 0) {
				out[n++] = *s++;
				continue;
			}
			if (cp < 0x80) {
				out[n++] = (char)cp;
			} else if (cp < 0x800) {
				out[n++] = (char)(0xc0 | (cp >> 6));
				out[n++] = (char)(0x80 | (cp & 0x3f));
			} else if (cp < 0x10000) {
				out[n++] = (char)(0xe0 | (cp >> 12));
				out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
				out[n++] = (char)(0x80 | (cp & 0x3f));
			} else {
				out[n++] = (char)(0xf0 | (cp >> 18));
				out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
				out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
				out[n++] = (char)(0x80 | (cp & 0x3f));
			}
			s = semi + 1;
		} else {
			out[n++] = *s++;
		}
	}
	out[n] = '\0';
	ps->p = end + cl;
	if (outlen) {
		*outlen = n;
	}
	return out;
}

static int
b64val(int c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static xpc_object_t
decode_base64(const char *text)
{
	size_t len = strlen(text);
	uint8_t *out = malloc(len * 3 / 4 + 3);
	if (out == NULL) {
		return NULL;
	}
	size_t n = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (const char *s = text; *s && *s != '='; s++) {
		int v = b64val((unsigned char)*s);
		if (v < 0) {
			continue;
		}
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[n++] = (uint8_t)(acc >> bits);
		}
	}
	xpc_object_t data = xpc_data_create(out, n);
	free(out);
	return data;
}

static xpc_object_t
decode_date(const char *text)
{
	struct tm tm = { 0 };
	if (sscanf(text, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
	    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		return NULL;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	return xpc_date_create((int64_t)timegm(&tm) * 1000000000);
}

static xpc_object_t parse_value(struct parser *ps, int depth);

static xpc_object_t
parse_dict(struct parser *ps, int depth)
{
	xpc_object_t dict = xpc_dictionary_create_empty();
	for (;;) {
		char name[32];
		bool closing, empty;
		const char *save = ps->p;
		if (!read_tag(ps, name, sizeof(name), &closing, &empty)) {
			break;
		}
		if (closing && strcmp(name, "dict") == 0) {
			return dict;
		}
		if (closing || strcmp(name, "key") != 0) {
			break;
		}
		char *key = empty ? strdup("") : read_text(ps, "key", NULL);
		if (key == NULL) {
			break;
		}
		xpc_object_t value = parse_value(ps, depth + 1);
		if (value == NULL) {
			free(key);
			break;
		}
		xpc_dictionary_set_value(dict, key, value);
		xpc_release(value);
		free(key);
		(void)save;
	}
	xpc_release(dict);
	return NULL;
}

static xpc_object_t
parse_array(struct parser *ps, int depth)
{
	xpc_object_t array = xpc_array_create_empty();
	for (;;) {
		skip_space(ps);
		if ((size_t)(ps->end - ps->p) >= 8 && memcmp(ps->p, "</array>", 8) == 0) {
			ps->p += 8;
			return array;
		}
		xpc_object_t value = parse_value(ps, depth + 1);
		if (value == NULL) {
			break;
		}
		xpc_array_append_value(array, value);
		xpc_release(value);
	}
	xpc_release(array);
	return NULL;
}

static xpc_object_t
parse_value(struct parser *ps, int depth)
{
	char name[32];
	bool closing, empty;

	if (depth > 128 || !read_tag(ps, name, sizeof(name), &closing, &empty) || closing) {
		return NULL;
	}
	if (strcmp(name, "true") == 0 && empty) {
		return XPC_BOOL_TRUE;
	}
	if (strcmp(name, "false") == 0 && empty) {
		return XPC_BOOL_FALSE;
	}
	if (strcmp(name, "dict") == 0) {
		return empty ? xpc_dictionary_create_empty() : parse_dict(ps, depth);
	}
	if (strcmp(name, "array") == 0) {
		return empty ? xpc_array_create_empty() : parse_array(ps, depth);
	}
	if (strcmp(name, "plist") == 0) {
		xpc_object_t v = parse_value(ps, depth + 1);
		bool c2, e2;
		char end[32];
		if (v != NULL && (!read_tag(ps, end, sizeof(end), &c2, &e2) || !c2 || strcmp(end, "plist") != 0)) {
			xpc_release(v);
			return NULL;
		}
		return v;
	}

	size_t len = 0;
	char *text = empty ? strdup("") : read_text(ps, name, &len);
	if (text == NULL) {
		return NULL;
	}
	xpc_object_t v = NULL;
	if (strcmp(name, "string") == 0) {
		v = xpc_string_create(text);
	} else if (strcmp(name, "integer") == 0) {
		char *endp;
		errno = 0;
		const char *t = text;
		while (isspace((unsigned char)*t)) t++;
		if (*t == '-') {
			long long n = strtoll(t, &endp, 0);
			v = (errno == 0) ? xpc_int64_create(n) : NULL;
		} else {
			unsigned long long n = strtoull(t, &endp, 0);
			if (errno == 0) {
				v = n > INT64_MAX ? xpc_uint64_create(n) : xpc_int64_create((int64_t)n);
			}
		}
	} else if (strcmp(name, "real") == 0) {
		v = xpc_double_create(strtod(text, NULL));
	} else if (strcmp(name, "data") == 0) {
		v = decode_base64(text);
	} else if (strcmp(name, "date") == 0) {
		v = decode_date(text);
	}
	free(text);
	return v;
}

xpc_object_t
xpc_create_from_plist(const void *data, size_t length)
{
	if (data == NULL || length < 8) {
		return NULL;
	}
	if (memcmp(data, "bplist", 6) == 0) {
		return NULL;
	}
	struct parser ps = { data, (const char *)data + length };
	return parse_value(&ps, 0);
}
