#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#include <termios.h>

// Default baud rate
#define BAUDRATE        B9600

// Frame constants
#define FLAG            0x7E
#define A_TX            0x03   // Tx command / Rx reply
#define A_RX            0x01   // Rx command / Tx reply
#define C_SET           0x03
#define C_UA            0x07
#define C_DISC          0x0B
#define C_I0            0x00   // I frame Ns=0
#define C_I1            0x40   // I frame Ns=1
#define C_RR0           0x05   // ACK, expect Ns=0 (acknowledges Ns=1)
#define C_RR1           0x85   // ACK, expect Ns=1 (acknowledges Ns=0)
#define C_REJ0          0x01   // NACK, retransmit Ns=0
#define C_REJ1          0x81   // NACK, retransmit Ns=1
#define ESC             0x7D

#define MAX_PAYLOAD_SIZE 1000

typedef enum { TRANSMITTER, RECEIVER } LinkLayerRole;

typedef struct {
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int numTransmissions;
    int timeout;
} LinkLayer;

int llopen(LinkLayer params);
int llwrite(const unsigned char *buf, int bufSize);
int llread(unsigned char *packet);
int llclose();

#endif
