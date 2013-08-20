levelq
=======

A simple queue service using leveldb as storage.

Compile
---------

::
    $ git clone https://github.com/ifduyue/levelq.git
    $ cd levelq
    $ git submodule update --init
    $ make

Usage
------

put::

    $ curl -X PUT -d value http://127.0.0.1:1219/queue_name
    OK

get::

    $ curl http://127.0.0.1:1219/queue_name
    value

info::

    $ curl -X OPTIONS http://127.0.0.1:1219/queue_name
    {"name":"queue_name","putpos":1,"getpos":1}

purge/delete::

    $ curl -X PURGE http://127.0.0.1:1219/queue_name
    OK

