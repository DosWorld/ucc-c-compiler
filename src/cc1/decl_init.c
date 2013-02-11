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


typedef struct decl_init_iter
{
	decl_init **pos;
} decl_init_iter;
#define INIT_ITER_VALID(i) ((i) && (i)->pos)


static void init_iter_adv(decl_init_iter *i, int n)
{
	if(!i || !i->pos)
		return;

	while(n --> 0)
		if(!*++i->pos){
			i->pos = NULL;
			break;
		}
}

static stmt *stmt_sub_init_code(stmt *parent)
{
	stmt *sub = stmt_new_wrapper(code, parent->symtab);
	dynarray_add(&parent->codes, sub);
	return sub;
}

static void decl_init_create_assignments_discard(
		decl_init_iter *init_iter,
		type_ref *const tfor_wrapped,
		expr *base,
		stmt *init_code);

static void decl_initialise_scalar(
		decl_init_iter *init_iter, type_ref *const tfor,
		expr *base, stmt *init_code);

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

static expr *expr_new_array_idx_e(expr *base, expr *idx)
{
	expr *op = expr_new_op(op_plus);
	op->lhs = base;
	op->rhs = idx;
	return expr_new_deref(op);
}

static expr *expr_new_array_idx(expr *base, int i)
{
	return expr_new_array_idx_e(base, expr_new_val(i));
}

static void array_insert_sorted(stmt ***psorted_array_inits,
		int i, int *pmax_i, stmt *init_code_dummy)
{
#define sorted_array_inits (*psorted_array_inits)
#define max_i (*pmax_i)

	stmt *sub_init = init_code_dummy->codes[0];
	const int old = max_i;
	int changed = 0;

	if(i > max_i)
		max_i = i, changed = 1;

	UCC_ASSERT(!init_code_dummy->codes[1], "too many sub-inits");
	init_code_dummy->codes = NULL;

	if(changed || !sorted_array_inits){
		int old_sz = (old + 2) * sizeof *sorted_array_inits;

		if(!sorted_array_inits)
			old_sz = 0;

		sorted_array_inits = urealloc(sorted_array_inits,
				(max_i + 2) * sizeof *sorted_array_inits,
				old_sz);

		sorted_array_inits[max_i + 1] = NULL; /* dynarray compatability */
	}

	sorted_array_inits[i] = sub_init;

#undef max_i
#undef sorted_array_inits
}

