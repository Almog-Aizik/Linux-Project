all: SQLite server i2c

SQLite: PC/SQLite.c PC/shared_defs.c
	gcc PC/SQLite.c PC/shared_defs.c -o SQLite -lsqlite3

server: PC/server.cpp PC/shared_defs.c
	g++ PC/server.cpp PC/shared_defs.c -o server -lsqlite3

i2c: beaglebone/i2c.c
	arm-none-linux-gnueabihf-gcc beaglebone/i2c.c -o i2c

.PHONY: clean
clean:
	rm -f SQLite server i2c