all: relay

CFLAGS=-c -g -Wall -D_REENTRANT

relay: relay.o fq.o mp3.o
	gcc -g -o relay relay.o fq.o mp3.o -lpthread -lrt

relay.o: relay.c relay.h mp3.h fq.h
	gcc ${CFLAGS} relay.c

fq.o: fq.c fq.h
	gcc ${CFLAGS} fq.c

clean::
	rm -f relay.o fq.o *~

clear: clean
	rm -f relay
