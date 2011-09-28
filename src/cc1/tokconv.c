#include <stdio.h>
#include <stdarg.h>

#include "tokenise.h"
#include "tree.h"
#include "tokconv.h"
#include "util.h"
#include "macros.h"

extern enum token curtok;

enum type_primitive curtok_to_type_primitive()
{
	switch(curtok){
		case token_int:  return type_int;
		case token_char: return type_char;
		case token_void: return type_void;
		default: break;
	}
	return type_unknown;
}

enum type_spec curtok_to_type_specifier()
{
	switch(curtok){
		case token_const:  return spec_const;
		case token_extern: return spec_extern;
		case token_static: return spec_static;
		default: break;
	}
	return spec_none;
}

enum op_type curtok_to_op()
{
	switch(curtok){
		/* multiply - op_deref is handled by the parser */
		case token_multiply: return op_multiply;

		case token_divide: return op_divide;
		case token_plus: return op_plus;
		case token_minus: return op_minus;
		case token_modulus: return op_modulus;

		case token_eq: return op_eq;
		case token_ne: return op_ne;
		case token_le: return op_le;
		case token_lt: return op_lt;
		case token_ge: return op_ge;
		case token_gt: return op_gt;

		case token_or: return op_or;
		case token_and: return op_and;
		case token_orsc: return op_orsc;
		case token_andsc: return op_andsc;
		case token_not: return op_not;
		case token_bnot: return op_bnot;

		default: break;
	}

	return op_unknown;
}

int curtok_is_type()
{
	return curtok_to_type_primitive() != type_unknown;
}

int curtok_is_type_specifier()
{
	return curtok_to_type_specifier() != spec_none;
}

int curtok_is_type_prething()
{
	return curtok_is_type() || curtok_is_type_specifier();
}

const char *token_to_str(enum token t)
{
	switch(t){
		CASE_STR_PREFIX(token,  do);               CASE_STR_PREFIX(token,  if);             CASE_STR_PREFIX(token,  else);            CASE_STR_PREFIX(token,  while);
		CASE_STR_PREFIX(token,  for);              CASE_STR_PREFIX(token,  break);          CASE_STR_PREFIX(token,  return);          CASE_STR_PREFIX(token,  switch);
		CASE_STR_PREFIX(token,  case);             CASE_STR_PREFIX(token,  default);        CASE_STR_PREFIX(token,  sizeof);          CASE_STR_PREFIX(token,  extern);
		CASE_STR_PREFIX(token,  identifier);       CASE_STR_PREFIX(token,  integer);        CASE_STR_PREFIX(token,  character);       CASE_STR_PREFIX(token,  void);
		CASE_STR_PREFIX(token,  char);             CASE_STR_PREFIX(token,  int);            CASE_STR_PREFIX(token,  elipsis);         CASE_STR_PREFIX(token,  string);
		CASE_STR_PREFIX(token,  open_paren);       CASE_STR_PREFIX(token,  open_block);     CASE_STR_PREFIX(token,  open_square);     CASE_STR_PREFIX(token,  close_paren);
		CASE_STR_PREFIX(token,  close_block);      CASE_STR_PREFIX(token,  close_square);   CASE_STR_PREFIX(token,  comma);           CASE_STR_PREFIX(token,  semicolon);
		CASE_STR_PREFIX(token,  colon);            CASE_STR_PREFIX(token,  plus);           CASE_STR_PREFIX(token,  minus);           CASE_STR_PREFIX(token,  multiply);
		CASE_STR_PREFIX(token,  divide);           CASE_STR_PREFIX(token,  modulus);        CASE_STR_PREFIX(token,  increment);       CASE_STR_PREFIX(token,  decrement);
		CASE_STR_PREFIX(token,  assign);           CASE_STR_PREFIX(token,  dot);            CASE_STR_PREFIX(token,  eq);              CASE_STR_PREFIX(token,  le);
		CASE_STR_PREFIX(token,  lt);               CASE_STR_PREFIX(token,  ge);             CASE_STR_PREFIX(token,  gt);              CASE_STR_PREFIX(token,  ne);
		CASE_STR_PREFIX(token,  not);              CASE_STR_PREFIX(token,  bnot);           CASE_STR_PREFIX(token,  andsc);           CASE_STR_PREFIX(token,  and);
		CASE_STR_PREFIX(token,  orsc);             CASE_STR_PREFIX(token,  or);             CASE_STR_PREFIX(token,  eof);             CASE_STR_PREFIX(token,  unknown);
		CASE_STR_PREFIX(token,  const);            CASE_STR_PREFIX(token,  question);       CASE_STR_PREFIX(token,  plus_assign);     CASE_STR_PREFIX(token,  minus_assign);
		CASE_STR_PREFIX(token,  multiply_assign);  CASE_STR_PREFIX(token,  divide_assign);  CASE_STR_PREFIX(token,  modulus_assign);  CASE_STR_PREFIX(token,  not_assign);
		CASE_STR_PREFIX(token,  bnot_assign);      CASE_STR_PREFIX(token,  and_assign);     CASE_STR_PREFIX(token,  or_assign);       CASE_STR_PREFIX(token,  static);
	}
	return NULL;
}

void eat(enum token t, const char *fnam, int line)
{
	if(t != curtok)
		die_at(NULL, "expecting token %s, got %s (%s:%d)",
				token_to_str(t), token_to_str(curtok), fnam, line);
	nexttoken();
}

int curtok_in_list(va_list l)
{
	enum token t;
	while((t = va_arg(l, enum token)) != token_unknown)
		if(curtok == t)
			return 1;
	return 0;
}

#define NULL_AND_RET(fnam, cnam)  \
char *fnam()                      \
{                                 \
	extern char *cnam;              \
	char *ret = cnam;               \
	cnam = NULL;                    \
	return ret;                     \
}

NULL_AND_RET(token_current_spel, currentspelling)

void token_get_current_str(char **ps, int *pl)
{
	extern char *currentstring;
	extern int   currentstringlen;

	*ps = currentstring;
	*pl = currentstringlen;

	currentstring = NULL;
}
