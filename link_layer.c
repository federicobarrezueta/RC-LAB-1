#define _POSIX_SOURCE 1

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>

#include "link_layer.h"
#include "alarm_sigaction.h"

// State machine states
#define START 0
#define FLAG_RCV 1
#define A_RCV 2
#define C_RCV 3
#define BCC_OK 4
#define END 5

// Static connection state
static int fd = -1;
static LinkLayer ll;
static struct termios oldtio;
static int Ns = 0;
static int expectedNs = 0;

// ─────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────

static void sendSUFrame(unsigned char A, unsigned char C)
{
    unsigned char frame[5] = {FLAG, A, C, A ^ C, FLAG};
    write(fd, frame, 5);
}

// Wait for a specific S/U frame C byte.
// Returns 0 on success, -1 on failure.
static int receiveSUFrame(unsigned char expectedC)
{
    int state = START;
    unsigned char a_rcv = 0, c_rcv = 0;
    unsigned char buf;

    while (state != END)
    {
        if (read(fd, &buf, 1) <= 0)
            continue;

        switch (state)
        {
        case START:
            if (buf == FLAG)
                state = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (buf == FLAG)
                state = FLAG_RCV;
            else if (buf == A_TX || buf == A_RX)
            {
                a_rcv = buf;
                state = A_RCV;
            }
            else
                state = START;
            break;
        case A_RCV:
            if (buf == expectedC)
            {
                c_rcv = buf;
                state = C_RCV;
            }
            else if (buf == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case C_RCV:
            if (buf == (a_rcv ^ c_rcv))
                state = BCC_OK;
            else if (buf == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case BCC_OK:
            if (buf == FLAG)
                state = END;
            else
                state = FLAG_RCV;
            break;
        }
    }
    return 0;
}

static void stuffByte(unsigned char *frame, int *i, unsigned char byte)
{
    if (byte == FLAG || byte == ESC)
    {
        frame[(*i)++] = ESC;
        frame[(*i)++] = byte ^ 0x20;
    }
    else
    {
        frame[(*i)++] = byte;
    }
}

// ─────────────────────────────────────────────
// DLL API
// ─────────────────────────────────────────────

int llopen(LinkLayer params)
{
    ll = params;
    Ns = 0;
    expectedNs = 0;

    fd = open(ll.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(ll.serialPort);
        return -1;
    }

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }

    struct termios newtio;
    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = CS8 | CLOCAL | CREAD;
    cfsetispeed(&newtio, ll.baudRate);
    cfsetospeed(&newtio, ll.baudRate);
    newtio.c_iflag = 0;  // no input processing — don't drop bytes on framing errors
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1;
    newtio.c_cc[VMIN] = 0;
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    // Setup alarm handler
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    sigaction(SIGALRM, &act, NULL);

    if (ll.role == TRANSMITTER)
    {
        alarmCount = 0;
        alarmEnabled = FALSE;

        while (alarmCount <= ll.numTransmissions)
        {
            if (alarmEnabled == FALSE)
            {
                sendSUFrame(A_TX, C_SET);
                printf("SET sent (attempt %d)\n", alarmCount);
                alarm(ll.timeout);
                alarmEnabled = TRUE;
            }

            // Read one byte and feed into receiveSUFrame inline
            unsigned char buf;
            if (read(fd, &buf, 1) <= 0)
                continue;

            // Mini state machine for UA reception inside the retry loop
            static int state = START;
            static unsigned char a_rcv = 0, c_rcv = 0;

            switch (state)
            {
            case START:
                if (buf == FLAG)
                    state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (buf == FLAG)
                    state = FLAG_RCV;
                else if (buf == A_RX)
                {
                    a_rcv = buf;
                    state = A_RCV;
                }
                else
                    state = START;
                break;
            case A_RCV:
                if (buf == C_UA)
                {
                    c_rcv = buf;
                    state = C_RCV;
                }
                else if (buf == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;
            case C_RCV:
                if (buf == (a_rcv ^ c_rcv))
                    state = BCC_OK;
                else if (buf == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;
            case BCC_OK:
                if (buf == FLAG)
                {
                    alarm(0);
                    printf("UA received — connected\n");
                    state = START;
                    return fd;
                }
                else
                {
                    state = FLAG_RCV;
                }
                break;
            }
        }
        printf("llopen: failed after %d retransmissions\n", ll.numTransmissions);
        return -1;
    }
    else
    { // RECEIVER
        printf("Waiting for SET...\n");
        receiveSUFrame(C_SET);
        sendSUFrame(A_RX, C_UA);
        printf("SET received, UA sent — connected\n");
        return fd;
    }
}

int llwrite(const unsigned char *buf, int bufSize)
{
    // Build I frame
    unsigned char frame[(bufSize * 2) + 6];
    int i = 0;

    unsigned char C = Ns ? C_I1 : C_I0;
    frame[i++] = FLAG;
    frame[i++] = A_TX;
    frame[i++] = C;
    frame[i++] = A_TX ^ C;

    unsigned char bcc2 = 0;
    for (int j = 0; j < bufSize; j++)
    {
        stuffByte(frame, &i, buf[j]);
        bcc2 ^= buf[j];
    }
    stuffByte(frame, &i, bcc2);
    frame[i++] = FLAG;

    int frameLen = i;

    unsigned char expectedRR = (Ns == 0) ? C_RR1 : C_RR0;
    unsigned char expectedREJ = (Ns == 0) ? C_REJ0 : C_REJ1;

    alarmCount = 0;
    alarmEnabled = FALSE;
    int ackReceived = FALSE;
    int state = START;
    unsigned char a_rcv = 0, c_rcv = 0;

    while (!ackReceived && alarmCount <= ll.numTransmissions)
    {
        if (alarmEnabled == FALSE)
        {
            write(fd, frame, frameLen);
            printf("I frame sent (Ns=%d, attempt=%d)\n", Ns, alarmCount);
            alarm(ll.timeout);
            alarmEnabled = TRUE;
            state = START;
        }

        unsigned char byte;
        if (read(fd, &byte, 1) <= 0)
            continue;

        switch (state)
        {
        case START:
            if (byte == FLAG)
                state = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (byte == FLAG)
                state = FLAG_RCV;
            else if (byte == A_TX)
            {
                a_rcv = byte;
                state = A_RCV;
            }
            else
                state = START;
            break;
        case A_RCV:
            if (byte == expectedRR || byte == expectedREJ)
            {
                c_rcv = byte;
                state = C_RCV;
            }
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case C_RCV:
            if (byte == (a_rcv ^ c_rcv))
                state = BCC_OK;
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case BCC_OK:
            if (byte == FLAG)
            {
                if (c_rcv == expectedRR)
                {
                    ackReceived = TRUE;
                    printf("RR received — ACK\n");
                }
                else
                {
                    alarm(0);
                    alarmEnabled = FALSE;
                    printf("REJ received — retransmitting immediately\n");
                }
                state = START;
            }
            else
            {
                state = FLAG_RCV;
            }
            break;
        }
    }

    alarm(0);
    if (!ackReceived)
    {
        printf("llwrite: failed\n");
        return -1;
    }
    Ns = !Ns;
    return frameLen;
}

int llread(unsigned char *packet)
{
    unsigned char dataBuffer[MAX_PAYLOAD_SIZE + 1];
    int dataIdx = 0;
    int escaped = FALSE;
    unsigned char a_rcv = 0, c_rcv = 0, bcc1_rcv = 0;
    int state = START;
    unsigned char buf;

    while (1)
    {
        if (read(fd, &buf, 1) <= 0)
            continue;

        // Destuffing — only in data collection phase
        if (state == BCC_OK)
        {
            if (buf == ESC && !escaped)
            {
                escaped = TRUE;
                continue;
            }
            if (escaped)
            {
                buf ^= 0x20;
                escaped = FALSE;
                // store directly — never treat a destuffed byte as a frame delimiter
                if (dataIdx < MAX_PAYLOAD_SIZE + 1)
                    dataBuffer[dataIdx++] = buf;
                continue;
            }
        }

        switch (state)
        {
        case START:
            if (buf == FLAG)
                state = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (buf == FLAG)
                state = FLAG_RCV;
            else if (buf == A_TX)
            {
                a_rcv = buf;
                state = A_RCV;
            }
            else
                state = START;
            break;
        case A_RCV:
            if (buf == C_I0 || buf == C_I1)
            {
                c_rcv = buf;
                bcc1_rcv = a_rcv ^ c_rcv;
                state = C_RCV;
            }
            else if (buf == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case C_RCV:
            if (buf == bcc1_rcv)
            {
                state = BCC_OK;
                dataIdx = 0;
                escaped = FALSE;
            }
            else if (buf == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;
        case BCC_OK:
            if (buf == FLAG)
            {
                // Verify BCC2 (last byte in dataBuffer)
                unsigned char bcc2 = 0;
                for (int j = 0; j < dataIdx - 1; j++)
                    bcc2 ^= dataBuffer[j];

                if (bcc2 == dataBuffer[dataIdx - 1])
                {
                    int receivedNs = (c_rcv == C_I0) ? 0 : 1;
                    unsigned char Nr = (c_rcv == C_I0) ? 1 : 0;
                    unsigned char C_RR = (Nr == 1) ? C_RR1 : C_RR0;
                    sendSUFrame(A_TX, C_RR);

                    if (receivedNs != expectedNs)
                    {
                        printf("Duplicate frame (Ns=%d), discarding\n", receivedNs);
                        return 0;
                    }
                    int dataLen = dataIdx - 1;
                    memcpy(packet, dataBuffer, dataLen);
                    expectedNs = !expectedNs;
                    printf("RR(Nr=%d) sent\n", Nr);
                    return dataLen;
                }
                else
                {
                    printf("BCC2 error — sending REJ\n");
                    int receivedNs = (c_rcv == C_I0) ? 0 : 1;
                    unsigned char C_REJ = (receivedNs == 0) ? C_REJ0 : C_REJ1;
                    sendSUFrame(A_TX, C_REJ);
                    return -1;
                }
            }
            else
            {
                if (dataIdx < MAX_PAYLOAD_SIZE + 1)
                    dataBuffer[dataIdx++] = buf;
            }
            break;
        }
    }
}

int llclose()
{
    if (ll.role == TRANSMITTER)
    {
        sendSUFrame(A_TX, C_DISC);
        printf("DISC sent\n");
        receiveSUFrame(C_DISC);
        printf("DISC received\n");
        sendSUFrame(A_RX, C_UA);
        printf("UA sent — connection closed\n");
    }
    else
    {
        receiveSUFrame(C_DISC);
        printf("DISC received\n");
        sendSUFrame(A_RX, C_DISC);
        printf("DISC sent\n");
        receiveSUFrame(C_UA);
        printf("UA received — connection closed\n");
    }

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }
    close(fd);
    fd = -1;
    return 0;
}