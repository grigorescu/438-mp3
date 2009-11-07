all: relay

CFLAGS=-c -g -Wall -D_REENTRANT

relay: relay.o fq.o mp3.o
	gcc -g -o relay relay.o fq.o /home/engr/ece438/MP3/mp3.o -lpthread -lrt

relay.o: relay.c relay.h /home/engr/ece438/MP3/mp3.h fq.h
	gcc ${CFLAGS} relay.c

fq.o: fq.c fq.h
	gcc ${CFLAGS} fq.c

clean::
	rm -f *.o *~

clear: clean
	rm -f relay
