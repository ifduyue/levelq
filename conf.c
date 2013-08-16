#include "conf.h"
#include <string.h>
#include <stdio.h>

conf_t conf[1] = {{
    engine_leveldb,
    "127.0.0.1",
    1219,
    128 * 1048576, /* 128MB */
    8 * 1024, /* 8KB */
    8 * 1048576, /* 8MB */
    "./db",
    10,
    1,
    0
},};

void conf_init(conf_t *conf) {
    conf->engine = engine_leveldb;
    conf->host = strdup("127.0.0.1");
    conf->port = 1219;
    conf->cache_size = 128 * 1048576; /* 128MB */
    conf->block_size = 8 * 1024; /* 8KB */
    conf->write_buffer_size = 8 * 1048576; /* 8MB */
    conf->tcp_keepalive = 10;
    conf->tcp_nodelay = 1;
    conf->delete_after_get = 0;
    conf->db = strdup("./db");
}

int conf_loadfile(conf_t *conf, char *filename) {
    FILE *fp;
    char k[1024], v[1024], s[1024];
    unsigned int line = 0;

    fp = fopen(filename, "r");
    if (!fp) {
        twarn("unable to open %s", filename);
        return 1;
    }
    while (!feof(fp)) {
        line++;
        *s = 0;
        fscanf(fp, "%1024[^\n#]s", s);
        fscanf(fp, "%*[^\n]s");
        fgetc(fp);
        if (strchr(s, '=') == NULL) {
            continue;
        }
        sscanf(s, "%[^= \t\r]s", k);
        sscanf(strchr(s, '=') + 1, "%*[ \t\r]%[^ \t\r\n]s", v);
        if (!strcmp(k, "engine")) {
            if (!strcmp(v, "leveldb")) {
                conf->engine = engine_leveldb;
            }
            else if (!strcmp(v, "lmdb")) {
                conf->engine = engine_lmdb;
            }
            else {
                terrx(1, "supported engines are leveldb, lmdb");
            }
        }
        else if (!strcmp(k, "host")) {
            conf->host = strdup(v);
        }
        else if (!strcmp(k, "port")) {
            sscanf(v, "%hu", &conf->port);
        }
        else if (!strcmp(k, "cache_size")) {
            sscanf(v, "%u", &conf->cache_size);
        }
        else if (!strcmp(k, "block_size")) {
            sscanf(v, "%u", &conf->block_size);
        }
        else if (!strcmp(k, "write_buffer_size")) {
            sscanf(v, "%u", &conf->write_buffer_size);
        }
        else if (!strcmp(k, "db")) {
            conf->db = strdup(v);
        }
        else if (!strcmp(k, "tcp_keepalive")) {
            sscanf(v, "%u", &conf->tcp_keepalive);
        }
        else if (!strcmp(k, "tcp_nodelay")) {
            sscanf(v, "%u", &conf->tcp_nodelay);
        }
        else if (!strcmp(k, "delete_after_get")) {
            sscanf(v, "%u", &conf->delete_after_get);
        }
        else {
            twarnx("error in %s line %i", filename, line);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}
