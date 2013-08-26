#ifndef _H_H_
#define _H_H_

#include <err.h>
#include <stdlib.h>
#include "jemalloc/jemalloc.h"
#include "http_parser.h"
#include "uv.h"

#define BUFSIZE 512
#define LEVELQ_VERSION "0.0.1"
#define QUEUE_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_."
#define MAX_QNAME_LENGTH 200
#define HEADER "HTTP/1.1 %d %s\r\n"\
                 "Server: levelq/"LEVELQ_VERSION"\r\n"\
                 "Content-Type: application/octet-stream\r\n"\
                 "Content-Length: %zu\r\n"\
                 "Connection: %s\r\n"\
                 "Cache-Control: no-store, no-cache, must-revalidate\r\n"\
                 "Pragma: no-cache\r\n"\
                 "\r\n"

/** give offset of a field inside struct */
#ifndef offsetof
#define offsetof(type, field) ((unsigned long)&(((type *)0)->field))
#endif

/** given pointer to field inside struct, return pointer to struct */
#ifndef container_of
#define container_of(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
#endif

#define twarn(fmt, args...) warn("%s:%d in %s: " fmt, \
                                 __FILE__, __LINE__, __func__, ##args)
#define twarnx(fmt, args...) warnx("%s:%d in %s: " fmt, \
                                   __FILE__, __LINE__, __func__, ##args)
#define terr(eval, fmt, args...) err(eval, "%s:%d in %s: " fmt, \
                                 __FILE__, __LINE__, __func__, ##args)
#define terrx(eval, fmt, args...) errx(eval, "%s:%d in %s: " fmt, \
                                   __FILE__, __LINE__, __func__, ##args)

#define uv_check(r, msg) \
    do { \
        if (r) { \
            twarnx("%s: %s", msg, uv_strerror(r)); \
        } \
    } while (0)

#define uv_assert(r, msg) \
    do { \
        if (r) { \
            terrx(1, "%s: %s", msg, uv_strerror(r)); \
        } \
    } while (0)

typedef struct {
    uv_tcp_t handle;
    http_parser parser;
    unsigned short keepalive : 1;
} client_t;

typedef struct {
    uv_write_t write_req;
    client_t *client;
    char qname[200];
    size_t qname_length;
    enum http_method method;
    const char *body;
    size_t body_length;
} request_t;

typedef enum {
    engine_leveldb,
    engine_lmdb,
    engine_unqlite
} engine_t;

typedef struct {
    engine_t engine;
    char *host;
    unsigned short port;
    char *db;
    unsigned int tcp_keepalive;
    unsigned int tcp_nodelay;
    unsigned int delete_after_get;
    /* leveldb only */
    size_t leveldb_cache_size;
    size_t leveldb_block_size;
    size_t leveldb_write_buffer_size;
    /* lmdb only */
    size_t lmdb_mapsize;
} conf_t;

typedef struct {
    char *err;
    char *data;
    size_t len;
    char data_is_malloced;
} dbi_t;


extern conf_t conf[1];
extern http_parser_settings parser_settings;
extern uv_buf_t uvbuf[2];
extern uv_loop_t* uv_loop;
extern uv_tcp_t server;

#endif
