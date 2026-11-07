#include <stdlib.h>
#include <string.h>
#include "ast.h"

#define MAX_SYMS 8192

static Sym *table[MAX_SYMS];
static int scope_of[MAX_SYMS];
static int nsyms, depth, loops, frame;
static Sym *func;
static char *used_names[MAX_SYMS];
static int nused;
int global_size;

static const char *reserved[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "class", "def",
    "del", "elif", "except", "finally", "from", "global", "import", "in", "is", "lambda",
    "nonlocal", "not", "or", "pass", "raise", "try", "with", "yield",
    "print", "int", "bool", "abs", "sys", "_div", "_mod", "_upd", "_post", NULL
};

static int is_reserved(const char *s) {
    for (const char **r = reserved; *r; r++) if (!strcmp(*r, s)) return 1;
    return 0;
}

static int name_taken(const char *s) {
    for (int i = 0; i < nsyms; i++)
        if (scope_of[i] == 0 && !strcmp(table[i]->pyname, s)) return 1;
    for (int i = 0; i < nused; i++) if (!strcmp(used_names[i], s)) return 1;
    return 0;
}

static char *make_pyname(const char *name, int local) {
    char *base = malloc(strlen(name) + 2);
    strcpy(base, name);
    if (is_reserved(base)) strcat(base, "_");
    if (!local) return base;
    char *cand = strdup(base);
    for (int k = 1; name_taken(cand); k++) {
        free(cand);
        cand = malloc(strlen(base) + 16);
        sprintf(cand, "%s_%d", base, k);
    }
    free(base);
    used_names[nused++] = cand;
    return cand;
}

static Sym *lookup(const char *name) {
    for (int i = nsyms - 1; i >= 0; i--)
        if (!strcmp(table[i]->name, name)) return table[i];
    return NULL;
}

static Sym *declare(Node *n, SymKind kind) {
    for (int i = nsyms - 1; i >= 0 && scope_of[i] == depth; i--)
        if (!strcmp(table[i]->name, n->name)) {
            error_at(n->line, n->col, "redeclaration of '%s'", n->name);
            return n->sym = table[i];
        }
    if (nsyms == MAX_SYMS) { error_at(n->line, n->col, "too many symbols"); exit(1); }
    Sym *s = calloc(1, sizeof *s);
    s->name = n->name;
    s->kind = kind;
    s->global = depth == 0;
    s->pyname = make_pyname(n->name, !s->global);
    scope_of[nsyms] = depth;
    table[nsyms++] = s;
    return n->sym = s;
}

static void pop_scope(void) {
    while (nsyms && scope_of[nsyms - 1] == depth) nsyms--;
    depth--;
}

static void expr(Node *n, int want_value);

static Sym *resolve(Node *n, SymKind want) {
    Sym *s = lookup(n->name);
    if (!s) {
        error_at(n->line, n->col, "'%s' undeclared", n->name);
        int saved = depth;
        depth = 0;
        Node fake = *n;
        s = declare(&fake, want);
        depth = saved;
        for (int i = nsyms - 1; i > 0 && scope_of[i - 1] > 0; i--) {
            Sym *t = table[i]; table[i] = table[i - 1]; table[i - 1] = t;
            int d = scope_of[i]; scope_of[i] = scope_of[i - 1]; scope_of[i - 1] = d;
        }
        s->kind = want;
        s->def = n;
        s->nparams = n->n;
        return n->sym = s;
    }
    n->sym = s;
    if (s->kind != want) {
        static const char *what[] = {"a variable", "an array", "a function"};
        error_at(n->line, n->col, "'%s' is %s, not %s", n->name, what[s->kind], what[want]);
    }
    return s;
}

static void lvalue(Node *n) {
    if (n->kind == N_VAR || n->kind == N_INDEX) expr(n, 1);
    else { error_at(n->line, n->col, "lvalue required"); expr(n, 1); }
}

static void expr(Node *n, int want_value) {
    switch (n->kind) {
    case N_NUM: case N_EMPTY: return;
    case N_VAR: resolve(n, S_VAR); return;
    case N_INDEX: resolve(n, S_ARRAY); expr(n->kids[0], 1); return;
    case N_CALL: {
        Sym *s = resolve(n, S_FUNC);
        s->used = 1;
        if (s->kind == S_FUNC && s->nparams != n->n)
            error_at(n->line, n->col, "'%s' expects %d argument(s), got %d", n->name, s->nparams, n->n);
        if (s->kind == S_FUNC && s->is_void && want_value)
            error_at(n->line, n->col, "void value of '%s' used", n->name);
        for (int i = 0; i < n->n; i++) expr(n->kids[i], 1);
        return;
    }
    case N_ASSIGN: lvalue(n->kids[0]); expr(n->kids[1], 1); return;
    case N_PRE: case N_POST: lvalue(n->kids[0]); return;
    default:
        for (int i = 0; i < n->n; i++) expr(n->kids[i], 1);
    }
}

