struct A
{
	int i, j;
} a[] = {},
  b[] = { 1 },
	c[] = { 1, 2 },
	d[] = { { 1, 2,} },
	e[] = { { 1, 2, 3 } },
	f[] = { { 1, 2, }, /* ignored -> */{ 4, 5 } };
