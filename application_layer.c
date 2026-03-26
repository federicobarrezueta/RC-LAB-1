#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application_layer.h"
#include "link_layer.h"

// ─────────────────────────────────────────────
// Packet builders
// ─────────────────────────────────────────────

// Builds a control packet (START or END) into buf.
// Returns the packet size.
static int buildControlPacket(unsigned char *buf, unsigned char C,
                               long fileSize, const char *filename) {
    int i = 0;
    int nameLen = strlen(filename);

    buf[i++] = C;               // 0x02 = START, 0x03 = END

    // TLV: T=0, file size (4 bytes)
    buf[i++] = 0x00;            // T
    buf[i++] = 4;               // L
    buf[i++] = (fileSize >> 24) & 0xFF;
    buf[i++] = (fileSize >> 16) & 0xFF;
    buf[i++] = (fileSize >>  8) & 0xFF;
    buf[i++] = (fileSize >>  0) & 0xFF;

    // TLV: T=1, file name
    buf[i++] = 0x01;            // T
    buf[i++] = nameLen;         // L
    memcpy(&buf[i], filename, nameLen);
    i += nameLen;

    return i;
}

// Builds a data packet into buf.
// Returns the packet size.
static int buildDataPacket(unsigned char *buf,
                            const unsigned char *data, int dataLen) {
    buf[0] = 0x01;              // C = data packet
    buf[1] = dataLen / 256;     // L2
    buf[2] = dataLen % 256;     // L1
    memcpy(&buf[3], data, dataLen);
    return 3 + dataLen;
}

// ─────────────────────────────────────────────
// Application Layer
// ─────────────────────────────────────────────

void applicationLayer(ApplicationLayer params) {

    if (llopen(params.linkLayer) < 0) {
        printf("applicationLayer: llopen failed\n");
        return;
    }

    if (params.linkLayer.role == TRANSMITTER) {

        // Open file
        FILE *f = fopen(params.filename, "rb");
        if (!f) { perror(params.filename); return; }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        rewind(f);
        printf("Sending file: %s (%ld bytes)\n", params.filename, fileSize);

        // Send START control packet
        unsigned char ctrlBuf[300];
        int ctrlLen = buildControlPacket(ctrlBuf, 0x02, fileSize, params.filename);
        if (llwrite(ctrlBuf, ctrlLen) < 0) {
            printf("Failed to send START packet\n");
            fclose(f);
            return;
        }
        printf("START packet sent\n");

        // Fragment and send data packets
        unsigned char fileBuf[params.maxPayloadSize];
        unsigned char pktBuf[params.maxPayloadSize + 3];
        int bytesRead;
        int packetCount = 0;

        while ((bytesRead = fread(fileBuf, 1, params.maxPayloadSize, f)) > 0) {
            int pktLen = buildDataPacket(pktBuf, fileBuf, bytesRead);
            if (llwrite(pktBuf, pktLen) < 0) {
                printf("Failed to send data packet %d\n", packetCount);
                fclose(f);
                return;
            }
            packetCount++;
            printf("Data packet %d sent (%d bytes)\n", packetCount, bytesRead);
        }

        fclose(f);

        // Send END control packet
        ctrlLen = buildControlPacket(ctrlBuf, 0x03, fileSize, params.filename);
        if (llwrite(ctrlBuf, ctrlLen) < 0) {
            printf("Failed to send END packet\n");
            return;
        }
        printf("END packet sent — %d packets total\n", packetCount);

    } else { // RECEIVER

        FILE *f = NULL;
        long expectedSize = 0;
        unsigned char packet[MAX_PAYLOAD_SIZE];
        int receiving = 1;

        while (receiving) {
            int len = llread(packet);
            if (len <= 0) continue;

            unsigned char C = packet[0];

            if (C == 0x02) {
                // START packet — extract file size and name
                int idx = 1;
                char filename[256] = {0};

                while (idx < len) {
                    unsigned char T = packet[idx++];
                    unsigned char L = packet[idx++];

                    if (T == 0x00) {  // file size
                        expectedSize = 0;
                        for (int b = 0; b < L; b++)
                            expectedSize = (expectedSize << 8) | packet[idx++];
                    } else if (T == 0x01) {  // file name
                        memcpy(filename, &packet[idx], L);
                        filename[L] = '\0';
                        idx += L;
                    } else {
                        idx += L;  // skip unknown TLV
                    }
                }

                // Save as received filename (or use params.filename if set)
                const char *saveName = params.filename[0] ? params.filename : filename;
                f = fopen(saveName, "wb");
                if (!f) { perror(saveName); return; }
                printf("START received — file: %s, size: %ld bytes\n", saveName, expectedSize);

            } else if (C == 0x01) {
                // Data packet
                if (!f) { printf("Data packet before START, ignoring\n"); continue; }
                int dataLen = 256 * packet[1] + packet[2];
                fwrite(&packet[3], 1, dataLen, f);
                printf("Data packet received (%d bytes)\n", dataLen);

            } else if (C == 0x03) {
                // END packet
                if (f) { fclose(f); f = NULL; }
                printf("END received — file transfer complete\n");
                receiving = 0;
            }
        }
    }

    llclose();
}
