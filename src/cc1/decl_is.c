static type_ref *type_ref_skip_tdefs(type_ref *r)
{
	for(; r->type == type_ref_tdef; r = r->ref);
	return r;
}

type_ref *type_ref_is(type_ref *r, enum type_ref_type t, ...)
{
	r = type_ref_skip_tdefs(r);

	if(r->type != t)
		return NULL;

	if(r->type == type_ref_type){
		/* extra checks for a type */
		va_list l;
		enum type_primitive p;

		va_start(l, t);
		p = va_arg(l, enum type_primitive);
		va_end(l);

		if(r->bits.type->primitive != p)
			return NULL;
	}

	return r;
}

type_ref *decl_is(decl *d, enum type_ref_type t)
{
	return type_ref_is(d->ref, t);
}

int decl_is_ptr(decl *d)
{
	return !!decl_is(d, type_ref_ptr);
}

int decl_is_block(decl *d)
{
	return !!decl_is(d, type_ref_block);
}

int decl_is_func(decl *d)
{
	return !!decl_is(d, type_ref_func);
}

enum type_primitive type_ref_type_primitive(decl *d)
{
	const type_ref *r = type_ref_skip_tdefs(d->ref);
	return r->type == type_ref_type ? r->bits.type->primitive : type_unknown;
}

int decl_is_struct_or_union(decl *d)
{
	enum type_primitive t = type_ref_type_primitive(d);
	return t == type_struct || t == type_union;
}

int decl_is_fptr(decl *d)
{
	return d->ref->type == type_ref_ptr
		&& d->ref->ref->type == type_ref_func
		&& d->ref->ref->ref->type == type_ref_type;
}

int decl_is_void_ptr(decl *d)
{
	return decl_is_ptr(d)
		&& d->ref->ref->type == type_ref_type
		&& d->ref->ref->bits.type->primitive == type_void;
}

int decl_is_integral(decl *d)
{
	if(d->ref->type != type_ref_type)
		return 0;

	switch(d->ref->bits.type->primitive){
		case type_int:
		case type_char:
		case type__Bool:
		case type_short:
		case type_long:
		case type_llong:
		case type_enum:
		case type_intptr_t:
		case type_ptrdiff_t:
				return 1;

		case type_unknown:
		case type_void:
		case type_struct:
		case type_union:
		case type_float:
		case type_double:
		case type_ldouble:
				break;
	}

	return 0;
}

int type_ref_is_complete(const type_ref *r)
{
	/* decl is "void" or incomplete-struct or array[] */
	switch(r->type){
		case type_ref_type:
		{
			type *t = r->bits.type;

			switch(t->primitive){
				case type_void:
					return 0;
				case type_struct:
				case type_union:
				case type_enum:
					return !sue_incomplete(t->sue);

				default:break;
			}

			break;
		}

		case type_ref_array:
		{
			intval iv;

			const_fold_need_val(r->bits.array_size, &iv);

			return iv.val != 0 && type_ref_is_complete(r->ref);
		}

		default:break;
	}


	return 1;
}

int decl_complete(decl *d)
{
	return type_ref_is_complete((const type_ref *)d->ref);
}

#if 0
int decl_is_struct_or_union_possible_ptr(decl *d)
{
	return (d->type->primitive == type_struct || d->type->primitive == type_union);
}

int decl_is_struct_or_union_ptr(decl *d)
{
	return decl_is_struct_or_union_possible_ptr(d) && decl_is_ptr(d);
}

int decl_is_const(decl *d)
{
	/* const char *x is not const. char *const x is */
	decl_desc *dp = decl_leaf(d);
	if(dp)
		switch(dp->type){
			case decl_desc_ptr:
			case decl_desc_block:
				return dp->bits.qual & qual_const;
			default:
				break;
		}

	return d->type->qual & qual_const;
}

int decl_is_floating(decl *d)
{
	if(d->desc)
		return 0;

	switch(d->type->primitive){
		case type_float:
		case type_double:
		case type_ldouble:
			return 1;
		default:
			break;
	}
	return 0;
}

int decl_is_callable(decl *d)
{
	decl_desc *dp, *pre;

	for(pre = NULL, dp = d->desc; dp && dp->child; pre = dp, dp = dp->child);

	if(!dp)
		return 0;

	switch(dp->type){
		case decl_desc_block:
		case decl_desc_ptr:
			return pre && pre->type == decl_desc_func; /* ptr to func */

		case decl_desc_func:
			return 1;

		default:
			break;
	}

	return 0;
}

int decl_is_fptr(decl *d)
{
	decl_desc *dp, *prev;

	for(prev = NULL, dp = d->desc;
			dp && dp->child;
			prev = dp, dp = dp->child);

	return dp
		&& prev
		&& prev->type == decl_desc_func
		&& (dp->type == decl_desc_ptr || dp->type == decl_desc_block);
}

int decl_is_array(decl *d)
{
	decl_desc *dp = decl_desc_tail(d);
	return dp ? dp->type == decl_desc_array : 0;
}

int decl_has_array(decl *d)
{
	decl_desc *dp;

	ITER_DESC_TYPE(d, dp, decl_desc_array)
		return 1;

	return 0;
}

int decl_is_incomplete_array(decl *d)
{
	decl_desc *tail = decl_desc_tail(d);

	if(tail && tail->type == decl_desc_array){
		intval iv;

		const_fold_need_val(tail->bits.array_size, &iv);

		return iv.val == 0;
	}
	return 0;
}

#endif
