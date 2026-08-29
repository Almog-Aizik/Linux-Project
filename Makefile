all: admin_panel server BB

admin_panel: PC/admin_panel.c PC/shared_defs.c
	gcc PC/admin_panel.c PC/shared_defs.c -o admin_panel -lsqlite3

server: PC/server.cpp PC/shared_defs.c
	g++ PC/server.cpp PC/shared_defs.c -o server -lsqlite3

BB: beaglebone/bb.c
	arm-none-linux-gnueabihf-gcc beaglebone/bb.c -o bb-daemon
	
.PHONY: clean
clean:
	rm -f admin_panel server bb-daemon