static type_ref *decl_initialise_array(
		decl_init_iter *init_iter,
		type_ref *const tfor_wrapped, expr *base, stmt *init_code)
{
	decl_init *dinit = INIT_ITER_VALID(init_iter) ? init_iter->pos[0] : NULL;
	type_ref *tfor_deref, *tfor;
	int complete_to = 0;
	stmt *sub_init_code = stmt_sub_init_code(init_code);

	switch(dinit ? dinit->type : decl_init_brace){
		case decl_init_scalar:
		{
			expr *e = dinit->bits.expr;

			if(expr_kind(e, str)){
				/* can't const fold, since it's not folded yet */
				stringval *sv = &e->bits.str.sv;

				/* const char [] init - need to check tfor is of the same type */
				type_ref *rar = type_ref_is(tfor_wrapped, type_ref_array);

				if(type_ref_is_type(type_ref_next(rar), type_char)){
					int i;

					complete_to = sv->len;

					for(i = 0; i < complete_to; i++){
						expr *e  = expr_new_val(sv->str[i]);
						expr *to = expr_new_array_idx(base, i);

						dynarray_add(&sub_init_code->codes,
								expr_to_stmt(
									expr_new_assign(to, e),
									init_code->symtab));
					}

					tfor = tfor_wrapped;
					goto complete_ar;
				}
			}

			/* check for scalar init isn't done here */
			break;
		}

		case decl_init_brace:
			break;
	}

	tfor = tfor_wrapped;
	tfor_deref = type_ref_is(tfor_wrapped, type_ref_array)->ref;

	/* walk through the inits, pulling as many as we need/one sub-brace for a sub-init */
	/* e.g.
	 * int x[]    = { 1, 2, 3 };    subinit = int,    pull 1 for each
	 * int x[][2] = { 1, 2, 3, 4 }  subinit = int[2], pull 2 for each
	 *
	 * int x[][2] = { {1}, 2, 3, {4}, 5, {6}, {7} };
	 * subinit = int[2], pull as show:
	 *              { {1}, 2, 3, {4}, 5, {6}, {7} };
	 *                 ^   ^--^   ^   ^   ^    ^      -> 6 inits
	 */

	if(dinit){
		decl_init_iter sub_array_iter;
		decl_init_iter *array_iter;
		const int braced = dinit->type == decl_init_brace;
		int adv_iter_by = 0;
		stmt **sorted_array_inits = NULL;
		stmt *init_code_dummy = stmt_new_wrapper(code, init_code->symtab);

		int is_fixed_length = !type_ref_is_incomplete_array(tfor);
		int n_fixed_length  = is_fixed_length ? type_ref_array_len(tfor) : 0;
		int i, last_index = 0;

		if(dinit->type == decl_init_scalar){
			array_iter = init_iter;
		}else{
			sub_array_iter.pos = dinit->bits.inits;
			array_iter = &sub_array_iter;
		}

		INIT_DEBUG("initialising array from %s"
				", nested: %d, %p\n",
				decl_init_to_str(dinit->type),
				dinit->type == decl_init_brace,
				(void *)dinit);
		INIT_DEBUG_DEPTH(++);

		if(!array_iter->pos && !is_fixed_length){
			/* x = {} = 1-length array */
			is_fixed_length = 1;
			n_fixed_length = 1;
		}

		if(is_fixed_length)
			sorted_array_inits = umalloc((n_fixed_length + 1) * sizeof *sorted_array_inits);

		for(i = 0; array_iter->pos && (is_fixed_length ? i < n_fixed_length : 1); i++){
			/* index into the main-array */
			adv_iter_by++;

			if(array_iter->pos){
				decl_init *init_for_mem = array_iter->pos[0];
				desig *const desig = init_for_mem->desig;

				if(desig){
					if(desig->type != desig_ar)
						DIE_AT(&init_for_mem->where, "array designator expected");

					init_for_mem->desig = desig->next; /* advance */

					{
						expr *idx = desig->bits.ar;
						consty k;

						FOLD_EXPR(idx, init_code->symtab);
						const_fold(idx, &k);

						if(k.type != CONST_VAL)
							DIE_AT(&idx->where, "constant integral expression expected");

						i = k.bits.iv.val;
						if(i < 0)
							DIE_AT(&idx->where, "negative array index");

						if(is_fixed_length && i >= n_fixed_length)
							DIE_AT(&idx->where, "index %d out of bounds (0...%d)", i, n_fixed_length);

						if(i > last_index)
							last_index = i;
					}
				}
			}

			INIT_DEBUG("initialising (%s)[%d] with %s\n",
					type_ref_to_str(tfor), i,
					decl_init_to_str(array_iter->pos[0]->type));

			/* FIXME: check for dups */
			INIT_DEBUG_DEPTH(++);
			decl_init_create_assignments_discard(
					array_iter, tfor_deref,
					expr_new_array_idx(base, i),
					init_code_dummy);
			INIT_DEBUG_DEPTH(--);

			/* insert, sorted */
			array_insert_sorted(&sorted_array_inits, i, &last_index, init_code_dummy);

			if(!braced
			&& INIT_ITER_VALID(array_iter)
			&& array_iter->pos[0]->desig)
			{
				break;
			}
		}

		INIT_DEBUG("terminating array loop i=%d, is_fixed_length=%d, n_fixed_length=%d, array_iter->pos=%p\n",
				i, is_fixed_length, n_fixed_length, (void *)array_iter->pos);

		{
			/* need to zero-fill
			 * can't start at i,
			 * may have skipped over with designators
			 */
			const int fill_to = is_fixed_length ? n_fixed_length - 1 : last_index;

			INIT_DEBUG("fill_to = %d\n", fill_to);

			for(i = 0; i <= fill_to; i++){
				if(sorted_array_inits[i])
					continue;

				INIT_DEBUG("create ZERO array assignment to %s[%d]\n",
						type_ref_to_str(tfor_deref), i);

				decl_init_create_assignments_discard(
						NULL, tfor_deref, expr_new_array_idx(base, i),
						init_code_dummy);

				array_insert_sorted(&sorted_array_inits, i, &last_index, init_code_dummy);
			}

			complete_to = fill_to + 1;
		}

		dynarray_add_array(&sub_init_code->codes, sorted_array_inits);

		/* advance by the number of steps we moved over,
		 * if not nested, otherwise advance by one, over the sub-brace
		 */
		INIT_DEBUG("array iter adv by: %d (complete to %d)\n",
				(dinit->type == decl_init_scalar) ? complete_to : 1,
				complete_to);

		if(dinit->type == decl_init_brace)
			init_iter_adv(init_iter, 1);
		/* otherwise we're walking the init our parent is walking */

		INIT_DEBUG_DEPTH(--);
		INIT_DEBUG("array, len %d finished, i=%d, adv-by=%d\n",
				complete_to, i, dinit->type == decl_init_brace);

		free(sorted_array_inits);
		free(init_code_dummy);
	}

	/* patch the type size */
complete_ar:
	if(type_ref_is_incomplete_array(tfor)){
		tfor = type_ref_complete_array(tfor, complete_to);

		INIT_DEBUG("completed array to %d - %s (sub_init_code count %d)\n",
				complete_to, type_ref_to_str(tfor),
				dynarray_count(sub_init_code->codes));
	}

	return tfor;
}

