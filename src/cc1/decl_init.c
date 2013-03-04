#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "../util/util.h"
#include "../util/dynarray.h"
#include "../util/dynmap.h"
#include "../util/alloc.h"
#include "data_structs.h"
#include "cc1.h"
#include "fold.h"
#include "const.h"
#include "macros.h"
#include "sue.h"

#include "decl_init.h"

#ifdef DEBUG_DECL_INIT
static int init_debug_depth;

ucc_printflike(1, 2) void INIT_DEBUG(const char *fmt, ...)
{
	va_list l;
	int i;

	for(i = init_debug_depth; i > 0; i--)
		fputs("  ", stderr);

	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);
}

#  define INIT_DEBUG_DEPTH(op) init_debug_depth op
#else
#  define INIT_DEBUG_DEPTH(op)
#  define INIT_DEBUG(...)
#endif

typedef struct
{
	decl_init **array;
	decl_init **pos;
} init_iter;


int decl_init_is_const(decl_init *dinit, symtable *stab)
{
	desig *desig;

	for(desig = dinit->desig; desig; desig = desig->next)
		if(desig->type == desig_ar){
			consty k;
			const_fold(desig->bits.ar, &k);
			if(!is_const(k.type))
				return 0;
		}

	switch(dinit->type){
		case decl_init_scalar:
		{
			expr *e;
			consty k;

			e = FOLD_EXPR(dinit->bits.expr, stab);
			const_fold(e, &k);

			return is_const(k.type);
		}

		case decl_init_brace:
		{
			decl_init **i;

			for(i = dinit->bits.inits; i && *i; i++)
				if(!decl_init_is_const(*i, stab))
					return 0;

			return 1;
		}
	}

	ICE("bad decl init");
	return -1;
}

int decl_init_is_zero(decl_init *dinit)
{
	switch(dinit->type){
		case decl_init_scalar:
		{
			consty k;

			const_fold(dinit->bits.expr, &k);

			return k.type == CONST_VAL && k.bits.iv.val == 0;
		}

		case decl_init_brace:
		{
			decl_init **i;

			for(i = dinit->bits.inits; i && *i; i++)
				if(!decl_init_is_zero(*i))
					return 0;

			return 1;
		}
	}

	ICE("bad decl init");
	return -1;
}

decl_init *decl_init_new(enum decl_init_type t)
{
	decl_init *di = umalloc(sizeof *di);
	where_new(&di->where);
	di->type = t;
	return di;
}

void decl_init_free_1(decl_init *di)
{
	free(di);
}

const char *decl_init_to_str(enum decl_init_type t)
{
	switch(t){
		CASE_STR_PREFIX(decl_init, scalar);
		CASE_STR_PREFIX(decl_init, brace);
	}
	return NULL;
}

/*static expr *expr_new_array_idx_e(expr *base, expr *idx)
{
	expr *op = expr_new_op(op_plus);
	op->lhs = base;
	op->rhs = idx;
	return expr_new_deref(op);
}

static expr *expr_new_array_idx(expr *base, int i)
{
	return expr_new_array_idx_e(base, expr_new_val(i));
}*/

/*
 * brace up code
 * -------------
 */

static decl_init *decl_init_brace_up(init_iter *, type_ref *);

#define DINIT_STR(t) (const char *[]){"scalar","brace"}[t]


static decl_init *decl_init_brace_up_scalar(
		init_iter *iter, type_ref *const tfor)
{
	decl_init *first_init;

	if(!iter->pos){
		first_init = decl_init_new(decl_init_scalar);
		first_init->bits.expr = expr_new_val(0); /* default init for everything */
		return first_init;
	}

	first_init = *iter->pos++;

	if(first_init->desig)
		DIE_AT(&first_init->where, "initialising scalar with %s designator",
				DESIG_TO_STR(first_init->desig->type));

	if(first_init->type == decl_init_brace){
		init_iter it;
		it.array = it.pos = first_init->bits.inits;
		return decl_init_brace_up(&it, tfor);
	}

	return first_init;
}

static decl_init **decl_init_brace_up_array2(
		init_iter *iter, type_ref *next_type,
		const int limit)
{
	unsigned n = 0, i = 0;
	decl_init **ret = NULL;
	decl_init *this;

	while((this = *iter->pos)){
		desig *des;
		decl_init *next_braced_up;
		init_iter *chosen_iter = iter;

		init_iter desig_sub_iter;
		decl_init *desig_sub_bits[2];

		if(limit > -1 && i >= (unsigned)limit)
			break;

		if((des = this->desig)){
			consty k;

			this->desig = des->next;

			if(des->type != desig_ar){
				DIE_AT(&this->where,
						"%s designator can't designate array",
						DESIG_TO_STR(des->type));
			}

			const_fold(des->bits.ar, &k);

			if(k.type != CONST_VAL)
				DIE_AT(&this->where, "non-constant array-designator");

			i = k.bits.iv.val;
			if(limit > -1 && i >= (unsigned)limit)
				DIE_AT(&this->where, "designating outside of array bounds (%d)", limit);

			/* [0][1] = 5, [3][2] = 2
			 *
			 * we don't want [0][1]'s bracer to eat [3][2]'s init,
			 * so we create a sub-iterator for this
			 */

			if(this->type == decl_init_brace){
				desig_sub_iter.array = desig_sub_iter.pos = this->bits.inits;
			}else{
				desig_sub_bits[0] = this;
				desig_sub_bits[1] = NULL;

				desig_sub_iter.array = desig_sub_iter.pos = desig_sub_bits;
			}
			chosen_iter = &desig_sub_iter;
			iter->pos++;
		}

		next_braced_up = decl_init_brace_up(chosen_iter, next_type);

		dynarray_padinsert(&ret, i, &n, next_braced_up);

		i++;
	}

	return ret;
}

