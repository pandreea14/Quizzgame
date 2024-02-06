all: clear server client

server: server.c
		gcc -g serverCOR.c -Wall -o s -lpthread -lsqlite3 

client: client.c
		gcc -g client.c -Wall -o c

clear: