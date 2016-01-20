#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include <strbuf_fixed.h>

#include "../util/util.h"
#include "../util/alloc.h"
#include "../util/dynarray.h"

#include "sym.h"
#include "type_is.h"
#include "type_nav.h"
#include "expr.h"
#include "stmt.h"
#include "cc1.h" /* IS_32_BIT() */
#include "sue.h"
#include "funcargs.h"
#include "str.h" /* literal_print() */
#include "decl_init.h"

#include "gen_ir.h"
#include "gen_ir_internal.h"

struct irval
{
	enum irval_type
	{
		IRVAL_LITERAL,
		IRVAL_ID,
		IRVAL_NAMED,
		IRVAL_LBL
	} type;

	union
	{
		struct
		{
			type *ty;
			integral_t val;
		} lit;
		irid id;
		decl *decl;
		const char *lbl;
	} bits;
};

static const char *irtype_str_maybe_fn(type *, funcargs *maybe_args);

irval *gen_ir_expr(const struct expr *expr, irctx *ctx)
{
	return expr->f_ir(expr, ctx);
}

void gen_ir_stmt(const struct stmt *stmt, irctx *ctx)
{
	stmt->f_ir(stmt, ctx);
}

void gen_ir_comment(irctx *ctx, const char *fmt, ...)
{
	va_list l;

	(void)ctx;

	printf("# ");

	va_start(l, fmt);
	vprintf(fmt, l);
	va_end(l);

	putchar('\n');
}

static void gen_ir_spill_args(irctx *ctx, funcargs *args)
{
	decl **i;

	(void)ctx;

	for(i = args->arglist; i && *i; i++){
		decl *d = *i;
		const char *asm_spel = decl_asm_spel(d);

		printf("$%s = alloca %s\n", asm_spel, irtype_str(d->ref));
		printf("store $%s, $%s\n", asm_spel, d->spel);
	}
}

static void gen_ir_init_scalar(decl_init *init)
{
	int anyptr = 0;
	expr *e = init->bits.expr;
	consty k;

	memset(&k, 0, sizeof k);
	const_fold(e, &k);

	switch(k.type){
		case CONST_NEED_ADDR:
		case CONST_NO:
			ICE("non-constant expr-%s const=%d%s",
					e->f_str(),
					k.type,
					k.type == CONST_NEED_ADDR ? " (needs addr)" : "");
			break;

		case CONST_NUM:
			if(K_FLOATING(k.bits.num)){
				/* asm fp const */
				IRTODO("floating static constant");
				printf("<TODO: float constant>");
			}else{
				char buf[INTEGRAL_BUF_SIZ];
				integral_str(buf, sizeof buf, k.bits.num.val.i, e->tree_type);
				fputs(buf, stdout);
			}
			break;

		case CONST_ADDR:
			if(k.bits.addr.is_lbl)
				printf("$%s", k.bits.addr.bits.lbl);
			else
				printf("%ld", k.bits.addr.bits.memaddr);
			break;

		case CONST_STRK:
			stringlit_use(k.bits.str->lit);
			printf("$%s", k.bits.str->lit->lbl);
			anyptr = 1;
			break;
	}

	if(k.offset)
		printf(" add %" NUMERIC_FMT_D, k.offset);
	if(anyptr)
		printf(" anyptr");
}

static void gen_ir_zeroinit(type *ty)
{
	(void)ty;
	ICE("TODO: init: zero");
}

static void gen_ir_init_r(decl_init *init, type *ty)
{
	type *test;

	if(init == DYNARRAY_NULL)
		init = NULL;

	if(!init){
		if(type_is_incomplete_array(ty))
			/* flexarray */
			;
		else
			gen_ir_zeroinit(ty);
		return;
	}

	if((test = type_is_primitive(ty, type_struct))){
		ICE("TODO: init: struct");
	}else if((test = type_is(ty, type_array))){
		ICE("TODO: init: array");
	}else if((test = type_is_primitive(ty, type_union))){
		ICE("TODO: init: union");
	}else{
		gen_ir_init_scalar(init);
	}
}

static void gen_ir_init(decl *d)
{
	printf(" %s ", decl_linkage(d) == linkage_internal ? "internal" : "global");

	if(attribute_present(d, attr_weak))
		printf("weak ");

	if(type_is_const(d->ref))
		printf("const ");

	gen_ir_init_r(d->bits.var.init.dinit, d->ref);
}

