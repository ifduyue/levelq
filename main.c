#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>
#include <err.h>
#include <sys/stat.h>
#include <signal.h>

#include "h.h"
#include "db.h"
#include "db_lmdb.h"
#include "db_leveldb.h"
#include "conf.h"

typedef struct {
    dbi_t *item;
    char buf[1];
} repbuf_t;

repbuf_t *repbuf_new(size_t size) {
    repbuf_t *repbuf = (repbuf_t *)malloc(sizeof(repbuf_t) + size);
    assert(repbuf);
    repbuf->item = NULL;
    return repbuf;
}

void repbuf_free(repbuf_t *repbuf) {
    if (repbuf) {
        if (repbuf->item) {
            dbi_destroy(repbuf->item);
        }
        free(repbuf);
    }
}

uv_loop_t* uv_loop;
uv_tcp_t server;
http_parser_settings parser_settings;
header_kv_t header_kv;
uv_buf_t uvbuf[2];

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

    r = uv_accept(server_handle, (uv_stream_t*)&client->handle);
    uv_check(r, "accept");

    uv_read_start((uv_stream_t*)&client->handle, on_alloc, on_read);
}

int on_message_begin(http_parser* parser) {
    client_t *client = (client_t *)parser->data;
    client->request.qname_length = 0;
    client->request.body_length = 0;
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
    char tmp[48] = {0};
    dbi_t k, *vp;
    k.data = queue;
    k.len = strlen(queue);
    vp = db_get(&k);
    if (vp->err != NULL) {
        twarn("%s", vp->err);
        dbi_destroy(vp);
        return -1;
    }
    else if (vp->data == NULL) {
        // key not exists
        *getpos = 0;
        *putpos = 0;
        dbi_destroy(vp);
        return 1;
    }
    else {
        int r;
        uint64_t get, put;
        memcpy(tmp, vp->data, vp->len);
        r = sscanf(tmp, "%"PRIu64",%"PRIu64, &get, &put);
        if (r == 2) {
            *getpos = get;
            *putpos = put;
            dbi_destroy(vp);
            return 0;
        }
        else {
            twarnx("invalid key: %.*s", (int)vp->len, vp->data);
            dbi_destroy(vp);
            return -1;
        }
    }
}

void set_queue_getput_pos(char *qname, int qlen, uint64_t getpos, uint64_t putpos) {
    dbi_t key, val;
    char s[48];
    int len = snprintf(s, 48, "%" PRIu64 ",%" PRIu64, getpos, putpos);
    val.data = s;
    val.len = len;
    key.data = qname;
    key.len = qlen;
    db_put(&key, &val);
}

