%{
#include <stdlib.h>
#include "ast.h"

int yylex(void);
void yyerror(const char *msg);
Node *root;

#define L(x) (x).first_line, (x).first_column
%}

%code requires { #include "ast.h" }

%define parse.error detailed
%define parse.trace
%locations

%union {
    long long num;
    char *str;
    const char *op;
    Node *node;
}

%token INT "int" VOID "void" IF "if" ELSE "else" WHILE "while" DO "do" FOR "for"
%token BREAK "break" CONTINUE "continue" RETURN "return" PRINTF "printf"
%token AND "&&" OR "||" INC "++" DEC "--"
%token <str> IDENT "identifier" STRING "string"
%token <num> NUM "number"
%token <op> ASSIGN "assignment" EQOP "==/!=" RELOP "comparison"

%type <node> program external declarators declarator params param_list param
%type <node> block items stmt expr opt_expr args arg_list print_args

%expect 0
%precedence LOWER_THAN_ELSE
%precedence ELSE
%right ASSIGN
%left OR
%left AND
%left EQOP
%left RELOP
%left '+' '-'
%left '*' '/' '%'
%precedence PREFIX
%precedence INC DEC

%%

start
    : program                               { root = $1; }
    ;

program
    : %empty                                { $$ = node(N_PROGRAM, 1, 1); }
    | program external                      { $$ = $1; for (int i = 0; i < $2->n; i++) add($$, $2->kids[i]); }
    ;

external
    : INT declarators ';'                   { $$ = $2; }
    | INT IDENT '(' params ')' block        { $$ = node(N_BLOCK, L(@1)); Node *f = node2(N_FUNC, L(@2), $4, $6); f->name = $2; add($$, f); }
    | VOID IDENT '(' params ')' block       { $$ = node(N_BLOCK, L(@1)); Node *f = node2(N_FUNC, L(@2), $4, $6); f->name = $2; f->op = "void"; add($$, f); }
    | INT IDENT '(' params ')' ';'          { $$ = node(N_BLOCK, L(@1)); Node *f = node1(N_FUNC, L(@2), $4); f->name = $2; add($$, f); }
    | VOID IDENT '(' params ')' ';'         { $$ = node(N_BLOCK, L(@1)); Node *f = node1(N_FUNC, L(@2), $4); f->name = $2; f->op = "void"; add($$, f); }
    ;

declarators
    : declarator                            { $$ = node1(N_BLOCK, L(@1), $1); }
    | declarators ',' declarator            { $$ = add($1, $3); }
    ;

declarator
    : IDENT                                 { $$ = node(N_VAR_DECL, L(@1)); $$->name = $1; }
    | IDENT ASSIGN expr                     { if ($2[0] != '=' || $2[1]) { error_at(L(@2), "expected '=' in initializer"); YYABORT; }
                                              $$ = node1(N_VAR_DECL, L(@1), $3); $$->name = $1; }
    | IDENT '[' NUM ']'                     { $$ = node(N_ARRAY_DECL, L(@1)); $$->name = $1; $$->value = $3; }
    ;

params
    : %empty                                { $$ = node(N_PARAMS, @$.first_line, @$.first_column); }
    | VOID                                  { $$ = node(N_PARAMS, L(@1)); }
    | param_list                            { $$ = $1; }
    ;

param_list
    : param                                 { $$ = node1(N_PARAMS, L(@1), $1); }
    | param_list ',' param                  { $$ = add($1, $3); }
    ;

param
    : INT IDENT                             { $$ = node(N_PARAM, L(@2)); $$->name = $2; }
    | INT                                   { $$ = node(N_PARAM, L(@1)); }
    ;

block
    : '{' items '}'                         { $$ = $2; $$->line = @1.first_line; $$->col = @1.first_column; }
    ;

items
    : %empty                                { $$ = node(N_BLOCK, 0, 0); }
    | items stmt                            { $$ = add($1, $2); }
    | items INT declarators ';'             { $$ = $1; for (int i = 0; i < $3->n; i++) add($$, $3->kids[i]); }
    ;

stmt
    : block                                 { $$ = $1; }
    | IF '(' expr ')' stmt %prec LOWER_THAN_ELSE { $$ = node2(N_IF, L(@1), $3, $5); }
    | IF '(' expr ')' stmt ELSE stmt        { $$ = add(node2(N_IF, L(@1), $3, $5), $7); }
    | WHILE '(' expr ')' stmt               { $$ = node2(N_WHILE, L(@1), $3, $5); }
    | DO stmt WHILE '(' expr ')' ';'        { $$ = node2(N_DO, L(@1), $2, $5); }
    | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' stmt
                                            { $$ = add(add(node2(N_FOR, L(@1), $3, $5), $7), $9); }
    | BREAK ';'                             { $$ = node(N_BREAK, L(@1)); }
    | CONTINUE ';'                          { $$ = node(N_CONTINUE, L(@1)); }
    | RETURN ';'                            { $$ = node(N_RETURN, L(@1)); }
    | RETURN expr ';'                       { $$ = node1(N_RETURN, L(@1), $2); }
    | PRINTF '(' STRING print_args ')' ';'  { $$ = $4; $$->name = $3; $$->line = @1.first_line; $$->col = @1.first_column; }
    | expr ';'                              { $$ = node1(N_EXPR_STMT, L(@1), $1); }
    | ';'                                   { $$ = node(N_EMPTY, L(@1)); }
    ;

print_args
    : %empty                                { $$ = node(N_PRINTF, 0, 0); }
    | print_args ',' expr                   { $$ = add($1, $3); }
    ;

opt_expr
    : %empty                                { $$ = node(N_EMPTY, @$.first_line, @$.first_column); }
    | expr                                  { $$ = $1; }
    ;

expr
    : NUM                                   { $$ = node(N_NUM, L(@1)); $$->value = $1; }
    | IDENT                                 { $$ = node(N_VAR, L(@1)); $$->name = $1; }
    | IDENT '[' expr ']'                    { $$ = node1(N_INDEX, L(@1), $3); $$->name = $1; }
    | IDENT '(' args ')'                    { $$ = $3; $$->name = $1; $$->line = @1.first_line; $$->col = @1.first_column; }
    | '(' expr ')'                          { $$ = $2; }
    | expr ASSIGN expr                      { $$ = node2(N_ASSIGN, L(@2), $1, $3); $$->op = $2; }
    | expr OR expr                          { $$ = node2(N_LOGICAL, L(@2), $1, $3); $$->op = "||"; }
    | expr AND expr                         { $$ = node2(N_LOGICAL, L(@2), $1, $3); $$->op = "&&"; }
    | expr EQOP expr                        { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = $2; }
    | expr RELOP expr                       { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = $2; }
    | expr '+' expr                         { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = "+"; }
    | expr '-' expr                         { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = "-"; }
    | expr '*' expr                         { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = "*"; }
    | expr '/' expr                         { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = "/"; }
    | expr '%' expr                         { $$ = node2(N_BINARY, L(@2), $1, $3); $$->op = "%"; }
    | '-' expr %prec PREFIX                 { $$ = node1(N_UNARY, L(@1), $2); $$->op = "-"; }
    | '!' expr %prec PREFIX                 { $$ = node1(N_UNARY, L(@1), $2); $$->op = "!"; }
    | INC expr %prec PREFIX                 { $$ = node1(N_PRE, L(@1), $2); $$->op = "++"; }
    | DEC expr %prec PREFIX                 { $$ = node1(N_PRE, L(@1), $2); $$->op = "--"; }
    | expr INC                              { $$ = node1(N_POST, L(@2), $1); $$->op = "++"; }
    | expr DEC                              { $$ = node1(N_POST, L(@2), $1); $$->op = "--"; }
    ;

args
    : %empty                                { $$ = node(N_CALL, 0, 0); }
    | arg_list                              { $$ = $1; }
    ;

arg_list
    : expr                                  { $$ = node1(N_CALL, L(@1), $1); }
    | arg_list ',' expr                     { $$ = add($1, $3); }
    ;

%%

void yyerror(const char *msg) {
    error_at(yylloc.first_line, yylloc.first_column, "%s", msg);
}
