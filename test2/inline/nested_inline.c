// RUN: %check %s -fshow-inlined
// RUN: %ocheck 20 %s

f(int i)
{
	return 3 + i;
}

g(int i)
{
	return 2 * f(i); // CHECK: note: function inlined
}

main()
{
	return g(7); // CHECK: note: function inlined
}
