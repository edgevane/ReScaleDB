#include "string.h"
usize rsc_strlen(const char *s){usize n=0;while(s[n])n++;return n;}
int rsc_strcmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int rsc_strncmp(const char *a,const char *b,usize n){for(usize i=0;i<n;i++){if(a[i]!=b[i]||!a[i]||!b[i])return (unsigned char)a[i]-(unsigned char)b[i];}return 0;}
int rsc_memcmp(const void *a,const void *b,usize n){const u8 *pa=a,*pb=b;for(usize i=0;i<n;i++)if(pa[i]!=pb[i])return pa[i]-pb[i];return 0;}
void *rsc_memcpy(void *dst,const void *src,usize n){u8 *d=dst;const u8 *s=src;for(usize i=0;i<n;i++)d[i]=s[i];return dst;}
void *rsc_memmove(void *dst,const void *src,usize n){u8 *d=dst;const u8 *s=src;if(d<s)for(usize i=0;i<n;i++)d[i]=s[i];else for(usize i=n;i>0;i--)d[i-1]=s[i-1];return dst;}
void *rsc_memset(void *dst,int c,usize n){u8 *d=dst;for(usize i=0;i<n;i++)d[i]=(u8)c;return dst;}
char *rsc_strcpy(char *dst,const char *src){char *d=dst;while((*d++=*src++));return dst;}
char *rsc_strncpy(char *dst,const char *src,usize n){usize i=0;for(;i<n&&src[i];i++)dst[i]=src[i];for(;i<n;i++)dst[i]=0;return dst;}
