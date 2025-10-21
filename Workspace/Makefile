# Makefile for CPE464 tcp and udp test code
# updated by Hugh Smith - April 2023

# all target makes UDP test code
# tcpAll target makes the TCP test code


CC= gcc
CFLAGS= -g -Wall -std=gnu99
LIBS = 

OBJS = networks.o gethostbyname.o pollLib.o safeUtil.o

#uncomment next two lines if your using sendtoErr() library
LIBS += libcpe464.2.21.a -lstdc++ -ldl
CFLAGS += -D__LIBCPE464_


all: udpAll

udpAll: rcopy server
tcpAll: myClient myServer

rcopy: rcopy.c functions.c circularQueue.c $(OBJS) 
	$(CC) $(CFLAGS) -o rcopy rcopy.c functions.c circularQueue.c $(OBJS) $(LIBS)

server: server.c functions.c circularQueue.c $(OBJS) 
	$(CC) $(CFLAGS) -o server server.c functions.c circularQueue.c $(OBJS) $(LIBS)

myClient: myClient.c $(OBJS)
	$(CC) $(CFLAGS) -o myClient myClient.c  $(OBJS) $(LIBS)

myServer: myServer.c $(OBJS)
	$(CC) $(CFLAGS) -o myServer myServer.c $(OBJS) $(LIBS)

.c.o:
	gcc -c $(CFLAGS) $< -o $@ $(LIBS)

cleano:
	rm -f *.o

clean:
	rm -f myServer myClient rcopy server *.o




