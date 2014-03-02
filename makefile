CFLAGS=-W -Wall -g -ggdb

all: webServer clean

webServer: webServer.o
	gcc -o webServer webServer.o $(CFLAGS)

webServer.o: webServer.c
	gcc -c webServer.c $(CFLAGS)
	
clean:
	rm -rf *.o 