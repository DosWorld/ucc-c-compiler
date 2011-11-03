#ifndef TOKCONV_H
#define TOKCONV_H

enum type_primitive curtok_to_type_primitive(void);
enum type_spec      curtok_to_type_specifier(void);
enum op_type        curtok_to_op(void);

void eat(enum token, const char *fnam, int line);

#define EAT(t) eat(t, __FILE__, __LINE__)

int curtok_is_type(void);
int curtok_is_type_specifier(void);
int curtok_is_type_prething(void);
int curtok_in_list(va_list l);

char *token_current_spel(void);
void token_get_current_str(char **ps, int *pl);

enum op_type curtok_to_augmented_op(void);
int          curtok_is_augmented_assignment(void);

const char *token_to_str(enum token t);

#endif
