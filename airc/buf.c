/*
 * buf.c - dynamic string buffer
 *
 * Plan 9 style growable buffer for building strings,
 * JSON payloads, and accumulating output.
 */

#include "airc.h"

enum { Initcap = 256 };

void
bufinit(Buf *b)
{
	b->s = emalloc(Initcap);
	b->s[0] = '\0';
	b->len = 0;
	b->cap = Initcap;
}

void
bufgrow(Buf *b, int n)
{
	int need;

	need = b->len + n + 1;
	if(need <= b->cap)
		return;
	while(b->cap < need)
		b->cap *= 2;
	b->s = erealloc(b->s, b->cap);
}

void
bufadd(Buf *b, char *s, int len)
{
	if(len <= 0)
		return;
	bufgrow(b, len);
	memcpy(b->s + b->len, s, len);
	b->len += len;
	b->s[b->len] = '\0';
}

void
bufaddstr(Buf *b, char *s)
{
	if(s == NULL)
		return;
	bufadd(b, s, strlen(s));
}

void
bufaddc(Buf *b, int c)
{
	bufgrow(b, 1);
	b->s[b->len++] = c;
	b->s[b->len] = '\0';
}

void
bufaddfmt(Buf *b, char *fmt, ...)
{
	va_list ap;
	char tmp[4096];
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if(n > 0)
		bufadd(b, tmp, n);
}

void
bufreset(Buf *b)
{
	b->len = 0;
	if(b->s != NULL)
		b->s[0] = '\0';
}

void
buffree(Buf *b)
{
	free(b->s);
	b->s = NULL;
	b->len = 0;
	b->cap = 0;
}

char*
bufstr(Buf *b)
{
	return b->s;
}
