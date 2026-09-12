#pragma once
#include "../libs/types.h"
#include "pager.h"
int rsc_dump_all(const char *dump_path);
int rsc_load_all(const char *dump_path);
int rsc_dump_db(const char *db_path, const char *dump_path);
int rsc_load_db(const char *dump_path, const char *db_path);
int pager_save(Pager *p, const char *path);
int pager_load(Pager *p, const char *path);
