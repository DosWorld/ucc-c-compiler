main()
{
	struct
	{
		int a;
		struct
		{
			int b, c;
		} sub;
	} st;

	st.sub.b = 5;
	//st->sub->b = 3;

	//return st->sub->b;
}
