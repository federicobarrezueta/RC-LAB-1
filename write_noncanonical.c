// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

// included by <termios.h>
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BAUDRATE B9600

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>  
//Include statement for the alarm_sigaction.h file
#include "alarm_sigaction.h"

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

#define START 0
#define FLAG_RCV 1
#define A_RCV 2
#define C_RCV 3
#define BCC_OK 4
#define END 5

#define F 0x7E
#define A_SET 0x03
#define A_UA 0x01
#define C_SET 0x03
#define C_UA 0x07

#define ESC  0x7D

//UA = 

volatile int STOP = FALSE;
volatile int DATA_RCV_FINISHED = FALSE;

//Establish connection between writer and reader exiting when receiving confirmation 
void establishConnection(int fd, unsigned char *initial_frame){

        //SENDS SET MESSAGE

    // Create string to send
    //unsigned char buf; // +1: Save space for the final '\0' char             

    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.

    // Alarm variables

    #define MAX_RETRANSMISSIONS 3
    #define TIMEOUT 3

    //Declares the handler
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    
    sigaction(SIGALRM, &act, NULL);

    STOP = FALSE;

    //Init state machine
    int state = START;
    unsigned char a_rcv = 0;
    unsigned char c_rcv = 0;
    alarmCount = 0;
    alarmEnabled = FALSE;


    while (STOP == FALSE && alarmCount <= MAX_RETRANSMISSIONS){
        

        // If alarm is triggered or first cycle , retransmits
        if (alarmEnabled == FALSE)
        {

            printf("DEBUG: alarmCount=%d, retransmitting...\n", alarmCount); // add this
            // Envia SET
            int bytes = write(fd, initial_frame, 5);
            printf("%d bytes written (attempt %d)\n", bytes, alarmCount);

            // Arma alarme
            alarm(TIMEOUT);
            alarmEnabled = TRUE;

            // Reset da máquina de estados para cada tentativa
            state = START;
        }

        //Waits for UA

        //Read one byte at a time
        unsigned char buf = 0;
        int res = read(fd, &buf, 1);

        if (res <= 0) continue; // interrupted by the alarm
        
        printf("Byte read: %02X\n", buf);

           switch (state)
           {
            case START:
            
                if (buf == F)
                {
                    state = FLAG_RCV;
                    printf("FLAG_RCV\n");
                }
                else {
                    state = START;
                    
                }
            break;

            case FLAG_RCV:

                if (buf == F){
                    printf("FLAG_RCV\n");
                    state = FLAG_RCV;
                }
                    
                else if (buf == A_UA)
                {
                    a_rcv = buf; 
                    printf("A_RCV\n");
                    state = A_RCV;
                           
                }
                    
                else 
                    state = START;

            break;

            case A_RCV:

                if (buf == C_UA)
                {
                    c_rcv = buf;
                    printf("C_RCV\n");
                    state = C_RCV;
                }
                    
                else if (buf == F)
                {
                    printf("FLAG_RCV\n");
                    state = FLAG_RCV;
                }

                else 
                    state = START;

            break;

            case C_RCV:

                //int c_chk = A_SET;
                //int a_chk = C_UA;
                if(buf == (a_rcv^c_rcv))
                {
                    printf("BCC_OK\n");
                    state = BCC_OK;
                }
                    
                else if (buf == F)
                {
                    printf("BCC NOK");
                    printf("FLAG_RCV\n");
                    state = FLAG_RCV;
                }
                
                else
                    state = START;

            break;

            case BCC_OK:

                if (buf == F){
                    printf("END\n");
                    //alarm(0);           // cancels alarm
                    //alarmEnabled = FALSE;
                    state = END;
                    STOP = TRUE;
                }
                    
                else 
                    state = FLAG_RCV;            
            break;
        
            default:
                state = START;
                break;

            }
          


        }

    if (alarmCount <= MAX_RETRANSMISSIONS){

        printf("UA received successfully!");
        
    }

    else
    {
        printf("Connection failed after %d retransmissions.\n", MAX_RETRANSMISSIONS);
        //close(fd);
        //exit(-1);
    }  
   

    //Resets the alarm
    alarm(0);

}

void stuffByte(unsigned char *frame, int *i, unsigned char byte) {
    if (byte == F || byte == ESC) {
        frame[(*i)++] = ESC;
        frame[(*i)++] = byte ^ 0x20; //Byte stuffing if the char is F
    } else {
        frame[(*i)++] = byte;
    }
}

