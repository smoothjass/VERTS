all: myclient myserver

myclient: myclient.c
	gcc -g -Wall -O -o myclient myclient.c
myserver: myserver.c
	gcc -g -Wall -O -o myserver myserver.c
clean:
	rm -f myclient myserver
