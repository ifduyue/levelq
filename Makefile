CFLAGS=-Wall -Wextra -Werror -Wno-unused-result -O2 -g -pthread -I. -Ideps -Ideps/http-parser -Ideps/leveldb/include -Ideps/libuv/include -Ideps/mdb/libraries/liblmdb -Ideps/jemalloc/include -Ideps/unqlite
CLIBS=deps/libuv/.libs/libuv.a deps/leveldb/libleveldb.a deps/http-parser/http_parser.o deps/mdb/libraries/liblmdb/liblmdb.a deps/jemalloc/lib/libjemalloc.a deps/unqlite/unqlite.o -lstdc++
OBJS=db.o db_leveldb.o db_lmdb.o db_unqlite.o conf.o

ifeq ($(shell uname), Darwin)
	CLIBS+=-framework Carbon -framework CoreServices
else
	CLIBS+=-lrt
endif

levelq: main.c deps $(OBJS)
	$(CC) $< $(OBJS) -o $@ $(CFLAGS) $(CLIBS)

deps: libuv http-parser leveldb lmdb jemalloc unqlite

libuv: deps/libuv/.libs/libuv.a

deps/libuv/.libs/libuv.a:
	if [ ! -f deps/libuv/Makefile ]; then \
		if [ ! -f deps/libuv/configure ]; then \
			cd deps/libuv && ./autogen.sh; \
		fi; \
		cd deps/libuv && ./configure; \
	fi;
	$(MAKE) -C deps/libuv

http-parser: deps/http-parser/http_parser.o

deps/http-parser/http_parser.o:
	$(MAKE) -C deps/http-parser http_parser.o

leveldb: deps/leveldb/libleveldb.a

deps/leveldb/libleveldb.a:
	$(MAKE) -C deps/leveldb  libleveldb.a

lmdb: deps/mdb/libraries/liblmdb/liblmdb.a

deps/mdb/libraries/liblmdb/liblmdb.a:
	$(MAKE) -C deps/mdb/libraries/liblmdb liblmdb.a

jemalloc: deps/jemalloc/lib/libjemalloc.a

deps/jemalloc/lib/libjemalloc.a: deps/jemalloc/Makefile
	if [ ! -f deps/jemalloc/Makefile ]; then \
		cd deps/jemalloc && ./autogen.sh; \
	fi;
	$(MAKE) -C deps/jemalloc lib/libjemalloc.a

deps/jemalloc/Makefile:
	cd deps/jemalloc && ./autogen.sh

unqlite: deps/unqlite/unqlite.o

deps/unqlite/unqlite.o:
	$(MAKE) -C deps/unqlite unqlite.o

clean:
	$(MAKE) -C deps/http-parser clean
	$(MAKE) -C deps/leveldb clean
	$(MAKE) -C deps/mdb/libraries/liblmdb clean
	if [ -f deps/jemalloc/Makefile ]; then \
		$(MAKE) -C deps/jemalloc clean; \
	fi;
	if [ -f deps/libuv/Makefile ]; then \
		$(MAKE) -C deps/jemalloc clean; \
	fi;
	rm -f *.o deps/unqlite/unqlite.o

distclean: clean
	if [ -f deps/jemalloc/Makefile ]; then \
		$(MAKE) -C deps/jemalloc distclean; \
	fi;
	if [ -f deps/libuv/Makefile ]; then \
		$(MAKE) -C deps/libuv distclean; \
	fi;
	rm -f levelq

.PHONY:
	clean distclean libuv http-parser leveldb jemalloc unqlite