static decl_init **decl_init_brace_up_sue2(
		init_iter *iter, struct_union_enum_st *sue)
{
	decl_init **r = NULL;

	unsigned n = 0;
	int i;
	decl_init *this;

	for(i = 0; (this = *iter->pos); i++){
		desig *des;

		if((des = this->desig)){
			/* find member, set `i' to its index */
			decl *mem;
			unsigned j = 0;

			if(des->type != desig_struct){
				DIE_AT(&this->where,
						"%s designator can't designate struct",
						DESIG_TO_STR(des->type));
			}

			this->desig = des->next;

			mem = struct_union_member_find(sue, des->bits.member, &j);
			if(!mem)
				DIE_AT(&this->where,
						"%s %s contains no such member \"%s\"",
						sue_str(sue), sue->spel, des->bits.member);

			if(j)
				ICE("Plan 9 struct init TODO");

			for(j = 0; sue->members[j]; j++){
				decl *jmem = sue->members[j]->struct_member;

				/* XXX: could pull in the members to this struct,
				 * at the same offset at the anon-struct's members */
				fprintf(stderr, "search, at %p, %s (looking for %p)\n",
						sue->members[j]->struct_member,
						sue->members[j]->struct_member->spel, mem);

				if(!jmem->spel){
					/* anon struct/union, sub init it */
					fprintf(stderr, "found anon struct\n");

				}else if(jmem == mem){
					i = j;
					break;
				}
			}

			if(!sue->members[j])
				ICE("couldn't find member %s", des->bits.member);
		}

		{
			sue_member *mem = sue->members[i];
			if(!mem)
				break;

			dynarray_padinsert(&r, i, &n,
					decl_init_brace_up(iter,
						mem->struct_member->ref));
		}
	}

	return r;
}

static decl_init *decl_init_brace_up_aggregate(
		init_iter *iter,
		decl_init **(*brace_up_f)(),
		void *arg1, int arg2)
{
	if(iter->pos[0]->type != decl_init_brace){
		decl_init *r = decl_init_new(decl_init_brace);

		/* we need to pull from iter, bracing up our children inits */
		r->bits.inits = brace_up_f(iter, arg1, arg2);

		return r;
	}else{
		/* we're initialising ourselves with a brace,
		 * just need to do this to all sub-inits */
		decl_init *first = iter->pos[0];
		decl_init **old_subs = first->bits.inits;

		if(!old_subs){
			/* {} */
			first->bits.inits = NULL;
			dynarray_add(&first->bits.inits, (decl_init *)DYNARRAY_NULL);
		}else{
			init_iter it;

			it.array = it.pos = old_subs;

			first->bits.inits = brace_up_f(&it, arg1, arg2);
		}

		++iter->pos;

		free(old_subs);

		return first; /* this is in the {} state */
	}
}

static decl_init *decl_init_brace_up(init_iter *iter, type_ref *tfor)
{
	struct_union_enum_st *sue;

	if(type_ref_is(tfor, type_ref_array)){
		const int limit = type_ref_is_incomplete_array(tfor)
			? -1 : type_ref_array_len(tfor);

		return decl_init_brace_up_aggregate(
				iter, &decl_init_brace_up_array2,
				type_ref_next(tfor), limit);
	}

	if((sue = type_ref_is_s_or_u(tfor)))
		return decl_init_brace_up_aggregate(
				iter, &decl_init_brace_up_sue2,
				sue, 0);

	return decl_init_brace_up_scalar(iter, tfor);
}

static void DEBUG(decl_init *init, type_ref *tfor)
{
	decl_init *inits[2] = {
		init, NULL
	};
	init_iter it;
	it.array = it.pos = inits;

	decl_init *braced = decl_init_brace_up(&it, tfor);

	cc1_out = stdout;
	printf("braced = %p\n", (void *)braced);
	void print_decl_init(void *);
	print_decl_init(braced);

	exit(5);
}

void decl_init_create_assignments_for_spel(decl *d, stmt *init_code)
{
	(void)init_code;
	DEBUG(d->init, d->ref);
}

void decl_init_create_assignments_for_base(decl *d, expr *base, stmt *init_code)
{
	(void)init_code;
	(void)base;
	DEBUG(d->init, d->ref);
}