int sendData(int fd, unsigned char *data, int dataSize, int *Ns) {
    unsigned char iFrame[(dataSize * 2) + 6];
    int i = 0;

    // Build header
    iFrame[i++] = F;
    iFrame[i++] = A_SET;
    unsigned char C = (*Ns) ? 0x40 : 0x00;
    iFrame[i++] = C;
    unsigned char bcc1 = A_SET ^ C;
    iFrame[i++] = bcc1;

    // Add data with byte stuffing + compute BCC2
    unsigned char bcc2 = 0;
    for (int j = 0; j < dataSize; j++) {
        stuffByte(iFrame, &i, data[j]);
        bcc2 ^= data[j];
    }

    // Add BCC2 with byte stuffing
    stuffByte(iFrame, &i, bcc2);

    // Closing flag
    iFrame[i++] = F;

    int frameLen = i;  // save frame length before entering loop

    // Expected response C bytes based on current Ns
    // If we sent Ns=0, receiver ACKs with RR1 (0x85); NACK is REJ0 (0x01)
    // If we sent Ns=1, receiver ACKs with RR0 (0x05); NACK is REJ1 (0x81)
    unsigned char expectedRR  = (*Ns == 0) ? 0x85 : 0x05;
    unsigned char expectedREJ = (*Ns == 0) ? 0x01 : 0x81;

    alarmCount = 0;
    alarmEnabled = FALSE;
    int ackReceived = FALSE;

    // State machine variables for receiving RR/REJ (S/U frame: F A C BCC1 F)
    int state = START;
    unsigned char a_rcv = 0, c_rcv = 0;

    while (!ackReceived && alarmCount <= MAX_RETRANSMISSIONS) {

        // Trigger: first iteration OR timeout fired (alarmEnabled reset by handler)
        if (alarmEnabled == FALSE) {
            write(fd, iFrame, frameLen);
            printf("I frame sent (Ns=%d, attempt=%d)\n", *Ns, alarmCount);
            alarm(TIMEOUT);
            alarmEnabled = TRUE;
            state = START;  // reset state machine on each send
        }

        unsigned char buf = 0;
        int res = read(fd, &buf, 1);
        if (res <= 0) continue;  // interrupted or no data yet

        switch (state) {
            case START:
                if (buf == F) state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (buf == F) state = FLAG_RCV;
                else if (buf == A_SET) { a_rcv = buf; state = A_RCV; }
                else state = START;
                break;
            case A_RCV:
                if (buf == expectedRR || buf == expectedREJ) {
                    c_rcv = buf; state = C_RCV;
                }
                else if (buf == F) state = FLAG_RCV;
                else               state = START;
                break;
            case C_RCV:
                if      (buf == (a_rcv ^ c_rcv)) state = BCC_OK;
                else if (buf == F)               state = FLAG_RCV;
                else                             state = START;
                break;
            case BCC_OK:
                if (buf == F) {
                    if (c_rcv == expectedRR) {
                        ackReceived = TRUE;
                        printf("RR received — ACK\n");
                    } else {
                        // REJ: resend immediately, don't count as timeout
                        alarm(0);
                        alarmEnabled = FALSE;
                        printf("REJ received — retransmitting immediately\n");
                    }
                    state = START;
                } else {
                    state = FLAG_RCV;
                }
                break;
        }
    }

    alarm(0);  // cancel any pending alarm

    if (!ackReceived) {
        printf("sendData: failed after %d retransmissions\n", MAX_RETRANSMISSIONS);
        return -1;
    }

    *Ns = !*Ns;  // only toggle Ns after confirmed ACK
    return frameLen;
}


int main(int argc, char *argv[])
{

    setvbuf(stdout, NULL, _IONBF, 0);

    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    //Connection establishment phrase F A C BCC F
    unsigned char initial_frame[5] = {0x7E, 0x03,0x03, 0x03^0x03 , 0x7E};
    
    printf("New termios structure set\n");

//-------------------------------------------------------------ESTABLISH CONNECTION--------------------------------------------------------------------------


establishConnection(fd,initial_frame);


//-----------------------------------------------------------------DATA TRANSFER-----------------------------------------------------------------------------


unsigned char hello[5] = {'H', 'E', 'L', 'L', 'O'};
int Ns = 0;
sendData(fd, hello, 5, &Ns);

//----------------------------------------------------------------END TRANSMISSION---------------------------------------------------------------------------
    sleep(1);

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}