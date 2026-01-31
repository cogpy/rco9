/*
 * util.c - utility functions
 *
 * Memory allocation wrappers, string helpers,
 * path manipulation, and error reporting.
 */

#include "airc.h"

void*
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == NULL)
		fatal("out of memory allocating %lu bytes", n);
	memset(p, 0, n);
	return p;
}

void*
erealloc(void *p, ulong n)
{
	p = realloc(p, n);
	if(p == NULL)
		fatal("out of memory reallocating %lu bytes", n);
	return p;
}

char*
estrdup(char *s)
{
	char *t;

	if(s == NULL)
		return NULL;
	t = emalloc(strlen(s) + 1);
	strcpy(t, s);
	return t;
}

void
fatal(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "airc: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

void
warn(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "airc: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

char*
smprint(char *fmt, ...)
{
	va_list ap;
	char tmp[4096];

	va_start(ap, fmt);
	vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	return estrdup(tmp);
}

int
isterm(int fd)
{
	return isatty(fd);
}

char*
homedir(void)
{
	char *h;

	h = getenv("HOME");
	if(h == NULL || h[0] == '\0')
		h = getenv("home");	/* rc style */
	if(h == NULL || h[0] == '\0')
		fatal("cannot determine home directory");
	return h;
}

char*
pathjoin(char *dir, char *file)
{
	int n;

	n = strlen(dir);
	if(n > 0 && dir[n-1] == '/')
		return smprint("%s%s", dir, file);
	return smprint("%s/%s", dir, file);
}

int
mkdirp(char *path)
{
	char tmp[Maxpath];
	char *p;
	int len;

	len = snprintf(tmp, sizeof tmp, "%s", path);
	if(len >= (int)sizeof tmp)
		return -1;

	/* strip trailing slash */
	if(len > 0 && tmp[len-1] == '/')
		tmp[len-1] = '\0';

	for(p = tmp + 1; *p; p++){
		if(*p == '/'){
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	return mkdir(tmp, 0755);
}

char*
readfile(char *path)
{
	FILE *f;
	long sz;
	char *s;

	f = fopen(path, "r");
	if(f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz < 0){
		fclose(f);
		return NULL;
	}
	s = emalloc(sz + 1);
	if(fread(s, 1, sz, f) < (size_t)sz && ferror(f)){
		free(s);
		fclose(f);
		return NULL;
	}
	s[sz] = '\0';
	fclose(f);
	return s;
}

char*
trim(char *s)
{
	char *end;

	while(isspace((uchar)*s))
		s++;
	if(*s == '\0')
		return s;
	end = s + strlen(s) - 1;
	while(end > s && isspace((uchar)*end))
		end--;
	end[1] = '\0';
	return s;
}
