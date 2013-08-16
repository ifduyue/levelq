#ifndef _DB_H_
#define _DB_H_

#include "h.h"

dbi_t *dbi_new();
void dbi_destroy();

dbi_t *(*db_get)(dbi_t *key);
void (*db_put)(dbi_t *key, dbi_t *val);
void (*db_delete)(dbi_t *key);
void (*db_close)();


#endif
