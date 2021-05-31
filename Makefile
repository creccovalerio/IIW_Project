all:
	gcc -g server.c -Wall -Wextra -o server -lpthread -lrt -D_GNU_SOURCE
	gcc -g client.c -Wall -Wextra -o client -lpthread -lrt -D_GNU_SOURCE
clean:
	-rm server
	-rm client


