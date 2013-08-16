#include "db.h"
#include <assert.h>
#include <stdlib.h>

dbi_t *dbi_new() {
    dbi_t *item = (dbi_t *)malloc(sizeof(dbi_t));
    assert(item);
    item->err = NULL;
    item->data = NULL;
    item->len = 0;
    item->data_is_malloced = 1;
    return item;
}

void dbi_destroy(dbi_t *item) {
    if (item) {
        if (item->err) {
            free(item->err);
        }
        if (item->len && item->data && item->data_is_malloced) {
            free(item->data);
        }
        free(item);
    }
}
