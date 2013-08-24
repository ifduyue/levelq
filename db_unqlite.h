#ifndef _DB_UNQLITE_H_
#define _DB_UNQLITE_H_

void db_unqlite_init();
dbi_t *db_unqlite_get(dbi_t *key);
void db_unqlite_put(dbi_t *key, dbi_t *val);
void db_unqlite_delete(dbi_t *key);
void db_unqlite_close();

#endif
