foo(int x)
{
	printf("%d\n", x);
}

main()
{
	int x;

	x = 42;

	asm(
			"mov %[x], %%rdi; call _foo"
			:
			: [x] "m"(x)
		 );

	return 0;
}
