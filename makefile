CFLAGS=-W -Wall -g -ggdb

all: webServer clean

webServer: webServer.o
	gcc $(CFLAGS) -o webServer webServer.o

webServer.o: webServer.c
	gcc $(CFLAGS) -c webServer.c
	
clean:
	rm -rf *.o 