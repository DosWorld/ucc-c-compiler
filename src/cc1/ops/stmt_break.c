#include <string.h>

#include "ops.h"
#include "stmt_break.h"

const char *str_stmt_break()
{
	return "break";
}

void fold_stmt_break_continue(stmt *t, const char *desc, char *lbl)
{
	if(!lbl)
		die_at(&t->where, "%s outside a flow-control statement", desc);

	t->expr = expr_new_identifier(lbl);
	memcpy(&t->expr->where, &t->where, sizeof t->expr->where);

	t->expr->tree_type = decl_new();
	t->expr->tree_type->type->primitive = type_void;
}

void fold_stmt_break(stmt *t)
{
	char *lbl;

	/* break out of switch if that was the last statement to be in */
	if(curstmt_last_was_switch && curstmt_switch)
		lbl = curstmt_switch->lbl_break;
	else
		lbl = curstmt_flow ? curstmt_flow->lbl_break : NULL;

	fold_stmt_break_continue(t, "break", lbl);
}

func_gen_stmt *gen_stmt_break = gen_stmt_goto;
