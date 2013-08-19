CFLAGS=-Wall -Wextra -Werror -Wno-unused-result -O2 -g -pthread -I. -Ideps -Ideps/http-parser -Ideps/leveldb/include -Ideps/libuv/include -Ideps/mdb/libraries/liblmdb
CLIBS=deps/libuv/libuv.a deps/leveldb/libleveldb.a deps/http-parser/http_parser.o deps/mdb/libraries/liblmdb/liblmdb.a -lstdc++
OBJS=db.o db_leveldb.o db_lmdb.o conf.o

ifeq ($(shell uname), Darwin)
	CLIBS+=-framework Carbon -framework CoreServices
else
	CLIBS+=-lrt
endif

levelq: main.c deps $(OBJS)
	$(CC) $< $(OBJS) -o $@ $(CFLAGS) $(CLIBS)

deps: libuv http-parser leveldb lmdb

libuv: deps/libuv/libuv.a

deps/libuv/libuv.a:
	$(MAKE) -C deps/libuv libuv.a

http-parser: deps/http-parser/http_parser.o

deps/http-parser/http_parser.o:
	$(MAKE) -C deps/http-parser http_parser.o

leveldb: deps/leveldb/libleveldb.a

deps/leveldb/libleveldb.a:
	$(MAKE) -C deps/leveldb  libleveldb.a

lmdb: deps/mdb/libraries/liblmdb/liblmdb.a

deps/mdb/libraries/liblmdb/liblmdb.a:
	$(MAKE) -C deps/mdb/libraries/liblmdb liblmdb.a

clean:
	$(MAKE) -C deps/libuv clean
	$(MAKE) -C deps/http-parser clean
	$(MAKE) -C deps/leveldb clean
	$(MAKE) -C deps/mdb/libraries/liblmdb clean
	rm -f *.o

distclean: clean
	$(MAKE) -C deps/libuv distclean
	rm -f levelq

.PHONY:
	clean distclean libuv http-parser leveldb
