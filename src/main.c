#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

extern FILE *yyin;
extern int yydebug;
extern Node *root;
int yyparse(void);

static int usage(void) {
    fputs("usage: c2py file.c [--ast-json] [--run] [--py out.py] [--trace]\n", stderr);
    return 2;
}

int main(int argc, char **argv) {
    const char *file = NULL, *py = NULL;
    int json = 0, run = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ast-json")) json = 1;
        else if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--trace")) yydebug = 1;
        else if (!strcmp(argv[i], "--py") && i + 1 < argc) py = argv[++i];
        else if (argv[i][0] == '-' || file) return usage();
        else file = argv[i];
    }
    if (!file) return usage();
    if (!(yyin = fopen(file, "r"))) { perror(file); return 1; }
    src_file = file;

    if (yyparse() || error_count) return 1;
    if (json) { ast_json(stdout, root); fputc('\n', stdout); }
    if (sema(root)) return 1;
    if (py) {
        FILE *out = strcmp(py, "-") ? fopen(py, "w") : stdout;
        if (!out) { perror(py); return 1; }
        pygen(out, root);
        if (out != stdout) fclose(out);
    }
    if (run) { fflush(stdout); return interp(root); }
    return 0;
}
