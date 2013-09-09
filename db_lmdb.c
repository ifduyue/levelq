#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include "lmdb.h"
#include "db.h"

static MDB_env *env;
static MDB_dbi dbi = 0;


void db_lmdb_init()
{
    int r;
    MDB_txn *txn;
    r = mdb_env_create(&env);

    if (r) {
        terrx(r, "mdb_env_create failed: %s", mdb_strerror(r));
    }

    if (conf->lmdb_mapsize > 10485760u) {
        r = mdb_env_set_mapsize(env, conf->lmdb_mapsize);

        if (r) {
            terrx(r, "mdb_env_set_mapsize failed: %s", mdb_strerror(r));
        }
    }

    mkdir(conf->db, 0755);
    r = mdb_env_open(env, conf->db, MDB_WRITEMAP | MDB_MAPASYNC | MDB_NOTLS, 0664);

    if (r) {
        terrx(r, "mdb_env_open failed: %s", mdb_strerror(r));
    }

    r = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if (r) {
        mdb_env_close(env);
        terrx(r, "mdb_txn_begin failed: %s", mdb_strerror(r));
    }

    r = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);

    if (r) {
        mdb_txn_abort(txn);
        mdb_env_close(env);
        terrx(r, "mdb_dbi_open failed: %s", mdb_strerror(r));
    }

    mdb_txn_commit(txn);
}

dbi_t *db_lmdb_get(dbi_t *key)
{
    MDB_txn *txn = NULL;
    int r;
    dbi_t *item = dbi_new();
    item->data_is_malloced = 0;
    r = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if (r) {
        goto error;
    }

    MDB_val k, v;
    k.mv_size = key->len;
    k.mv_data = key->data;
    r = mdb_get(txn, dbi, &k, &v);

    switch (r) {
        case 0:
            item->data = v.mv_data;
            item->len = v.mv_size;
            break;

        case MDB_NOTFOUND:
            break;

        default:
            goto error;
    }

    mdb_txn_abort(txn);
    return item;
error:
    mdb_txn_abort(txn);
    item->err = strdup(mdb_strerror(r));
    return item;
}

void db_lmdb_put(dbi_t *key, dbi_t *val)
{
    MDB_txn *txn = NULL;
    int r;
    r = mdb_txn_begin(env, NULL, 0, &txn);

    if (r) {
        return;
    }

    MDB_val k, v;
    k.mv_size = key->len;
    k.mv_data = key->data;
    v.mv_size = val->len;
    v.mv_data = val->data;
    r = mdb_put(txn, dbi, &k, &v, 0);

    if (!r) {
        mdb_txn_commit(txn);
    }
    else {
        mdb_txn_abort(txn);
    }
}

void db_lmdb_delete(dbi_t *key)
{
    MDB_txn *txn = NULL;
    int r;
    r = mdb_txn_begin(env, NULL, 0, &txn);

    if (r) {
        goto error;
    }

    MDB_val k;
    k.mv_size = key->len;
    k.mv_data = key->data;
    r = mdb_del(txn, dbi, &k, NULL);

    if (r) {
        goto error;
    }

    mdb_txn_commit(txn);
    return;
error:
    mdb_txn_abort(txn);
}

void db_lmdb_close()
{
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}