static void gen_ir_decl(decl *d, irctx *ctx)
{
	funcargs *args = type_is(d->ref, type_func) ? type_funcargs(d->ref) : NULL;

	if((d->store & STORE_MASK_STORE) == store_typedef)
		return;

	printf("$%s = %s", decl_asm_spel(d), irtype_str_maybe_fn(d->ref, args));

	if(args){
		putchar('\n');
		if(decl_should_emit_code(d)){
			printf("{\n");
			gen_ir_spill_args(ctx, args);
			gen_ir_stmt(d->bits.func.code, ctx);

			/* if non-void function and function may fall off the end, dummy a return */
			if(d->bits.func.control_returns_undef){
				printf("ret undef\n");
			}else if(type_is_void(type_called(d->ref, NULL))){
				printf("ret void\n");
			}

			printf("}\n");
		}
	}else{
		if((d->store & STORE_MASK_STORE) != store_extern
		&& d->bits.var.init.dinit)
		{
			gen_ir_init(d);
		}
		putchar('\n');
	}
}

static void gen_ir_stringlit(const stringlit *lit, int predeclare)
{
	printf("$%s = [i%c x 0]", lit->lbl, lit->wide ? '4' : '1', lit->lbl);

	if(!predeclare){
		printf(" internal \"");
		literal_print(stdout, lit->str, lit->len);
		printf("\"");
	}
	printf("\n");
}

static void gen_ir_stringlits(dynmap *litmap, int predeclare)
{
	const stringlit *lit;
	size_t i;
	for(i = 0; (lit = dynmap_value(stringlit *, litmap, i)); i++)
		if(lit->use_cnt > 0 || predeclare)
			gen_ir_stringlit(lit, predeclare);
}

void gen_ir(symtable_global *globs)
{
	irctx ctx = { 0 };
	symtable_gasm **iasm = globs->gasms;
	decl **diter;

	gen_ir_stringlits(globs->literals, 1);

	for(diter = symtab_decls(&globs->stab); diter && *diter; diter++){
		decl *d = *diter;

		while(iasm && d == (*iasm)->before){
			IRTODO("emit global __asm__");

			if(!*++iasm)
				iasm = NULL;
		}

		gen_ir_decl(d, &ctx);
	}

	gen_ir_stringlits(globs->literals, 0); /* must be after code-gen - use_cnt */
}

static const char *irtype_btype_str(const btype *bt)
{
	switch(bt->primitive){
		case type_void:
			return "void";

		case type__Bool:
		case type_nchar:
		case type_schar:
		case type_uchar:
			return "i1";

		case type_int:
		case type_uint:
			return "i4";

		case type_short:
		case type_ushort:
			return "i2";

		case type_long:
		case type_ulong:
			if(IS_32_BIT())
				return "i4";
			return "i8";

		case type_llong:
		case type_ullong:
			return "i8";

		case type_float:
			return "f4";
		case type_double:
			return "f8";
		case type_ldouble:
			return "flarge";

		case type_enum:
			switch(bt->sue->size){
				case 1: return "i1";
				case 2: return "i2";
				case 4: return "i4";
				case 8: return "i8";
			}
			assert(0 && "unreachable");

		case type_struct:
		case type_union:
			assert(0 && "s/u");

		case type_unknown:
			assert(0 && "unknown type");
	}
}

const char *ir_op_str(enum op_type op, int arith_rshift)
{
	switch(op){
		case op_multiply: return "mul";
		case op_divide:   return "div";
		case op_modulus:  return "mod";
		case op_plus:     return "add";
		case op_minus:    return "sub";
		case op_xor:      return "xor";
		case op_or:       return "or";
		case op_and:      return "and";
		case op_shiftl:   return "shiftl";
		case op_shiftr:
			return arith_rshift ? "shiftr_arith" : "shiftr_logic";

		case op_eq:       return "eq";
		case op_ne:       return "ne";
		case op_le:       return "le";
		case op_lt:       return "lt";
		case op_ge:       return "ge";
		case op_gt:       return "gt";

		case op_not:
			assert(0 && "operator! should use cmp with zero");
		case op_bnot:
			assert(0 && "operator~ should use xor with -1");

		case op_orsc:
		case op_andsc:
			assert(0 && "invalid op string (shortcircuit)");

		case op_unknown:
			assert(0 && "unknown op");
	}
}

