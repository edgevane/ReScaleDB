#pragma once
#include "types.h"
void *rsc_alloc(usize n);
void rsc_free(void *p);
void *rsc_realloc(void *p, usize n);
void rsc_alloc_init(void *base, usize cap);
