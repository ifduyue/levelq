#ifndef _DB_LEVELDB_H_
#define _DB_LEVELDB_H_

void db_leveldb_init();
dbi_t *db_leveldb_get(dbi_t *key);
void db_leveldb_put(dbi_t *key, dbi_t *val);
void db_leveldb_delete(dbi_t *key);
void db_leveldb_close();

#endif
