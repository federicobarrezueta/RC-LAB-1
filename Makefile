CC = gcc
CFLAGS = -Wall

all: write_noncanonical read_noncanonical

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

clean:
	rm -f *.o write_noncanonical read_noncanonical