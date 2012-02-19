#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "../util/util.h"
#include "tree.h"
#include "cc1.h"
#include "gen_asm.h"
#include "asm.h"
#include "../util/util.h"
#include "asm_op.h"

static void asm_idiv(expr *e, symtable *tab)
{
	/*
	 * idiv — Integer Division
	 * The idiv asm_temp divides the contents of the 64 bit integer EDX:EAX (constructed by viewing EDX as the most significant four bytes and EAX as the least significant four bytes) by the specified operand value. The quotient result of the division is stored into EAX, while the remainder is placed in EDX.
	 * Syntax
	 * idiv <reg32>
	 * idiv <mem>
	 *
	 * Examples
	 *
	 * idiv ebx — divide the contents of EDX:EAX by the contents of EBX. Place the quotient in EAX and the remainder in EDX.
	 * idiv DWORD PTR [var] — divide the contents of EDX:EAS by the 32-bit value stored at memory location var. Place the quotient in EAX and the remainder in EDX.
	 */

	walk_expr(e->lhs, tab);
	walk_expr(e->rhs, tab);
	/* pop top stack (rhs) into b, and next top into a */

	asm_temp(1, "xor rdx,rdx");
	asm_temp(1, "pop rbx");
	asm_temp(1, "pop rax");
	asm_temp(1, "idiv rbx");

	asm_temp(1, "push r%cx", e->op == op_divide ? 'a' : 'd');
}

static void asm_compare(expr *e, symtable *tab)
{
	const char *cmp = NULL;

	walk_expr(e->lhs, tab);
	walk_expr(e->rhs, tab);
	asm_temp(1, "pop rbx");
	asm_temp(1, "pop rax");
	asm_temp(1, "xor rcx,rcx"); /* must be before cmp */
	asm_temp(1, "cmp rax,rbx");

	/* check for unsigned, since signed isn't explicitly set */
#define SIGNED(s, u) e->tree_type->type->spec & spec_unsigned ? u : s

	switch(e->op){
		case op_eq: cmp = "e";  break;
		case op_ne: cmp = "ne"; break;

		case op_le: cmp = SIGNED("le", "be"); break;
		case op_lt: cmp = SIGNED("l",  "b");  break;
		case op_ge: cmp = SIGNED("ge", "ae"); break;
		case op_gt: cmp = SIGNED("g",  "a");  break;

		default:
			ICE("asm_compare: unhandled comparison");
	}

	asm_temp(1, "set%s cl", cmp);
	asm_temp(1, "push rcx");
}

static void asm_shortcircuit(expr *e, symtable *tab)
{
	char *baillabel = asm_label_code("shortcircuit_bail");
	walk_expr(e->lhs, tab);

	asm_temp(1, "mov rax,[rsp]");
	asm_temp(1, "test rax,rax");
	/* leave the result on the stack (if false) and bail */
	asm_temp(1, "j%sz %s", e->op == op_andsc ? "" : "n", baillabel);
	asm_temp(1, "pop rax");
	walk_expr(e->rhs, tab);

	asm_label(baillabel);
	free(baillabel);
}

void asm_operate_struct(expr *e, symtable *tab)
{
	(void)tab;

	UCC_ASSERT(e->op == op_struct_ptr, "a.b should have been handled by now");

	walk_expr(e->lhs, tab);

	/* pointer to the struct is on the stack, get from the offset */
	asm_temp(1, "pop rax ; struct ptr");
	asm_temp(1, "sub rax, %d ; offset of member %s",
			e->rhs->tree_type->struct_offset,
			e->rhs->spel);
	asm_temp(1, "mov rax, [rax] ; val from struct");
	asm_temp(1, "push rax");
}

void asm_operate(expr *e, symtable *tab)
{
	const char *instruct = NULL;
	const char *rhs = "rcx";

	switch(e->op){
		/* normal mafs */
		case op_multiply: instruct = "imul"; break;
		case op_plus:     instruct = "add";  break;
		case op_xor:      instruct = "xor";  break;
		case op_or:       instruct = "or";   break;
		case op_and:      instruct = "and";  break;

		/* single register op */
		case op_minus: instruct = e->rhs ? "sub" : "neg"; break;
		case op_bnot:  instruct = "not";                  break;

#define SHIFT(side) \
		case op_shift ## side: instruct = "sh" # side; rhs = "cl"; break

		SHIFT(l);
		SHIFT(r);

		case op_not:
			/* compare with 0 */
			walk_expr(e->lhs, tab);
			asm_temp(1, "xor rbx,rbx");
			asm_temp(1, "pop rax");
			asm_temp(1, "test rax,rax");
			asm_temp(1, "setz bl");
			asm_temp(1, "push rbx");
			return;

		case op_deref:
			walk_expr(e->lhs, tab);
			asm_temp(1, "pop rax");

			if(asm_type_size(e->tree_type) == ASM_SIZE_WORD)
				asm_temp(1, "mov rax, [rax]");
			else
				asm_temp(1, "movzx rax, byte [rax]");

			asm_temp(1, "push rax");
			return;

		case op_struct_dot:
		case op_struct_ptr:
			asm_operate_struct(e, tab);
			return;

		/* comparison */
		case op_eq:
		case op_ne:
		case op_le:
		case op_lt:
		case op_ge:
		case op_gt:
			asm_compare(e, tab);
			return;

		/* shortcircuit */
		case op_orsc:
		case op_andsc:
			asm_shortcircuit(e, tab);
			return;

		case op_divide:
		case op_modulus:
			asm_idiv(e, tab);
			return;

		case op_unknown:
			ICE("asm_operate: unknown operator got through");
	}

	/* asm_temp(1, "%s rax", incr ? "inc" : "dec"); TODO: optimise */

	/* get here if op is *, +, - or ~ */
	walk_expr(e->lhs, tab);
	if(e->rhs){
		walk_expr(e->rhs, tab);
		asm_temp(1, "pop rcx");
		asm_temp(1, "pop rax");
		asm_temp(1, "%s rax, %s", instruct, rhs);
	}else{
		asm_temp(1, "pop rax");
		asm_temp(1, "%s rax", instruct);
	}

	asm_temp(1, "push rax");
}
