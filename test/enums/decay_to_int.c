// RUN: %check -e %s

enum E
{
	X, Y, Z
};

main()
{
	__typeof((enum E)0) x;

	x(); // CHECK: error: identifier-expression (type 'int') not callable
}
