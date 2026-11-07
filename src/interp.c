#include <stdlib.h>
#include <string.h>
#include "ast.h"

#define MAX_STEPS 10000000L
#define MAX_DEPTH 10000

typedef enum { F_NORMAL, F_BREAK, F_CONTINUE, F_RETURN } Flow;

extern int global_size;
static long long *globals, *frame, ret_val;
static long steps;
static int call_depth;

static void fail(Node *n, const char *msg) {
    fflush(stdout);
    error_at(n->line, n->col, "runtime: %s", msg);
    exit(1);
}

static void tick(Node *n) {
    if (++steps > MAX_STEPS) fail(n, "step limit exceeded");
}

static long long eval(Node *n);
static Flow exec(Node *n);

static long long *addr(Node *n) {
    long long *base = n->sym->global ? globals : frame;
    if (n->kind == N_VAR) return base + n->sym->offset;
    long long i = eval(n->kids[0]);
    if (i < 0 || i >= n->sym->size) fail(n, "array index out of bounds");
    return base + n->sym->offset + i;
}

static long long arith(Node *n, char op, long long a, long long b) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': if (!b) fail(n, "division by zero"); return a / b;
    case '%': if (!b) fail(n, "division by zero"); return a % b;
    }
    return 0;
}

static long long compare(const char *op, long long a, long long b) {
    if (!strcmp(op, "==")) return a == b;
    if (!strcmp(op, "!=")) return a != b;
    if (!strcmp(op, "<")) return a < b;
    if (!strcmp(op, "<=")) return a <= b;
    if (!strcmp(op, ">")) return a > b;
    return a >= b;
}

static long long call(Node *n) {
    Sym *f = n->sym;
    long long *fr = calloc(f->frame_size + 1, sizeof *fr);
    for (int i = 0; i < n->n; i++) fr[i] = eval(n->kids[i]);
    if (++call_depth > MAX_DEPTH) fail(n, "recursion depth exceeded");
    long long *saved = frame;
    frame = fr;
    long long r = exec(f->def->kids[1]) == F_RETURN ? ret_val : 0;
    frame = saved;
    call_depth--;
    free(fr);
    return r;
}

static long long eval(Node *n) {
    tick(n);
    switch (n->kind) {
    case N_NUM: return n->value;
    case N_VAR: case N_INDEX: return *addr(n);
    case N_CALL: return call(n);
    case N_ASSIGN: {
        long long *p = addr(n->kids[0]);
        long long v = eval(n->kids[1]);
        return *p = n->op[0] == '=' ? v : arith(n, n->op[0], *p, v);
    }
    case N_BINARY: {
        long long a = eval(n->kids[0]), b = eval(n->kids[1]);
        return strchr("+-*/%", n->op[0]) && !n->op[1] ? arith(n, n->op[0], a, b) : compare(n->op, a, b);
    }
    case N_LOGICAL:
        if (n->op[0] == '&') return eval(n->kids[0]) && eval(n->kids[1]);
        return eval(n->kids[0]) || eval(n->kids[1]);
    case N_UNARY: {
        long long v = eval(n->kids[0]);
        return n->op[0] == '-' ? -v : !v;
    }
    case N_PRE: case N_POST: {
        long long *p = addr(n->kids[0]), old = *p;
        *p += n->op[0] == '+' ? 1 : -1;
        return n->kind == N_PRE ? *p : old;
    }
    case N_EMPTY: return 1;
    default: return 0;
    }
}

static void print(Node *n) {
    int arg = 0;
    for (const char *p = n->name; *p; p++) {
        if (*p != '%') { putchar(*p); continue; }
        if (*++p == 'd') printf("%lld", eval(n->kids[arg++]));
        else putchar('%');
    }
}

static Flow exec(Node *n) {
    tick(n);
    Flow f;
    switch (n->kind) {
    case N_BLOCK:
        for (int i = 0; i < n->n; i++)
            if ((f = exec(n->kids[i])) != F_NORMAL) return f;
        return F_NORMAL;
    case N_VAR_DECL:
        frame[n->sym->offset] = n->n ? eval(n->kids[0]) : 0;
        return F_NORMAL;
    case N_ARRAY_DECL:
        memset(frame + n->sym->offset, 0, n->sym->size * sizeof *frame);
        return F_NORMAL;
    case N_IF:
        if (eval(n->kids[0])) return exec(n->kids[1]);
        return n->n > 2 ? exec(n->kids[2]) : F_NORMAL;
    case N_WHILE:
        while (eval(n->kids[0])) {
            if ((f = exec(n->kids[1])) == F_BREAK) break;
            if (f == F_RETURN) return f;
        }
        return F_NORMAL;
    case N_DO:
        do {
            if ((f = exec(n->kids[0])) == F_BREAK) break;
            if (f == F_RETURN) return f;
        } while (eval(n->kids[1]));
        return F_NORMAL;
    case N_FOR:
        for (eval(n->kids[0]); eval(n->kids[1]); eval(n->kids[2])) {
            if ((f = exec(n->kids[3])) == F_BREAK) break;
            if (f == F_RETURN) return f;
        }
        return F_NORMAL;
    case N_BREAK: return F_BREAK;
    case N_CONTINUE: return F_CONTINUE;
    case N_RETURN:
        ret_val = n->n ? eval(n->kids[0]) : 0;
        return F_RETURN;
    case N_PRINTF: print(n); return F_NORMAL;
    case N_EXPR_STMT: eval(n->kids[0]); return F_NORMAL;
    default: return F_NORMAL;
    }
}

int interp(Node *program) {
    globals = calloc(global_size + 1, sizeof *globals);
    Node *main_call = NULL;
    for (int i = 0; i < program->n; i++) {
        Node *k = program->kids[i];
        if (k->kind == N_VAR_DECL && k->n) globals[k->sym->offset] = eval(k->kids[0]);
        if (k->kind == N_FUNC && !strcmp(k->name, "main")) main_call = k;
    }
    Node call_node = *main_call;
    call_node.kind = N_CALL;
    call_node.n = 0;
    long long r = call(&call_node);
    fflush(stdout);
    return (int)(r & 255);
}
