/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbuf.h"

#define MIN_SIZE        16

static int
overflow(struct sbuf *s)
{
	s->s_flags |= SBUF_OVERFLOWED;
	return -1;
}

static int
make_room(struct sbuf *s, size_t len)
{
	if ((size_t)s->s_len + len + 1 <= (size_t)s->s_size) {
		return 0;
	}
	if (!(s->s_flags & SBUF_AUTOEXTEND)) {
		return -1;
	}
	size_t size = s->s_size > 0 ? (size_t)s->s_size : MIN_SIZE;
	while ((size_t)s->s_len + len + 1 > size) {
		size *= 2;
	}
	if (size > 0x7fffffff) {
		return -1;
	}
	char *buf;
	if (s->s_flags & SBUF_DYNAMIC) {
		buf = realloc(s->s_buf, size);
	} else {
		buf = malloc(size);
		if (buf != NULL && s->s_len > 0) {
			memcpy(buf, s->s_buf, (size_t)s->s_len);
		}
	}
	if (buf == NULL) {
		return -1;
	}
	s->s_buf = buf;
	s->s_size = (int)size;
	s->s_flags |= SBUF_DYNAMIC;
	return 0;
}

struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int length, int flags)
{
	if (length < 0 || (buf != NULL && length == 0)) {
		errno = EINVAL;
		return NULL;
	}
	int dynstruct = 0;
	if (s == NULL) {
		if ((s = calloc(1, sizeof(*s))) == NULL) {
			return NULL;
		}
		dynstruct = SBUF_DYNSTRUCT;
	} else {
		memset(s, 0, sizeof(*s));
	}
	s->s_flags = (flags & SBUF_USRFLAGMSK) | dynstruct;
	if (buf != NULL) {
		s->s_buf = buf;
		s->s_size = length;
	} else {
		size_t size = length > 0 ? (size_t)length : MIN_SIZE;
		if ((s->s_buf = malloc(size)) == NULL) {
			if (dynstruct) {
				free(s);
			}
			return NULL;
		}
		s->s_size = (int)size;
		s->s_flags |= SBUF_DYNAMIC;
	}
	s->s_buf[0] = '\0';
	return s;
}

void
sbuf_clear(struct sbuf *s)
{
	s->s_len = 0;
	s->s_flags &= ~(SBUF_FINISHED | SBUF_OVERFLOWED);
}

int
sbuf_setpos(struct sbuf *s, ssize_t pos)
{
	if (pos < 0 || pos > s->s_len) {
		return -1;
	}
	s->s_len = (int)pos;
	s->s_flags &= ~(SBUF_FINISHED | SBUF_OVERFLOWED);
	return 0;
}

int
sbuf_bcat(struct sbuf *s, const void *buf, size_t len)
{
	if (s->s_flags & SBUF_OVERFLOWED) {
		return -1;
	}
	if (make_room(s, len) != 0) {
		size_t room = s->s_size > s->s_len + 1 ? (size_t)(s->s_size - s->s_len - 1) : 0;
		memcpy(s->s_buf + s->s_len, buf, room);
		s->s_len += (int)room;
		return overflow(s);
	}
	memcpy(s->s_buf + s->s_len, buf, len);
	s->s_len += (int)len;
	return 0;
}

int
sbuf_bcpy(struct sbuf *s, const void *buf, size_t len)
{
	sbuf_clear(s);
	return sbuf_bcat(s, buf, len);
}

int
sbuf_cat(struct sbuf *s, const char *str)
{
	return sbuf_bcat(s, str, strlen(str));
}

int
sbuf_cpy(struct sbuf *s, const char *str)
{
	sbuf_clear(s);
	return sbuf_cat(s, str);
}

int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{
	if (s->s_flags & SBUF_OVERFLOWED) {
		return -1;
	}
	va_list copy;
	va_copy(copy, ap);
	int n = vsnprintf(s->s_buf + s->s_len, (size_t)(s->s_size - s->s_len), fmt, copy);
	va_end(copy);
	if (n < 0) {
		return overflow(s);
	}
	if (n < s->s_size - s->s_len) {
		s->s_len += n;
		return 0;
	}
	if (make_room(s, (size_t)n) != 0) {
		s->s_len = s->s_size - 1;
		return overflow(s);
	}
	vsnprintf(s->s_buf + s->s_len, (size_t)(s->s_size - s->s_len), fmt, ap);
	s->s_len += n;
	return 0;
}

int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = sbuf_vprintf(s, fmt, ap);
	va_end(ap);
	return r;
}

int
sbuf_putc(struct sbuf *s, int c)
{
	char ch = (char)c;
	return sbuf_bcat(s, &ch, 1);
}

int
sbuf_trim(struct sbuf *s)
{
	if (s->s_flags & SBUF_OVERFLOWED) {
		return -1;
	}
	while (s->s_len > 0 && (s->s_buf[s->s_len - 1] == ' ' || s->s_buf[s->s_len - 1] == '\t' ||
	    s->s_buf[s->s_len - 1] == '\n' || s->s_buf[s->s_len - 1] == '\r')) {
		s->s_len--;
	}
	return 0;
}

int
sbuf_error(const struct sbuf *s)
{
	return (s->s_flags & SBUF_OVERFLOWED) ? ENOMEM : 0;
}

int
sbuf_finish(struct sbuf *s)
{
	s->s_buf[s->s_len] = '\0';
	s->s_flags |= SBUF_FINISHED;
	if (s->s_flags & SBUF_OVERFLOWED) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

char *
sbuf_data(struct sbuf *s)
{
	return s->s_buf;
}

ssize_t
sbuf_len(struct sbuf *s)
{
	return (s->s_flags & SBUF_OVERFLOWED) ? -1 : s->s_len;
}

int
sbuf_done(const struct sbuf *s)
{
	return (s->s_flags & SBUF_FINISHED) != 0;
}

void
sbuf_delete(struct sbuf *s)
{
	if (s->s_flags & SBUF_DYNAMIC) {
		free(s->s_buf);
	}
	if (s->s_flags & SBUF_DYNSTRUCT) {
		free(s);
	} else {
		memset(s, 0, sizeof(*s));
	}
}
