#ifndef UTIL_H
#define UTIL_H

typedef struct where
{
	const char *fname;
	int line, chr;
} where;

void warn_at(struct where *, const char *, ...);
void die_at(struct where *, const char *, ...);
void vdie(struct where *w, va_list l, const char *fmt);
void die(const char *fmt, ...);
void die_ice(const char *, int);
char *fline(FILE *f);
void dynarray_add(void ***, void *);

void ice(const char *f, int line, const char *fmt, ...);
void icw(const char *f, int line, const char *fmt, ...);
#define ICE(...) ice(__FILE__, __LINE__, __VA_ARGS__)
#define ICW(...) icw(__FILE__, __LINE__, __VA_ARGS__)

#endif
