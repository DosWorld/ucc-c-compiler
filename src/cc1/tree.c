#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "../util/alloc.h"
#include "../util/util.h"
#include "data_structs.h"
#include "macros.h"
#include "sym.h"
#include "../util/platform.h"
#include "../util/limits.h"
#include "sue.h"
#include "decl.h"
#include "cc1.h"
#include "defs.h"

const where *eof_where = NULL;

numeric *numeric_new(long v)
{
	numeric *num = umalloc(sizeof *num);
	num->val.i = v;
	return num;
}

int numeric_cmp(const numeric *a, const numeric *b)
{
	UCC_ASSERT(
			(a->suffix & VAL_FLOATING) == (b->suffix & VAL_FLOATING),
			"cmp int and float?");

	if(a->suffix & VAL_FLOATING){
		const floating_t fa = a->val.f, fb = b->val.f;

		if(fa > fb)
			return 1;
		if(fa < fb)
			return -1;
		return 0;

	}else{
		const integral_t la = a->val.i, lb = b->val.i;

		if(la > lb)
			return 1;
		if(la < lb)
			return -1;
		return 0;
	}
}

int integral_str(char *buf, size_t nbuf, integral_t v, type_ref *ty)
{
	/* the final resting place for an integral */
	const int is_signed = type_ref_is_signed(ty);

	if(ty){
		sintegral_t sv;
		v = integral_truncate(v, type_ref_size(ty, NULL), &sv);
		if(is_signed)
			v = sv;
	}

	return snprintf(
			buf, nbuf,
			is_signed
				? "%" NUMERIC_FMT_D
				: "%" NUMERIC_FMT_U,
			v, is_signed);
}

integral_t integral_truncate_bits(
		integral_t val, unsigned bits,
		sintegral_t *signed_iv)
{
	integral_t pos_mask = ~(~0ULL << bits);
	integral_t truncated = val & pos_mask;

	if(fopt_mode & FOPT_CAST_W_BUILTIN_TYPES){
		/* we use sizeof our types so our conversions match the target conversions */
#define BUILTIN(type)                    \
		if(bits == sizeof(type) * CHAR_BIT){ \
			if(signed_iv)                      \
				*signed_iv = (signed type)val;   \
			return (unsigned type)val;         \
		}

		BUILTIN(char);
		BUILTIN(short);
		BUILTIN(int);
		BUILTIN(long);
		BUILTIN(long long);
	}

	/* not builtin - bitfield, etc */
	if(signed_iv){
		const unsigned shamt = CHAR_BIT * sizeof(val) - bits;

		/* implementation specific signed right shift.
		 * this is to sign extend the value
		 */
		*signed_iv = (sintegral_t)val << shamt >> shamt;
	}

	return truncated;
}

integral_t integral_truncate(
		integral_t val, unsigned bytes,
		sintegral_t *sign_extended)
{
	return integral_truncate_bits(
			val, bytes * CHAR_BIT,
			sign_extended);
}

int integral_high_bit(integral_t val, type_ref *ty)
{
	if(type_ref_is_signed(ty)){
		const sintegral_t as_signed = val;

		if(as_signed < 0)
			val = integral_truncate(val, type_ref_size(ty, &ty->where), NULL);
	}

	{
		int r;
		for(r = -1; val; r++, val >>= 1);
		return r;
	}
}

static type *type_new_primitive1(enum type_primitive p)
{
	type *t = umalloc(sizeof *t);
	where_cc1_current(&t->where);
	t->primitive = p;
	return t;
}

const type *type_new_primitive_sue(enum type_primitive p, struct_union_enum_st *s)
{
	type *t = type_new_primitive1(p);
	t->sue = s;
	return t;
}

const type *type_new_primitive(enum type_primitive p)
{
	return type_new_primitive1(p);
}

unsigned type_primitive_size(enum type_primitive tp)
{
	switch(tp){
		case type__Bool:
		case type_void:
			return 1;

		case type_schar:
		case type_uchar:
		case type_nchar:
			return UCC_SZ_CHAR;

		case type_short:
		case type_ushort:
			return UCC_SZ_SHORT;

		case type_int:
		case type_uint:
			return UCC_SZ_INT;

		case type_float:
			return 4;

		case type_long:
		case type_ulong:
			/* 4 on 32-bit */
			if(IS_32_BIT())
				return 4; /* FIXME: 32-bit long */
			return UCC_SZ_LONG;

		case type_llong:
		case type_ullong:
			return UCC_SZ_LONG_LONG;

		case type_double:
			return IS_32_BIT() ? 4 : 8;

		case type_ldouble:
			/* 80-bit float */
			ICW("TODO: long double");
			return IS_32_BIT() ? 12 : 16;

		case type_enum:
		case type_union:
		case type_struct:
			ICE("s/u/e size");

		case type_unknown:
			break;
	}

	ICE("type %s in %s()",
			type_primitive_to_str(tp), __func__);
	return -1;
}

unsigned type_size(const type *t, where *from)
{
	if(t->sue)
		return sue_size(t->sue, from);

	return type_primitive_size(t->primitive);
}

unsigned type_primitive_align(enum type_primitive p)
{
	/* align to the size,
	 * except for double and ldouble
	 * (long changes but this is accounted for in type_primitive_size)
	 */
	switch(p){
		case type_double:
			if(IS_32_BIT()){
				/* 8 on Win32, 4 on Linux32 */
				if(platform_sys() == PLATFORM_CYGWIN)
					return 8;
				return 4;
			}
			return 8; /* 8 on 64-bit */

		case type_ldouble:
			return IS_32_BIT() ? 4 : 16;

		default:
			return type_primitive_size(p);
	}
}

