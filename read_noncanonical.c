// Read from serial port in non-canonical mode
#define _POSIX_SOURCE 1
#define BAUDRATE B9600        // fix: was B38400

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define FALSE 0
#define TRUE 1
#define BUF_SIZE 256

#define START   0
#define FLAG_RCV 1
#define A_RCV   2
#define C_RCV   3
#define BCC_OK  4
#define END     5

#define F     0x7E
#define ESC   0x7D
#define A_SET 0x03
#define A_UA  0x01
#define C_SET 0x03
#define C_UA  0x07

volatile int STOP = FALSE;

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const char *serialPortName = argv[1];
    if (argc < 2) {
        printf("Usage: %s <SerialPort>\n", argv[0]);
        exit(1);
    }

    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror(serialPortName); exit(-1); }

    struct termios oldtio, newtio;
    if (tcgetattr(fd, &oldtio) == -1) { perror("tcgetattr"); exit(-1); }

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1;
    newtio.c_cc[VMIN] = 0;
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) { perror("tcsetattr"); exit(-1); }

    printf("New termios structure set\n");

    //-----------------------------------------------------------
    // PHASE 1: RECEIVE SET, SEND UA
    //-----------------------------------------------------------
    unsigned char buf;
    int state = START;
    int a_chk = A_SET;
    int c_chk = C_SET;
    STOP = FALSE;

    while (STOP == FALSE) {
        int res = read(fd, &buf, 1);
        if (res <= 0) continue;
        printf("Byte read: %02X\n", buf);

        switch (state) {
            case START:
                if (buf == F) state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (buf == F) state = FLAG_RCV;
                else if (buf == A_SET) state = A_RCV;
                else state = START;
                break;
            case A_RCV:
                if (buf == C_SET) state = C_RCV;
                else if (buf == F) state = FLAG_RCV;
                else state = START;
                break;
            case C_RCV:
                if (buf == (a_chk ^ c_chk)) state = BCC_OK;
                else if (buf == F) state = FLAG_RCV;
                else state = START;
                break;
            case BCC_OK:
                if (buf == F) { printf("SET received!\n"); state = END; STOP = TRUE; }
                else state = FLAG_RCV;
                break;
        }
    }

    // Send UA
    unsigned char ua_frame[5] = {F, A_UA, C_UA, A_UA ^ C_UA, F};
    int written = write(fd, ua_frame, 5);
    printf("%d bytes UA written\n", written);

    //-----------------------------------------------------------
    // PHASE 2: RECEIVE I FRAMES, SEND RR
    //-----------------------------------------------------------
    unsigned char dataBuffer[BUF_SIZE];
    int dataIdx = 0;
    int escaped = FALSE;
    unsigned char a_rcv = 0, c_rcv = 0, bcc1_rcv = 0;
    int expectedNs = 0;
    state = START;
    STOP = FALSE;

    while (STOP == FALSE) {
        int res = read(fd, &buf, 1);
        if (res <= 0) continue;

        // Destuffing — only in data field (BCC_OK state)
        if (state == BCC_OK) {
            if (buf == ESC && !escaped) 
            { 
                escaped = TRUE; 
                continue; 
            }
            if (escaped) { 
                buf = buf ^ 0x20; 
                escaped = FALSE; }
        }

        switch (state) {
            case START:
                if (buf == F) 
                    state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (buf == F) 
                    state = FLAG_RCV;
                else if (buf == A_SET) { 
                    a_rcv = buf; 
                    state = A_RCV; 
                }
                else state = START;
                break;
            case A_RCV:
                if (buf == 0x00 || buf == 0x40) {
                    c_rcv = buf;
                    bcc1_rcv = a_rcv ^ c_rcv;
                    state = C_RCV;
                }
                else if (buf == F) 
                    state = FLAG_RCV;
                else state = START;
                break;
            case C_RCV:
                if (buf == bcc1_rcv) { 
                    state = BCC_OK; 
                    dataIdx = 0; 
                    escaped = FALSE; 
                }
                else if (buf == F) 
                    state = FLAG_RCV;
                else 
                    state = START;
                break;
            case BCC_OK:
                if (buf == F) {
                    // verify BCC2 — last byte in dataBuffer is BCC2
                    unsigned char bcc2 = 0;
                    for (int j = 0; j < dataIdx - 1; j++) bcc2 ^= dataBuffer[j];

                    if (bcc2 == dataBuffer[dataIdx - 1]) {
                        int receivedNs = (c_rcv == 0x00) ? 0 : 1;

                        // Send RR regardless (ACK either way)
                        unsigned char Nr = (c_rcv == 0x00) ? 1 : 0;
                        unsigned char C_RR = (Nr == 1) ? 0x85 : 0x05;
                        unsigned char rr_frame[5] = {F, A_SET, C_RR, A_SET ^ C_RR, F};
                        write(fd, rr_frame, 5);

                        if (receivedNs != expectedNs) {
                            printf("Duplicate I frame (Ns=%d), discarding\n", receivedNs);
                        } else {
                            printf("I frame OK! Data: ");
                            for (int j = 0; j < dataIdx - 1; j++) printf("%02X ", dataBuffer[j]);
                            printf("\n");
                            expectedNs = !expectedNs;  // advance only on new frame
                        }
                        printf("RR(Nr=%d) sent\n", Nr);
                    } else {
                        printf("BCC2 error! Sending REJ\n");
                        unsigned char Nr = (c_rcv == 0x00) ? 0 : 1;
                        unsigned char C_REJ = (Nr == 0) ? 0x01 : 0x81;
                        unsigned char rej_frame[5] = {F, A_SET, C_REJ, A_SET ^ C_REJ, F};
                        write(fd, rej_frame, 5);
                    }
                    state = START;
                }
                else dataBuffer[dataIdx++] = buf;
                break;
        }
    }

    printf("Exiting...\n");

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1) { perror("tcsetattr"); exit(-1); }
    close(fd);
    return 0;
}