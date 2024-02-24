all: client comserver

client: client.c
	gcc -Wall -g -o client client.c

comserver: comserver.c
	gcc -Wall -g -o server comserver.c

clean:
	rm -fr client server
	rm -f cs_pipe_* sc_pipe_*