#define EXPR_STRUCT(b, nam) expr_new_struct(b, 1 /* a.b */, \
					expr_new_identifier(nam))

static int sue_cmp(void *va, void *vb)
{
	/* same decl? */
	return va == vb ? 0 : 1;
}

static void decl_initialise_sue(decl_init_iter *init_iter,
		struct_union_enum_st *sue, expr *base, stmt *init_code)
{
	/* iterate over each member, pulling from the dinit */
	decl_init *dinit = INIT_ITER_VALID(init_iter) ? init_iter->pos[0] : NULL;
	decl_init_iter sue_init_iter;
	int braced;
	int i, cnt;
	char *initialised;
	dynmap *init_maps = dynmap_new(&sue_cmp);
	stmt *init_code_dummy = stmt_new_wrapper(code, init_code->symtab);

	cnt = dynarray_count(sue->members);
	initialised = umalloc(cnt * sizeof *initialised);

	if(sue_incomplete(sue)){
		type_ref *r = type_ref_new_type(type_new_primitive(type_struct));
		r->bits.type->sue = sue;

		DIE_AT(&base->where, "initialising %s", type_ref_to_str(r));
	}

	if(dinit == NULL){
		braced = 1; /* adv by one */
		goto zero_init;
	}

	braced = dinit->type == decl_init_brace;
	sue_init_iter.pos = braced ? dinit->bits.inits : init_iter->pos;

	for(i = 0;;){
		decl *sue_mem = NULL;
		expr *accessor = NULL;

		if(sue_init_iter.pos){
			decl_init *init_for_mem = sue_init_iter.pos[0];

			/* got a designator - skip to that decl
			 * don't do duplicate init checks here,
			 * since struct { ... } x = { .i[8] = 5, .i[2] = 3 } is valid
			 */
			if(init_for_mem->desig){
				desig *const desig = init_for_mem->desig;
				char *mem;

				/* advance for sub-inits */
				init_for_mem->desig = desig->next;

				if(desig->type != desig_struct){
					if(!braced)
						break; /* similar to the below check */
					DIE_AT(&init_for_mem->where, "struct designator expected");
				}

				mem = desig->bits.member;
				accessor = EXPR_STRUCT(base, mem);

				/* find + update iter */
				for(i = 0; i < cnt; i++){
					sue_mem = sue->members[i]->struct_member;
					if(!strcmp(mem, sue_mem->spel))
						break; /* found */
				}

				if(i >= cnt){
					/* have a designator, no member.
					 * could be: struct{struct{int i;}a; int j;} x = { .a=1,2, .j=3};
					 *
					 * - if we're initialised from a non-brace init, break
					 */
					if(!braced)
						break;
					DIE_AT(&init_for_mem->where, "no such member %s::%s to initialise", sue->spel, mem);
				}

				INIT_DEBUG("designating %s::%s...\n", sue->spel, sue_mem->spel);

				/* this means we forget about desig
				 * might be useful for later code analysis? */
				free(desig); /* XXX: lost data */
			}
		}else{
			/* null init */
			break;
		}

		if(!accessor){
			UCC_ASSERT(sue_init_iter.pos, "no next init - should've been caught below");

			if(i >= cnt){
				/* trying to initialise past the end
				 * could be like this: struct{struct{int i;}a; int j;} x = { .a=1,2};
				 */
				if(braced)
					WARN_AT(&sue_init_iter.pos[0]->where, "excess initialiser for struct");
				break;
			}

			sue_mem = sue->members[i]->struct_member;
			accessor = EXPR_STRUCT(base, sue_mem->spel);

			INIT_DEBUG("initialising next member %s::%s...\n", sue->spel, sue_mem->spel);
		}

		initialised[i]++;

		INIT_DEBUG_DEPTH(++);
		INIT_DEBUG("... with %s\n", INIT_ITER_VALID(&sue_init_iter)
				? decl_init_to_str(sue_init_iter.pos[0]->type) : "n/a");

		{
			decl_init_create_assignments_discard(
					&sue_init_iter,
					sue_mem->ref,
					accessor,
					init_code_dummy);

			/* init_code_dummy->codes now has the init-codes */
			/* FIXME: check for dups */
			if(dynmap_get(decl *, stmt **, init_maps, sue_mem))
				ICW("overwriting dynmap{%s}\n", sue_mem->spel);

			dynmap_set(decl *, stmt **, init_maps,
					sue_mem, init_code_dummy->codes);

			init_code_dummy->codes = NULL;
		}

		INIT_DEBUG_DEPTH(--);

		/* terminating case - we're at the end of the sue_init_iter
		 * or `i > cnt` (last member) and the next isn't a desig */
		if(!sue_init_iter.pos)
			break;
		if(++i > cnt && !sue_init_iter.pos[0]->desig)
			break;
	}

zero_init:
	for(i = 0; i < cnt; i++)
		if(!initialised[i]){
			decl *d_mem = sue->members[i]->struct_member;
			expr *access = EXPR_STRUCT(base, d_mem->spel);

			decl_init_create_assignments_discard(
					NULL, d_mem->ref, access, init_code_dummy);

			/* init_code_dummy->codes now has the init-codes */
			/* FIXME: check for dups */
			if(dynmap_get(decl *, stmt **, init_maps, d_mem))
				ICW("overwriting dynmap{%s} with zero\n", d_mem->spel);

			dynmap_set(decl *, stmt **, init_maps,
					d_mem, init_code_dummy->codes);

			init_code_dummy->codes = NULL;
		}

	{
		/* linked to init_code */
		stmt *sub_init_code = stmt_sub_init_code(init_code);
		int i;

		/* go through members in struct order */
		for(i = 0; i < cnt; i++){
			decl *d = sue->members[i]->struct_member;
			stmt **inits = dynmap_get(decl *, stmt **, init_maps, d);

			UCC_ASSERT(inits, "no inits for %s::%s", sue->spel, d->spel);

			/* FIXME: check for dups */
			dynarray_add_array(&sub_init_code->codes, inits);
		}
	}

	if(!braced)
		/* we walk over the one brace, not multiple scalar/subinits */
		init_iter_adv(init_iter, cnt);
	/* otherwise we've walked over the scalar inits of our parent */


	INIT_DEBUG("initialised %s, *init_iter += %d -> (%s)\n",
			sue_str(sue), cnt,
			INIT_ITER_VALID(init_iter)
				? decl_init_to_str(init_iter->pos[0]->type)
				: "n/a");

	free(init_code_dummy);
	free(initialised);
	dynmap_free(init_maps);
}

