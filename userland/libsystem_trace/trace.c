/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <os/log.h>
#include <os/log_private.h>
#include <os/trace_private.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct os_log_s {
	const char *subsystem;
	const char *category;
	bool disabled;
};

struct os_log_s _os_log_default = { NULL, NULL, false };
struct os_log_s _os_log_disabled = { NULL, NULL, true };

static int mirror_all = -1;

void
_libtrace_init(void)
{
}

void
_libtrace_fork_child(void)
{
}

bool
_os_trace_basesystem_storage_available(void)
{
	return true;
}

os_log_t
os_log_create(const char *subsystem, const char *category)
{
	struct os_log_s *log = calloc(1, sizeof(*log));
	if (log == NULL) {
		return &_os_log_default;
	}
	log->subsystem = subsystem ? strdup(subsystem) : NULL;
	log->category = category ? strdup(category) : NULL;
	return log;
}

static bool
mirroring_all(void)
{
	if (mirror_all < 0) {
		mirror_all = getenv("OS_ACTIVITY_DT_MODE") != NULL;
	}
	return mirror_all;
}

static os_trace_mode_t trace_mode;

void
os_trace_set_mode(os_trace_mode_t mode)
{
	trace_mode = mode;
}

os_trace_mode_t
os_trace_get_mode(void)
{
	return trace_mode;
}

static bool
logging_off(void)
{
	return (trace_mode & (OS_TRACE_MODE_DISABLE | OS_TRACE_MODE_OFF)) != 0;
}

bool
os_log_type_enabled(os_log_t log, os_log_type_t type)
{
	if (log == NULL || log->disabled || logging_off()) {
		return false;
	}
	return type == OS_LOG_TYPE_DEFAULT || type == OS_LOG_TYPE_ERROR ||
	       type == OS_LOG_TYPE_FAULT || mirroring_all();
}

enum { ARG_SCALAR, ARG_COUNT, ARG_STRING, ARG_POINTER, ARG_OBJECT, ARG_WSTRING, ARG_ERRNO, ARG_MASK };

struct reader {
	const uint8_t *p, *end;
};

struct arg {
	unsigned kind;
	uint8_t size;
	uint64_t value;
};

static bool
next_arg(struct reader *r, struct arg *a)
{
	if (r->p == NULL || r->p + 2 > r->end) {
		return false;
	}
	a->kind = r->p[0] >> 4;
	a->size = r->p[1];
	const uint8_t *data = r->p + 2;
	if (data + a->size > r->end) {
		return false;
	}
	a->value = 0;
	memcpy(&a->value, data, a->size < sizeof(a->value) ? a->size : sizeof(a->value));
	r->p = data + a->size;
	return true;
}

struct out {
	char *buf;
	size_t size, len;
};

static void
put(struct out *o, const char *s, size_t n)
{
	while (n-- > 0 && o->len + 1 < o->size) {
		o->buf[o->len++] = *s++;
	}
	o->buf[o->len] = '\0';
}

