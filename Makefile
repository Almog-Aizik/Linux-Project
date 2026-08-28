all: SQLite server BB

SQLite: PC/SQLite.c PC/shared_defs.c
	gcc PC/SQLite.c PC/shared_defs.c -o SQLite -lsqlite3

server: PC/server.cpp PC/shared_defs.c
	g++ PC/server.cpp PC/shared_defs.c -o server -lsqlite3

BB: beaglebone/bb.c
	arm-none-linux-gnueabihf-gcc beaglebone/bb.c -o bb-daemon
.PHONY: clean
clean:
	rm -f SQLite server bb-daemon