static void decl_initialise_scalar(
		decl_init_iter *init_iter, type_ref *const tfor,
		expr *base, stmt *init_code)
{
	decl_init *const dinit = INIT_ITER_VALID(init_iter) ? init_iter->pos[0] : NULL;
	expr *assign_from, *assign_init;

	if(dinit){
		if(dinit->desig){
			if(dinit->type != decl_init_brace)
				return; /* similar to struct/array get-outs */

			DIE_AT(&dinit->where, "%s-designator for scalar",
					DESIG_TO_STR(dinit->desig->type));
		}

		if(dinit->type == decl_init_brace){
			/* initialising scalar with { ... } - pick first */
			decl_init **inits = dinit->bits.inits;
			decl_init_iter new_iter = { inits };

			if(inits && inits[1])
				WARN_AT(&inits[1]->where, "excess initaliser%s", inits[2] ? "s" : "");

			/* this seems to be called when it shouldn't... */
			decl_initialise_scalar(&new_iter, tfor, base, init_code);
			goto fin;
		}

		assert(dinit->type == decl_init_scalar);
		assign_from = dinit->bits.expr;

		INIT_DEBUG("scalar %s (%ld)\n",
				assign_from->f_str(),
				assign_from->bits.iv.val);
	}else{
		/* implicit cast (alternatively allow assignment to pointers from the
		 * constant 0) */

		assign_from = expr_new_cast(tfor, 1);
		assign_from->expr = expr_new_val(0);
		INIT_DEBUG("scalar zero\n");
	}

	assign_init = expr_new_assign(base, assign_from);
	assign_init->assign_is_init = 1;

	dynarray_add(&init_code->codes,
			expr_to_stmt(assign_init, init_code->symtab));

	if(dinit)
fin: init_iter_adv(init_iter, 1); /* we've used this init */
}

