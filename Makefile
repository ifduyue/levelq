CFLAGS=-Wall -Wextra -Werror -O2 -g -pthread -I. -Ideps -Ideps/http-parser -Ideps/leveldb/include -Ideps/libuv/include
CLIBS=-static -Ldeps/leveldb -Ldeps/libuv -Ldeps/http-parser -lleveldb -luv -lhttp_parser -lpthread -lrt -lstdc++

levelq: main.c deps
	$(CC) $< -o $@ $(CFLAGS) $(CLIBS)

leveldb_example: leveldb_example.c
	$(CC) $< -o $@ $(CFLAGS) $(CLIBS)

deps: libuv http-parser leveldb

libuv: deps/libuv/libuv.a

deps/libuv/libuv.a:
	$(MAKE) -C deps/libuv libuv.a

http-parser: deps/http-parser/libhttp_parser.a

deps/http-parser/libhttp_parser.a:
	$(MAKE) -C deps/http-parser package

leveldb: deps/leveldb/libleveldb.a

deps/leveldb/libleveldb.a:
	$(MAKE) -C deps/leveldb  libleveldb.a

clean:
	$(MAKE) -C deps/libuv clean
	$(MAKE) -C deps/http-parser clean
	$(MAKE) -C deps/leveldb clean

distclean: clean
	rm -f levelq

.PHONY:
	clean distclean deps deps/libuv deps/http-parser deps/leveldb
