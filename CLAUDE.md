# RCOM Lab 1 – Data Link Layer Protocol
FEUP | Computer Networks (L.EEC0031) | 2024/2025

## Project Goal
Implement a **Stop-and-Wait data link layer protocol** over RS-232 serial communication in C.
Transfer a file between two computers using a two-layer architecture:
- **Data Link Layer (DLL)** – framing, error detection, retransmission, connection management
- **Application Layer (AL)** – file fragmentation, packet building, uses DLL API

---

## Architecture

```
Application Layer   [application_layer.c]
        |
   llopen / llwrite / llread / llclose   ← DLL API
        |
 Data Link Layer    [link_layer.c]
        |
   open / write / read / close           ← Linux serial port API
        |
 Serial Port Driver [serial_port.c]      ← already implemented, do not modify
        |
  /dev/ttyS0  ──────── RS-232 ──────────  /dev/ttyS0
```

---

## Key Files

| File | Status | Description |
|------|--------|-------------|
| `cable.c` | provided | Virtual serial cable simulator (use `socat` or this to link two ptys) |
| `write_noncanonical.c` | provided | Example of non-canonical serial port write |
| `read_noncanonical.c` | provided | Example of non-canonical serial port read |
| `serial_port.c` | **do not modify** | Serial port open/read/write/close — already implemented |
| `main.c` | **do not modify** | Parses CLI args, calls applicationLayer() |
| `application_layer.c` | **TO DEVELOP** | AL logic: build packets, fragment file, call DLL API |
| `link_layer.c` | **TO DEVELOP** | DLL logic: framing, BCC, stuffing, state machines, retransmission |

---

## DLL API (link_layer.c) — what to implement

```c
int llopen(LinkLayer connectionParameters);  // establish connection (SET/UA exchange)
int llwrite(const unsigned char *buf, int bufSize); // send data in I frame, wait ACK
int llread(unsigned char *packet);           // receive I frame, return data packet
int llclose();                               // terminate connection (DISC exchange)
```

---

## Frame Formats

### Supervision / Unnumbered frames (S/U): `F A C BCC1 F`
| Field | Value | Meaning |
|-------|-------|---------|
| F | 0x7E | Flag — start/end of frame |
| A | 0x03 | Command from Tx or reply from Rx |
| A | 0x01 | Command from Rx or reply from Tx |
| C | 0x03 | SET |
| C | 0x07 | UA |
| C | 0x05 | RR0 |
| C | 0x85 | RR1 |
| C | 0x01 | REJ0 |
| C | 0x81 | REJ1 |
| C | 0x0B | DISC |
| BCC1 | A XOR C | Header error detection |

### Information frames (I): `F A C BCC1 D1...DN BCC2 F`
| Field | Value | Meaning |
|-------|-------|---------|
| C | 0x00 | I frame Ns=0 |
| C | 0x40 | I frame Ns=1 |
| BCC2 | D1 XOR D2 XOR ... XOR DN | Data field error detection |

---

## Byte Stuffing (Transparency)

Applied to the data field + BCC2 **after** computing BCC2:
- `0x7E` → `0x7D 0x5E`
- `0x7D` → `0x7D 0x5D`

Destuffing is the inverse — applied on reception **before** verifying BCC2.

---

## Application Layer Packets

### Data packet: `C L2 L1 P1 ... Pk`
- C = 0x01 (data)
- k = 256 * L2 + L1 (payload size)

### Control packet: `C T1 L1 V1 T2 L2 V2 ...`
- C = 0x02 (START), C = 0x03 (END)
- TLV fields: T=0 (file size), T=1 (file name)

---

## Connection Phases

1. **Establishment**: Tx sends SET → Rx replies UA
2. **Data transfer**: Tx sends I(Ns) → Rx replies RR(Ns+1) or REJ(Ns)
3. **Termination**: Tx sends DISC → Rx replies DISC → Tx sends UA

---

## Stop-and-Wait / Retransmission

- After sending any frame, **activate a timer**
- If timeout → resend, up to `numTransmissions` retries
- If REJ received → resend immediately
- If RR received → deactivate timer, proceed
- Ns alternates 0/1 (mod-2); detect duplicates by checking expected Ns

---

## State Machines

Reception is always **byte-by-byte** using `read(fd, &buf, 1)`.
Two state machines to implement:
- **S/U frame reception** (5 states): START → FLAG → A → C → BCC1 check → END
- **I frame reception** (7 states): adds data collection loop after BCC1 check

---

## Testing with the Cable Simulator

```bash
# Create a virtual serial cable between two pseudo-terminals
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# Output example: /dev/pts/2 and /dev/pts/3

# Run receiver on one terminal
./receiver /dev/pts/3

# Run transmitter on another
./transmitter /dev/pts/2 penguin.gif
```

Or use `cable.c` if it simulates a noisy/delayed cable for FER/efficiency testing.

---

## Build

```bash
make          # build everything
make clean    # clean objects and binaries
```

---

## Evaluation Checklist
- [ ] Frame synchronisation (state machine)
- [ ] BCC1 and BCC2 error detection
- [ ] Byte stuffing / destuffing
- [ ] Retransmission on timeout and REJ
- [ ] Duplicate frame detection (Ns mod-2)
- [ ] Layer independence (AL has no knowledge of DLL internals)
- [ ] Control packets: START / END with TLV fields
- [ ] Data packets: correct fragmentation and reassembly
- [ ] File integrity after transfer
- [ ] Performance evaluation: efficiency S vs FER and propagation delay
