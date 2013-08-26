#include "leveldb/c.h"
#include "db.h"

static leveldb_t *leveldb_db;
static leveldb_options_t *leveldb_options;
static leveldb_readoptions_t *leveldb_roptions;
static leveldb_writeoptions_t *leveldb_woptions;
static leveldb_cache_t *leveldb_cache;
static leveldb_filterpolicy_t *leveldb_filterpolicy;

void db_leveldb_init() {
    char *errstr = NULL;
    leveldb_options = leveldb_options_create();
    /* create if missing */
    leveldb_options_set_create_if_missing(leveldb_options, 1);
    /* lru cache */
    leveldb_cache = leveldb_cache_create_lru(conf->leveldb_cache_size);
    leveldb_options_set_cache(leveldb_options, leveldb_cache);
    /* block size */
    leveldb_options_set_block_size(leveldb_options, conf->leveldb_block_size);
    /* write buffer size */
    leveldb_options_set_write_buffer_size(leveldb_options, conf->leveldb_write_buffer_size);
    /* filter policy */
    leveldb_filterpolicy = leveldb_filterpolicy_create_bloom(10);
    leveldb_options_set_filter_policy(leveldb_options, leveldb_filterpolicy);
    /* open db */
    leveldb_db = leveldb_open(leveldb_options, conf->db, &errstr);
    if (errstr) {
        terrx(1, "unable to open db at %s: %s", conf->db, errstr);
    }
    leveldb_roptions = leveldb_readoptions_create();
    leveldb_woptions = leveldb_writeoptions_create();
}

dbi_t *db_leveldb_get(dbi_t *key) {
    dbi_t *item = dbi_new();
    item->data = leveldb_get(leveldb_db, leveldb_roptions, key->data, key->len, &item->len, &item->err);
    return item;
}

void db_leveldb_put(dbi_t *key, dbi_t *val) {
    leveldb_put(leveldb_db, leveldb_woptions, key->data, key->len, val->data, val->len, NULL);
}

void db_leveldb_delete(dbi_t *key) {
    leveldb_delete(leveldb_db, leveldb_woptions, key->data, key->len, NULL);
}

void db_leveldb_close() {
    leveldb_close(leveldb_db); 
    leveldb_cache_destroy(leveldb_cache);
    leveldb_filterpolicy_destroy(leveldb_filterpolicy);
    leveldb_options_destroy(leveldb_options);
    leveldb_readoptions_destroy(leveldb_roptions);
    leveldb_writeoptions_destroy(leveldb_woptions);
}
