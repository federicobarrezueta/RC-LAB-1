# link_layer Implementation Plan

## Files to create
- `link_layer.h` — types, constants, API declarations
- `link_layer.c` — full DLL implementation
- Update `Makefile` — add `transmitter` and `receiver` targets

---

## link_layer.h

```c
#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#define BAUDRATE        B9600
#define FLAG            0x7E
#define A_TX            0x03   // Tx command / Rx reply
#define A_RX            0x01   // Rx command / Tx reply
#define C_SET           0x03
#define C_UA            0x07
#define C_DISC          0x0B
#define C_I0            0x00   // I frame Ns=0
#define C_I1            0x40   // I frame Ns=1
#define C_RR0           0x05   // ACK Ns=0, expect Ns=1
#define C_RR1           0x85   // ACK Ns=1, expect Ns=0
#define C_REJ0          0x01   // NACK, resend Ns=0
#define C_REJ1          0x81   // NACK, resend Ns=1
#define ESC             0x7D

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
```

---

## Static state in link_layer.c

```c
static int fd;
static LinkLayer ll;       // stored from llopen
static int Ns = 0;         // sender sequence number
static int expectedNs = 0; // receiver expected Ns
// alarmEnabled, alarmCount from alarm_sigaction.h (reused as-is)
```

---

## Helpers (internal, not in .h)

### sendSUFrame(A, C)
Builds and writes a 5-byte S/U frame: `F A C (A^C) F`

### receiveSUFrame(expectedC) → int
S/U frame state machine (START→FLAG_RCV→A_RCV→C_RCV→BCC_OK→END), byte-by-byte.
Returns 0 on match, -1 on mismatch/error.
Reused by llopen and llclose.

---

## llopen(params)
*Source: establishConnection() + serial port setup from write/read_noncanonical*

```
1. open(serialPort, O_RDWR | O_NOCTTY)
2. configure termios: BAUDRATE, CS8, CLOCAL, CREAD, VMIN=0, VTIME=1
3. store params in static ll
4. setup SIGALRM → alarmHandler

If TRANSMITTER:
  alarmCount=0, alarmEnabled=FALSE
  while alarmCount <= numTransmissions:
    if alarmEnabled==FALSE:
      sendSUFrame(A_TX, C_SET)
      alarm(timeout), alarmEnabled=TRUE
    if receiveSUFrame(C_UA) == 0: alarm(0), return fd
  return -1

If RECEIVER:
  receiveSUFrame(C_SET)   ← blocks until SET arrives
  sendSUFrame(A_RX, C_UA)
  return fd
```

---

## llwrite(buf, bufSize)
*Source: sendData() from write_noncanonical*

```
1. Build I frame:
     F | A_TX | C(Ns) | BCC1 | [stuffed data + stuffed BCC2] | F
2. expectedRR  = (Ns==0) ? C_RR1 : C_RR0
   expectedREJ = (Ns==0) ? C_REJ0 : C_REJ1
3. alarmCount=0, alarmEnabled=FALSE, ackReceived=FALSE
4. while !ackReceived && alarmCount <= numTransmissions:
     if alarmEnabled==FALSE:
       write frame, alarm(timeout), alarmEnabled=TRUE, reset SM
     read 1 byte → S/U state machine
     on RR:  ackReceived=TRUE
     on REJ: alarm(0), alarmEnabled=FALSE  ← immediate resend
5. alarm(0)
6. if ackReceived: Ns=!Ns; return frameLen
   else: return -1
```

---

## llread(packet)
*Source: read_noncanonical Phase 2*

```
1. State machine: START→FLAG_RCV→A_RCV→C_RCV→BCC_OK→data collection
2. In BCC_OK: apply destuffing on each byte
3. On closing FLAG:
     bcc2 = XOR(dataBuffer[0..N-2])
     if bcc2 == dataBuffer[N-1]:
       sendSUFrame(A_TX, RR for receivedNs)
       if receivedNs == expectedNs:
         memcpy(packet, dataBuffer, N-1)
         expectedNs = !expectedNs
         return N-1   ← data length
       else:
         return 0     ← duplicate, caller ignores
     else:
       sendSUFrame(A_TX, REJ for receivedNs)
       return -1
```

---

## llclose()
*New — not in prototypes*

```
If TRANSMITTER:
  sendSUFrame(A_TX, C_DISC)
  receiveSUFrame(C_DISC)   ← wait for Rx's DISC
  sendSUFrame(A_RX, C_UA)

If RECEIVER:
  receiveSUFrame(C_DISC)   ← wait for Tx's DISC
  sendSUFrame(A_RX, C_DISC)
  receiveSUFrame(C_UA)     ← wait for Tx's UA

tcsetattr(fd, TCSANOW, &oldtio)  ← restore termios
close(fd)
return 0
```

---

## Makefile additions

```makefile
OBJS = link_layer.o alarm_sigaction.o

transmitter: transmitter.o $(OBJS)
    gcc -Wall -o transmitter transmitter.o $(OBJS)

receiver: receiver.o $(OBJS)
    gcc -Wall -o receiver receiver.o $(OBJS)
```

`transmitter.c` and `receiver.c` are thin wrappers for now,
later replaced by `application_layer.c` + `main.c`.

---

## Verification
```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
./receiver /dev/pts/3 &
./transmitter /dev/pts/2
```
Expected sequence: SET → UA → I(Ns=0) → RR1 → DISC → DISC → UA → clean exit