unsigned type_align(const type *t, where *from)
{
	return t->sue
		? sue_align(t->sue, from)
		: type_primitive_align(t->primitive);
}

int type_floating(enum type_primitive p)
{
	switch(p){
		case type_float:
		case type_double:
		case type_ldouble:
			return 1;
		default:
			return 0;
	}
}

const char *op_to_str(const enum op_type o)
{
	switch(o){
		case op_multiply: return "*";
		case op_divide:   return "/";
		case op_plus:     return "+";
		case op_minus:    return "-";
		case op_modulus:  return "%";
		case op_eq:       return "==";
		case op_ne:       return "!=";
		case op_le:       return "<=";
		case op_lt:       return "<";
		case op_ge:       return ">=";
		case op_gt:       return ">";
		case op_or:       return "|";
		case op_xor:      return "^";
		case op_and:      return "&";
		case op_orsc:     return "||";
		case op_andsc:    return "&&";
		case op_not:      return "!";
		case op_bnot:     return "~";
		case op_shiftl:   return "<<";
		case op_shiftr:   return ">>";
		CASE_STR_PREFIX(op, unknown);
	}
	return NULL;
}

const char *type_primitive_to_str(const enum type_primitive p)
{
	switch(p){
		case type_nchar:  return "char";
		case type_schar:  return "signed char";
		case type_uchar:  return "unsigned char";

		CASE_STR_PREFIX(type, void);
		CASE_STR_PREFIX(type, short);
		CASE_STR_PREFIX(type, int);
		CASE_STR_PREFIX(type, long);
		case type_ushort: return "unsigned short";
		case type_uint:   return "unsigned int";
		case type_ulong:  return "unsigned long";
		CASE_STR_PREFIX(type, float);
		CASE_STR_PREFIX(type, double);
		CASE_STR_PREFIX(type, _Bool);

		case type_llong:   return "long long";
		case type_ullong:  return "unsigned long long";
		case type_ldouble: return "long double";

		CASE_STR_PREFIX(type, struct);
		CASE_STR_PREFIX(type, union);
		CASE_STR_PREFIX(type, enum);

		CASE_STR_PREFIX(type, unknown);
	}
	return NULL;
}

const char *type_qual_to_str(const enum type_qualifier qual, int trailing_space)
{
	static char buf[32];
	/* trailing space is purposeful */
	snprintf(buf, sizeof buf, "%s%s%s%s",
		qual & qual_const    ? "const"    : "",
		qual & qual_volatile ? "volatile" : "",
		qual & qual_restrict ? "restrict" : "",
		qual && trailing_space ? " " : "");
	return buf;
}

int op_can_compound(enum op_type o)
{
	switch(o){
		case op_plus:
		case op_minus:
		case op_multiply:
		case op_divide:
		case op_modulus:
		case op_not:
		case op_bnot:
		case op_and:
		case op_or:
		case op_xor:
		case op_shiftl:
		case op_shiftr:
			return 1;
		default:
			break;
	}
	return 0;
}

int op_can_float(enum op_type o)
{
	switch(o){
		case op_modulus:
		case op_xor:
		case op_or:
		case op_and:
		case op_shiftl:
		case op_shiftr:
		case op_bnot:
			return 0;
		default:
			return 1;
	}
}

int op_is_commutative(enum op_type o)
{
	switch(o){
		case op_multiply:
		case op_plus:
		case op_xor:
		case op_or:
		case op_and:
		case op_eq:
		case op_ne:
			return 1;

		case op_unknown:
			ICE("bad op");
		case op_minus:
		case op_divide:
		case op_modulus:
		case op_orsc:
		case op_andsc:
		case op_shiftl:
		case op_shiftr:
		case op_le:
		case op_lt:
		case op_ge:
		case op_gt:
		case op_not:
		case op_bnot:
			break;
	}
	return 0;
}

int op_is_comparison(enum op_type o)
{
	switch(o){
		case op_eq:
		case op_ne:
		case op_le:
		case op_lt:
		case op_ge:
		case op_gt:
			return 1;
		default:
			break;
	}
	return 0;
}

int op_is_shortcircuit(enum op_type o)
{
	switch(o){
		case op_andsc:
		case op_orsc:
			return 1;
		default:
			return 0;
	}
}

int op_returns_bool(enum op_type o)
{
	return op_is_comparison(o) || op_is_shortcircuit(o);
}

const char *type_to_str(const type *t)
{
#define BUF_SIZE (sizeof(buf) - (bufp - buf))
	static char buf[TYPE_STATIC_BUFSIZ];
	char *bufp = buf;

	if(t->sue){
		snprintf(bufp, BUF_SIZE, "%s %s",
				sue_str(t->sue),
				t->sue->spel);

	}else{
		switch(t->primitive){
			case type_void:
			case type__Bool:
			case type_nchar: case type_schar: case type_uchar:
			case type_short: case type_ushort:
			case type_int:   case type_uint:
			case type_long:  case type_ulong:
			case type_float:
			case type_double:
			case type_llong: case type_ullong:
			case type_ldouble:
				snprintf(bufp, BUF_SIZE, "%s",
						type_primitive_to_str(t->primitive));
				break;

			case type_unknown:
				ICE("unknown type primitive (%s)", where_str(&t->where));
			case type_enum:
			case type_struct:
			case type_union:
				ICE("struct/union/enum without ->sue");
		}
	}

	return buf;
}
