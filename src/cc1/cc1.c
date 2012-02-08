#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#include <unistd.h>
#include <signal.h>

#include "../util/util.h"
#include "tree.h"
#include "tokenise.h"
#include "../util/util.h"
#include "parse.h"
#include "fold.h"
#include "gen_asm.h"
#include "gen_str.h"
#include "sym.h"
#include "fold_sym.h"
#include "cc1.h"

struct
{
	int is_opt;
	const char *arg;
	int mask;
} args[] = {
	{ 0,  "all",             ~0 },
	{ 0,  "extra",            0 },

	{ 0,  "mismatch-arg",    WARN_ARG_MISMATCH                      },
	{ 0,  "array-comma",     WARN_ARRAY_COMMA                       },
	{ 0,  "mismatch-assign", WARN_ASSIGN_MISMATCH | WARN_COMPARE_MISMATCH },
	{ 0,  "return-type",     WARN_RETURN_TYPE                       },

	{ 0,  "sign-compare",    WARN_SIGN_COMPARE                      },
	{ 0,  "extern-assume",   WARN_EXTERN_ASSUME                     },

	{ 0,  "implicit-int",    WARN_IMPLICIT_INT                      },
	{ 0,  "implicit-func",   WARN_IMPLICIT_FUNC                     },
	{ 0,  "implicit",        WARN_IMPLICIT_FUNC | WARN_IMPLICIT_INT },

	/* TODO */
	{ 0,  "unused-parameter", WARN_UNUSED_PARAM },
	{ 0,  "unused-variable",  WARN_UNUSED_VAR   },
	{ 0,  "unused-value",     WARN_UNUSED_VAL   },
	{ 0,  "unused",           WARN_UNUSED_PARAM | WARN_UNUSED_VAR | WARN_UNUSED_VAL },

	/* TODO */
	{ 0,  "uninitialised",    WARN_UNINITIALISED },

	/* TODO */
	{ 0,  "array-bounds",     WARN_ARRAY_BOUNDS },

	/* TODO */
	{ 0,  "shadow",           WARN_SHADOW },

	/* TODO */
	{ 0,  "format",           WARN_FORMAT },

	/* TODO */
	{ 0,  "pointer-arith",    WARN_PTR_ARITH  }, /* void *x; x++; */
	{ 0,  "int-ptr-cast",     WARN_INT_TO_PTR },

	{ 0,  "optimisation",     WARN_OPT_POSSIBLE },


/* --- options --- */

	{ 1,  "enable-asm",    FOPT_ENABLE_ASM   },
	{ 1,  "strict-types",  FOPT_STRICT_TYPES },
	{ 1,  "const-fold",    FOPT_CONST_FOLD   },

	{ 0,  NULL, 0 }
};


FILE *cc_out[NUM_SECTIONS];     /* temporary section files */
char  fnames[NUM_SECTIONS][32]; /* duh */
FILE *cc1_out;                  /* final output */

enum warning warn_mode = ~(WARN_VOID_ARITH | WARN_COMPARE_MISMATCH | WARN_IMPLICIT_INT);
enum fopt    fopt_mode = FOPT_CONST_FOLD;

int caught_sig = 0;

const char *section_names[NUM_SECTIONS] = {
	"text", "data", "bss"
};


/* compile time check for enum <-> int compat */
#define COMP_CHECK(pre, test) \
struct unused_ ## pre { char check[test ? -1 : 1]; }
COMP_CHECK(a, sizeof warn_mode != sizeof(int));
COMP_CHECK(b, sizeof fopt_mode != sizeof(int));


void ccdie(int verbose, const char *fmt, ...)
{
	int i = strlen(fmt);
	va_list l;

	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);

	if(fmt[i-1] == ':'){
		fputc(' ', stderr);
		perror(NULL);
	}else{
		fputc('\n', stderr);
	}

	if(verbose){
		fputs("warnings:\n", stderr);
		for(i = 0; args[i].arg; i++)
			fprintf(stderr, "  -%c%s\n", args[i].is_opt["Wf"], args[i].arg);
	}

	exit(1);
}

void cc1_warn_atv(struct where *where, int die, enum warning w, const char *fmt, va_list l)
{
	if(!die && (w & warn_mode) == 0)
		return;

	vwarn(where, fmt, l);

	if(die)
		exit(1);
}

void cc1_warn_at(struct where *where, int die, enum warning w, const char *fmt, ...)
{
	va_list l;

	va_start(l, fmt);
	cc1_warn_atv(where, die, w, fmt, l);
	va_end(l);
}

