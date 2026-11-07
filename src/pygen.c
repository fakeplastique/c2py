#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

enum { H_DIV = 1, H_MOD = 2, H_UPD = 4, H_POST = 8 };

static FILE *out;
static int helpers, lines;
static Node *loops[256];
static int nloops;

static void ex(Node *n);

static void line(int ind, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(out, "%*s", ind * 4, "");
    vfprintf(out, fmt, ap);
    va_end(ap);
    lines++;
}

static void div_mod(char op, Node *a, const char *a_name, Node *b) {
    helpers |= op == '/' ? H_DIV : H_DIV | H_MOD;
    fprintf(out, op == '/' ? "_div(" : "_mod(");
    if (a) ex(a); else fputs(a_name, out);
    fputs(", ", out);
    ex(b);
    fputc(')', out);
}

static void compound(const char *name, char op, Node *rhs) {
    if (op == '/' || op == '%') { div_mod(op, NULL, name, rhs); return; }
    fprintf(out, "%s %c ", name, op);
    ex(rhs);
}

static void upd(Node *target, char op, Node *v, int one) {
    helpers |= H_UPD | H_DIV | H_MOD;
    fprintf(out, "_upd(%s, ", target->sym->pyname);
    ex(target->kids[0]);
    fprintf(out, ", '%c', ", op);
    if (v) ex(v); else fprintf(out, "%d", one);
    fputc(')', out);
}

static void ex(Node *n) {
    Node *a = n->n > 0 ? n->kids[0] : NULL, *b = n->n > 1 ? n->kids[1] : NULL;
    switch (n->kind) {
    case N_NUM: fprintf(out, "%lld", n->value); return;
    case N_VAR: fputs(n->sym->pyname, out); return;
    case N_INDEX: fprintf(out, "%s[", n->sym->pyname); ex(a); fputc(']', out); return;
    case N_CALL:
        fprintf(out, "%s(", n->sym->pyname);
        for (int i = 0; i < n->n; i++) { if (i) fputs(", ", out); ex(n->kids[i]); }
        fputc(')', out);
        return;
    case N_ASSIGN:
        if (a->kind == N_INDEX) { upd(a, n->op[0], b, 0); return; }
        fprintf(out, "(%s := ", a->sym->pyname);
        if (n->op[0] == '=') ex(b); else compound(a->sym->pyname, n->op[0], b);
        fputc(')', out);
        return;
    case N_PRE: case N_POST: {
        char op = n->op[0];
        if (a->kind == N_INDEX) {
            if (n->kind == N_PRE) { upd(a, op, NULL, 1); return; }
            helpers |= H_POST;
            fprintf(out, "_post(%s, ", a->sym->pyname);
            ex(a->kids[0]);
            fprintf(out, ", %d)", op == '+' ? 1 : -1);
            return;
        }
        const char *x = a->sym->pyname;
        if (n->kind == N_PRE) fprintf(out, "(%s := %s %c 1)", x, x, op);
        else fprintf(out, "((%s := %s %c 1) %c 1)", x, x, op, op == '+' ? '-' : '+');
        return;
    }
    case N_BINARY:
        if (strchr("/%", n->op[0])) { div_mod(n->op[0], a, NULL, b); return; }
        fputs(strchr("+-*", n->op[0]) ? "(" : "int(", out);
        ex(a);
        fprintf(out, " %s ", n->op);
        ex(b);
        fputc(')', out);
        return;
    case N_LOGICAL:
        fputs("int(bool(", out);
        ex(a);
        fprintf(out, ") %s bool(", n->op[0] == '&' ? "and" : "or");
        ex(b);
        fputs("))", out);
        return;
    case N_UNARY:
        fputs(n->op[0] == '-' ? "(-" : "int(not ", out);
        ex(a);
        fputc(')', out);
        return;
    case N_EMPTY: fputs("True", out); return;
    default: return;
    }
}

static void expr_stmt(Node *n, int ind) {
    Node *a = n->n ? n->kids[0] : NULL;
    if (n->kind == N_EMPTY) return;
    if ((n->kind == N_ASSIGN || n->kind == N_PRE || n->kind == N_POST) && a->kind == N_VAR) {
        const char *x = a->sym->pyname;
        if (n->kind != N_ASSIGN) { line(ind, "%s %c= 1\n", x, n->op[0]); return; }
        line(ind, "%s ", x);
        char op = n->op[0];
        if (op == '=') { fputs("= ", out); ex(n->kids[1]); }
        else if (op == '/' || op == '%') { fputs("= ", out); compound(x, op, n->kids[1]); }
        else { fprintf(out, "%c= ", op); ex(n->kids[1]); }
        fputc('\n', out);
        return;
    }
    if ((n->kind == N_ASSIGN && strchr("=+-*", n->op[0])) || n->kind == N_PRE || n->kind == N_POST) {
        if (a->kind == N_INDEX) {
            line(ind, "");
            ex(a);
            if (n->kind == N_ASSIGN) {
                fprintf(out, n->op[0] == '=' ? " = " : " %c= ", n->op[0]);
                ex(n->kids[1]);
            } else fprintf(out, " %c= 1", n->op[0]);
            fputc('\n', out);
            return;
        }
    }
    line(ind, "");
    ex(n);
    fputc('\n', out);
}

static void stmt(Node *n, int ind);

static void body(Node *n, int ind) {
    int before = lines;
    stmt(n, ind);
    if (lines == before) line(ind, "pass\n");
}

static void loop_body(Node *loop, Node *b, int ind) {
    loops[nloops++] = loop;
    body(b, ind);
    nloops--;
}