void after_write(uv_write_t *req, int status) {
    uv_check(status, "write");

    repbuf_t *repbuf = (repbuf_t *)req->data;
    repbuf_free(repbuf);

    client_t *client = (client_t *)container_of(req, client_t, write_req);
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
    dbi_t k, v, *vp;

    repbuf_t *repbuf  = repbuf_new(BUFSIZE);
    client->write_req.data = repbuf;
    if (request->qname_length == 0 ||  strspn(request->qname, QUEUE_CHARS) != request->qname_length) {
        /* invalid qname */
        len = snprintf(repbuf->buf, BUFSIZE, HEADER, 400, "Bad Request", (size_t)18);
        uvbuf[0].base = repbuf->buf;
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
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 404, "NOT FOUND", (size_t)16);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uvbuf[1].base = "QUEUE NOT EXISTS";
                uvbuf[1].len = 16;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            else if (r < 0) {
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 500, "Internal Server Error", (size_t)21);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            if (getpos == putpos) {
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 404, "NOT FOUND", (size_t)11);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uvbuf[1].base = "QUEUE EMPTY";
                uvbuf[1].len = 11;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }

            qlen = snprintf(qname, sizeof(qname), "%s:%"PRIu64, request->qname, getpos);
            k.len = qlen;
            k.data = qname;
            vp = db_get(&k);
            repbuf->item = vp;
            if (vp->err != NULL) {
                uvbuf[1].base = vp->err;
                uvbuf[1].len = strlen(vp->err);
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 400, "Bad Request", uvbuf[1].len);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            set_queue_getput_pos(request->qname, request->qname_length, getpos + 1, putpos);
            len = snprintf(repbuf->buf, BUFSIZE, HEADER, 200, "OK", vp->len);
            uvbuf[0].base = repbuf->buf;
            uvbuf[0].len = len;
            uvbuf[1].base = vp->data;
            uvbuf[1].len = vp->len;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            if (conf->delete_after_get) {
                db_delete(&k);
            }
            break;
        case HTTP_PUT:
            r = queue_getput_pos(request->qname, &getpos, &putpos);
            if (r < 0) {
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 500, "Internal Server Error", (size_t)21);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            qlen = snprintf(qname, sizeof(qname), "%s:%"PRIu64, request->qname, putpos);
            k.data = qname;
            k.len = qlen;
            v.data = (char *)request->body;
            v.len = request->body_length;
            db_put(&k, &v);
            set_queue_getput_pos(request->qname, request->qname_length, getpos, putpos+1);
            len = snprintf(repbuf->buf, BUFSIZE, HEADER, 200, "OK", (size_t)2);
            uvbuf[0].base = repbuf->buf;
            uvbuf[0].len = len;
            uvbuf[1].base = "OK";
            uvbuf[1].len = 2;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
        case HTTP_DELETE:
        case HTTP_PURGE:
            set_queue_getput_pos(request->qname, request->qname_length, 0, 0);
            len = snprintf(repbuf->buf, BUFSIZE, HEADER, 200, "OK", (size_t)2);
            uvbuf[0].base = repbuf->buf;
            uvbuf[0].len = len;
            uvbuf[1].base = "OK";
            uvbuf[1].len = 2;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
        case HTTP_OPTIONS:
            r = queue_getput_pos(request->qname, &getpos, &putpos);
            if (r < 0) {
                len = snprintf(repbuf->buf, BUFSIZE, HEADER, 500, "Internal Server Error", (size_t)21);
                uvbuf[0].base = repbuf->buf;
                uvbuf[0].len = len;
                uvbuf[1].base = "Internal Server Error";
                uvbuf[1].len = 21;
                uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
                break;
            }
            len = snprintf(repbuf->buf, BUFSIZE, "{\"name\":\"%s\",\"putpos\":%"PRIu64",\"getpos\":%"PRIu64"}\n", request->qname, putpos, getpos);
            uvbuf[1].base = repbuf->buf;
            uvbuf[1].len = len;
            uvbuf[0].base = repbuf->buf + len + 2;
            len = snprintf(repbuf->buf + len + 2, BUFSIZE - len - 1, HEADER, 200, "OK", (size_t)len);
            uvbuf[0].len = len;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
        default:
            len = snprintf(repbuf->buf, BUFSIZE, HEADER, 400, "Bad Request", (size_t)14);
            uvbuf[0].base = repbuf->buf;
            uvbuf[0].len = len;
            uvbuf[1].base = "INVALID METHOD";
            uvbuf[1].len = 14;
            uv_write((uv_write_t *)&client->write_req, (uv_stream_t *)&client->handle, uvbuf, 2, after_write);
            break;
    }
    return 0;
}

void signal_handler(int sig) {
    uv_stop(uv_loop);
    db_close();
    terrx(sig, "receive signal %d, exited.", sig);
}

int main(int argc, char *argv[]) {
    int r;
    
    if (argc == 2 && conf_loadfile(conf, argv[1]) != 0) {
        terrx(1, "failed to load conf %s", argv[1]);
    }

    switch (conf->engine) {
        case engine_leveldb:
            db_leveldb_init();
            db_get = db_leveldb_get;
            db_put = db_leveldb_put;
            db_delete = db_leveldb_delete;
            db_close = db_leveldb_close;
            break;
        case engine_lmdb:
            db_lmdb_init();
            db_get = db_lmdb_get;
            db_put = db_lmdb_put;
            db_delete = db_lmdb_delete;
            db_close = db_lmdb_close;
            break;
        default:
            terrx(-1, "unsuppored db engine");
    }

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

    printf("            levelq "LEVELQ_VERSION"\n");
    printf("engine:                   : %s\n", conf->engine == engine_leveldb ? "leveldb" : conf->engine == engine_lmdb ? "lmdb" : "unknown");
    printf("db                        : %s\n", conf->db);
    printf("tcp_keepalive             : %u\n", conf->tcp_keepalive);
    printf("tcp_nodelay               : %s\n", conf->tcp_nodelay ? "true" : "false");
    printf("delete_after_get          : %s\n", conf->delete_after_get ? "true" : "false");
    if (conf->engine == engine_leveldb) {
        printf("leveldb_cache_size        : %zu\n", conf->leveldb_cache_size);
        printf("leveldb_block_size        : %zu\n", conf->leveldb_block_size);
        printf("leveldb_write_buffer_size : %zu\n", conf->leveldb_write_buffer_size);
    }
    else if (conf->engine == engine_lmdb) {
        printf("lmdb_mapsize              : %zu\n", conf->lmdb_mapsize);
    }
    printf("\n");
    printf("listening on %s:%hu\n", conf->host, conf->port);

    signal(SIGINT, signal_handler);
    signal(SIGKILL, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGSEGV, signal_handler);

    uv_run(uv_loop, UV_RUN_DEFAULT);
    db_close();

    return 0;
}
