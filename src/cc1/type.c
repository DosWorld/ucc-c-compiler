#include "type.h"

static int type_convertible(enum type_primitive p)
{
	switch(p){
		case type__Bool:
		case type_char:
		case type_int:   case type_uint:
		case type_short: case type_ushort:
		case type_long:  case type_ulong:
		case type_llong: case type_ullong:
		case type_float:
		case type_double:
		case type_ldouble:
			return 1;
		default:
			return 0;
	}
}

enum type_cmp type_cmp(const type *a, const type *b)
{
	switch(a->primitive){
		case type_void:
			return b->primitive == type_void ? TYPE_EQUAL : TYPE_NOT_EQUAL;

		case type_struct:
		case type_union:
		case type_enum:
			return a->sue == b->sue ? TYPE_EQUAL : TYPE_NOT_EQUAL;

		case type_unknown:
			ICE("type unknown in %s", __func__);

		default:
			if(a->primitive == b->primitive)
				return TYPE_EQUAL;

			if(type_convertible(a->type) == type_convertible(b->type))
				return TYPE_CONVERTIBLE;

			return TYPE_NOT_EQUAL;
	}

	ucc_unreach(TYPE_NOT_EQUAL);
}

int type_is_signed(const type *t)
{
	switch(a->primitive){
		case type_char:
			/* XXX: note we treat char as signed */
			/* TODO: -fsigned-char */
		case type_int:
		case type_short:
		case type_long:
		case type_llong:
		case type_float:
		case type_double:
		case type_ldouble:
			return 1;

		case type_void:
		case type__Bool:
		case type_uchar:
		case type_uint:
		case type_ushort:
		case type_ulong:
		case type_ullong:
		case type_struct:
		case type_union:
		case type_enum:
			return 0;

		case type_unknown:
			ICE("type_unknown in %s", __func__);
	}
}
