#ifndef _CONF_H_
#define _CONF_H_

#include "h.h"

void conf_init(conf_t *conf);
int conf_loadfile(conf_t *conf, char *filename);


#endif