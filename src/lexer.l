%{
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "parser.h"

static int cur_line = 1, cur_col = 1;

static void advance(void) {
    yylloc.first_line = cur_line;
    yylloc.first_column = cur_col;
    for (int i = 0; i < yyleng; i++) {
        if (yytext[i] == '\n') { cur_line++; cur_col = 1; }
        else cur_col++;
    }
    yylloc.last_line = cur_line;
    yylloc.last_column = cur_col;
}

static char *unescape(const char *s, int len) {
    char *out = malloc(len + 1), *o = out;
    for (int i = 1; i < len - 1; i++) {
        if (s[i] != '\\') { *o++ = s[i]; continue; }
        switch (s[++i]) {
            case 'n': *o++ = '\n'; break;
            case 't': *o++ = '\t'; break;
            default: *o++ = s[i];
        }
    }
    *o = 0;
    return out;
}

#define YY_USER_ACTION advance();
%}

%option noyywrap nounput noinput

%%

[ \t\r\n]+                  ;
"//".*                      ;
"/*"([^*]|\*+[^*/])*\*+"/"  ;
"/*"                        { error_at(yylloc.first_line, yylloc.first_column, "unterminated comment"); return YYerror; }

"int"       return INT;
"void"      return VOID;
"if"        return IF;
"else"      return ELSE;
"while"     return WHILE;
"do"        return DO;
"for"       return FOR;
"break"     return BREAK;
"continue"  return CONTINUE;
"return"    return RETURN;
"printf"    return PRINTF;

[A-Za-z_][A-Za-z0-9_]*  { yylval.str = strdup(yytext); return IDENT; }
[0-9]+                  { yylval.num = strtoll(yytext, NULL, 10); return NUM; }
\"(\\[nt\\"]|[^"\\\n])*\" { yylval.str = unescape(yytext, yyleng); return STRING; }

"="|"+="|"-="|"*="|"/="|"%="  { yylval.op = strdup(yytext); return ASSIGN; }
"=="|"!="                     { yylval.op = strdup(yytext); return EQOP; }
"<"|"<="|">"|">="             { yylval.op = strdup(yytext); return RELOP; }
"&&"        return AND;
"||"        return OR;
"++"        return INC;
"--"        return DEC;

[-+*/%!(){}\[\];,]  return yytext[0];

.           { error_at(yylloc.first_line, yylloc.first_column, "unexpected character '%s'", yytext); return YYerror; }

%%
