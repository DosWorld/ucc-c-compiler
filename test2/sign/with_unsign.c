// RUN: %check %s

f(signed s, unsigned u)
{
	(void)(s == u); // CHECK: warning: signed and unsigned types in '=='
	return 1 ? s : u; // CHECK: warning: signed and unsigned types in '?:'
}
