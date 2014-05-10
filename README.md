Primecoin XPM Pool Server
==============

See: https://bitcointalk.org/index.php?topic=598542.0
--------------

The server works with:
- ZEROMQ message system
- Google protobuf protocol
- libwt database abstraction
- libwt webserver
- Postgres SQL server

How to compile primecoind:
- Build all dependencies
- Set your include paths etc. in makefile.unix
- In primeserver/src: make -f makefile.unix

How to compile webserver:
- Open Eclipse C++ project
- Configure libwt include paths etc.
- OR: just use the existing makefile and set env variables when necessary to find libs
- Compile

Use src/build.sh to compile the protobuf definition if needed.

Important: When starting the server for the first time use -initwtdb command line option to create all database tables.

Also to enable the pool server (after having synched), primecoind needs to be started with -gen

The webserver needs to be started as:
./webserver --docroot /usr/local/share/Wt --http-address 0.0.0.0 --http-port 80

