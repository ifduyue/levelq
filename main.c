#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <err.h>

#include "leveldb/c.h"
#include "http_parser.h"
#include "uv.h"

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
            uv_err_t err = uv_last_error(uv_loop); \
            twarnx("%s: %s", msg, uv_strerror(err)); \
        } \
    } while (0)

#define uv_assert(r, msg) \
    do { \
        if (r) { \
            uv_err_t err = uv_last_error(uv_loop); \
            terrx(1, "%s: %s", msg, uv_strerror(err)); \
        } \
    } while (0)
    
#define LEVELQ_VERSION "0.0.1"
#define QUEUE_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_."
#define MAX_QNAME_LENGTH 200
#define HEADER "HTTP/%hu.%hu %d %s\r\n"\
                 "Server: levelq/"LEVELQ_VERSION"\r\n"\
                 "Content-Type: application/octet-stream\r\n"\
                 "Content-Length: %zu\r\n"\
                 "Connection: keep-alive\r\n"\
                 "Cache-Control: no-store, no-cache, must-revalidate\r\n"\
                 "Pragma: no-cache\r\n"\
                 "\r\n"

typedef struct {
    char host[1024];
    unsigned short port;
    unsigned int cache_size;
    unsigned int block_size;
    size_t write_buffer_size;
    char db[10240];
    unsigned int tcp_keepalive;
    unsigned int tcp_nodelay;
    unsigned int delete_after_get;
} conf_t;

typedef struct {
    char qname[200];
    size_t qname_length;
    enum http_method method;
    const char *body;
    size_t body_length;
    unsigned long long reqnum;
} request_t;
static unsigned long long reqnum = 1;

typedef struct {
    http_parser parser;
    uv_tcp_t handle;
    uv_write_t write_req;
    request_t request;
    unsigned short keepalive : 1;
    unsigned long long connum;
} client_t;
static unsigned long long connum = 1;

static conf_t conf[1];
static char repbuf[1048576];
static char repbuf2[1048576];
static uv_buf_t uvbuf[2];

static leveldb_t *db;
static leveldb_options_t *db_options;
static leveldb_readoptions_t *db_roptions;
static leveldb_writeoptions_t *db_woptions;

static uv_loop_t* uv_loop;
static uv_tcp_t server;

static http_parser_settings parser_settings;

static struct {
    struct {
        const char *at;
        size_t length;
    } k, v;
} header_kv;