static void irtype_str_r(strbuf_fixed *buf, type *t, funcargs *maybe_args)
{
	t = type_skip_all(t);

	switch(t->type){
		case type_btype:
			switch(t->bits.type->primitive){
				case type_struct:
				{
					int first = 1;
					sue_member **i;

					strbuf_fixed_printf(buf, "{");

					for(i = t->bits.type->sue->members; i && *i; i++){
						if(!first)
							strbuf_fixed_printf(buf, ", ");

						irtype_str_r(buf, (*i)->struct_member->ref, NULL);

						first = 0;
					}

					strbuf_fixed_printf(buf, "}");
					break;
				}

				case type_union:
					ICE("TODO: union type");

				default:
					strbuf_fixed_printf(buf, "%s", irtype_btype_str(t->bits.type));
					break;
			}
			break;

		case type_ptr:
		case type_block:
			irtype_str_r(buf, t->ref, NULL);
			strbuf_fixed_printf(buf, "*");
			break;

		case type_func:
		{
			size_t i;
			int first = 1;
			funcargs *fargs = t->bits.func.args;
			decl **arglist = fargs->arglist;
			int have_arg_names = 1;
			int variadic_or_any;

			for(i = 0; arglist && arglist[i]; i++){
				decl *d;
				if(!maybe_args){
					have_arg_names = 0;
					break;
				}
				d = maybe_args->arglist[i];
				if(!d)
					break;
				if(!d->spel){
					have_arg_names = 0;
					break;
				}
			}

			irtype_str_r(buf, t->ref, NULL);
			strbuf_fixed_printf(buf, "(");

			/* ignore fargs->args_old_proto
			 * technically args_old_proto means the [ir]type is T(...)
			 * however, then we don't have access to arguments.
			 * therefore, the function must be cast to T(...) at all call
			 * sites, to enable us to call it with any arguments, as a valid C old-function
			 */

			for(i = 0; arglist && arglist[i]; i++){
				if(!first)
					strbuf_fixed_printf(buf, ", ");

				irtype_str_r(buf, arglist[i]->ref, NULL);

				if(have_arg_names){
					decl *arg = maybe_args->arglist[i];

					/* use arg->spel, as decl_asm_spel() is used for the alloca-version */
					strbuf_fixed_printf(buf, " $%s", arg->spel);
				}

				first = 0;
			}

			variadic_or_any = (FUNCARGS_EMPTY_NOVOID(fargs) || fargs->variadic);

			strbuf_fixed_printf(buf, "%s%s)",
					variadic_or_any && fargs->arglist ? ", " : "",
					variadic_or_any ? "..." : "");
			break;
		}

		case type_array:
			strbuf_fixed_printf(buf, "[");
			irtype_str_r(buf, t->ref, NULL);
			strbuf_fixed_printf(buf, " x %" NUMERIC_FMT_D "]",
					const_fold_val_i(t->bits.array.size));
			break;

		default:
			assert(0 && "unskipped type");
	}
}

static const char *irtype_str_maybe_fn(type *t, funcargs *maybe_args)
{
	static char ar[128];
	strbuf_fixed buf = STRBUF_FIXED_INIT_ARRAY(ar);

	irtype_str_r(&buf, t, maybe_args);

	return strbuf_fixed_detach(&buf);
}

const char *irtype_str(type *t)
{
	return irtype_str_maybe_fn(t, NULL);
}

const char *irval_str(irval *v)
{
	static char buf[256];

	switch(v->type){
		case IRVAL_LITERAL:
		{
			strbuf_fixed sbuf = STRBUF_FIXED_INIT_ARRAY(buf);
			size_t len;

			irtype_str_r(&sbuf, v->bits.lit.ty, NULL);
			len = strlen(buf);

			assert(len < sizeof buf);

			snprintf(buf + len, sizeof buf - len,
					" %" NUMERIC_FMT_D,
					v->bits.lit.val);
			break;
		}

		case IRVAL_LBL:
			snprintf(buf, sizeof buf, "$%s", v->bits.lbl);
			break;

		case IRVAL_ID:
			snprintf(buf, sizeof buf, "$%u", v->bits.id);
			break;

		case IRVAL_NAMED:
		{
			assert(v->bits.decl->sym);
			snprintf(buf, sizeof buf, "$%s", decl_asm_spel(v->bits.decl));
			break;
		}
	}
	return buf;
}

static irval *irval_new(enum irval_type t)
{
	irval *v = umalloc(sizeof *v);
	v->type = t;
	return v;
}

irval *irval_from_id(irid id)
{
	irval *v = irval_new(IRVAL_ID);
	v->bits.id = id;
	return v;
}

irval *irval_from_l(type *ty, integral_t l)
{
	irval *v = irval_new(IRVAL_LITERAL);
	v->bits.lit.val = l;
	v->bits.lit.ty = ty;
	return v;
}

irval *irval_from_sym(irctx *ctx, struct sym *sym)
{
	irval *v = irval_new(IRVAL_NAMED);
	(void)ctx;
	v->bits.decl = sym->decl;
	return v;
}

irval *irval_from_lbl(irctx *ctx, char *lbl)
{
	irval *v = irval_new(IRVAL_LBL);
	(void)ctx;
	v->bits.lbl = lbl;
	return v;
}

irval *irval_from_noop(void)
{
	return irval_from_l(NULL, 0);
}

void irval_free(irval *v)
{
	free(v);
}

void irval_free_abi(void *v)
{
	irval_free(v);
}
