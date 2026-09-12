#pragma once
static inline int rsc_isspace(char c){return c==' '||c=='\t'||c=='\n'||c=='\r';}
static inline int rsc_isdigit(char c){return c>='0'&&c<='9';}
static inline int rsc_isalpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
static inline int rsc_isalnum(char c){return rsc_isalpha(c)||rsc_isdigit(c);}
static inline char rsc_toupper(char c){return c>='a'&&c<='z'?c-32:c;}
static inline char rsc_tolower(char c){return c>='A'&&c<='Z'?c+32:c;}