void conf_init(conf_t *conf) {
    strcpy(conf->host, "127.0.0.1");
    conf->port = 1219;
    conf->cache_size = 128 * 1048576; /* 128MB */
    conf->block_size = 4 * 1024; /* 4KB */
    conf->write_buffer_size = 32 * 1048576; /* 32MB */
    conf->tcp_keepalive = 10;
    conf->tcp_nodelay = 1;
    strcpy(conf->db, "./db");
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
        if (strcmp(k, "host") == 0) {
            strcpy(conf->host, v);
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
            sscanf(v, "%zu", &conf->write_buffer_size);
        }
        else if (!strcmp(k, "db")) {
            strcpy(conf->db, v);
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

void on_close(uv_handle_t* handle) {
    client_t* client = (client_t*)handle->data;
    free(client);
}

uv_buf_t on_alloc(uv_handle_t* handle, size_t suggested_size) {
    (void)handle;
    uv_buf_t buf;
    buf.base = malloc(suggested_size);
    buf.len = suggested_size;
    return buf;
}

void on_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf) {
    ssize_t parsed;

    client_t* client = (client_t *)tcp->data;
    
    if (nread >= 0) {
        parsed = http_parser_execute(&client->parser, &parser_settings, buf.base, nread);
        if (parsed < nread) {
            uv_close((uv_handle_t *)&client->handle, on_close);
        }
    } else {
        uv_close((uv_handle_t *)&client->handle, on_close);
    }

    free(buf.base);
}

void on_connect(uv_stream_t* server_handle, int status) {
    uv_check(status, "connect");

    int r;

    assert((uv_tcp_t*)server_handle == &server);
    uv_tcp_keepalive((uv_tcp_t *)server_handle, conf->tcp_keepalive, conf->tcp_keepalive);
    uv_tcp_nodelay((uv_tcp_t *)server_handle, conf->tcp_nodelay);
    
    client_t *client = (client_t *)malloc(sizeof(client_t));

    uv_tcp_init(uv_loop, &client->handle);
    http_parser_init(&client->parser, HTTP_REQUEST);

    client->parser.data = client;
    client->handle.data = client;
    client->keepalive = 0;
    client->connum = connum++;

    r = uv_accept(server_handle, (uv_stream_t*)&client->handle);
    uv_check(r, "accept");

    uv_read_start((uv_stream_t*)&client->handle, on_alloc, on_read);
}

int on_message_begin(http_parser* parser) {
    client_t *client = (client_t *)parser->data;
    client->request.qname_length = 0;
    client->request.body_length = 0;
    client->request.reqnum = reqnum++;
    return 0;
}

int on_url(http_parser* parser, const char* at, size_t length) {
    client_t *client = (client_t *)parser->data;
    request_t *request = &client->request;
    struct http_parser_url url;
    http_parser_parse_url(at, length, 0, &url);
    if (url.field_set & (1 << UF_PATH)) {
        if (at[url.field_data[UF_PATH].off] == '/') {
            if (url.field_data[UF_PATH].len > 1) {
                request->qname_length = url.field_data[UF_PATH].len - 1;
                memcpy(request->qname, at + url.field_data[UF_PATH].off + 1, url.field_data[UF_PATH].len - 1);
                request->qname[url.field_data[UF_PATH].len - 1] = 0;
            }
        }
        else {
            request->qname_length = url.field_data[UF_PATH].len;
            memcpy(request->qname, at + url.field_data[UF_PATH].off, url.field_data[UF_PATH].len);
            request->qname[url.field_data[UF_PATH].len] = 0;
        }
    }
    
    return 0;
}

int on_header_field(http_parser* parser, const char* at, size_t length) {
    (void)parser;
    header_kv.k.at = at;
    header_kv.k.length = length;
    return 0;
}

int on_header_value(http_parser* parser, const char* at, size_t length) {
    client_t *client = (client_t *)parser->data;
    header_kv.v.at = at;
    header_kv.v.length = length;
    if (!client->keepalive && !memcmp("Connection", header_kv.k.at, header_kv.k.length) && !strncasecmp("keep-alive", at, length)) {
        client->keepalive = 1; 
    }
    return 0;
}


int on_headers_complete(http_parser* parser) {
    client_t *client = (client_t *)parser->data;
    client->request.method = (enum http_method)parser->method;
    return 0;
}

int on_body(http_parser* parser, const char* at, size_t length) {
    client_t *client = (client_t *)parser->data;
    client->request.body = at;
    client->request.body_length = length;
    return 0;
}

int queue_getput_pos(char *queue, uint64_t *getpos, uint64_t *putpos) {
    char *val=NULL, *err=NULL, tmp[48] = {0};
    size_t len;
    
    val = leveldb_get(db, db_roptions, queue, strlen(queue), &len, &err);
    if (err != NULL) {
        twarn("%s", err);
        return -1;
    }
    else if (val == NULL) {
        // key not exists
        if (getpos) {
            *getpos = 0;
        }
        if (putpos) {
            *putpos = 0;
        }
        return 1;
    }
    else {
        char *period = strchr(val, ',');
        if (period != NULL) {
            *period = 0;
            if (getpos) {
                sscanf(val, "%"PRIu64, getpos);
            }
            if (putpos) {
                memcpy(tmp, period + 1, len - (period - val) - 1);
                sscanf(tmp, "%"PRIu64, putpos);
            }
            leveldb_free(val);
            return 0;
        }
        else {
            *period = ',';
            memcpy(tmp, val, len);
            twarnx("invalid key: %s", val);
            leveldb_free(val);
            return -1;
        }
    }
}

int set_queue_getput_pos(char *qname, int qlen, uint64_t getpos, uint64_t putpos) {
    char val[48], *err=NULL;
    int len = snprintf(val, 48, "%" PRIu64 ",%" PRIu64, getpos, putpos);
    leveldb_put(db, db_woptions, qname, qlen, val, len, &err);
    if (err != NULL) {
        twarn("unable to leveldb_put %s at %s: %s", val, qname, err); 
        return 1;
    }
    return 0;
}

void after_write(uv_write_t *req, int status) {
    client_t *client = (client_t *)container_of(req, client_t, write_req);
    uv_check(status, "write");
    if (!client->keepalive && !uv_is_closing((uv_handle_t *)req->handle)) {
        uv_close((uv_handle_t *)req->handle, on_close);
    }
}

int on_message_complete(http_parser* parser) {
    client_t *client = (client_t*)parser->data;
    request_t *request = &client->request;
    char qname[255];
    int qlen, len, r;
    uint64_t getpos, putpos;
    if (request->qname_length == 0 ||  strspn(request->qname, QUEUE_CHARS) != request->qname_length) {
        /* invalid qname */
        len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 400, "Bad Request", 18);
        uvbuf[0].base = repbuf;
        uvbuf[0].len = len;
        uvbuf[1].base = "INVALID QUEUE NAME";
        uvbuf[1].len = 18;
        uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
        return 0;
    }
    switch (request->method) {
        case HTTP_GET:
            r = queue_getput_pos(request->qname, &getpos, &putpos);
            if (r > 0) {
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 404, "NOT FOUND", 16);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "QUEUE NOT EXISTS";
                uvbuf[1].len = 16;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            else if (r < 0) {
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 500, "Internal Server Error", 21);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            if (getpos == putpos) {
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 404, "NOT FOUND", 11);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "QUEUE EMPTY";
                uvbuf[1].len = 11;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            size_t vallen;
            char *err = NULL;
            qlen = snprintf(qname, sizeof(qname), "%s:%"PRIu64, request->qname, getpos);
            char *val = leveldb_get(db, db_roptions, qname, qlen, &vallen, &err);
            if (err != NULL) {
                uvbuf[1].base = err;
                uvbuf[1].len = strlen(err);
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 400, "Bad Request", uvbuf[1].len);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            set_queue_getput_pos(request->qname, request->qname_length, getpos + 1, putpos);
            len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 200, "OK", vallen);
            uvbuf[0].base = repbuf;
            uvbuf[0].len = len;
            uvbuf[1].base = val;
            uvbuf[1].len = vallen;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            leveldb_free(val);
            if (conf->delete_after_get) {
                leveldb_delete(db, db_woptions, qname, qlen, NULL);
            }
            break;
        case HTTP_PUT:
            r = queue_getput_pos(request->qname, &getpos, &putpos);
            if (r < 0) {
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 500, "Internal Server Error", 21);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            qlen = snprintf(qname, sizeof(qname), "%s:%"PRIu64, request->qname, putpos);
            leveldb_put(db, db_woptions, qname, qlen, request->body, request->body_length, &err);
            if (err == NULL) {
                set_queue_getput_pos(request->qname, request->qname_length, getpos, putpos+1);
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 200, "OK", 2);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "OK";
                uvbuf[1].len = 2;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            }
            else {
                uvbuf[1].base = err;
                uvbuf[1].len = strlen(err);
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 400, "Bad Request", uvbuf[1].len);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            }
            break;
        case HTTP_DELETE:
        case HTTP_PURGE:
            set_queue_getput_pos(request->qname, request->qname_length, 0, 0);
            len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 200, "OK", 2);
            uvbuf[0].base = repbuf;
            uvbuf[0].len = len;
            uvbuf[1].base = "OK";
            uvbuf[1].len = 2;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
        case HTTP_OPTIONS:
            r = queue_getput_pos(request->qname, &getpos, &putpos);
            if (r < 0) {
                len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 500, "Internal Server Error", 21);
                uvbuf[0].base = repbuf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            len = snprintf(repbuf2, 1048576, "{\"name\":\"%s\",\"putpos\":%"PRIu64",\"getpos\":%"PRIu64"}\n", request->qname, putpos, getpos);
            uvbuf[1].base = repbuf2;
            uvbuf[1].len = len;
            len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 200, "OK", len);
            uvbuf[0].base = repbuf;
            uvbuf[0].len = len;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
        default:
            len = snprintf(repbuf, 1048576, HEADER, parser->http_major, parser->http_minor, 400, "Bad Request", 14);
            uvbuf[0].base = repbuf;
            uvbuf[0].len = len;
            uvbuf[1].base = "INVALID METHOD";
            uvbuf[1].len = 14;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *errstr = NULL;
    int r;
    
    conf_init(conf);
    if (argc == 2 && conf_loadfile(conf, argv[1]) != 0) {
        terrx(1, "failed to load conf %s", argv[1]);
    }
    
    db_options = leveldb_options_create();
    leveldb_options_set_create_if_missing(db_options, 1);
    leveldb_options_set_cache(db_options, leveldb_cache_create_lru(conf->cache_size));
    leveldb_options_set_block_size(db_options, conf->block_size);
    leveldb_options_set_write_buffer_size(db_options, conf->write_buffer_size);
    db = leveldb_open(db_options, conf->db, &errstr);
    if (errstr) {
        terrx(1, "unable to open db at %s: %s", conf->db, errstr);
    }
    db_roptions = leveldb_readoptions_create();
    db_woptions = leveldb_writeoptions_create();

    parser_settings.on_message_begin = on_message_begin;
    parser_settings.on_url = on_url;
    parser_settings.on_body = on_body;
    parser_settings.on_headers_complete = on_headers_complete;
    parser_settings.on_message_complete = on_message_complete;
    parser_settings.on_header_field = on_header_field;
    parser_settings.on_header_value = on_header_value;

    uv_loop = uv_default_loop();
  
    r = uv_tcp_init(uv_loop, &server);
    uv_assert(r, "uv_tcp_init");

    struct sockaddr_in address = uv_ip4_addr(conf->host, conf->port);
    r = uv_tcp_bind(&server, address);
    uv_assert(r, "uv_tcp_bind");

    r = uv_listen((uv_stream_t*)&server, 128, on_connect);
    uv_assert(r, "uv_listen");

    printf("listening on      : %s:%hu\n", conf->host, conf->port);
    printf("db                : %s\n", conf->db);
    printf("cache_size        : %u\n", conf->cache_size);
    printf("block_size        : %u\n", conf->block_size);
    printf("write_buffer_size : %u\n", conf->write_buffer_size);
    printf("tcp_keepalive     : %u\n", conf->tcp_keepalive);
    printf("tcp_nodelay       : %s\n", conf->tcp_nodelay ? "true" : "false");
    printf("delete_after_get  : %s\n", conf->delete_after_get ? "true" : "false");

    uv_run(uv_loop, UV_RUN_DEFAULT);
    leveldb_close(db);

    return 0;
}
