CC = gcc
CFLAGS = -Wall

OBJS = link_layer.o alarm_sigaction.o application_layer.o

all: write_noncanonical read_noncanonical transmitter receiver

write_noncanonical: write_noncanonical.o alarm_sigaction.o
	$(CC) $(CFLAGS) -o write_noncanonical write_noncanonical.o alarm_sigaction.o

write_noncanonical.o: write_noncanonical.c alarm_sigaction.h
	$(CC) $(CFLAGS) -c write_noncanonical.c

alarm_sigaction.o: alarm_sigaction.c alarm_sigaction.h
	$(CC) $(CFLAGS) -c alarm_sigaction.c

read_noncanonical: read_noncanonical.o
	$(CC) $(CFLAGS) -o read_noncanonical read_noncanonical.o

read_noncanonical.o: read_noncanonical.c
	$(CC) $(CFLAGS) -c read_noncanonical.c

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

clean:
	rm -f *.o write_noncanonical read_noncanonical transmitter receiver