all: client server
client: client.c spec.h
	gcc -Wall -Wextra -pedantic -g -o client client.c spec.h -lrt
server: server.c spec.h
	mkdir server; gcc -Wall -Wextra -pedantic -g -o server/server server.c spec.h -lrt
clean:
	@rm client server/server
