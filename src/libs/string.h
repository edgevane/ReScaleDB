#pragma once
#include "types.h"
usize rsc_strlen(const char *s);
int rsc_strcmp(const char *a, const char *b);
int rsc_strncmp(const char *a, const char *b, usize n);
int rsc_memcmp(const void *a, const void *b, usize n);
void *rsc_memcpy(void *dst, const void *src, usize n);
void *rsc_memmove(void *dst, const void *src, usize n);
void *rsc_memset(void *dst, int c, usize n);
char *rsc_strcpy(char *dst, const char *src);
char *rsc_strncpy(char *dst, const char *src, usize n);
