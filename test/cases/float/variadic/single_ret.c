// RUN: %ucc -o %t %s
// RUN: %t | grep '^3$'

int printf(const char *, ...) __attribute__((format(printf, 1, 2)));

double f(int a, ...)
{
	__builtin_va_list l;

	__builtin_va_start(l, a);

	double r = __builtin_va_arg(l, double);

	__builtin_va_end(l);

	return r;
}

main()
{
	printf("%.0f\n", f(2, (float)3));
}