static type_ref *decl_init_create_assignments(
		decl_init_iter *init_iter,
		type_ref *const tfor_wrapped, /* could be typedef/cast */
		expr *base,
		stmt *init_code)
{
	/* iterate over tfor's array/struct members/scalar,
	 * pulling from dinit as necessary */
	type_ref *tfor, *tfor_ret = tfor_wrapped;
	struct_union_enum_st *sue;

	if((tfor = type_ref_is(tfor_wrapped, type_ref_array))){
		tfor_ret = decl_initialise_array(init_iter, tfor, base, init_code);

	}else if((sue = type_ref_is_s_or_u(tfor_wrapped))){
		decl_initialise_sue(init_iter, sue, base, init_code);

	}else{
		decl_initialise_scalar(init_iter, tfor_wrapped, base, init_code);
	}

	return tfor_ret;
}

static void decl_init_create_assignments_discard(
		decl_init_iter *init_iter, type_ref *const tfor_wrapped,
		expr *base, stmt *init_code)
{
	type_ref *t = decl_init_create_assignments(init_iter, tfor_wrapped, base, init_code);

	if(t != tfor_wrapped)
		type_ref_free_1(t);
}

static type_ref *decl_init_create_assignments_from_init(
		decl_init *single_init,
		type_ref *const tfor_wrapped, /* could be typedef/cast */
		expr *base,
		stmt *init_code)
{
	decl_init *ar[] = { single_init, NULL };
	decl_init_iter it = { ar };
	struct_union_enum_st *sue;

	/* init validity checks */
	if(single_init->type == decl_init_scalar){
		type_ref *tar = NULL;

		if((sue = type_ref_is_s_or_u(tfor_wrapped))
		|| (tar = type_ref_is(       tfor_wrapped, type_ref_array)))
		{
			if(type_ref_is_type(type_ref_next(tar), type_char)){
				/* is char[] */
				expr *e = single_init->bits.expr;

				if(expr_kind(e, str))
					goto fine; /* arg is char * */
			}

			DIE_AT(&single_init->where, "%s must be initalised with an initialiser list",
					sue ? sue_str(sue) : "array");
		}
	}

fine:
	return decl_init_create_assignments(
			&it, tfor_wrapped, base, init_code);
}

void decl_init_create_assignments_for_spel(decl *d, stmt *init_code)
{
	d->ref = decl_init_create_assignments_from_init(
			d->init, d->ref,
			expr_new_identifier(d->spel), init_code);
}

void decl_init_create_assignments_for_base(decl *d, expr *base, stmt *init_code)
{
	d->ref = decl_init_create_assignments_from_init(
			d->init, d->ref, base, init_code);
}
