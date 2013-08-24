#include "unqlite.h"
#include "db.h"
#include <stdio.h>
#include <assert.h>

static unqlite *db = NULL;

void db_unqlite_init() {
    mkdir(conf->db, 0755);
    size_t len = strlen(conf->db);
    char *path = alloca(len + 15);
    assert(path); 
    if (conf->db[len - 1] != '/') {
        sprintf(path, "%s/data.unqlite", conf->db);
    }
    else {
        sprintf(path, "%sdata.unqlite", conf->db);
    }
    int rc = unqlite_open(&db, path, UNQLITE_OPEN_CREATE | UNQLITE_OPEN_READWRITE);
    if (rc != UNQLITE_OK) {
        terrx(1, "unable to open db at %s", path);
    }
}

dbi_t *db_unqlite_get(dbi_t *key) {
    dbi_t *item = dbi_new();
    unqlite_int64 len;
    int rc = unqlite_kv_fetch(db, key->data, key->len, NULL, &len);
    switch (rc) {
        case UNQLITE_OK:
            break;
        case UNQLITE_NOTFOUND:
            break;
        case UNQLITE_BUSY:
            item->err = strdup("unqlite is busy");
            break;
        case UNQLITE_IOERR:
            item->err = strdup("OS specific error");
            break;
        case UNQLITE_NOMEM:
            item->err = strdup("out of memory");
            break;
        default:
            item->err = strdup("unknown error");
            break;
    }
    if (rc != UNQLITE_OK) {
        return item;
    }
    item->data = malloc(len);
    assert(item->data);
    item->data_is_malloced = 1;
    unqlite_kv_fetch(db, key->data, key->len, item->data, &len);
    item->len = (size_t)len;
    if (rc != UNQLITE_OK) {
        item->err = strdup("unqlite_kv_fetch error");    
    }
    return item;
}

void db_unqlite_put(dbi_t *key, dbi_t *val) {
    unqlite_kv_store(db, key->data, key->len, val->data, val->len);
}

void db_unqlite_delete(dbi_t *key) {
    unqlite_kv_delete(db, key->data, key->len);
}

void db_unqlite_close() {
    unqlite_close(db); 
}
