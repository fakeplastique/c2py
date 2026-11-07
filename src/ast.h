#ifndef AST_H
#define AST_H

#include <stdio.h>

typedef enum {
    N_PROGRAM, N_FUNC, N_PARAMS, N_PARAM, N_VAR_DECL, N_ARRAY_DECL,
    N_BLOCK, N_IF, N_WHILE, N_DO, N_FOR, N_BREAK, N_CONTINUE, N_RETURN,
    N_EXPR_STMT, N_EMPTY, N_PRINTF,
    N_NUM, N_VAR, N_INDEX, N_CALL, N_ASSIGN, N_BINARY, N_LOGICAL,
    N_UNARY, N_PRE, N_POST
} Kind;

typedef enum { S_VAR, S_ARRAY, S_FUNC } SymKind;

typedef struct Node Node;

typedef struct Sym {
    char *name;
    char *pyname;
    SymKind kind;
    int global;
    int offset;
    int size;
    int nparams;
    int is_void;
    int frame_size;
    int used;
    Node *def;
} Sym;

struct Node {
    Kind kind;
    const char *op;
    char *name;
    long long value;
    int line, col;
    int n, cap;
    Node **kids;
    Sym *sym;
};

Node *node(Kind kind, int line, int col);
Node *add(Node *parent, Node *child);
Node *node1(Kind kind, int line, int col, Node *a);
Node *node2(Kind kind, int line, int col, Node *a, Node *b);
void ast_json(FILE *out, Node *n);
const char *kind_name(Kind k);

extern const char *src_file;
extern int error_count;
void error_at(int line, int col, const char *fmt, ...);

int sema(Node *program);
int interp(Node *program);
void pygen(FILE *out, Node *program);

#endif
