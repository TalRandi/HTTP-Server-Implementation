all: server.c

	gcc server.c threadpool.c -o server -lpthread -Wall

all-GDB: server.c

	gcc -g server.c threadpool.c -o server -lpthread -Wall