static void local_decl(Node *n) {
    if (n->kind == N_VAR_DECL) {
        if (n->n) expr(n->kids[0], 1);
        Sym *s = declare(n, S_VAR);
        s->offset = frame++;
    } else {
        if (n->value <= 0) error_at(n->line, n->col, "array size must be positive");
        Sym *s = declare(n, S_ARRAY);
        s->size = (int)n->value;
        s->offset = frame;
        frame += s->size;
    }
}

static void check_format(Node *n) {
    int want = 0;
    for (const char *p = n->name; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == 'd') want++;
        else if (*p != '%') { error_at(n->line, n->col, "unsupported format '%%%c'", *p ? *p : ' '); return; }
    }
    if (want != n->n)
        error_at(n->line, n->col, "printf format expects %d argument(s), got %d", want, n->n);
}

static void stmt(Node *n);

static void items(Node *n) {
    for (int i = 0; i < n->n; i++) {
        Node *k = n->kids[i];
        if (k->kind == N_VAR_DECL || k->kind == N_ARRAY_DECL) local_decl(k);
        else stmt(k);
    }
}

static void stmt(Node *n) {
    switch (n->kind) {
    case N_BLOCK: depth++; items(n); pop_scope(); return;
    case N_IF:
        expr(n->kids[0], 1);
        stmt(n->kids[1]);
        if (n->n > 2) stmt(n->kids[2]);
        return;
    case N_WHILE: expr(n->kids[0], 1); loops++; stmt(n->kids[1]); loops--; return;
    case N_DO: loops++; stmt(n->kids[0]); loops--; expr(n->kids[1], 1); return;
    case N_FOR:
        for (int i = 0; i < 3; i++) expr(n->kids[i], i == 1);
        loops++; stmt(n->kids[3]); loops--;
        return;
    case N_BREAK: case N_CONTINUE:
        if (!loops) error_at(n->line, n->col, "'%s' outside of loop", kind_name(n->kind));
        return;
    case N_RETURN:
        if (n->n && func->is_void) error_at(n->line, n->col, "void function '%s' returns a value", func->name);
        if (!n->n && !func->is_void) error_at(n->line, n->col, "return without value in function '%s'", func->name);
        if (n->n) expr(n->kids[0], 1);
        return;
    case N_PRINTF:
        check_format(n);
        for (int i = 0; i < n->n; i++) expr(n->kids[i], 1);
        return;
    case N_EXPR_STMT: expr(n->kids[0], 0); return;
    default: return;
    }
}

static void function(Node *n) {
    Node *params = n->kids[0];
    Sym *s = lookup(n->name);
    int is_void = n->op != NULL;
    if (s && s->kind == S_FUNC) {
        n->sym = s;
        if (s->nparams != params->n || s->is_void != is_void)
            error_at(n->line, n->col, "conflicting declaration of '%s'", n->name);
    } else {
        s = declare(n, S_FUNC);
        s->nparams = params->n;
        s->is_void = is_void;
    }
    if (n->n < 2) return;
    if (s->def) { error_at(n->line, n->col, "redefinition of '%s'", n->name); return; }
    s->def = n;
    func = s;
    frame = 0;
    nused = 0;
    depth = 1;
    for (int i = 0; i < params->n; i++) {
        Node *p = params->kids[i];
        if (!p->name) { error_at(p->line, p->col, "parameter name omitted"); continue; }
        declare(p, S_VAR)->offset = frame++;
    }
    items(n->kids[1]);
    pop_scope();
    s->frame_size = frame;
}

int sema(Node *program) {
    for (int i = 0; i < program->n; i++) {
        Node *k = program->kids[i];
        if (k->kind == N_FUNC) { function(k); continue; }
        if (k->kind == N_VAR_DECL && k->n) expr(k->kids[0], 1);
        Sym *s = declare(k, k->kind == N_VAR_DECL ? S_VAR : S_ARRAY);
        s->offset = global_size;
        s->size = k->kind == N_ARRAY_DECL ? (int)k->value : 0;
        if (k->kind == N_ARRAY_DECL && k->value <= 0) error_at(k->line, k->col, "array size must be positive");
        global_size += s->size ? s->size : 1;
    }
    Sym *m = lookup("main");
    if (!m || m->kind != S_FUNC || !m->def) error_at(1, 1, "no definition of 'main'");
    else if (m->is_void || m->nparams) error_at(m->def->line, m->def->col, "'main' must be 'int main()'");
    for (int i = 0; i < nsyms; i++)
        if (table[i]->kind == S_FUNC && table[i]->used && !table[i]->def)
            error_at(1, 1, "function '%s' is never defined", table[i]->name);
    return error_count;
}
