#ifndef _DB_LMDB_H_
#define _DB_LMDB_H_

#include "h.h"

void db_lmdb_init();
dbi_t *db_lmdb_get(dbi_t *key);
void db_lmdb_put(dbi_t *key, dbi_t *val);
void db_lmdb_delete(dbi_t *key);
void db_lmdb_close();

#endif