static void
putf(struct out *o, const char *spec, ...)
{
	char tmp[512];
	va_list ap;

	va_start(ap, spec);
	int n = vsnprintf(tmp, sizeof(tmp), spec, ap);
	va_end(ap);
	if (n > 0) {
		put(o, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
	}
}

static void
compose(struct out *o, const char *fmt, const uint8_t *buf, size_t bufsize, int saved_errno)
{
	struct reader r = { NULL, NULL };
	if (buf != NULL && bufsize >= 2) {
		r.p = buf + 2;
		r.end = buf + bufsize;
	}

	for (const char *p = fmt; *p != '\0';) {
		if (*p != '%') {
			const char *q = strchr(p, '%');
			size_t n = q ? (size_t)(q - p) : strlen(p);
			put(o, p, n);
			p += n;
			continue;
		}
		if (p[1] == '%') {
			put(o, "%", 1);
			p += 2;
			continue;
		}

		char spec[64];
		size_t sl = 0;
		spec[sl++] = '%';
		p++;
		if (*p == '{') {
			const char *q = strchr(p, '}');
			p = q ? q + 1 : p + strlen(p);
		}
		struct arg a;
		while (*p != '\0' && strchr("-+ #0'", *p) && sl < 16) {
			spec[sl++] = *p++;
		}
		if (*p == '*') {
			p++;
			int w = next_arg(&r, &a) ? (int)a.value : 0;
			sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", w);
		}
		while (*p >= '0' && *p <= '9' && sl < 32) {
			spec[sl++] = *p++;
		}
		if (*p == '.') {
			spec[sl++] = *p++;
			if (*p == '*') {
				p++;
				int prec = next_arg(&r, &a) ? (int)a.value : 0;
				sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", prec);
			}
			while (*p >= '0' && *p <= '9' && sl < 40) {
				spec[sl++] = *p++;
			}
		}
		while (*p != '\0' && strchr("hljztLq", *p)) {
			p++;
		}
		char conv = *p;
		if (conv == '\0') {
			break;
		}
		p++;

		if (conv == 'm') {
			put(o, strerror(saved_errno), strlen(strerror(saved_errno)));
			if (r.p != NULL && r.p + 2 <= r.end && (r.p[0] >> 4) == ARG_ERRNO) {
				next_arg(&r, &a);
			}
			continue;
		}
		if (!next_arg(&r, &a)) {
			put(o, "<?>", 3);
			continue;
		}
		switch (conv) {
		case 's':
			spec[sl++] = 's';
			spec[sl] = '\0';
			putf(o, spec, a.value ? (const char *)(uintptr_t)a.value : "(null)");
			break;
		case '@':
			putf(o, "<object %p>", (void *)(uintptr_t)a.value);
			break;
		case 'P':
			put(o, "<data>", 6);
			break;
		case 'p':
			putf(o, "%p", (void *)(uintptr_t)a.value);
			break;
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
			double d;
			memcpy(&d, &a.value, sizeof(d));
			spec[sl++] = conv;
			spec[sl] = '\0';
			putf(o, spec, d);
			break;
		}
		case 'c':
			spec[sl++] = 'c';
			spec[sl] = '\0';
			putf(o, spec, (int)a.value);
			break;
		case 'd': case 'i': {
			long long v = a.size == 1 ? (int8_t)a.value : a.size == 2 ? (int16_t)a.value :
			    a.size == 4 ? (int32_t)a.value : (long long)a.value;
			spec[sl++] = 'l';
			spec[sl++] = 'l';
			spec[sl++] = conv;
			spec[sl] = '\0';
			putf(o, spec, v);
			break;
		}
		case 'u': case 'o': case 'x': case 'X':
			spec[sl++] = 'l';
			spec[sl++] = 'l';
			spec[sl++] = conv;
			spec[sl] = '\0';
			putf(o, spec, (unsigned long long)a.value);
			break;
		default:
			put(o, "<?>", 3);
			break;
		}
	}
}

static int
severity(os_log_type_t type)
{
	switch (type) {
	case OS_LOG_TYPE_FAULT:
		return 2;
	case OS_LOG_TYPE_ERROR:
		return 3;
	case OS_LOG_TYPE_DEFAULT:
		return 5;
	default:
		return -1;
	}
}

static void
send_syslog(int sev, const char *text)
{
	static int fd = -1;
	if (fd < 0) {
		int s = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (s < 0) {
			return;
		}
		fcntl(s, F_SETFD, FD_CLOEXEC);
		fcntl(s, F_SETFL, O_NONBLOCK);
		if (!__sync_bool_compare_and_swap(&fd, -1, s)) {
			close(s);
		}
	}
	struct sockaddr_un sun = { .sun_len = sizeof(sun), .sun_family = AF_UNIX };
	strlcpy(sun.sun_path, "/var/run/syslog", sizeof(sun.sun_path));
	char line[1200], when[16];
	time_t now = time(NULL);
	struct tm tm;
	strftime(when, sizeof(when), "%b %e %H:%M:%S", localtime_r(&now, &tm));
	int n = snprintf(line, sizeof(line), "<%d>%s %s", 8 + sev, when, text);
	if (n > 0) {
		sendto(fd, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1, 0,
		    (struct sockaddr *)&sun, sizeof(sun));
	}
}

