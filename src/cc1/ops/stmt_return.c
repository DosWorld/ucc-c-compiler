#include "ops.h"
#include "stmt_return.h"

#include "expr_block.h"
#include "../funcargs.h"
#include "../type_is.h"
#include "../type_nav.h"

#include "../cc1_out_ctx.h"
#include "../inline.h"
#include "../sanitize.h"

const char *str_stmt_return(void)
{
	return "return";
}

void fold_stmt_return(stmt *s)
{
	decl *in_func = symtab_func(s->symtab);
	type *ret_ty;
	int void_func;

	if(!in_func)
		die_at(&s->where, "return outside a function");

	if(in_func->ref){
		ret_ty = type_called(in_func->ref, NULL);
		void_func = type_is_void(ret_ty);
	}else{
		/* we're the first return stmt in a block */
		ret_ty = NULL;
	}

	if(s->expr){
		int void_return;

		fold_expr_nodecay(s->expr, s->symtab);
		if(!type_is_s_or_u(s->expr->tree_type))
			FOLD_EXPR(s->expr, s->symtab);

		(void)!fold_check_expr(s->expr, FOLD_CHK_ALLOW_VOID, s->f_str());

		void_return = type_is_void(s->expr->tree_type);

		if(void_return)
			cc1_warn_at(&s->where, return_void,
					"void function returns void expression");

		if(ret_ty){
			if(void_return){
				if(!void_func){
					cc1_warn_at(&s->where, return_type,
							"void return in non-void function %s", in_func->spel);
				}

			}else{
				/* void return handled implicitly with a cast to void */
				fold_type_chk_and_cast_ty(
						ret_ty, &s->expr,
						s->symtab, &s->where, "return type");

				if(void_func){
					cc1_warn_at(&s->where, return_type,
							"return with a value in void function %s", in_func->spel);
				}
			}
		}else{
			/* in a block */
			void_func = 0;
		}

	}else{
		if(!ret_ty){
			/* in a void block */
			void_func = 1;
		}else if(!void_func){
			cc1_warn_at(&s->where, return_type,
					"empty return in non-void function %s", in_func->spel);
		}
	}

	if(!ret_ty){
		/* first return of a block */
		ret_ty = s->expr ? s->expr->tree_type : type_nav_btype(cc1_type_nav, type_void);
		expr_block_set_ty(in_func, ret_ty, s->symtab);
	}
}

#if 0
static int may_tail_call_typematch(const stmt *s)
{
	return may_tail_call_typematch(s);
	expr *e = s->expr;
	type *cur_ret = type_func_call(symtab_func(s->symtab)->ref, NULL);
	type *e_ty;
	int cur_ret_su = !!type_is_s_or_u(cur_ret);

	if(!e){
		/* void --> T */
		return !cur_ret_su;
	}

	e_ty = e->tree_type;

	switch(type_cmp(e_ty, cur_ret, 0)){
		case TYPE_EQUAL:
		case TYPE_EQUAL_TYPEDEF:
		case TYPE_QUAL_ADD:
		case TYPE_QUAL_SUB:
		case TYPE_QUAL_POINTED_ADD:
		case TYPE_QUAL_POINTED_SUB:
		case TYPE_QUAL_NESTED_CHANGE:
		case TYPE_CONVERTIBLE_IMPLICIT:
		case TYPE_CONVERTIBLE_EXPLICIT:
			return 1;

		case TYPE_NOT_EQUAL:
			break;
	}
	return 0;
}

/*
 * this is responsible for determining if we can tail call
 * at the language level - types are compatible,
 * no __attribute__((cleanup(...))) to do, etc.
 */
static int may_tail_call(const stmt *s)
{
	return may_tail_call_typematch(s) && no_cleanups(s);
}
#endif

void gen_stmt_return(const stmt *s, out_ctx *octx)
{
	struct cc1_out_ctx **pcc1_octx, *cc1_octx;

	/* need to generate the ret expr before the scope leave code */
	const out_val *ret_exp;

#if 0
	if(may_tail_call(s, octx)){
	}
#endif

	ret_exp = s->expr ? gen_expr(s->expr, octx) : NULL;

	if(s->expr
	&& type_is_ptr_or_block(s->expr->tree_type)
	&& attribute_present(symtab_func(s->symtab), attr_returns_nonnull))
	{
		sanitize_returns_nonnull(ret_exp, octx);
	}

	gen_scope_leave(s->symtab, symtab_root(s->symtab), octx);

	pcc1_octx = cc1_out_ctx(octx);
	if((cc1_octx = *pcc1_octx) && cc1_octx->inline_.depth)
		inline_ret_add(octx, ret_exp);
	else
		out_ctrl_end_ret(octx, ret_exp, s->expr ? s->expr->tree_type : NULL);
}

void dump_stmt_return(const stmt *s, dump *ctx)
{
	dump_desc_stmt(ctx, "return", s);

	if(s->expr){
		dump_inc(ctx);
		dump_expr(s->expr, ctx);
		dump_dec(ctx);
	}
}

void style_stmt_return(const stmt *s, out_ctx *octx)
{
	stylef("return ");
	IGNORE_PRINTGEN(gen_expr(s->expr, octx));
	stylef(";");
}

void init_stmt_return(stmt *s)
{
	s->f_passable = fold_passable_no;
}
