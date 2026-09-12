#include "../../libs/ctype.h"
#include "../../libs/string.h"
#include "../../libs/types.h"
typedef enum { TOK_EOF, TOK_IDENT, TOK_INT, TOK_STRING, TOK_COMMA, TOK_LP, TOK_RP, TOK_EQ, TOK_LT, TOK_GT, TOK_STAR, TOK_SEMI } TokKind;
typedef struct { TokKind kind; char text[64]; } Tok;
static void skip(const char **p){ while(rsc_isspace(**p)) (*p)++; }
int lex_next(const char **p, Tok *t){
    skip(p); char c=**p; if(!c){t->kind=TOK_EOF;return 0;}
    if(c=='('){t->kind=TOK_LP;(*p)++;return 0;}
    if(c==')'){t->kind=TOK_RP;(*p)++;return 0;}
    if(c==','){t->kind=TOK_COMMA;(*p)++;return 0;}
    if(c=='*'){t->kind=TOK_STAR;(*p)++;return 0;}
    if(c==';'){t->kind=TOK_SEMI;(*p)++;return 0;}
    if(c=='='){t->kind=TOK_EQ;t->text[0]='=';t->text[1]=0;(*p)++;return 0;}
    if(c=='<'||c=='>'){t->kind=c=='<'?TOK_LT:TOK_GT;t->text[0]=c;(*p)++; if(**p=='='){t->text[1]='=';t->text[2]=0;(*p)++;} else t->text[1]=0; return 0;}
    if(c=='\''){ (*p)++; int i=0; while(**p && **p!='\'' && i<63){t->text[i++]=*(*p)++;} t->text[i]=0; if(**p=='\'')(*p)++; t->kind=TOK_STRING; return 0;}
    if(rsc_isdigit(c)){ int i=0; while(rsc_isdigit(**p)&&i<63) t->text[i++]=*(*p)++; t->text[i]=0; t->kind=TOK_INT; return 0;}
    if(rsc_isalpha(c)||c=='_'){ int i=0; while((rsc_isalnum(**p)||**p=='_')&&i<63) t->text[i++]=*(*p)++; t->text[i]=0; t->kind=TOK_IDENT; return 0;}
    (*p)++; return lex_next(p,t);
}