static void
send_message(os_log_t log, os_log_type_t type, const char *msg)
{
	int sev = severity(type);
	if (log == NULL || log->disabled || (sev < 0 && !mirroring_all())) {
		return;
	}
	char text[1100];
	const char *name = getprogname();
	int n;
	if (log->subsystem != NULL) {
		n = snprintf(text, sizeof(text), "%s[%d]: (%s%s%s) %s", name ? name : "?", getpid(),
		    log->subsystem, log->category ? ":" : "", log->category ? log->category : "", msg);
	} else {
		n = snprintf(text, sizeof(text), "%s[%d]: %s", name ? name : "?", getpid(), msg);
	}
	if (n <= 0) {
		return;
	}
	if (mirroring_all()) {
		size_t len = (size_t)n < sizeof(text) - 1 ? (size_t)n : sizeof(text) - 2;
		text[len] = '\n';
		write(STDERR_FILENO, text, len + 1);
	} else {
		send_syslog(sev, text);
	}
}

static void
log_impl(os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	int saved_errno = errno;
	char msg[1024];
	struct out o = { msg, sizeof(msg), 0 };

	if (logging_off()) {
		return;
	}
	msg[0] = '\0';
	compose(&o, format, buf, size, saved_errno);
	send_message(log, type, msg);
	errno = saved_errno;
}

void
_os_log_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	log_impl(log, type, format, buf, size);
}

void
_os_log_debug_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	log_impl(log, type, format, buf, size);
}

void
_os_log_error_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	log_impl(log, type, format, buf, size);
}

void
_os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	log_impl(log, type, format, buf, size);
}

#define PACK_DATA_MAX 4096

char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type, char *buf, size_t size)
{
	int saved_errno = errno;
	char msg[1024];
	struct out o = { buf ? buf : msg, buf ? size : sizeof(msg), 0 };

	if (o.size == 0) {
		return buf;
	}
	o.buf[0] = '\0';
	compose(&o, pack->olp_format, pack->olp_data, PACK_DATA_MAX, saved_errno);
	send_message(log, type, o.buf);
	errno = saved_errno;
	return buf;
}

static void
strip_annotations(char *dst, size_t size, const char *fmt)
{
	size_t n = 0;

	for (const char *p = fmt; *p != '\0' && n + 1 < size; p++) {
		dst[n++] = *p;
		if (*p == '%' && p[1] == '{') {
			const char *q = strchr(p + 1, '}');
			if (q == NULL) {
				break;
			}
			p = q;
		} else if (*p == '%' && p[1] == '%') {
			if (n + 1 < size) {
				dst[n++] = '%';
			}
			p++;
		}
	}
	dst[n] = '\0';
}

char *
os_log_send_and_compose(os_log_send_and_compose_flags_t flags, const char **fmt_out, char *buf,
    size_t size, os_log_t log, os_log_type_t type, const char *fmt, ...)
{
	char plain[512];
	char msg[1024];
	char *out = (flags & OS_LOG_F_COMPOSE) && buf ? buf : msg;
	size_t outsize = (flags & OS_LOG_F_COMPOSE) && buf ? size : sizeof(msg);
	va_list ap;

	if (fmt_out != NULL) {
		*fmt_out = fmt;
	}
	strip_annotations(plain, sizeof(plain), fmt);
	va_start(ap, fmt);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	vsnprintf(out, outsize, plain, ap);
#pragma clang diagnostic pop
	va_end(ap);
	if (flags & OS_LOG_F_SEND) {
		send_message(log, type, out);
	}
	return (flags & OS_LOG_F_COMPOSE) ? buf : NULL;
}

bool
os_log_shim_enabled(void *addr)
{
	(void)addr;
	return false;
}

void
os_log_with_args(os_log_t log, os_log_type_t type, const char *format, va_list args, void *ret_addr)
{
	(void)ret_addr;
	int saved_errno = errno;
	char msg[1024];
	vsnprintf(msg, sizeof(msg), format, args);
	send_message(log, type, msg);
	errno = saved_errno;
}

void
os_log_with_args_4syslog(os_log_t log, os_log_type_t type, const char *format, va_list args,
    void *ret_addr)
{
	os_log_with_args(log, type, format, args, ret_addr);
}
