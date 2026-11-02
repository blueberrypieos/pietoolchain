/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _USBUF_H_
#define _USBUF_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/sbuf.h>
#include <stdarg.h>

__BEGIN_DECLS

struct sbuf *sbuf_new(struct sbuf *s, char *buf, int length, int flags);
#define sbuf_new_auto() sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND)
void sbuf_clear(struct sbuf *s);
int sbuf_setpos(struct sbuf *s, ssize_t pos);
int sbuf_bcat(struct sbuf *s, const void *buf, size_t len);
int sbuf_bcpy(struct sbuf *s, const void *buf, size_t len);
int sbuf_cat(struct sbuf *s, const char *str);
int sbuf_cpy(struct sbuf *s, const char *str);
int sbuf_printf(struct sbuf *s, const char *fmt, ...) __printflike(2, 3);
int sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap) __printflike(2, 0);
int sbuf_putc(struct sbuf *s, int c);
int sbuf_trim(struct sbuf *s);
int sbuf_error(const struct sbuf *s);
int sbuf_finish(struct sbuf *s);
char *sbuf_data(struct sbuf *s);
ssize_t sbuf_len(struct sbuf *s);
int sbuf_done(const struct sbuf *s);
void sbuf_delete(struct sbuf *s);

__END_DECLS

#endif
