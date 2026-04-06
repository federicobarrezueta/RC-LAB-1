CC     = gcc
CFLAGS = -Wall

OBJS = link_layer.o alarm_sigaction.o application_layer.o

all: transmitter receiver cable test_protocol

alarm_sigaction.o: alarm_sigaction.c alarm_sigaction.h
	$(CC) $(CFLAGS) -c alarm_sigaction.c

transmitter: transmitter.o $(OBJS)
	$(CC) $(CFLAGS) -o transmitter transmitter.o $(OBJS)

transmitter.o: transmitter.c link_layer.h
	$(CC) $(CFLAGS) -c transmitter.c

receiver: receiver.o $(OBJS)
	$(CC) $(CFLAGS) -o receiver receiver.o $(OBJS)

receiver.o: receiver.c link_layer.h
	$(CC) $(CFLAGS) -c receiver.c

link_layer.o: link_layer.c link_layer.h alarm_sigaction.h
	$(CC) $(CFLAGS) -c link_layer.c

application_layer.o: application_layer.c application_layer.h link_layer.h
	$(CC) $(CFLAGS) -c application_layer.c

cable: cable.c
	$(CC) $(CFLAGS) -o cable cable.c -lm

test_protocol: test_protocol.c
	$(CC) $(CFLAGS) -o test_protocol test_protocol.c -lm

clean:
	rm -f *.o transmitter receiver cable test_protocol