static void py_string(const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 0x20) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static void stmt(Node *n, int ind) {
    switch (n->kind) {
    case N_BLOCK:
        for (int i = 0; i < n->n; i++) stmt(n->kids[i], ind);
        return;
    case N_VAR_DECL:
        line(ind, "%s = ", n->sym->pyname);
        if (n->n) ex(n->kids[0]); else fputc('0', out);
        fputc('\n', out);
        return;
    case N_ARRAY_DECL:
        line(ind, "%s = [0] * %lld\n", n->sym->pyname, n->value);
        return;
    case N_IF: {
        const char *kw = "if";
        for (;;) {
            line(ind, "%s ", kw);
            ex(n->kids[0]);
            fputs(":\n", out);
            body(n->kids[1], ind + 1);
            if (n->n < 3) return;
            if (n->kids[2]->kind != N_IF) break;
            n = n->kids[2];
            kw = "elif";
        }
        line(ind, "else:\n");
        body(n->kids[2], ind + 1);
        return;
    }
    case N_WHILE:
        line(ind, "while ");
        ex(n->kids[0]);
        fputs(":\n", out);
        loop_body(n, n->kids[1], ind + 1);
        return;
    case N_DO:
        line(ind, "while True:\n");
        loop_body(n, n->kids[0], ind + 1);
        line(ind + 1, "if not ");
        ex(n->kids[1]);
        fputs(":\n", out);
        line(ind + 2, "break\n");
        return;
    case N_FOR:
        expr_stmt(n->kids[0], ind);
        line(ind, "while ");
        ex(n->kids[1]);
        fputs(":\n", out);
        loop_body(n, n->kids[3], ind + 1);
        expr_stmt(n->kids[2], ind + 1);
        return;
    case N_BREAK: line(ind, "break\n"); return;
    case N_CONTINUE: {
        Node *l = loops[nloops - 1];
        if (l->kind == N_FOR) expr_stmt(l->kids[2], ind);
        if (l->kind == N_DO) {
            line(ind, "if not ");
            ex(l->kids[1]);
            fputs(":\n", out);
            line(ind + 1, "break\n");
        }
        line(ind, "continue\n");
        return;
    }
    case N_RETURN:
        line(ind, "return");
        if (n->n) { fputc(' ', out); ex(n->kids[0]); }
        fputc('\n', out);
        return;
    case N_PRINTF:
        line(ind, "print(");
        py_string(n->name);
        fputs(" % (", out);
        for (int i = 0; i < n->n; i++) { ex(n->kids[i]); fputs(", ", out); }
        fputs("), end=\"\")\n", out);
        return;
    case N_EXPR_STMT: expr_stmt(n->kids[0], ind); return;
    default: return;
    }
}

static int has_global(char **names, int k, const char *s) {
    for (int i = 0; i < k; i++) if (!strcmp(names[i], s)) return 1;
    return 0;
}

static void find_globals(Node *n, char **names, int *k) {
    if ((n->kind == N_ASSIGN || n->kind == N_PRE || n->kind == N_POST)
        && n->kids[0]->kind == N_VAR && n->kids[0]->sym->global
        && !has_global(names, *k, n->kids[0]->sym->pyname))
        names[(*k)++] = n->kids[0]->sym->pyname;
    for (int i = 0; i < n->n; i++) find_globals(n->kids[i], names, k);
}

static void function(Node *f) {
    Node *params = f->kids[0];
    fprintf(out, "\n\ndef %s(", f->sym->pyname);
    for (int i = 0; i < params->n; i++)
        fprintf(out, "%s%s", i ? ", " : "", params->kids[i]->sym->pyname);
    fputs("):\n", out);
    char *names[1024];
    int k = 0;
    find_globals(f->kids[1], names, &k);
    if (k) {
        line(1, "global ");
        for (int i = 0; i < k; i++) fprintf(out, "%s%s", i ? ", " : "", names[i]);
        fputc('\n', out);
    }
    int before = lines;
    stmt(f->kids[1], 1);
    Node *b = f->kids[1];
    if (!f->sym->is_void && !(b->n && b->kids[b->n - 1]->kind == N_RETURN)) line(1, "return 0\n");
    else if (lines == before) line(1, "pass\n");
}

void pygen(FILE *dst, Node *program) {
    char *buf;
    size_t len;
    out = open_memstream(&buf, &len);
    for (int i = 0; i < program->n; i++) {
        Node *k = program->kids[i];
        if (k->kind == N_FUNC) { if (k->n == 2) function(k); }
        else { fputc('\n', out); stmt(k, 0); }
    }
    fclose(out);
    out = dst;
    fputs("import sys\n", out);
    if (helpers & H_DIV)
        fputs("\n\ndef _div(a, b):\n    q = abs(a) // abs(b)\n    return q if (a < 0) == (b < 0) else -q\n", out);
    if (helpers & H_MOD)
        fputs("\n\ndef _mod(a, b):\n    return a - b * _div(a, b)\n", out);
    if (helpers & H_UPD)
        fputs("\n\ndef _upd(a, i, op, v):\n"
              "    a[i] = {'=': lambda x, y: y, '+': lambda x, y: x + y, '-': lambda x, y: x - y,\n"
              "            '*': lambda x, y: x * y, '/': _div, '%': _mod}[op](a[i], v)\n"
              "    return a[i]\n", out);
    if (helpers & H_POST)
        fputs("\n\ndef _post(a, i, d):\n    a[i] += d\n    return a[i] - d\n", out);
    fwrite(buf, 1, len, out);
    free(buf);
    fputs("\n\nif __name__ == \"__main__\":\n    sys.setrecursionlimit(10100)\n"
          "    raise SystemExit(main() & 255)\n", out);
}