void io_cleanup(void)
{
	int i;
	for(i = 0; i < NUM_SECTIONS; i++){
		if(fclose(cc_out[i]) == EOF && !caught_sig)
			fprintf(stderr, "close %s: %s\n", fnames[i], strerror(errno));
		if(remove(fnames[i]) && !caught_sig)
			fprintf(stderr, "remove %s: %s\n", fnames[i], strerror(errno));
	}
}

void io_setup(void)
{
	int i;

	if(!cc1_out)
		cc1_out = stdout;

	for(i = 0; i < NUM_SECTIONS; i++){
		snprintf(fnames[i], sizeof fnames[i], "/tmp/cc1_%d%d", getpid(), i);

		cc_out[i] = fopen(fnames[i], "w+"); /* need to seek */
		if(!cc_out[i])
			ccdie(0, "open \"%s\":", fnames[i]);
	}

	atexit(io_cleanup);
}

void io_fin(int do_sections)
{
	int i;

	for(i = 0; i < NUM_SECTIONS; i++){
		/* cat cc_out[i] to cc1_out, with section headers */
		if(do_sections){
			char buf[256];
			long last = ftell(cc_out[i]);

			if(last == -1 || fseek(cc_out[i], 0, SEEK_SET) == -1)
				ccdie(0, "seeking on section file %d:", i);

			if(fprintf(cc1_out, "section .%s\n", section_names[i]) < 0)
				ccdie(0, "write to cc1 output:");

			while(fgets(buf, sizeof buf, cc_out[i]))
				if(fputs(buf, cc1_out) == EOF)
					ccdie(0, "write to cc1 output:");

			if(ferror(cc_out[i]))
				ccdie(0, "read from section file %d:", i);
		}
	}

	if(fclose(cc1_out))
		ccdie(0, "close cc1 output");
}

void sigh(int sig)
{
	(void)sig;
	caught_sig = 1;
	io_cleanup();
}

int main(int argc, char **argv)
{
	static symtable *globs;
	void (*gf)(symtable *);
	FILE *f;
	const char *fname;
	int i;

	/*signal(SIGINT , sigh);*/
	signal(SIGQUIT, sigh);
	signal(SIGTERM, sigh);
	signal(SIGABRT, sigh);
	signal(SIGSEGV, sigh);

	gf = gen_asm;
	fname = NULL;

	for(i = 1; i < argc; i++){
		if(!strcmp(argv[i], "-X")){
			if(++i == argc)
				goto usage;

			if(!strcmp(argv[i], "print"))
				gf = gen_str;
			else if(!strcmp(argv[i], "asm"))
				gf = gen_asm;
			else
				goto usage;

		}else if(!strcmp(argv[i], "-o")){
			if(++i == argc)
				goto usage;

			if(strcmp(argv[i], "-")){
				cc1_out = fopen(argv[i], "w");
				if(!cc1_out){
					ccdie(0, "open %s:", argv[i]);
					return 1;
				}
			}

		}else if(!strcmp(argv[i], "-w")){
			warn_mode = WARN_NONE;

		}else if(argv[i][0] == '-' && (argv[i][1] == 'W' || argv[i][1] == 'f')){
			char *arg = argv[i] + 2;
			int *mask;
			int j, found, rev;

			j = rev = found = 0;

			if(!strncmp(arg, "no-", 3)){
				arg += 3;
				rev = 1;
			}

			if(argv[i][1] == 'f'){
				mask = (int *)&fopt_mode;
				for(; !args[j].is_opt; j++);
			}else{
				mask = (int *)&warn_mode;
			}

			for(; args[j].arg; j++)
				if(!strcmp(arg, args[j].arg)){
					if(rev)
						*mask &= ~args[j].mask;
					else
						*mask |=  args[j].mask;
					found = 1;
					break;
				}

			if(!found){
				fprintf(stderr, "\"%s\" unrecognised\n", argv[i]);
				goto usage;
			}

		}else if(!fname){
			fname = argv[i];
		}else{
usage:
			ccdie(1, "Usage: %s [-W[no-]warning] [-f[no-]option] [-X backend] [-o output] file", *argv);
		}
	}

	if(fname && strcmp(fname, "-")){
		f = fopen(fname, "r");
		if(!f)
			ccdie(0, "open %s:", fname);
	}else{
		f = stdin;
		fname = "-";
	}

	io_setup();

	tokenise_set_file(f, fname);
	globs = parse();
	if(f != stdin)
		fclose(f);

	if(globs->decls){
		fold(globs);
		symtab_fold(globs, 0);
		gf(globs);
	}

	io_fin(gf == gen_asm);

	return 0;
}
