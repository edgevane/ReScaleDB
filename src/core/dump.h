#pragma once
#include "../libs/types.h"
int rsc_dump_all(const char *dump_path);
int rsc_load_all(const char *dump_path);
int rsc_dump_db(const char *db_path, const char *dump_path);
int rsc_load_db(const char *dump_path, const char *db_path);
