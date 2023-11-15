all: myclient myserver

myclient: myclient.c
	g++ -g -Wall -O -o myclient myclient.c
myserver: myserver.c
	gcc -g -Wall -O -o myserver myserver.c -lldap -llber
clean:
	rm -f myclient myserver