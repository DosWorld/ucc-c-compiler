#include <stdlib.h>
#include <string.h>

#include "ops.h"
#include "stmt_code.h"
#include "../out/lbl.h"


const char *str_stmt_code()
{
	return "code";
}

void fold_stmt_code(stmt *s)
{
	decl **diter;
	stmt **siter;
	int warned = 0;

	fold_symtab_scope(s->symtab);

	/* NOTE: this only folds + adds decls, not args */
	for(diter = s->decls; diter && *diter; diter++){
		decl *d = *diter;

		if(d->func_code)
			DIE_AT(&d->func_code->where, "can't nest functions");
		else if(decl_is_func(d) && d->store == store_static)
			DIE_AT(&d->where, "block-scoped function cannot have static storage");

		fold_decl(d, s->symtab);

		if(d->init){
			/* this creates the below s->inits array */
			if(d->store == store_static){
				fold_decl_global_init(d, s->symtab);
			}else{
				EOF_WHERE(&d->where,
						fold_gen_init_assignment(d, s)
					);

				/* folded below */
			}
		}

		d->is_definition = 1; /* always the def for non-globals */

		SYMTAB_ADD(s->symtab, d,
				decl_store_static_or_extern(d->store) ? sym_global : sym_local);
	}

	for(siter = s->inits; siter && *siter; siter++){
		stmt *const st = *siter;
		EOF_WHERE(&st->where, fold_stmt(st));
	}

	for(siter = s->codes; siter && *siter; siter++){
		stmt  *const st = *siter;

		EOF_WHERE(&st->where, fold_stmt(st));

		/*
		 * check for dead code
		 */
		if(!warned
		&& st->kills_below_code
		&& siter[1]
		&& !stmt_kind(siter[1], label)
		&& !stmt_kind(siter[1], case)
		&& !stmt_kind(siter[1], default)
		){
			cc1_warn_at(&siter[1]->where, 0, 1, WARN_DEAD_CODE, "dead code after %s (%s)", st->f_str(), siter[1]->f_str());
			warned = 1;
		}
	}

	/* static folding */
	if(s->decls){
		decl **siter;

		for(siter = s->decls; *siter; siter++){
			decl *d = *siter;
			/*
			 * check static decls - after we fold,
			 * so we've linked the syms and can change ->spel
			 */
			if(d->store == store_static){
				char *old = d->spel;
				d->spel = out_label_static_local(curdecl_func->spel, d->spel);
				free(old);
			}
		}
	}
}

void gen_code_decls(symtable *stab)
{
	decl **diter;

	/* declare statics */
	for(diter = stab->decls; diter && *diter; diter++){
		decl *d = *diter;
		int func;

		if((func = !!type_ref_is(d->ref, type_ref_func)) || decl_store_static_or_extern(d->store)){
			int gen = 1;

			if(func){
				/* check if the func is defined globally */
				symtable *globs;
				decl **i;

				for(globs = stab; globs->parent; globs = globs->parent);

				for(i = globs->decls; i && *i; i++){
					if(!strcmp(d->spel, (*i)->spel)){
						gen = 0;
						break;
					}
				}
			}

			if(gen)
				gen_asm_global(d);
		}
	}
}

void gen_stmt_code(stmt *s)
{
	stmt **titer;
	int done_inits;

	/* stmt_for needs to do this too */
	gen_code_decls(s->symtab);

	FOR_INIT_AND_CODE(titer, s, done_inits,
		gen_stmt(*titer);
	)
}

static int code_passable(stmt *s)
{
	stmt **i;
	int done_inits;

	/* note: check for inits which call noreturn funcs */

	FOR_INIT_AND_CODE(i, s, done_inits,
		stmt *sub = *i;
		if(!fold_passable(sub))
			return 0;
	)

	return 1;
}

void mutate_stmt_code(stmt *s)
{
	s->f_passable = code_passable;
}
