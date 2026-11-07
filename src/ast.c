#include "ast.h"
#include <stdarg.h>
#include <stdlib.h>

const char *src_file = "<stdin>";
int error_count = 0;

static const char *names[] = {
    "program", "function", "params", "param", "var_decl", "array_decl",
    "block", "if", "while", "do_while", "for", "break", "continue", "return",
    "expr_stmt", "empty", "printf",
    "num", "var", "index", "call", "assign", "binary", "logical",
    "unary", "pre", "post"
};

const char *kind_name(Kind k) { return names[k]; }

Node *node(Kind kind, int line, int col) {
    Node *n = calloc(1, sizeof *n);
    n->kind = kind;
    n->line = line;
    n->col = col;
    return n;
}

Node *add(Node *p, Node *c) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4;
        p->kids = realloc(p->kids, p->cap * sizeof *p->kids);
    }
    p->kids[p->n++] = c;
    return p;
}

Node *node1(Kind kind, int line, int col, Node *a) {
    return add(node(kind, line, col), a);
}

Node *node2(Kind kind, int line, int col, Node *a, Node *b) {
    return add(node1(kind, line, col, a), b);
}

void error_at(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: error: ", src_file, line, col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    error_count++;
}

static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 0x20) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

void ast_json(FILE *out, Node *n) {
    if (!n) { fputs("null", out); return; }
    fprintf(out, "{\"type\":\"%s\",\"line\":%d,\"col\":%d", names[n->kind], n->line, n->col);
    if (n->name) { fputs(",\"name\":", out); json_str(out, n->name); }
    if (n->op) { fputs(",\"op\":", out); json_str(out, n->op); }
    if (n->kind == N_NUM || n->kind == N_ARRAY_DECL) fprintf(out, ",\"value\":%lld", n->value);
    fputs(",\"children\":[", out);
    for (int i = 0; i < n->n; i++) {
        if (i) fputc(',', out);
        ast_json(out, n->kids[i]);
    }
    fputs("]}", out);
}
